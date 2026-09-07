#pragma once

#include "winchisel/core/error.hpp"
#include "winchisel/core/os.hpp"
#include "winchisel/core/settings.hpp"

#include <cstdint>
#include <filesystem>
#include <string>

namespace winchisel::platform {

std::uint32_t current_windows_build();
bool is_supported_windows();
bool is_user_an_admin();

// Restart current process elevated (ShellExecuteW runas). Returns false if the user cancelled UAC.
bool restart_elevated();

std::filesystem::path appdata_dir();
std::filesystem::path settings_path();

winchisel::core::Result<winchisel::core::Settings> load_settings();
winchisel::core::Result<void> save_settings(const winchisel::core::Settings& settings);

void show_unsupported_os_message();
void show_error_message(const wchar_t* text);
void set_console_visible(bool visible);
void boot_log(const char* message);
void set_current_directory_to_exe();

}  // namespace winchisel::platform
