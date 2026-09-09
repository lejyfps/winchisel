#include "winchisel/platform/startup.hpp"

#include "winchisel/platform/registry.hpp"
#include "winchisel/platform/system.hpp"
#include "com_apartment.hpp"

#include <Windows.h>
#include <shlobj.h>
#include <taskschd.h>
#include <comdef.h>

#include <algorithm>
#include <cwctype>
#include <filesystem>
#include <iterator>

#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "taskschd.lib")

namespace winchisel::platform {
namespace {

using winchisel::core::Error;
using winchisel::core::StartupEntry;
using winchisel::core::StartupLocation;

Error startup_error(std::string detail) {
    boot_log(("startup operation failed: " + detail).c_str());
    return {.detail = std::move(detail)};
}

std::wstring to_wide(std::string const& text) {
    if (text.empty()) return {};
    const auto length =
        MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(), static_cast<int>(text.size()), nullptr, 0);
    if (length <= 0) return {};
    std::wstring result(static_cast<std::size_t>(length), L'\0');
    if (MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(), static_cast<int>(text.size()), result.data(), length) <= 0)
        return {};
    return result;
}

std::string to_utf8(std::wstring_view text) {
    if (text.empty()) return {};
    const auto length = WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0, nullptr, nullptr);
    if (length <= 0) return {};
    std::string result(static_cast<std::size_t>(length), '\0');
    WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), result.data(), length, nullptr, nullptr);
    return result;
}

std::wstring to_lower(std::wstring_view text) {
    std::wstring lowered(text);
    std::ranges::transform(lowered, lowered.begin(), towlower);
    return lowered;
}

// Registry Run/RunOnce source descriptors for the scan.
struct RunSource {
    HKEY hive{};
    bool machine{};
    char const* run_key{};
    std::string approved_key;
    StartupLocation location{};
    bool default_enabled{};
    bool wow32{};
};

char const kRun[] = "Software\\Microsoft\\Windows\\CurrentVersion\\Run";
char const kRunOnce[] = "Software\\Microsoft\\Windows\\CurrentVersion\\RunOnce";
char const kApproved[] = "Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\StartupApproved\\";
char const kSystemAppData[] =
    "Software\\Classes\\Local Settings\\Software\\Microsoft\\Windows\\CurrentVersion\\AppModel\\SystemAppData";

// The StartupApproved flag Task Manager writes: 12 bytes, first byte
// 02 = enabled / 03 = disabled, trailing 8 bytes = change FILETIME.
RegistryNativeValue approved_value(bool enabled) {
    ULARGE_INTEGER now{};
    FILETIME filetime{};
    GetSystemTimeAsFileTime(&filetime);
    now.LowPart = filetime.dwLowDateTime;
    now.HighPart = filetime.dwHighDateTime;
    const auto bytes = winchisel::core::build_approved_data(enabled, now.QuadPart);
    RegistryNativeValue value{false, REG_BINARY, {}};
    value.data.assign(bytes.begin(), bytes.end());
    return value;
}

winchisel::core::RegistryTarget native_target(bool machine, std::string key_path, std::string value_name) {
    return {
        machine ? winchisel::core::RegistryHive::local_machine : winchisel::core::RegistryHive::current_user,
        std::move(key_path),
        std::move(value_name),
        winchisel::core::RegistryValueType::binary,
    };
}

bool read_approved_flag(bool machine, std::string const& approved_key, std::string const& value_name, bool default_enabled) {
    auto current = read_registry_native(native_target(machine, approved_key, value_name));
    if (!current || current->missing || current->type != REG_BINARY) return default_enabled;
    if (auto parsed = winchisel::core::parse_approved_data(current->data)) return *parsed;
    return default_enabled;
}

winchisel::core::Result<void> write_approved_flag(bool machine, std::string const& approved_key, std::string const& value_name, bool enabled) {
    return write_registry_native(native_target(machine, approved_key, value_name), approved_value(enabled));
}

