#include "winchisel/platform/registry.hpp"
#include "winchisel/platform/revert.hpp"
#include "winchisel/platform/system.hpp"

#include <windows.h>

#include <cstring>
#include <string>
#include <vector>

namespace winchisel::platform {
namespace {

using winchisel::core::Error;

using winchisel::core::RegistryHive;
using winchisel::core::RegistryTarget;
using winchisel::core::RegistryValue;
using winchisel::core::RegistryValueType;

HKEY native_hive(RegistryHive hive) {
    return hive == RegistryHive::current_user ? HKEY_CURRENT_USER : HKEY_LOCAL_MACHINE;
}

std::wstring to_wide(std::string const& text) {
    if (text.empty()) {
        return {};
    }
    const auto length = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(), static_cast<int>(text.size()), nullptr, 0);
    if (length <= 0) {
        return {};
    }
    std::wstring result(static_cast<size_t>(length), L'\0');
    if (MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(), static_cast<int>(text.size()), result.data(), length) <= 0) {
        return {};
    }
    return result;
}

std::string to_utf8(std::wstring const& text) {
    if (text.empty()) {
        return {};
    }
    const auto length = WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0, nullptr, nullptr);
    std::string result(static_cast<size_t>(length), '\0');
    WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), result.data(), length, nullptr, nullptr);
    return result;
}

Error registry_error(std::string detail) {
    boot_log(("registry operation failed: " + detail).c_str());
    return {.detail = std::move(detail)};
}

bool expected_type_matches(RegistryValueType expected, DWORD actual) {
    switch (expected) {
        case RegistryValueType::dword: return actual == REG_DWORD;
        case RegistryValueType::string: return actual == REG_SZ || actual == REG_EXPAND_SZ;
        case RegistryValueType::binary: return actual == REG_BINARY;
    }
    return false;
}

}  // namespace

// True when writing `desired` would leave the stored value untouched, so the
// journal can skip no-op pairs (re-applying an already-matching profile
// stays silent).
bool registry_native_matches(winchisel::core::RegistryNativeValue const& before, RegistryTarget const& target,
    RegistryValue const& desired) {
    (void)target;
    if (const auto* missing = std::get_if<std::monostate>(&desired)) {
        (void)missing;
        return before.missing;
    }
    if (before.missing) return false;
    if (const auto* dword = std::get_if<std::uint32_t>(&desired)) {
        if (before.type != REG_DWORD || before.data.size() != sizeof(std::uint32_t)) return false;
        std::uint32_t stored{};
        std::memcpy(&stored, before.data.data(), sizeof(stored));
        return stored == *dword;
    }
    if (const auto* text = std::get_if<std::string>(&desired)) {
        if (before.type != REG_SZ && before.type != REG_EXPAND_SZ) return false;
        if (before.data.size() % sizeof(wchar_t) != 0) return false;
        const auto chars = before.data.size() / sizeof(wchar_t);
        std::wstring wide(reinterpret_cast<wchar_t const*>(before.data.data()), chars);
        while (!wide.empty() && wide.back() == L'\0') wide.pop_back();
        const int length =
            WideCharToMultiByte(CP_UTF8, 0, wide.data(), static_cast<int>(wide.size()), nullptr, 0, nullptr, nullptr);
        if (length <= 0) return false;
        std::string utf8(static_cast<std::size_t>(length), '\0');
        if (WideCharToMultiByte(CP_UTF8, 0, wide.data(), static_cast<int>(wide.size()), utf8.data(), length, nullptr,
                nullptr) <= 0)
            return false;
        return utf8 == *text;
    }
    if (const auto* binary = std::get_if<std::vector<std::uint8_t>>(&desired)) {
        return before.type == REG_BINARY && before.data == *binary;
    }
    return false;
}

