#pragma once

#include "winchisel/core/navigation.hpp"
#include "winchisel/core/settings.hpp"

namespace winchisel::application {

class Session {
public:
    static Session& instance();

    bool bootstrap();

    const winchisel::core::Settings& settings() const { return settings_; }
    void set_settings(winchisel::core::Settings settings);

    winchisel::core::Screen current_screen() const { return screen_; }
    void set_screen(winchisel::core::Screen screen) { screen_ = screen; }

private:
    Session() = default;

    winchisel::core::Settings settings_{};
    winchisel::core::Screen screen_{winchisel::core::Screen::home};
};

}  // namespace winchisel::application
