#include <windows.h>
#include <bcrypt.h>
#include <wincrypt.h>

#include <array>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <span>
#include <string>
#include <vector>

#pragma comment(lib, "bcrypt.lib")
#pragma comment(lib, "crypt32.lib")

namespace {

std::string hex_encode(std::span<std::byte const> bytes);

std::vector<std::byte> read(std::filesystem::path const& path) {
    std::ifstream input(path, std::ios::binary);
    input.seekg(0, std::ios::end);
    const auto size = static_cast<std::size_t>(input.tellg());
    input.seekg(0);
    std::vector<std::byte> result(size);
    input.read(reinterpret_cast<char*>(result.data()), static_cast<std::streamsize>(size));
    return result;
}

std::string sha256_file(std::filesystem::path const& path) {
    BCRYPT_ALG_HANDLE algorithm{}; BCRYPT_HASH_HANDLE hash{};
    if (BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_SHA256_ALGORITHM, nullptr, 0) < 0) return {};
    DWORD object_size{}, result{}; BCryptGetProperty(algorithm, BCRYPT_OBJECT_LENGTH, reinterpret_cast<BYTE*>(&object_size), sizeof(object_size), &result, 0);
    std::vector<std::byte> object(object_size), digest(32);
    if (BCryptCreateHash(algorithm, &hash, reinterpret_cast<BYTE*>(object.data()), object_size, nullptr, 0, 0) < 0) { BCryptCloseAlgorithmProvider(algorithm, 0); return {}; }
    std::ifstream input(path, std::ios::binary);
    std::array<char, 65536> buffer{};
    while (input.good()) {
        input.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
        const auto n = input.gcount();
        if (n && BCryptHashData(hash, reinterpret_cast<BYTE*>(buffer.data()), static_cast<ULONG>(n), 0) < 0) {
            BCryptDestroyHash(hash); BCryptCloseAlgorithmProvider(algorithm, 0); return {};
        }
    }
    const bool ok = input.eof() && BCryptFinishHash(hash, reinterpret_cast<BYTE*>(digest.data()), 32, 0) >= 0;
    if (hash) BCryptDestroyHash(hash); BCryptCloseAlgorithmProvider(algorithm, 0);
    if (!ok) return {};
    return hex_encode(digest);
}

bool write(std::filesystem::path const& path, std::span<std::byte const> bytes) {
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    output.write(reinterpret_cast<char const*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    return static_cast<bool>(output);
}

std::vector<std::byte> sha256(std::span<std::byte const> value) {
    BCRYPT_ALG_HANDLE algorithm{}; BCRYPT_HASH_HANDLE hash{};
    if (BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_SHA256_ALGORITHM, nullptr, 0) < 0) return {};
    DWORD object_size{}, result{}; BCryptGetProperty(algorithm, BCRYPT_OBJECT_LENGTH, reinterpret_cast<BYTE*>(&object_size), sizeof(object_size), &result, 0);
    std::vector<std::byte> object(object_size), digest(32);
    const auto status = BCryptCreateHash(algorithm, &hash, reinterpret_cast<BYTE*>(object.data()), object_size, nullptr, 0, 0) >= 0
        && BCryptHashData(hash, reinterpret_cast<BYTE*>(const_cast<std::byte*>(value.data())), static_cast<ULONG>(value.size()), 0) >= 0
        && BCryptFinishHash(hash, reinterpret_cast<BYTE*>(digest.data()), static_cast<ULONG>(digest.size()), 0) >= 0;
    if (hash) BCryptDestroyHash(hash); BCryptCloseAlgorithmProvider(algorithm, 0); return status ? digest : std::vector<std::byte>{};
}

std::string hex_encode(std::span<std::byte const> bytes) {
    static constexpr char digits[] = "0123456789abcdef";
    std::string output; output.reserve(bytes.size() * 2);
    for (const auto byte : bytes) { const auto value = static_cast<unsigned>(byte); output.push_back(digits[value >> 4]); output.push_back(digits[value & 15]); }
    return output;
}

std::string base64(std::span<std::byte const> bytes) {
    DWORD size{}; CryptBinaryToStringA(reinterpret_cast<BYTE const*>(bytes.data()), static_cast<DWORD>(bytes.size()), CRYPT_STRING_BASE64 | CRYPT_STRING_NOCRLF, nullptr, &size);
    std::string output(size, '\0'); CryptBinaryToStringA(reinterpret_cast<BYTE const*>(bytes.data()), static_cast<DWORD>(bytes.size()), CRYPT_STRING_BASE64 | CRYPT_STRING_NOCRLF, output.data(), &size); output.resize(size); return output;
}