namespace {

struct RawRegistryValue {
    bool missing{true};
    DWORD type{};
    std::vector<std::uint8_t> data;
};

winchisel::core::Result<RawRegistryValue> read_raw(RegistryTarget const& target) {
    const auto key_path = to_wide(target.key_path);
    const auto value_name = to_wide(target.value_name);
    if ((!target.key_path.empty() && key_path.empty()) || (!target.value_name.empty() && value_name.empty())) {
        return std::unexpected(registry_error("invalid UTF-8 registry target"));
    }
    HKEY key{};
    auto status = RegOpenKeyExW(native_hive(target.hive), key_path.c_str(), 0, KEY_QUERY_VALUE, &key);
    if (status == ERROR_FILE_NOT_FOUND) return RawRegistryValue{};
    if (status != ERROR_SUCCESS) return std::unexpected(registry_error(std::to_string(status)));
    DWORD type{};
    DWORD bytes{};
    status = RegQueryValueExW(key, value_name.c_str(), nullptr, &type, nullptr, &bytes);
    if (status == ERROR_FILE_NOT_FOUND) { RegCloseKey(key); return RawRegistryValue{}; }
    if (status != ERROR_SUCCESS) { RegCloseKey(key); return std::unexpected(registry_error(std::to_string(status))); }
    RawRegistryValue raw{.missing = false, .type = type, .data = std::vector<std::uint8_t>(bytes)};
    status = RegQueryValueExW(key, value_name.c_str(), nullptr, &type, raw.data.data(), &bytes);
    RegCloseKey(key);
    if (status != ERROR_SUCCESS) return std::unexpected(registry_error(std::to_string(status)));
    raw.type = type;
    raw.data.resize(bytes);
    return raw;
}

winchisel::core::Result<void> write_raw(RegistryTarget const& target, RawRegistryValue const& raw) {
    const auto key_path = to_wide(target.key_path);
    const auto value_name = to_wide(target.value_name);
    HKEY key{};
    LONG status{};
    if (raw.missing) {
        status = RegOpenKeyExW(native_hive(target.hive), key_path.c_str(), 0, KEY_SET_VALUE, &key);
        if (status == ERROR_FILE_NOT_FOUND) return {};
        if (status != ERROR_SUCCESS) return std::unexpected(registry_error(std::to_string(status)));
        status = RegDeleteValueW(key, value_name.c_str());
        if (status == ERROR_FILE_NOT_FOUND) status = ERROR_SUCCESS;
        RegCloseKey(key);
        if (status != ERROR_SUCCESS) return std::unexpected(registry_error(std::to_string(status)));
        return {};
    }
    status = RegCreateKeyExW(native_hive(target.hive), key_path.c_str(), 0, nullptr, 0, KEY_SET_VALUE, nullptr, &key, nullptr);
    if (status != ERROR_SUCCESS) return std::unexpected(registry_error(std::to_string(status)));
    status = RegSetValueExW(key, value_name.c_str(), 0, raw.type, raw.data.empty() ? nullptr : raw.data.data(), static_cast<DWORD>(raw.data.size()));
    RegCloseKey(key);
    if (status != ERROR_SUCCESS) return std::unexpected(registry_error(std::to_string(status)));
    return {};
}

}  // namespace

winchisel::core::Result<RegistryValue> read_registry_value(RegistryTarget const& target) {
    const auto key_path = to_wide(target.key_path);
    const auto value_name = to_wide(target.value_name);
    if ((!target.key_path.empty() && key_path.empty()) || (!target.value_name.empty() && value_name.empty())) {
        return std::unexpected(registry_error("invalid UTF-8 registry target"));
    }

    HKEY key{};
    auto status = RegOpenKeyExW(native_hive(target.hive), key_path.c_str(), 0, KEY_QUERY_VALUE, &key);
    if (status == ERROR_FILE_NOT_FOUND) {
        return RegistryValue{std::monostate{}};
    }
    if (status != ERROR_SUCCESS) {
        return std::unexpected(registry_error(std::to_string(status)));
    }

    DWORD type{};
    DWORD bytes{};
    status = RegQueryValueExW(key, value_name.c_str(), nullptr, &type, nullptr, &bytes);
    if (status == ERROR_FILE_NOT_FOUND) {
        RegCloseKey(key);
        return RegistryValue{std::monostate{}};
    }
    if (status != ERROR_SUCCESS || !expected_type_matches(target.type, type)) {
        RegCloseKey(key);
        return std::unexpected(registry_error(status == ERROR_SUCCESS ? "unexpected registry value type" : std::to_string(status)));
    }

    std::vector<std::uint8_t> data(bytes);
    status = RegQueryValueExW(key, value_name.c_str(), nullptr, &type, data.data(), &bytes);
    RegCloseKey(key);
    if (status != ERROR_SUCCESS) {
        return std::unexpected(registry_error(std::to_string(status)));
    }
    if (!expected_type_matches(target.type, type)) {
        return std::unexpected(registry_error("unexpected registry value type"));
    }
    data.resize(bytes);

    switch (target.type) {
        case RegistryValueType::dword: {
            if (data.size() != sizeof(std::uint32_t)) {
                return std::unexpected(registry_error("invalid DWORD size"));
            }
            std::uint32_t dword{};
            std::memcpy(&dword, data.data(), sizeof(dword));
            return RegistryValue{dword};
        }
        case RegistryValueType::string: {
            std::wstring text(reinterpret_cast<wchar_t const*>(data.data()), data.size() / sizeof(wchar_t));
            if (!text.empty() && text.back() == L'\0') {
                text.pop_back();
            }
            return RegistryValue{to_utf8(text)};
        }
        case RegistryValueType::binary:
            return RegistryValue{std::move(data)};
    }
    return std::unexpected(registry_error("unknown registry value type"));
}

