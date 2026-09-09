#include <windows.h>
#include <shellapi.h>
#include <bcrypt.h>
#include <wincrypt.h>

#include <array>
#include <cwchar>
#include <filesystem>
#include <fstream>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#pragma comment(lib, "bcrypt.lib")
#pragma comment(lib, "crypt32.lib")

namespace {

constexpr DWORD k_wait_timeout_ms = 120'000;

std::optional<std::wstring> argument(std::wstring_view name) {
    int argc{};
    auto argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    if (!argv) return std::nullopt;
    std::optional<std::wstring> result;
    for (int i = 1; i + 1 < argc; ++i) {
        if (name == argv[i]) { result = argv[i + 1]; break; }
    }
    LocalFree(argv);
    return result;
}

std::optional<std::filesystem::path> absolute_existing_file(std::wstring const& value) {
    std::error_code error;
    auto path = std::filesystem::weakly_canonical(std::filesystem::path(value), error);
    if (error || !path.is_absolute() || !std::filesystem::is_regular_file(path, error) || error) return std::nullopt;
    return path;
}

std::optional<std::filesystem::path> absolute_target(std::wstring const& value) {
    std::error_code error;
    auto path = std::filesystem::weakly_canonical(std::filesystem::path(value), error);
    if (error || !path.is_absolute() || _wcsicmp(path.extension().c_str(), L".exe") != 0) return std::nullopt;
    std::array<wchar_t, 32768> current{};
    const auto length = GetModuleFileNameW(nullptr, current.data(), static_cast<DWORD>(current.size()));
    if (!length || length == static_cast<DWORD>(current.size())) return std::nullopt;
    const auto self = std::filesystem::weakly_canonical(std::filesystem::path(std::wstring(current.data(), length)), error);
    if (error || _wcsicmp(path.c_str(), self.c_str()) == 0) return std::nullopt;
    return path;
}

std::string sha256_hex(std::filesystem::path const& file) {
    BCRYPT_ALG_HANDLE algorithm{};
    BCRYPT_HASH_HANDLE hash{};
    if (BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_SHA256_ALGORITHM, nullptr, 0) < 0) return {};
    DWORD object_size{}, bytes{};
    if (BCryptGetProperty(algorithm, BCRYPT_OBJECT_LENGTH, reinterpret_cast<BYTE*>(&object_size), sizeof(object_size), &bytes, 0) < 0) { BCryptCloseAlgorithmProvider(algorithm, 0); return {}; }
    std::vector<BYTE> object(object_size), digest(32);
    if (BCryptCreateHash(algorithm, &hash, object.data(), object_size, nullptr, 0, 0) < 0) { BCryptCloseAlgorithmProvider(algorithm, 0); return {}; }
    std::ifstream input(file, std::ios::binary);
    std::array<char, 64 * 1024> buffer{};
    while (input.good()) {
        input.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
        const auto read = input.gcount();
        if (read && BCryptHashData(hash, reinterpret_cast<BYTE*>(buffer.data()), static_cast<ULONG>(read), 0) < 0) { BCryptDestroyHash(hash); BCryptCloseAlgorithmProvider(algorithm, 0); return {}; }
    }
    const bool ok = input.eof() && BCryptFinishHash(hash, digest.data(), static_cast<ULONG>(digest.size()), 0) >= 0;
    BCryptDestroyHash(hash); BCryptCloseAlgorithmProvider(algorithm, 0);
    if (!ok) return {};
    static constexpr char hex[] = "0123456789abcdef";
    std::string output; output.reserve(64);
    for (const auto byte : digest) { output.push_back(hex[byte >> 4]); output.push_back(hex[byte & 15]); }
    return output;
}

std::optional<std::string> ascii_hash(std::wstring const& value) {
    if (value.size() != 64) return std::nullopt;
    std::string result;
    result.reserve(value.size());
    for (const auto character : value) {
        if (!((character >= L'0' && character <= L'9') || (character >= L'a' && character <= L'f'))) return std::nullopt;
        result.push_back(static_cast<char>(character));
    }
    return result;
}