int keygen(std::filesystem::path const& private_path) {
    BCRYPT_ALG_HANDLE algorithm{}; BCRYPT_KEY_HANDLE key{};
    if (BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_ECDSA_P256_ALGORITHM, nullptr, 0) < 0 || BCryptGenerateKeyPair(algorithm, &key, 256, 0) < 0 || BCryptFinalizeKeyPair(key, 0) < 0) return 1;
    DWORD private_size{}, public_size{}, result{};
    BCryptExportKey(key, nullptr, BCRYPT_ECCPRIVATE_BLOB, nullptr, 0, &private_size, 0); BCryptExportKey(key, nullptr, BCRYPT_ECCPUBLIC_BLOB, nullptr, 0, &public_size, 0);
    std::vector<std::byte> private_key(private_size), public_key(public_size);
    const bool ok = BCryptExportKey(key, nullptr, BCRYPT_ECCPRIVATE_BLOB, reinterpret_cast<BYTE*>(private_key.data()), private_size, &result, 0) >= 0 && write(private_path, private_key) && BCryptExportKey(key, nullptr, BCRYPT_ECCPUBLIC_BLOB, reinterpret_cast<BYTE*>(public_key.data()), public_size, &result, 0) >= 0;
    BCryptDestroyKey(key); BCryptCloseAlgorithmProvider(algorithm, 0); if (!ok) return 1;
    std::cout << "Private key written to " << private_path.string() << "\nPublic key C++ bytes:\n";
    for (std::size_t i{}; i < public_key.size(); ++i) { if (i % 12 == 0) std::cout << "    "; std::cout << "0x" << std::hex << std::uppercase << (static_cast<unsigned>(public_key[i]) & 0xff) << ", "; if (i % 12 == 11) std::cout << "\n"; }
    std::cout << "\n"; return 0;
}

int sign(std::filesystem::path const& private_path, std::filesystem::path const& manifest_path) {
    const auto private_key = read(private_path), manifest = read(manifest_path), digest = sha256(manifest); if (private_key.empty() || digest.empty()) return 1;
    BCRYPT_ALG_HANDLE algorithm{}; BCRYPT_KEY_HANDLE key{}; if (BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_ECDSA_P256_ALGORITHM, nullptr, 0) < 0 || BCryptImportKeyPair(algorithm, nullptr, BCRYPT_ECCPRIVATE_BLOB, &key, reinterpret_cast<BYTE*>(const_cast<std::byte*>(private_key.data())), static_cast<ULONG>(private_key.size()), 0) < 0) return 1;
    DWORD size{}; BCryptSignHash(key, nullptr, reinterpret_cast<BYTE*>(const_cast<std::byte*>(digest.data())), static_cast<ULONG>(digest.size()), nullptr, 0, &size, 0); std::vector<std::byte> signature(size);
    const bool ok = BCryptSignHash(key, nullptr, reinterpret_cast<BYTE*>(const_cast<std::byte*>(digest.data())), static_cast<ULONG>(digest.size()), reinterpret_cast<BYTE*>(signature.data()), size, &size, 0) >= 0;
    BCryptDestroyKey(key); BCryptCloseAlgorithmProvider(algorithm, 0); if (!ok) return 1;
    std::ofstream output(manifest_path.string() + ".sig", std::ios::binary | std::ios::trunc); output << base64(signature) << "\n"; return output ? 0 : 1;
}

int manifest(std::string_view version, std::filesystem::path const& setup, std::filesystem::path const& portable, std::filesystem::path const& output_path) {
    const auto setup_hash = sha256_file(setup), portable_hash = sha256_file(portable);
    std::error_code error;
    const auto setup_size = std::filesystem::file_size(setup, error); if (error) return 1;
    const auto portable_size = std::filesystem::file_size(portable, error); if (error || setup_hash.empty() || portable_hash.empty()) return 1;
    std::ofstream output(output_path, std::ios::binary | std::ios::trunc);
    output << "{\n  \"version\": \"" << version << "\",\n  \"artifacts\": [\n"
           << "    {\n      \"id\": \"setup-x64\",\n      \"file\": \"" << setup.filename().string() << "\",\n      \"sha256\": \"" << setup_hash << "\",\n      \"size\": " << setup_size << "\n    },\n"
           << "    {\n      \"id\": \"portable-x64\",\n      \"file\": \"" << portable.filename().string() << "\",\n      \"sha256\": \"" << portable_hash << "\",\n      \"size\": " << portable_size << "\n    }\n  ]\n}\n";
    return output ? 0 : 1;
}

} // namespace

int main(int argc, char** argv) {
    if (argc == 3 && std::string_view(argv[1]) == "keygen") return keygen(argv[2]);
    if (argc == 4 && std::string_view(argv[1]) == "sign") return sign(argv[2], argv[3]);
    if (argc == 6 && std::string_view(argv[1]) == "manifest") return manifest(argv[2], argv[3], argv[4], argv[5]);
    std::cerr << "Usage: release_signer keygen <private-key-file> | release_signer sign <private-key-file> <release.json> | release_signer manifest <version> <setup.exe> <portable.exe> <release.json>\n"; return 2;
}