winchisel::core::Result<void> write_registry_value(RegistryTarget const& target, RegistryValue const& value) {
    const auto key_path = to_wide(target.key_path);
    const auto value_name = to_wide(target.value_name);
    if ((!target.key_path.empty() && key_path.empty()) || (!target.value_name.empty() && value_name.empty())) {
        return std::unexpected(registry_error("invalid UTF-8 registry target"));
    }

    HKEY key{};
    LONG status{};
    if (std::holds_alternative<std::monostate>(value)) {
        status = RegOpenKeyExW(native_hive(target.hive), key_path.c_str(), 0, KEY_SET_VALUE, &key);
        if (status == ERROR_FILE_NOT_FOUND) {
            return {};
        }
    } else {
        status = RegCreateKeyExW(native_hive(target.hive), key_path.c_str(), 0, nullptr, 0, KEY_SET_VALUE, nullptr, &key, nullptr);
    }
    if (status != ERROR_SUCCESS) {
        return std::unexpected(registry_error(std::to_string(status)));
    }

    if (std::holds_alternative<std::monostate>(value)) {
        status = RegDeleteValueW(key, value_name.c_str());
        if (status == ERROR_FILE_NOT_FOUND) {
            status = ERROR_SUCCESS;
        }
    } else if (const auto* dword = std::get_if<std::uint32_t>(&value); dword && target.type == RegistryValueType::dword) {
        status = RegSetValueExW(key, value_name.c_str(), 0, REG_DWORD, reinterpret_cast<BYTE const*>(dword), sizeof(*dword));
    } else if (const auto* string = std::get_if<std::string>(&value); string && target.type == RegistryValueType::string) {
        const auto text = to_wide(*string);
        if (!string->empty() && text.empty()) {
            status = ERROR_INVALID_DATA;
        } else {
            // Preserve REG_EXPAND_SZ: rewriting an expandable string as
            // REG_SZ would change how consumers expand its content, and a
            // later rollback could never restore the original type.
            DWORD current_type{};
            DWORD write_type = REG_SZ;
            if (RegGetValueW(native_hive(target.hive), key_path.c_str(), value_name.c_str(),
                             RRF_RT_ANY | RRF_NOEXPAND, &current_type, nullptr, nullptr) == ERROR_SUCCESS &&
                current_type == REG_EXPAND_SZ) {
                write_type = REG_EXPAND_SZ;
            }
            status = RegSetValueExW(key, value_name.c_str(), 0, write_type, reinterpret_cast<BYTE const*>(text.c_str()),
                                    static_cast<DWORD>((text.size() + 1) * sizeof(wchar_t)));
        }
    } else if (const auto* binary = std::get_if<std::vector<std::uint8_t>>(&value); binary && target.type == RegistryValueType::binary) {
        status = RegSetValueExW(key, value_name.c_str(), 0, REG_BINARY, binary->data(), static_cast<DWORD>(binary->size()));
    } else {
        status = ERROR_DATATYPE_MISMATCH;
    }
    RegCloseKey(key);
    if (status != ERROR_SUCCESS) {
        return std::unexpected(registry_error(std::to_string(status)));
    }
    return {};
}

winchisel::core::Result<void> write_registry_values_atomic(
    std::vector<std::pair<RegistryTarget, RegistryValue>> const& changes, std::string_view page,
    std::string_view label) {
    std::vector<RawRegistryValue> previous;
    previous.reserve(changes.size());
    for (auto const& [target, _] : changes) {
        auto value = read_raw(target);
        if (!value) return std::unexpected(value.error());
        previous.push_back(std::move(*value));
    }
    for (std::size_t index{}; index < changes.size(); ++index) {
        auto result = write_registry_value(changes[index].first, changes[index].second);
        if (result) continue;
        auto original_error = result.error();
        bool rollback_ok = true;
        while (index > 0) {
            --index;
            rollback_ok = static_cast<bool>(write_raw(changes[index].first, previous[index])) && rollback_ok;
        }
        if (!rollback_ok) original_error.detail += "; rollback incomplete";
        return std::unexpected(std::move(original_error));
    }
    if (!label.empty() && !revert_recording_suppressed()) {
        winchisel::core::RevertEntry entry = make_revert_entry(std::string(page), std::string(label));
        for (std::size_t index{}; index < changes.size(); ++index) {
            winchisel::core::RegistryNativeValue before{
                previous[index].missing, previous[index].type, previous[index].data};
            if (registry_native_matches(before, changes[index].first, changes[index].second)) continue;
            winchisel::core::RevertRegistryStep step;
            step.target = changes[index].first;
            step.before = std::move(before);
            entry.registry.push_back(std::move(step));
        }
        if (auto recorded = record_revert(std::move(entry)); !recorded) {
            boot_log(("revert journal record failed: " + recorded.error().detail).c_str());
        }
    }
    return {};
}

winchisel::core::Result<void> rollback_registry_values(
    std::vector<std::pair<RegistryTarget, RegistryValue>> const& previous) {
    bool ok = true;
    for (auto it = previous.rbegin(); it != previous.rend(); ++it) {
        ok = static_cast<bool>(write_registry_value(it->first, it->second)) && ok;
    }
    if (!ok) return std::unexpected(registry_error("rollback incomplete"));
    return {};
}

winchisel::core::Result<RegistryNativeValue> read_registry_native(RegistryTarget const& target) {
    auto raw = read_raw(target);
    if (!raw) return std::unexpected(raw.error());
    return RegistryNativeValue{raw->missing, raw->type, std::move(raw->data)};
}

winchisel::core::Result<void> write_registry_native(RegistryTarget const& target, RegistryNativeValue const& value) {
    return write_raw(target, RawRegistryValue{value.missing, value.type, value.data});
}

}  // namespace winchisel::platform