void scan_run_source(RunSource const& source, std::vector<StartupEntry>& entries) {
    const auto access = KEY_QUERY_VALUE | (source.wow32 ? KEY_WOW64_32KEY : 0);
    HKEY key{};
    if (RegOpenKeyExW(source.hive, to_wide(source.run_key).c_str(), 0, access, &key) != ERROR_SUCCESS) return;
    for (DWORD index{};; ++index) {
        wchar_t name[256]{};
        DWORD length = static_cast<DWORD>(std::size(name));
        DWORD type{};
        if (RegEnumValueW(key, index, name, &length, nullptr, &type, nullptr, nullptr) != ERROR_SUCCESS) break;
        if (type != REG_SZ && type != REG_EXPAND_SZ) continue;
        std::wstring command;
        DWORD bytes{};
        if (RegQueryValueExW(key, name, nullptr, nullptr, nullptr, &bytes) != ERROR_SUCCESS || bytes == 0 ||
            bytes > 32768 || bytes % sizeof(wchar_t) != 0)
            continue;
        command.resize(bytes / sizeof(wchar_t));
        if (RegQueryValueExW(key, name, nullptr, nullptr, reinterpret_cast<LPBYTE>(command.data()), &bytes) != ERROR_SUCCESS)
            continue;
        while (!command.empty() && command.back() == L'\0') command.pop_back();
        if (command.empty()) continue;
        const std::string value_name = to_utf8({name, length});
        const bool enabled = read_approved_flag(source.machine, source.approved_key, value_name, source.default_enabled);
        std::string id = (source.machine ? "reg:machine:" : "reg:user:");
        id += value_name;
        entries.push_back({
            std::move(id),
            value_name,
            to_utf8(command),
            std::string(source.run_key),
            value_name,
            source.location,
            enabled,
        });
    }
    RegCloseKey(key);
}

void scan_startup_folder(bool common, std::vector<StartupEntry>& entries) {
    PWSTR raw{};
    if (FAILED(SHGetKnownFolderPath(common ? FOLDERID_CommonStartup : FOLDERID_Startup, 0, nullptr, &raw)) || raw == nullptr)
        return;
    const std::filesystem::path dir = raw;
    CoTaskMemFree(raw);
    std::error_code ec;
    if (!std::filesystem::is_directory(dir, ec)) return;
    ec.clear();
    std::filesystem::directory_iterator it(dir, std::filesystem::directory_options::skip_permission_denied, ec);
    if (ec) return;
    const std::string approved = std::string(kApproved) + "StartupFolder";
    for (auto const& item : it) {
        const std::wstring wide_file = item.path().filename().wstring();
        if (wide_file.empty()) continue;
        if (to_lower(wide_file) == L"desktop.ini") continue;
        const std::string file = to_utf8(wide_file);
        std::wstring wide_stem = item.path().stem().wstring();
        if (wide_stem.empty()) wide_stem = wide_file;
        const std::string stem = to_utf8(wide_stem);
        const std::string folder = to_utf8(dir.wstring());
        const bool enabled = read_approved_flag(common, approved, file, true);
        std::string id = common ? "folder:common:" : "folder:user:";
        id += file;
        entries.push_back({
            std::move(id),
            stem,
            to_utf8(item.path().wstring()),
            folder,
            file,
            common ? StartupLocation::folder_machine : StartupLocation::folder_user,
            enabled,
        });
    }
}

void scan_uwp_tasks(std::vector<StartupEntry>& entries) {
    // Packaged apps declare StartupTasks in their manifest; Windows records
    // the enable state per task below SystemAppData. Tasks without a state
    // value were never configured and are skipped rather than guessed.
    HKEY root{};
    if (RegOpenKeyExW(HKEY_CURRENT_USER, to_wide(kSystemAppData).c_str(), 0, KEY_ENUMERATE_SUB_KEYS, &root) != ERROR_SUCCESS)
        return;
    for (DWORD family_index{};; ++family_index) {
        wchar_t family[256]{};
        DWORD family_length = static_cast<DWORD>(std::size(family));
        if (RegEnumKeyExW(root, family_index, family, &family_length, nullptr, nullptr, nullptr, nullptr) != ERROR_SUCCESS)
            break;
        const std::wstring family_path = to_wide(kSystemAppData) + L"\\" + std::wstring(family, family_length);
        HKEY family_key{};
        if (RegOpenKeyExW(HKEY_CURRENT_USER, family_path.c_str(), 0, KEY_ENUMERATE_SUB_KEYS, &family_key) != ERROR_SUCCESS)
            continue;
        const std::string family_name = to_utf8({family, family_length});
        for (DWORD task_index{};; ++task_index) {
            wchar_t task[256]{};
            DWORD task_length = static_cast<DWORD>(std::size(task));
            if (RegEnumKeyExW(family_key, task_index, task, &task_length, nullptr, nullptr, nullptr, nullptr) != ERROR_SUCCESS)
                break;
            const std::wstring task_path = family_path + L"\\" + std::wstring(task, task_length);
            DWORD state{};
            DWORD bytes = sizeof(state);
            DWORD type{};
            if (RegGetValueW(HKEY_CURRENT_USER, task_path.c_str(), L"State", RRF_RT_REG_DWORD, &type, &state, &bytes) != ERROR_SUCCESS)
                continue;
            const auto parsed = winchisel::core::parse_uwp_startup_state(state);
            if (!parsed) continue;
            const std::string task_id = to_utf8({task, task_length});
            std::string id = "uwp:" + family_name + ":" + task_id;
            entries.push_back({
                std::move(id),
                family_name + " (" + task_id + ")",
                family_name,
                to_utf8(task_path),
                family_name + "|" + task_id,
                StartupLocation::uwp_task,
                *parsed,
            });
        }
        RegCloseKey(family_key);
    }
    RegCloseKey(root);
}

