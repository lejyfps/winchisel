#pragma once

#include <future>
#include <thread>

namespace winchisel::ui {

// Moving an unfinished async future into a detached waiter prevents
// std::future's destructor from blocking the UI thread during app shutdown.
template <typename T>
void finish_in_background(std::future<T>& future) {
    if (!future.valid()) return;
    std::thread([pending = std::move(future)]() mutable noexcept {
        try { pending.wait(); } catch (...) {}
    }).detach();
}

}