bool wait_for_process(std::wstring_view pid_text) {
    if (pid_text.empty() || pid_text.size() > 10) return false;
    DWORD pid{};
    for (const wchar_t c : pid_text) {
        if (c < L'0' || c > L'9') return false;
        const unsigned digit = static_cast<unsigned>(c - L'0');
        if (pid > (0xFFFFFFFFu - digit) / 10) return false;
        pid = pid * 10 + digit;
    }
    if (!pid) return false;
    const auto process = OpenProcess(SYNCHRONIZE, FALSE, pid);
    if (!process) return GetLastError() == ERROR_INVALID_PARAMETER;
    const auto result = WaitForSingleObject(process, k_wait_timeout_ms);
    CloseHandle(process);
    return result == WAIT_OBJECT_0;
}

bool launch(std::filesystem::path const& target) {
    std::wstring command = L"\"" + target.wstring() + L"\"";
    STARTUPINFOW startup{.cb = sizeof(startup)};
    PROCESS_INFORMATION process{};
    if (!CreateProcessW(target.c_str(), command.data(), nullptr, nullptr, FALSE, 0, nullptr, target.parent_path().c_str(), &startup, &process)) return false;
    CloseHandle(process.hThread); CloseHandle(process.hProcess);
    return true;
}

std::vector<BYTE> decode_base64(std::string const& value) {
    if (value.empty() || value.size() > 65536) return {};
    DWORD size{};
    if (!CryptStringToBinaryA(value.c_str(), static_cast<DWORD>(value.size()), CRYPT_STRING_BASE64, nullptr, &size, nullptr, nullptr) || !size) return {};
    std::vector<BYTE> output(size);
    if (!CryptStringToBinaryA(value.c_str(), static_cast<DWORD>(value.size()), CRYPT_STRING_BASE64, output.data(), &size, nullptr, nullptr)) return {};
    output.resize(size);
    return output;
}

