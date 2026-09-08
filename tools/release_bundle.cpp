#include <windows.h>

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <string_view>
#include <vector>

namespace {
#pragma pack(push, 1)
struct Footer { char magic[8]; std::uint64_t table_offset; std::uint64_t table_size; std::uint32_t mode; };
#pragma pack(pop)
static_assert(sizeof(Footer) == 28);
constexpr char k_magic[] = "WCHBNDL1";

struct File { std::filesystem::path source; std::string relative; std::uint64_t offset{}; std::uint64_t size{}; };

bool write(std::ofstream& out, void const* data, std::size_t size) { out.write(static_cast<char const*>(data), static_cast<std::streamsize>(size)); return static_cast<bool>(out); }

bool append(std::ofstream& out, std::filesystem::path const& source) {
    std::ifstream input(source, std::ios::binary);
    std::vector<char> buffer(64 * 1024);
    while (input.good()) { input.read(buffer.data(), static_cast<std::streamsize>(buffer.size())); const auto count = input.gcount(); if (count) out.write(buffer.data(), count); }
    return input.eof() && static_cast<bool>(out);
}

int bundle(std::filesystem::path const& stub, std::filesystem::path const& stage, std::filesystem::path const& output, std::uint32_t mode) {
    std::error_code error;
    if (!std::filesystem::is_regular_file(stub, error) || !std::filesystem::is_directory(stage, error)) return 2;
    std::vector<File> files;
    for (std::filesystem::recursive_directory_iterator it(stage, error), end; !error && it != end; it.increment(error)) {
        if (!it->is_regular_file(error)) continue;
        auto relative = std::filesystem::relative(it->path(), stage, error).generic_string();
        if (error || relative.empty() || relative.find("..") != std::string::npos) return 2;
        files.push_back({it->path(), std::move(relative), 0, std::filesystem::file_size(it->path(), error)});
        if (error) return 2;
    }
    std::sort(files.begin(), files.end(), [](auto const& a, auto const& b) { return a.relative < b.relative; });
    std::uint64_t table_size = sizeof(std::uint32_t);
    for (auto const& file : files) table_size += sizeof(std::uint32_t) + file.relative.size() + sizeof(std::uint64_t) * 2;
    const auto stub_size = std::filesystem::file_size(stub, error); if (error) return 2;
    std::uint64_t next = stub_size + table_size;
    for (auto& file : files) { file.offset = next; next += file.size; }
    std::filesystem::create_directories(output.parent_path(), error); if (error) return 2;
    std::ofstream out(output, std::ios::binary | std::ios::trunc); if (!out || !append(out, stub)) return 3;
    const auto count = static_cast<std::uint32_t>(files.size()); if (!write(out, &count, sizeof(count))) return 3;
    for (auto const& file : files) {
        const auto length = static_cast<std::uint32_t>(file.relative.size());
        if (!write(out, &length, sizeof(length)) || !write(out, file.relative.data(), length) || !write(out, &file.offset, sizeof(file.offset)) || !write(out, &file.size, sizeof(file.size))) return 3;
    }
    for (auto const& file : files) if (!append(out, file.source)) return 3;
    const Footer footer{{'W','C','H','B','N','D','L','1'}, stub_size, table_size, mode};
    return write(out, &footer, sizeof(footer)) ? 0 : 3;
}
}

int main(int argc, char** argv) {
    if (argc != 5 || (std::string(argv[1]) != "portable" && std::string(argv[1]) != "setup")) { std::cerr << "Usage: release_bundle <portable|setup> <stub.exe> <stage-dir> <output.exe>\n"; return 1; }
    return bundle(argv[2], argv[3], argv[4], std::string(argv[1]) == "setup" ? 2u : 1u);
}
