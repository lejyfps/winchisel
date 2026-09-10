#include <windows.h>
#include <shellapi.h>
#include <shlobj.h>
#include <commctrl.h>
#include <bcrypt.h>
#include <wincrypt.h>
#include <tlhelp32.h>

#include <algorithm>
#include <array>
#include <cstdio>
#include <cwchar>
#include <filesystem>
#include <fstream>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#pragma comment(lib, "bcrypt.lib")
#pragma comment(lib, "crypt32.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "comctl32.lib")
// ComCtl v6 for the marquee progress bar (v5 would render it as empty).
#pragma comment(linker, "\"/manifestdependency:type='win32' name='Microsoft.Windows.Common-Controls' version='6.0.0.0' processorArchitecture='*' publicKeyToken='6595b64144ccf1df' language='*'\"")

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

std::optional<std::filesystem::path> absolute_exe(std::wstring const& value) {
    std::error_code error;
    auto path = std::filesystem::weakly_canonical(std::filesystem::path(value), error);
    if (error || !path.is_absolute() || _wcsicmp(path.extension().c_str(), L".exe") != 0) return std::nullopt;
    return path;
}

std::optional<std::filesystem::path> absolute_target(std::wstring const& value) {
    const auto path = absolute_exe(value);
    if (!path) return std::nullopt;
    std::array<wchar_t, 32768> current{};
    const auto length = GetModuleFileNameW(nullptr, current.data(), static_cast<DWORD>(current.size()));
    if (!length || length == static_cast<DWORD>(current.size())) return std::nullopt;
    std::error_code error;
    const auto self = std::filesystem::weakly_canonical(std::filesystem::path(std::wstring(current.data(), length)), error);
    if (error || _wcsicmp(path->c_str(), self.c_str()) == 0) return std::nullopt;
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
    if (const auto process = OpenProcess(SYNCHRONIZE, FALSE, pid)) {
        const auto result = WaitForSingleObject(process, k_wait_timeout_ms);
        CloseHandle(process);
        return result == WAIT_OBJECT_0;
    }
    const auto open_error = GetLastError();
    if (open_error == ERROR_INVALID_PARAMETER) return true;
    if (open_error != ERROR_ACCESS_DENIED) return false;
    // Middle tier: query-limited rights are often granted where waiting is
    // not. Poll the exit code instead of enumerating the whole snapshot.
    if (auto query = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid)) {
        const auto query_deadline = GetTickCount64() + k_wait_timeout_ms;
        bool gone = false;
        while (!gone) {
            DWORD code{};
            gone = !GetExitCodeProcess(query, &code) || code != STILL_ACTIVE;
            if (!gone) {
                if (GetTickCount64() >= query_deadline) break;
                Sleep(500);
            }
        }
        CloseHandle(query);
        if (gone) {
            // Racy PID reuse can only delay us: verify absence once more
            // through the snapshot before declaring the parent gone.
            HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
            if (snapshot != INVALID_HANDLE_VALUE) {
                PROCESSENTRY32W entry{};
                entry.dwSize = sizeof(entry);
                bool still_there = false;
                if (Process32FirstW(snapshot, &entry)) do {
                    if (entry.th32ProcessID == pid) { still_there = true; break; }
                } while (Process32NextW(snapshot, &entry));
                CloseHandle(snapshot);
                if (!still_there) return true;
            }
        }
        return false;
    }
    // Less-privileged updater, elevated parent: no handle access, so poll
    // the process snapshot until the PID disappears or we time out.
    const auto deadline = GetTickCount64() + k_wait_timeout_ms;
    for (;;) {
        bool alive = false;
        HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
        if (snapshot == INVALID_HANDLE_VALUE) {
            if (GetTickCount64() >= deadline) return false;
        } else {
            PROCESSENTRY32W entry{};
            entry.dwSize = sizeof(entry);
            if (Process32FirstW(snapshot, &entry)) do {
                if (entry.th32ProcessID == pid) { alive = true; break; }
            } while (Process32NextW(snapshot, &entry));
            CloseHandle(snapshot);
            if (!alive) return true;
        }
        if (GetTickCount64() >= deadline) return false;
        Sleep(500);
    }
}