// ECDSA P-256 / SHA-256 over the exact manifest bytes. The public key is
// embedded so the updater never trusts a caller-provided hash on its own.
bool verify_manifest_signature(std::string const& json, std::string const& signature_text) {
    // BCRYPT_ECCPUBLIC_BLOB: header followed by P-256 X and Y coordinates.
    static constexpr std::array<BYTE, 72> public_key{
        0x45, 0x43, 0x53, 0x31, 0x20, 0x00, 0x00, 0x00, 0x45, 0xAF, 0x42, 0x47,
        0x0F, 0x10, 0x9F, 0x23, 0xF3, 0x97, 0x11, 0xD5, 0x88, 0xD2, 0x88, 0x5C,
        0x4E, 0x9A, 0x29, 0x72, 0xCE, 0x9D, 0x22, 0x9B, 0xCD, 0xF5, 0x0B, 0xF9,
        0x40, 0x85, 0x03, 0x68, 0x99, 0x35, 0xF0, 0x40, 0x41, 0x82, 0xE2, 0xF1,
        0x69, 0xAA, 0x81, 0x23, 0x28, 0x87, 0x81, 0x27, 0xDD, 0x0B, 0xE8, 0x0C,
        0xFA, 0x38, 0x05, 0xB1, 0x61, 0x7F, 0xB7, 0xD4, 0x21, 0xF5, 0x5C, 0xEB,
    };
    const auto signature = decode_base64(signature_text);
    if (signature.empty() || json.empty() || json.size() > 1024 * 1024) return false;
    BCRYPT_ALG_HANDLE algorithm{}, hash_algorithm{};
    BCRYPT_KEY_HANDLE key{};
    BCRYPT_HASH_HANDLE hash{};
    if (BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_ECDSA_P256_ALGORITHM, nullptr, 0) < 0) return false;
    if (BCryptImportKeyPair(algorithm, nullptr, BCRYPT_ECCPUBLIC_BLOB, &key,
            const_cast<BYTE*>(public_key.data()), static_cast<ULONG>(public_key.size()), 0) < 0) {
        BCryptCloseAlgorithmProvider(algorithm, 0);
        return false;
    }
    DWORD object_size{}, result{};
    if (BCryptOpenAlgorithmProvider(&hash_algorithm, BCRYPT_SHA256_ALGORITHM, nullptr, 0) < 0 ||
        BCryptGetProperty(hash_algorithm, BCRYPT_OBJECT_LENGTH, reinterpret_cast<BYTE*>(&object_size),
            sizeof(object_size), &result, 0) < 0 || object_size == 0) {
        BCryptDestroyKey(key);
        if (hash_algorithm) BCryptCloseAlgorithmProvider(hash_algorithm, 0);
        BCryptCloseAlgorithmProvider(algorithm, 0);
        return false;
    }
    std::vector<BYTE> object(object_size), digest(32);
    const bool ok = BCryptCreateHash(hash_algorithm, &hash, object.data(), object_size, nullptr, 0, 0) >= 0 &&
        BCryptHashData(hash, reinterpret_cast<BYTE*>(const_cast<char*>(json.data())), static_cast<ULONG>(json.size()), 0) >= 0 &&
        BCryptFinishHash(hash, digest.data(), static_cast<ULONG>(digest.size()), 0) >= 0 &&
        BCryptVerifySignature(key, nullptr, digest.data(), static_cast<ULONG>(digest.size()),
            const_cast<BYTE*>(signature.data()), static_cast<ULONG>(signature.size()), 0) >= 0;
    if (hash) BCryptDestroyHash(hash);
    if (hash_algorithm) BCryptCloseAlgorithmProvider(hash_algorithm, 0);
    if (key) BCryptDestroyKey(key);
    if (algorithm) BCryptCloseAlgorithmProvider(algorithm, 0);
    return ok;
}

bool manifest_binds_file(std::string const& manifest, std::string const& file_name, std::string const& expected_sha256) {
    auto skip_ws = [](std::string const& text, std::size_t p) {
        while (p < text.size() && (text[p] == ' ' || text[p] == '\t' || text[p] == '\r' || text[p] == '\n')) ++p;
        return p;
    };
    std::size_t pos{};
    while ((pos = manifest.find("\"file\"", pos)) != std::string::npos) {
        std::size_t p = skip_ws(manifest, pos + 7);
        if (p >= manifest.size() || manifest[p] != ':') { pos += 7; continue; }
        p = skip_ws(manifest, p + 1);
        if (p >= manifest.size() || manifest[p] != '"') { pos += 7; continue; }
        ++p;
        if (manifest.compare(p, file_name.size(), file_name) != 0) { pos = p; continue; }
        p += file_name.size();
        if (p >= manifest.size() || manifest[p] != '"') { pos = p; continue; }
        ++p;
        const auto sha = manifest.find("\"sha256\"", p);
        if (sha == std::string::npos) return false;
        if (manifest.find_first_of("{}", p) < sha) { pos = p; continue; }
        std::size_t q = skip_ws(manifest, sha + 8);
        if (q >= manifest.size() || manifest[q] != ':') { pos = sha + 8; continue; }
        q = skip_ws(manifest, q + 1);
        if (q >= manifest.size() || manifest[q] != '"') { pos = sha + 8; continue; }
        ++q;
        if (manifest.compare(q, expected_sha256.size(), expected_sha256) != 0) { pos = q; continue; }
        q += expected_sha256.size();
        if (q >= manifest.size() || manifest[q] != '"') { pos = q; continue; }
        return true;
    }
    return false;
}