std::string bstr_to_utf8(BSTR value, DWORD length) {
    if (value == nullptr || length == 0) return {};
    return to_utf8({value, length});
}

std::string trigger_summary(std::vector<int> const& types) {
    std::string summary;
    for (int type : types) {
        if (type != 8 && type != 9) continue;
        if (!summary.empty()) summary += ", ";
        summary += type == 8 ? "Boot" : "Logon";
    }
    return summary;
}

// Reads one scheduled task. Returns nullopt when the task has no
// boot/logon trigger or cannot be inspected; failures never abort the scan.
struct ScannedTask {
    std::string name;
    std::string path;
    std::string description;
    std::string triggers;
    std::string action;
    bool enabled{};
};

std::optional<ScannedTask> inspect_task(IRegisteredTask* task) {
    BSTR name_raw{};
    BSTR path_raw{};
    VARIANT_BOOL enabled_raw{};
    if (FAILED(task->get_Name(&name_raw)) || FAILED(task->get_Path(&path_raw)) || FAILED(task->get_Enabled(&enabled_raw))) {
        SysFreeString(name_raw);
        SysFreeString(path_raw);
        return std::nullopt;
    }
    ScannedTask scanned;
    scanned.name = bstr_to_utf8(name_raw, SysStringLen(name_raw));
    scanned.path = bstr_to_utf8(path_raw, SysStringLen(path_raw));
    scanned.enabled = enabled_raw == VARIANT_TRUE;
    SysFreeString(name_raw);
    SysFreeString(path_raw);
    if (scanned.name.empty()) return std::nullopt;

    ITaskDefinition* definition{};
    if (FAILED(task->get_Definition(&definition)) || definition == nullptr) return std::nullopt;
    bool has_startup_trigger = false;
    ITriggerCollection* triggers{};
    if (SUCCEEDED(definition->get_Triggers(&triggers)) && triggers != nullptr) {
        LONG count{};
        if (SUCCEEDED(triggers->get_Count(&count))) {
            std::vector<int> types;
            for (LONG index = 1; index <= count; ++index) {
                ITrigger* trigger{};
                if (FAILED(triggers->get_Item(index, &trigger)) || trigger == nullptr) continue;
                TASK_TRIGGER_TYPE2 type{};
                if (SUCCEEDED(trigger->get_Type(&type))) {
                    types.push_back(static_cast<int>(type));
                    if (winchisel::core::is_startup_trigger(static_cast<int>(type))) has_startup_trigger = true;
                }
                trigger->Release();
            }
            scanned.triggers = trigger_summary(types);
        }
        triggers->Release();
    }
    if (!has_startup_trigger) {
        definition->Release();
        return std::nullopt;
    }
    IRegistrationInfo* registration{};
    if (SUCCEEDED(definition->get_RegistrationInfo(&registration)) && registration != nullptr) {
        BSTR description_raw{};
        if (SUCCEEDED(registration->get_Description(&description_raw))) {
            scanned.description = bstr_to_utf8(description_raw, SysStringLen(description_raw));
            SysFreeString(description_raw);
        }
        registration->Release();
    }
    IActionCollection* actions{};
    if (SUCCEEDED(definition->get_Actions(&actions)) && actions != nullptr) {
        LONG count{};
        if (SUCCEEDED(actions->get_Count(&count)) && count >= 1) {
            IAction* action{};
            if (SUCCEEDED(actions->get_Item(1, &action)) && action != nullptr) {
                TASK_ACTION_TYPE action_type{};
                if (SUCCEEDED(action->get_Type(&action_type)) && action_type == TASK_ACTION_EXEC) {
                    IExecAction* exec{};
                    if (SUCCEEDED(action->QueryInterface(IID_PPV_ARGS(&exec))) && exec != nullptr) {
                        BSTR command_raw{};
                        BSTR args_raw{};
                        if (SUCCEEDED(exec->get_Path(&command_raw))) {
                            scanned.action = bstr_to_utf8(command_raw, SysStringLen(command_raw));
                            SysFreeString(command_raw);
                        }
                        if (SUCCEEDED(exec->get_Arguments(&args_raw))) {
                            const auto args = bstr_to_utf8(args_raw, SysStringLen(args_raw));
                            SysFreeString(args_raw);
                            if (!args.empty()) {
                                scanned.action += " ";
                                scanned.action += args;
                            }
                        }
                        exec->Release();
                    }
                }
                action->Release();
            }
        }
        actions->Release();
    }
    definition->Release();
    return scanned;
}