bool launch(std::filesystem::path const& target, std::filesystem::path const& host) {
    // Forward the exact host marker the old app was using (Zed preserves
    // launch arguments across restarts). The bootstrap stub ignores unknown
    // arguments, so this never changes stub behavior.
    std::wstring command = L"\"" + target.wstring() + L"\" --portable-host \"" + host.wstring() + L"\"";
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
std::string narrow(std::wstring_view value);
void updater_log(std::string const& line);

// Reads an entire small file with generous sharing: virus scanners,
// indexers or a second Winchisel instance may hold these files open
// momentarily. Retries plausibly transient I/O failures instead of failing
// the update outright; deterministic problems (oversized file) fail fast.
// Returns ERROR_SUCCESS with the bytes, or the Win32 error to report.
DWORD read_file_retry(std::filesystem::path const& path, std::string& output, std::uintmax_t max_size) {
    constexpr int max_attempts = 6;
    constexpr DWORD retry_delay_ms = 500;
    DWORD error = ERROR_FILE_NOT_FOUND;
    for (int attempt{}; attempt < max_attempts; ++attempt) {
        output.clear();
        error = ERROR_SUCCESS;
        bool retryable = false;
        HANDLE file = CreateFileW(path.c_str(), GENERIC_READ,
            FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
            nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (file == INVALID_HANDLE_VALUE) {
            error = GetLastError();
            retryable = error == ERROR_SHARING_VIOLATION || error == ERROR_LOCK_VIOLATION ||
                error == ERROR_ACCESS_DENIED || error == ERROR_FILE_NOT_FOUND || error == ERROR_PATH_NOT_FOUND;
        } else {
            LARGE_INTEGER size{};
            if (!GetFileSizeEx(file, &size) || size.QuadPart < 0 ||
                    static_cast<std::uintmax_t>(size.QuadPart) > max_size) {
                error = ERROR_INVALID_DATA;
            } else {
                output.resize(static_cast<std::size_t>(size.QuadPart));
                DWORD read{};
                if (size.QuadPart != 0 && (!ReadFile(file, output.data(),
                        static_cast<DWORD>(size.QuadPart), &read, nullptr) ||
                        static_cast<std::uintmax_t>(read) != static_cast<std::uintmax_t>(size.QuadPart))) {
                    // Short read: the file changed under us; re-reading may succeed.
                    error = GetLastError();
                    if (error == ERROR_SUCCESS) error = ERROR_INVALID_DATA;
                    retryable = true;
                }
            }
            CloseHandle(file);
        }
        if (error == ERROR_SUCCESS) return ERROR_SUCCESS;
        if (!retryable || attempt + 1 >= max_attempts) break;
        if (attempt == 0) updater_log("sibling read failed, retrying: " + narrow(path.wstring()) + " code=" + std::to_string(error));
        Sleep(retry_delay_ms);
    }
    return error == ERROR_SUCCESS ? ERROR_FILE_NOT_FOUND : error;
}
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
// to its signed SHA-256 entry before any privileged file move. Returns
// ERROR_SUCCESS or the most truthful Win32 error for the dialog: the raw
// I/O error for unreadable siblings, ERROR_INVALID_DATA for a bad signature
// or a manifest that does not bind the staged file.
DWORD verify_staged_against_signed_manifest(std::filesystem::path const& staged, std::string const& expected_hash) {
    const auto dir = staged.parent_path();
    std::string manifest, signature;
    if (const DWORD error = read_file_retry(dir / L"manifest.json", manifest, 1024 * 1024); error != ERROR_SUCCESS) {
        updater_log("update manifest unreadable code=" + std::to_string(error));
        return error;
    }
    if (const DWORD error = read_file_retry(dir / L"manifest.json.sig", signature, 16384); error != ERROR_SUCCESS) {
        updater_log("update manifest signature unreadable code=" + std::to_string(error));
        return error;
    }
    if (manifest.empty() || signature.empty()) {
        updater_log("update manifest empty");
        return ERROR_INVALID_DATA;
    }
    if (!verify_manifest_signature(manifest, signature)) {
        updater_log("update manifest signature invalid");
        return ERROR_INVALID_DATA;
    }
    const std::string file_name = staged.filename().string();
    if (file_name.empty() || file_name.size() > 128 || !manifest_binds_file(manifest, file_name, expected_hash)) {
        updater_log("update manifest does not bind staged file");
        return ERROR_INVALID_DATA;
    }
    return ERROR_SUCCESS;
}

int fail(DWORD code) { return static_cast<int>(code); }

// Fail closed when any component of the path is a reparse point: otherwise
// a pre-planted symlink could redirect the privileged file moves.
bool has_reparse_point(std::filesystem::path const& path) {
    std::error_code error;
    const auto absolute = std::filesystem::absolute(path, error);
    if (error) return true;
    auto current = absolute;
    for (;;) {
        const auto attributes = GetFileAttributesW(current.c_str());
        if (attributes == INVALID_FILE_ATTRIBUTES) {
            const auto code = GetLastError();
            if (code != ERROR_FILE_NOT_FOUND && code != ERROR_PATH_NOT_FOUND) return true;
        } else if (attributes & FILE_ATTRIBUTE_REPARSE_POINT) {
            return true;
        }
        const auto parent = current.parent_path();
        if (parent == current) break;
        current = parent;
    }
    return false;
}

std::string narrow(std::wstring_view value) {
    if (value.empty()) return {};
    const int size = WideCharToMultiByte(CP_UTF8, 0, value.data(), static_cast<int>(value.size()), nullptr, 0, nullptr, nullptr);
    if (size <= 0) return {};
    std::string output(static_cast<std::size_t>(size), '\0');
    if (WideCharToMultiByte(CP_UTF8, 0, value.data(), static_cast<int>(value.size()), output.data(), size, nullptr, nullptr) != size) return {};
    return output;
}

// Diagnosis for silent failures: every run appends timestamped stages to
// %LocalAppData%\Winchisel\logs\updater.log. Logging is best effort and never
// changes the update outcome.
void updater_log(std::string const& line) {
    PWSTR raw{};
    if (FAILED(SHGetKnownFolderPath(FOLDERID_LocalAppData, 0, nullptr, &raw)) || !raw) return;
    const std::filesystem::path dir = std::filesystem::path(raw) / L"Winchisel" / L"logs";
    CoTaskMemFree(raw);
    std::error_code error;
    std::filesystem::create_directories(dir, error);
    if (error) return;
    std::ofstream out(dir / L"updater.log", std::ios::app);
    if (!out) return;
    SYSTEMTIME stamp{};
    GetLocalTime(&stamp);
    char prefix[64]{};
    sprintf_s(prefix, "%04u-%02u-%02u %02u:%02u:%02u", stamp.wYear, stamp.wMonth, stamp.wDay,
        stamp.wHour, stamp.wMinute, stamp.wSecond);
    out << prefix << " [" << GetCurrentProcessId() << "] " << line << '\n';
}

// Small "something is happening" window shown while the app is closed and the
// replacement runs. It lives on its own thread so the file work never blocks
// it, has no close button, and closes itself when the update finishes. The
// bar is determinate: one step per completed phase (Zed steps per job).
struct ProgressWindow {
    HANDLE thread{};
    DWORD thread_id{};
    HANDLE ready{};
    HWND window{};
};

namespace {
constexpr UINT WM_UPDATE_STEP = WM_USER + 1;
constexpr int k_bar_control_id = 1001;
constexpr int k_update_steps = 9;
}  // namespace

LRESULT CALLBACK progress_wnd_proc(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam) {
    if (message == WM_UPDATE_STEP) {
        if (HWND bar = GetDlgItem(hwnd, k_bar_control_id)) SendMessageW(bar, PBM_STEPIT, 0, 0);
        return 0;
    }
    if (message == WM_CLOSE) return 0;  // No close button; never dismiss early.
    return DefWindowProcW(hwnd, message, wparam, lparam);
}

// Transient locks (virus scanner, indexer, Explorer) fail the first move;
// retry briefly like Zed's job engine instead of giving up immediately.
// GetLastError is preserved for the caller on final failure.
bool retry_file_op(std::string const& what, std::function<bool()> const& op) {
    const auto deadline = GetTickCount64() + 10'000;
    bool logged = false;
    for (;;) {
        SetLastError(ERROR_SUCCESS);
        if (op()) return true;
        const auto code = GetLastError();
        if (code != ERROR_SHARING_VIOLATION && code != ERROR_LOCK_VIOLATION && code != ERROR_ACCESS_DENIED) return false;
        if (GetTickCount64() >= deadline) return false;
        if (!logged) {
            logged = true;
            updater_log("retrying " + what + " code=" + std::to_string(code));
        }
        Sleep(100);
    }
}

DWORD WINAPI progress_thread_main(LPVOID param) {
    auto state = static_cast<ProgressWindow*>(param);
    INITCOMMONCONTROLSEX controls{.dwSize = sizeof(controls), .dwICC = ICC_PROGRESS_CLASS};
    InitCommonControlsEx(&controls);
    const HINSTANCE module = GetModuleHandleW(nullptr);
    constexpr wchar_t class_name[] = L"WinchiselUpdaterProgress";
    WNDCLASSW wc{};
    wc.lpfnWndProc = progress_wnd_proc;
    wc.hInstance = module;
    wc.lpszClassName = class_name;
    wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
    RegisterClassW(&wc);
    constexpr int width = 400, height = 136;
    HWND window = CreateWindowExW(WS_EX_TOPMOST | WS_EX_TOOLWINDOW, class_name, L"Winchisel Update",
        WS_POPUP | WS_CAPTION | WS_VISIBLE, CW_USEDEFAULT, CW_USEDEFAULT, width, height,
        nullptr, nullptr, module, nullptr);
    if (window) {
        // Share the handle before signalling ready: the event provides the
        // happens-before edge the worker thread needs for progress_step.
        state->window = window;
        RECT work{};
        if (SystemParametersInfoW(SPI_GETWORKAREA, 0, &work, 0)) {
            SetWindowPos(window, nullptr, work.left + (work.right - work.left - width) / 2,
                work.top + (work.bottom - work.top - height) / 2, 0, 0, SWP_NOSIZE | SWP_NOZORDER);
        }
        CreateWindowExW(0, L"STATIC", L"Updating Winchisel to the new version ...\r\nThis window closes automatically.",
            WS_CHILD | WS_VISIBLE, 16, 14, width - 32, 44, window, nullptr, module, nullptr);
        if (HWND bar = CreateWindowExW(0, PROGRESS_CLASSW, nullptr, WS_CHILD | WS_VISIBLE,
                16, 70, width - 32, 18, window, reinterpret_cast<HMENU>(static_cast<INT_PTR>(k_bar_control_id)), module, nullptr)) {
            SendMessageW(bar, PBM_SETRANGE, 0, MAKELPARAM(0, k_update_steps * 10));
            SendMessageW(bar, PBM_SETSTEP, 10, 0);
        }
    }
    SetEvent(state->ready);
    MSG message{};
    while (GetMessageW(&message, nullptr, 0, 0)) {
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }
    return 0;
}

void progress_show(ProgressWindow& ui) {
    ui.ready = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (!ui.ready) return;
    ui.thread = CreateThread(nullptr, 0, progress_thread_main, &ui, 0, &ui.thread_id);
    if (!ui.thread) {
        CloseHandle(ui.ready);
        ui.ready = nullptr;
        return;
    }
    // The thread signals after creating its window, so its message queue
    // exists and the WM_QUIT in progress_hide is delivered reliably.
    WaitForSingleObject(ui.ready, 10'000);
}

void progress_hide(ProgressWindow& ui) {
    if (ui.thread) {
        PostThreadMessageW(ui.thread_id, WM_QUIT, 0, 0);
        WaitForSingleObject(ui.thread, 10'000);
        CloseHandle(ui.thread);
        ui.thread = nullptr;
    }
    if (ui.ready) {
        CloseHandle(ui.ready);
        ui.ready = nullptr;
    }
    ui.window = nullptr;
}

// One determinate step per completed phase, posted to the UI thread.
void progress_step(ProgressWindow& ui) {
    if (ui.window) PostMessageW(ui.window, WM_UPDATE_STEP, 0, 0);
}

// Fail closed but loud: hide the progress window, log the stage, and tell the
// user what happened instead of vanishing silently.
int finish_update(ProgressWindow& ui, DWORD code, std::string const& stage) {
    progress_hide(ui);
    if (code == 0) {
        updater_log("success");
        return 0;
    }
    updater_log("failed stage=" + stage + " code=" + std::to_string(code));
    std::wstring message = L"The update could not be installed.\n\nStage: " +
        std::wstring(stage.begin(), stage.end()) + L"\nError code: " + std::to_wstring(code) +
        L"\n\nThe previous version was kept when possible.\nDetails: %LocalAppData%\\Winchisel\\logs\\updater.log";
    MessageBoxW(nullptr, message.c_str(), L"Winchisel Update", MB_OK | MB_ICONERROR | MB_TOPMOST | MB_SETFOREGROUND);
    return static_cast<int>(code);
}

} // namespace

int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int) {
    const auto staged_arg = argument(L"--staged");
    const auto target_arg = argument(L"--target");
    const auto hash_arg = argument(L"--sha256");
    const auto pid_arg = argument(L"--wait-pid");
    const auto host_arg = argument(L"--host");
    if (!staged_arg || !target_arg || !hash_arg || !pid_arg) return fail(ERROR_INVALID_PARAMETER);
    const auto expected_hash = ascii_hash(*hash_arg);
    if (!expected_hash) return fail(ERROR_INVALID_PARAMETER);
    const auto staged = absolute_existing_file(*staged_arg);
    const auto target = absolute_target(*target_arg);
    if (!staged || !target) return fail(ERROR_INVALID_PARAMETER);
    ProgressWindow ui{};
    progress_show(ui);
    updater_log("begin target=" + narrow(target->wstring()) + " staged=" + narrow(staged->wstring()) + " pid=" + narrow(*pid_arg));
    // The exact host marker the old app was using; falls back to the target
    // so a missing/invalid passthrough can never fail the update.
    auto host = *target;
    if (host_arg) {
        if (auto parsed = absolute_exe(*host_arg)) host = *parsed;
        else updater_log("ignoring invalid --host, using target");
    }
    if (sha256_hex(*staged) != *expected_hash) return finish_update(ui, ERROR_INVALID_DATA, "verify-staged");
    progress_step(ui);
    if (const DWORD manifest_error = verify_staged_against_signed_manifest(*staged, *expected_hash); manifest_error != ERROR_SUCCESS) return finish_update(ui, manifest_error, "verify-manifest");
    progress_step(ui);
    if (has_reparse_point(*target)) return finish_update(ui, ERROR_INVALID_DATA, "verify-target");
    progress_step(ui);
    updater_log("waiting for process exit");
    if (!wait_for_process(*pid_arg)) return finish_update(ui, ERROR_TIMEOUT, "wait-process");
    progress_step(ui);

    const auto nonce = std::to_wstring(GetCurrentProcessId()) + L"-" + std::to_wstring(GetTickCount64());
    const auto replacement = target->wstring() + L".update-" + nonce;
    const auto backup = target->wstring() + L".backup-" + nonce;
    if (!retry_file_op("copy", [&] { return CopyFileW(staged->c_str(), replacement.c_str(), TRUE) != FALSE; }) || sha256_hex(replacement) != *expected_hash) { DeleteFileW(replacement.c_str()); return finish_update(ui, ERROR_CRC, "copy"); }
    progress_step(ui);
    if (!retry_file_op("backup", [&] { return MoveFileExW(target->c_str(), backup.c_str(), MOVEFILE_WRITE_THROUGH) != FALSE; })) { const auto error = GetLastError(); DeleteFileW(replacement.c_str()); return finish_update(ui, error ? error : ERROR_ACCESS_DENIED, "backup"); }
    progress_step(ui);
    if (!retry_file_op("replace", [&] { return MoveFileExW(replacement.c_str(), target->c_str(), MOVEFILE_WRITE_THROUGH) != FALSE; })) {
        const auto error = GetLastError();
        MoveFileExW(backup.c_str(), target->c_str(), MOVEFILE_WRITE_THROUGH);
        DeleteFileW(replacement.c_str());
        return finish_update(ui, error ? error : ERROR_ACCESS_DENIED, "replace");
    }
    progress_step(ui);
    if (sha256_hex(*target) != *expected_hash) {
        MoveFileExW(target->c_str(), (target->wstring() + L".failed").c_str(), MOVEFILE_REPLACE_EXISTING);
        MoveFileExW(backup.c_str(), target->c_str(), MOVEFILE_REPLACE_EXISTING);
        return finish_update(ui, ERROR_CRC, "verify-target-hash");
    }
    progress_step(ui);
    updater_log("launching new version");
    if (!launch(*target, host)) {
        const auto error = GetLastError();
        MoveFileExW(target->c_str(), (target->wstring() + L".failed").c_str(), MOVEFILE_REPLACE_EXISTING);
        MoveFileExW(backup.c_str(), target->c_str(), MOVEFILE_REPLACE_EXISTING);
        return finish_update(ui, error ? error : ERROR_ACCESS_DENIED, "launch");
    }
    progress_step(ui);
    // Keep the newest few backups for manual recovery; drop the rest.
    {
        std::error_code error;
        const auto prefix = target->filename().wstring() + L".backup-";
        std::vector<std::pair<std::filesystem::file_time_type, std::filesystem::path>> backups;
        for (auto const& entry : std::filesystem::directory_iterator(target->parent_path(), error)) {
            const auto name = entry.path().filename().wstring();
            if (name.rfind(prefix, 0) != 0 || entry.path() == std::filesystem::path(backup)) continue;
            std::error_code time_error;
            const auto stamp = std::filesystem::last_write_time(entry.path(), time_error);
            if (time_error) { DeleteFileW(entry.path().c_str()); continue; }
            backups.emplace_back(stamp, entry.path());
        }
        std::ranges::sort(backups, [](auto const& a, auto const& b) { return a.first > b.first; });
        for (std::size_t i = 2; i < backups.size(); ++i) DeleteFileW(backups[i].second.c_str());
    }
    return finish_update(ui, 0, "done");
}