// The staged file hash alone is caller-asserted. Require the sibling signed
// manifest (written by stage_release_artifact) and bind the staged file name
// to its signed SHA-256 entry before any privileged file move.
bool verify_staged_against_signed_manifest(std::filesystem::path const& staged, std::string const& expected_hash) {
    const auto dir = staged.parent_path();
    std::ifstream manifest_in(dir / L"manifest.json", std::ios::binary);
    std::ifstream signature_in(dir / L"manifest.json.sig", std::ios::binary);
    if (!manifest_in || !signature_in) return false;
    std::string manifest((std::istreambuf_iterator<char>(manifest_in)), std::istreambuf_iterator<char>());
    std::string signature((std::istreambuf_iterator<char>(signature_in)), std::istreambuf_iterator<char>());
    if (manifest.empty() || manifest.size() > 1024 * 1024 || signature.empty() || signature.size() > 16384) return false;
    if (!verify_manifest_signature(manifest, signature)) return false;
    const std::string file_name = staged.filename().string();
    if (file_name.empty() || file_name.size() > 128) return false;
    return manifest_binds_file(manifest, file_name, expected_hash);
}

int fail(DWORD code) { return static_cast<int>(code); }

} // namespace

int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int) {
    const auto staged_arg = argument(L"--staged");
    const auto target_arg = argument(L"--target");
    const auto hash_arg = argument(L"--sha256");
    const auto pid_arg = argument(L"--wait-pid");
    if (!staged_arg || !target_arg || !hash_arg || !pid_arg) return fail(ERROR_INVALID_PARAMETER);
    const auto expected_hash = ascii_hash(*hash_arg);
    if (!expected_hash) return fail(ERROR_INVALID_PARAMETER);
    const auto staged = absolute_existing_file(*staged_arg);
    const auto target = absolute_target(*target_arg);
    if (!staged || !target || sha256_hex(*staged) != *expected_hash) return fail(ERROR_INVALID_DATA);
    if (!verify_staged_against_signed_manifest(*staged, *expected_hash)) return fail(ERROR_INVALID_DATA);
    if (!wait_for_process(*pid_arg)) return fail(ERROR_TIMEOUT);

    const auto nonce = std::to_wstring(GetCurrentProcessId()) + L"-" + std::to_wstring(GetTickCount64());
    const auto replacement = target->wstring() + L".update-" + nonce;
    const auto backup = target->wstring() + L".backup-" + nonce;
    if (!CopyFileW(staged->c_str(), replacement.c_str(), TRUE) || sha256_hex(replacement) != *expected_hash) { DeleteFileW(replacement.c_str()); return fail(ERROR_CRC); }
    if (!MoveFileExW(target->c_str(), backup.c_str(), MOVEFILE_WRITE_THROUGH)) { DeleteFileW(replacement.c_str()); return fail(GetLastError()); }
    if (!MoveFileExW(replacement.c_str(), target->c_str(), MOVEFILE_WRITE_THROUGH)) {
        const auto error = GetLastError();
        MoveFileExW(backup.c_str(), target->c_str(), MOVEFILE_WRITE_THROUGH);
        DeleteFileW(replacement.c_str());
        return fail(error);
    }
    if (!launch(*target)) {
        const auto error = GetLastError();
        MoveFileExW(target->c_str(), (target->wstring() + L".failed").c_str(), MOVEFILE_REPLACE_EXISTING);
        MoveFileExW(backup.c_str(), target->c_str(), MOVEFILE_REPLACE_EXISTING);
        return fail(error ? error : ERROR_ACCESS_DENIED);
    }
    std::error_code error;
    for (auto const& entry : std::filesystem::directory_iterator(target->parent_path(), error)) {
        auto name = entry.path().filename().wstring();
        auto prefix = target->filename().wstring() + L".backup-";
        if (name.rfind(prefix, 0) == 0 && entry.path() != std::filesystem::path(backup)) DeleteFileW(entry.path().c_str());
    }
    return 0;
}
