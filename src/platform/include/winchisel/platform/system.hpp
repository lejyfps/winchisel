#pragma once

#include "winchisel/core/error.hpp"
#include "winchisel/core/settings.hpp"

#include <cstdint>
#include <filesystem>
#include <functional>
#include <string>

namespace winchisel::platform {

inline constexpr std::uint32_t k_min_windows_build = 26100;

std::uint32_t current_windows_build();
bool is_supported_windows();
bool is_user_an_admin();

// Restart current process elevated (ShellExecuteW runas). Returns false if the user cancelled UAC.
bool restart_elevated();
bool is_autostart_enabled();
winchisel::core::Result<void> set_autostart_enabled(bool enabled);

std::filesystem::path appdata_dir();
std::filesystem::path settings_path();

winchisel::core::Result<winchisel::core::Settings> load_settings();
winchisel::core::Language system_ui_language();
winchisel::core::Result<void> save_settings(const winchisel::core::Settings& settings);

void show_unsupported_os_message();
void show_error_message(const wchar_t* text);
void set_console_visible(bool visible);
void boot_log(const char* message);
void set_current_directory_to_exe();

using ProtectionProgress = std::function<void(bool is_stage, std::string_view text)>;

winchisel::core::Result<void> create_restore_point();
winchisel::core::Result<void> run_system_repair(ProtectionProgress const& progress);
winchisel::core::Result<void> run_disk_cleanup();
winchisel::core::Result<void> remove_temp_files(ProtectionProgress const& progress);

// Command-based Extras actions.  These functions never create a visible console
// window and are intended to be called from a background worker by the UI.
winchisel::core::Result<void> apply_winchisel_power_plan();
winchisel::core::Result<void> set_widgets_removed(bool enabled);
winchisel::core::Result<void> set_teredo_disabled(bool enabled);
winchisel::core::Result<void> set_hpet_disabled(bool enabled);

struct ExtrasCommandState {
    bool power_plan_active{};
    bool widgets_removed{};
    bool hpet_disabled{};
};
ExtrasCommandState read_extras_command_state();

}  // namespace winchisel::platform