void visit_task_folder(ITaskFolder* folder, std::vector<StartupEntry>& entries) {
    IRegisteredTaskCollection* tasks{};
    if (SUCCEEDED(folder->GetTasks(TASK_ENUM_HIDDEN, &tasks)) && tasks != nullptr) {
        LONG count{};
        if (SUCCEEDED(tasks->get_Count(&count))) {
            for (LONG index = 1; index <= count; ++index) {
                VARIANT item{};
                item.vt = VT_I4;
                item.lVal = index;
                IRegisteredTask* task{};
                if (FAILED(tasks->get_Item(item, &task)) || task == nullptr) continue;
                if (auto scanned = inspect_task(task)) {
                    std::string detail = scanned->triggers;
                    if (!scanned->action.empty()) {
                        if (!detail.empty()) detail += " | ";
                        detail += scanned->action;
                    }
                    if (!scanned->description.empty()) {
                        if (!detail.empty()) detail += " | ";
                        detail += scanned->description;
                    }
                    entries.push_back({
                        "task:" + scanned->path,
                        scanned->name,
                        scanned->action,
                        std::move(detail),
                        scanned->path,
                        StartupLocation::scheduled_task,
                        scanned->enabled,
                    });
                }
                task->Release();
            }
        }
        tasks->Release();
    }
    ITaskFolderCollection* folders{};
    if (SUCCEEDED(folder->GetFolders(0, &folders)) && folders != nullptr) {
        LONG count{};
        if (SUCCEEDED(folders->get_Count(&count))) {
            for (LONG index = 1; index <= count; ++index) {
                VARIANT item{};
                item.vt = VT_I4;
                item.lVal = index;
                ITaskFolder* sub{};
                if (FAILED(folders->get_Item(item, &sub)) || sub == nullptr) continue;
                visit_task_folder(sub, entries);
                sub->Release();
            }
        }
        folders->Release();
    }
}

void scan_scheduled_tasks(std::vector<StartupEntry>& entries) {
    detail::ComApartment com;
    if (!com) return;
    ITaskService* service{};
    if (FAILED(CoCreateInstance(CLSID_TaskScheduler, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&service))) || service == nullptr)
        return;
    VARIANT empty{};
    VariantInit(&empty);
    if (FAILED(service->Connect(empty, empty, empty, empty))) {
        service->Release();
        return;
    }
    BSTR root_path = SysAllocString(L"\\");
    ITaskFolder* root{};
    const auto status = service->GetFolder(root_path, &root);
    SysFreeString(root_path);
    service->Release();
    if (FAILED(status) || root == nullptr) return;
    visit_task_folder(root, entries);
    root->Release();
}

}  // namespace

winchisel::core::Result<std::vector<StartupEntry>> scan_startup_entries() {
    std::vector<StartupEntry> entries;
    const std::string approved_run = std::string(kApproved) + "Run";
    const std::string approved_run_once = std::string(kApproved) + "RunOnce";
    const std::string approved_run32 = std::string(kApproved) + "Run32";
    const std::string approved_run_once32 = std::string(kApproved) + "RunOnce32";
    const RunSource sources[] = {
        {HKEY_CURRENT_USER, false, kRun, approved_run, StartupLocation::registry_run_user, true, false},
        {HKEY_LOCAL_MACHINE, true, kRun, approved_run, StartupLocation::registry_run_machine, true, false},
        {HKEY_CURRENT_USER, false, kRunOnce, approved_run_once, StartupLocation::registry_runonce_user, true, false},
        {HKEY_LOCAL_MACHINE, true, kRunOnce, approved_run_once, StartupLocation::registry_runonce_machine, true, false},
        {HKEY_LOCAL_MACHINE, true, kRun, approved_run32, StartupLocation::registry_run32_machine, false, true},
        {HKEY_LOCAL_MACHINE, true, kRunOnce, approved_run_once32, StartupLocation::registry_runonce32_machine, false, true},
    };
    for (auto const& source : sources) scan_run_source(source, entries);
    scan_startup_folder(false, entries);
    scan_startup_folder(true, entries);
    scan_uwp_tasks(entries);
    scan_scheduled_tasks(entries);
    std::ranges::sort(entries, [](StartupEntry const& left, StartupEntry const& right) {
        return to_lower(to_wide(left.name)) < to_lower(to_wide(right.name));
    });
    return entries;
}

