#include "winchisel/application/session.hpp"

#include "winchisel/platform/system.hpp"

namespace winchisel::application {

Session& Session::instance() {
    static Session s;
    return s;
}

bool Session::bootstrap() {
    if (!winchisel::platform::is_supported_windows()) {
        winchisel::platform::show_unsupported_os_message();
        return false;
    }
    if (!winchisel::platform::is_user_an_admin()) {
        winchisel::platform::restart_elevated();
        return false;
    }
    if (auto loaded = winchisel::platform::load_settings()) {
        settings_ = *loaded;
    } else {
        settings_ = winchisel::core::settings_defaults();
    }
    winchisel::platform::set_console_visible(settings_.show_console);
    return true;
}

void Session::set_settings(winchisel::core::Settings settings) {
    settings_ = std::move(settings);
    (void)winchisel::platform::save_settings(settings_);
}

}  // namespace winchisel::application
