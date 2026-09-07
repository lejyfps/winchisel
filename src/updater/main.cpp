#include <windows.h>
#include <shellapi.h>
#include <bcrypt.h>

#include <array>
#include <cwchar>
#include <filesystem>
#include <fstream>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#pragma comment(lib, "bcrypt.lib")

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

bool wait_for_process(std::wstring_view pid_text) {
    const std::wstring pid_value(pid_text);
    wchar_t* end{};
    const auto pid = wcstoul(pid_value.c_str(), &end, 10);
    if (!pid || !end || *end) return false;
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

int fail(DWORD code) { return static_cast<int>(code); }

} // namespace

int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int) {
    const auto staged_arg = argument(L"--staged");
    const auto target_arg = argument(L"--target");
    const auto hash_arg = argument(L"--sha256");
    const auto pid_arg = argument(L"--wait-pid");
    if (!staged_arg || !target_arg || !hash_arg || !pid_arg || hash_arg->size() != 64) return fail(ERROR_INVALID_PARAMETER);
    const auto staged = absolute_existing_file(*staged_arg);
    const auto target = absolute_target(*target_arg);
    if (!staged || !target || sha256_hex(*staged) != std::string(hash_arg->begin(), hash_arg->end())) return fail(ERROR_INVALID_DATA);
    if (!wait_for_process(*pid_arg)) return fail(ERROR_TIMEOUT);

    const auto nonce = std::to_wstring(GetCurrentProcessId()) + L"-" + std::to_wstring(GetTickCount64());
    const auto replacement = target->wstring() + L".update-" + nonce;
    const auto backup = target->wstring() + L".backup-" + nonce;
    if (!CopyFileW(staged->c_str(), replacement.c_str(), TRUE) || sha256_hex(replacement) != std::string(hash_arg->begin(), hash_arg->end())) { DeleteFileW(replacement.c_str()); return fail(ERROR_CRC); }
    if (!MoveFileExW(target->c_str(), backup.c_str(), MOVEFILE_WRITE_THROUGH)) { DeleteFileW(replacement.c_str()); return fail(GetLastError()); }
    if (!MoveFileExW(replacement.c_str(), target->c_str(), MOVEFILE_WRITE_THROUGH)) {
        const auto error = GetLastError();
        MoveFileExW(backup.c_str(), target->c_str(), MOVEFILE_WRITE_THROUGH);
        DeleteFileW(replacement.c_str());
        return fail(error);
    }
    if (!launch(*target)) return fail(GetLastError());
    return 0;
}
