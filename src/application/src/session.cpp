#include "winchisel/application/session.hpp"

#include "winchisel/platform/system.hpp"

namespace winchisel::application {

Session& Session::instance() {
    static Session s;
    return s;
}

bool Session::bootstrap() {
    winchisel::platform::boot_log("bootstrap begin");
    if (!winchisel::platform::is_supported_windows()) {
        winchisel::platform::boot_log("unsupported os");
        winchisel::platform::show_unsupported_os_message();
        return false;
    }
    if (!winchisel::platform::is_user_an_admin()) {
        winchisel::platform::boot_log("not admin — continuing (elevation after first window is stable)");
    } else {
        winchisel::platform::boot_log("admin ok");
    }
    if (auto loaded = winchisel::platform::load_settings()) {
        settings_ = *loaded;
    } else {
        settings_ = winchisel::core::settings_defaults();
    }
    settings_.autostart_enabled = winchisel::platform::is_autostart_enabled();
    (void)winchisel::platform::save_settings(settings_);
    winchisel::platform::set_console_visible(settings_.show_console);
    return true;
}

void Session::set_settings(winchisel::core::Settings settings) {
    if (settings.autostart_enabled != settings_.autostart_enabled) {
        if (auto result = winchisel::platform::set_autostart_enabled(settings.autostart_enabled); !result) {
            settings.autostart_enabled = settings_.autostart_enabled;
        }
    }
    if (settings.show_console != settings_.show_console) {
        winchisel::platform::set_console_visible(settings.show_console);
    }
    settings_ = std::move(settings);
    (void)winchisel::platform::save_settings(settings_);
}

}  // namespace winchisel::application