std::pair<bool, std::string> approved_key_for(StartupEntry const& entry) {
    const bool machine = entry.location == StartupLocation::registry_run_machine ||
        entry.location == StartupLocation::registry_runonce_machine ||
        entry.location == StartupLocation::registry_run32_machine ||
        entry.location == StartupLocation::registry_runonce32_machine ||
        entry.location == StartupLocation::folder_machine;
    std::string subkey = "Run";
    switch (entry.location) {
        case StartupLocation::registry_runonce_user:
        case StartupLocation::registry_runonce_machine: subkey = "RunOnce"; break;
        case StartupLocation::registry_run32_machine: subkey = "Run32"; break;
        case StartupLocation::registry_runonce32_machine: subkey = "RunOnce32"; break;
        case StartupLocation::folder_user:
        case StartupLocation::folder_machine: subkey = "StartupFolder"; break;
        case StartupLocation::registry_run_user:
        case StartupLocation::registry_run_machine:
        case StartupLocation::uwp_task:
        case StartupLocation::scheduled_task: break;
    }
    return {machine, std::string(kApproved) + subkey};
}

winchisel::core::Result<void> set_scheduled_entry_enabled(std::string const& full_path, bool enabled) {
    detail::ComApartment com;
    if (!com) return std::unexpected(startup_error("COM initialization failed: " + std::to_string(com.hr)));
    ITaskService* service{};
    if (FAILED(CoCreateInstance(CLSID_TaskScheduler, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&service))) || service == nullptr)
        return std::unexpected(startup_error("task scheduler unavailable"));
    VARIANT empty{};
    VariantInit(&empty);
    if (FAILED(service->Connect(empty, empty, empty, empty))) {
        service->Release();
        return std::unexpected(startup_error("task scheduler connect failed"));
    }
    BSTR root_path = SysAllocString(L"\\");
    ITaskFolder* root{};
    auto status = service->GetFolder(root_path, &root);
    SysFreeString(root_path);
    service->Release();
    if (FAILED(status) || root == nullptr) return std::unexpected(startup_error("task folder unavailable"));
    const auto wide_path = to_wide(full_path);
    if (wide_path.empty()) {
        root->Release();
        return std::unexpected(startup_error("invalid task path"));
    }
    BSTR task_path = SysAllocString(wide_path.c_str());
    IRegisteredTask* task{};
    status = root->GetTask(task_path, &task);
    SysFreeString(task_path);
    root->Release();
    if (FAILED(status) || task == nullptr) return std::unexpected(startup_error("task not found: " + full_path));
    status = task->put_Enabled(enabled ? VARIANT_TRUE : VARIANT_FALSE);
    task->Release();
    if (FAILED(status)) return std::unexpected(startup_error("task update failed: " + std::to_string(status)));
    return {};
}

winchisel::core::Result<void> set_startup_entry_enabled(StartupEntry const& entry, bool enabled) {
    switch (entry.location) {
        case StartupLocation::registry_run_user:
        case StartupLocation::registry_run_machine:
        case StartupLocation::registry_runonce_user:
        case StartupLocation::registry_runonce_machine:
        case StartupLocation::registry_run32_machine:
        case StartupLocation::registry_runonce32_machine:
        case StartupLocation::folder_user:
        case StartupLocation::folder_machine: {
            const auto approved = approved_key_for(entry);
            return write_approved_flag(approved.first, approved.second, entry.key, enabled);
        }
        case StartupLocation::uwp_task: {
            const auto separator = entry.key.find('|');
            if (separator == std::string::npos) return std::unexpected(startup_error("invalid packaged app key"));
            const std::string subkey =
                std::string(kSystemAppData) + "\\" + entry.key.substr(0, separator) + "\\" + entry.key.substr(separator + 1);
            winchisel::core::RegistryTarget target{
                winchisel::core::RegistryHive::current_user,
                subkey,
                "State",
                winchisel::core::RegistryValueType::dword,
            };
            return write_registry_value(target, winchisel::core::RegistryValue{enabled ? std::uint32_t{2} : std::uint32_t{1}});
        }
        case StartupLocation::scheduled_task: return set_scheduled_entry_enabled(entry.key, enabled);
    }
    return std::unexpected(startup_error("unknown startup entry"));
}
}  // namespace winchisel::platform
