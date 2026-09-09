#include <windows.h>
#include <compressapi.h>

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <string_view>
#include <vector>

#pragma comment(lib, "cabinet.lib")

namespace {
#pragma pack(push, 1)
struct Footer { char magic[8]; std::uint64_t table_offset; std::uint64_t table_size; std::uint32_t mode; };
#pragma pack(pop)
static_assert(sizeof(Footer) == 28);
constexpr char k_magic[] = "WCHBNDL2";

struct File { std::filesystem::path source; std::string relative; std::uint64_t offset{}; std::uint64_t size{}; std::uint64_t compressed_size{}; std::filesystem::path packed; };

// Mirrors the unpacker limits so the build fails instead of exhausting RAM
// on an unexpectedly large stage directory.
constexpr std::uint64_t k_max_file_size = 256ULL * 1024 * 1024;
constexpr std::uint64_t k_max_total_size = 1024ULL * 1024 * 1024;
constexpr std::size_t k_max_file_count = 10000;
constexpr std::size_t k_max_relative_length = 4096;

bool checked_add(std::uint64_t a, std::uint64_t b, std::uint64_t& out) {
    if (a > UINT64_MAX - b) return false;
    out = a + b;
    return true;
}

// Component-wise path check: the old substring search rejected legitimate
// names like "a...b" while missing absolute roots and reserved names.
bool safe_relative(std::string const& relative) {
    if (relative.empty() || relative.size() > k_max_relative_length) return false;
    const std::filesystem::path path(relative);
    if (path.has_root_path()) return false;
    for (auto const& part : path) {
        const auto name = part.string();
        if (name.empty() || name == "." || name == "..") return false;
    }
    return relative.find('\0') == std::string::npos;
}

bool write(std::ofstream& out, void const* data, std::size_t size) { out.write(static_cast<char const*>(data), static_cast<std::streamsize>(size)); return static_cast<bool>(out); }

bool append(std::ofstream& out, std::filesystem::path const& source) {
    std::ifstream input(source, std::ios::binary);
    std::vector<char> buffer(64 * 1024);
    while (input.good()) { input.read(buffer.data(), static_cast<std::streamsize>(buffer.size())); const auto count = input.gcount(); if (count) out.write(buffer.data(), count); }
    return input.eof() && static_cast<bool>(out);
}

bool compress_file(COMPRESSOR_HANDLE compressor, File& file) {
    if (file.size > k_max_file_size) { std::cerr << "File too large: " << file.relative << "\n"; return false; }
    std::ifstream input(file.source, std::ios::binary);
    std::vector<std::byte> raw(static_cast<std::size_t>(file.size));
    input.read(reinterpret_cast<char*>(raw.data()), static_cast<std::streamsize>(raw.size()));
    if (!input && !input.eof()) return false;
    SIZE_T required{};
    Compress(compressor, raw.data(), raw.size(), nullptr, 0, &required);
    if (GetLastError() != ERROR_INSUFFICIENT_BUFFER || !required) return false;
    std::vector<std::byte> packed(required);
    if (!Compress(compressor, raw.data(), raw.size(), packed.data(), packed.size(), &required)) return false;
    packed.resize(required);
    file.compressed_size = packed.size();
    file.packed = file.source; file.packed += L".wchpack";
    std::ofstream out(file.packed, std::ios::binary | std::ios::trunc);
    out.write(reinterpret_cast<char const*>(packed.data()), static_cast<std::streamsize>(packed.size()));
    return static_cast<bool>(out);
}

int bundle(std::filesystem::path const& stub, std::filesystem::path const& stage, std::filesystem::path const& output, std::uint32_t mode) {
    std::error_code error;
    if (!std::filesystem::is_regular_file(stub, error) || !std::filesystem::is_directory(stage, error)) return 2;
    std::vector<File> files;
    for (std::filesystem::recursive_directory_iterator it(stage, error), end; !error && it != end; it.increment(error)) {
        if (!it->is_regular_file(error)) continue;
        auto relative = std::filesystem::relative(it->path(), stage, error).generic_string();
        if (error || !safe_relative(relative)) return 2;
        const auto size = std::filesystem::file_size(it->path(), error);
        if (error || size > k_max_file_size) return 2;
        files.push_back({it->path(), std::move(relative), 0, size, {}});
        if (files.size() > k_max_file_count) return 2;
    }
    std::sort(files.begin(), files.end(), [](auto const& a, auto const& b) { return a.relative < b.relative; });
    COMPRESSOR_HANDLE compressor{};
    if (!CreateCompressor(COMPRESS_ALGORITHM_XPRESS_HUFF, nullptr, &compressor)) return 2;
    bool compressed_ok = true;
    std::uint64_t total_raw{};
    for (auto& file : files) {
        if (!compressed_ok) break;
        if (!checked_add(total_raw, file.size, total_raw) || total_raw > k_max_total_size) { std::cerr << "Total stage size exceeds limit\n"; compressed_ok = false; break; }
        if (!compress_file(compressor, file)) compressed_ok = false;
    }
    CloseCompressor(compressor);
    if (!compressed_ok) {
        for (auto const& file : files) std::filesystem::remove(file.packed, error);
        return 2;
    }
    std::uint64_t table_size = sizeof(std::uint32_t);
    for (auto const& file : files) {
        std::uint64_t entry = sizeof(std::uint32_t) + file.relative.size() + sizeof(std::uint64_t) * 3;
        if (!checked_add(table_size, entry, table_size)) return 2;
    }
    const auto stub_size = std::filesystem::file_size(stub, error); if (error) return 2;
    std::uint64_t next = stub_size;
    if (!checked_add(next, table_size, next)) return 2;
    for (auto& file : files) { file.offset = next; if (!checked_add(next, file.compressed_size, next)) return 2; }
    std::filesystem::create_directories(output.parent_path(), error); if (error) return 2;
    std::ofstream out(output, std::ios::binary | std::ios::trunc); if (!out || !append(out, stub)) return 3;
    const auto count = static_cast<std::uint32_t>(files.size()); if (!write(out, &count, sizeof(count))) return 3;
    for (auto const& file : files) {
        const auto length = static_cast<std::uint32_t>(file.relative.size());
        if (!write(out, &length, sizeof(length)) || !write(out, file.relative.data(), length) || !write(out, &file.offset, sizeof(file.offset)) || !write(out, &file.size, sizeof(file.size)) || !write(out, &file.compressed_size, sizeof(file.compressed_size))) return 3;
    }
    for (auto const& file : files) {
        if (!append(out, file.packed)) return 3;
        std::filesystem::remove(file.packed, error);
    }
    const Footer footer{{'W','C','H','B','N','D','L','2'}, stub_size, table_size, mode};
    return write(out, &footer, sizeof(footer)) ? 0 : 3;
}
}

int main(int argc, char** argv) {
    if (argc != 5 || (std::string(argv[1]) != "portable" && std::string(argv[1]) != "setup")) { std::cerr << "Usage: release_bundle <portable|setup> <stub.exe> <stage-dir> <output.exe>\n"; return 1; }
    return bundle(argv[2], argv[3], argv[4], std::string(argv[1]) == "setup" ? 2u : 1u);
}
