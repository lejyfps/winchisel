#pragma once

#include <chrono>
#include <future>
#include <mutex>
#include <vector>

namespace winchisel::ui {

// Unfinished std::future destructors block the UI. Park them here, drop
// already-finished waiters, and join leftovers on window close (with a
// short timeout so shutdown cannot hang on winget/DISM).
inline std::mutex& background_wait_mutex() {
    static std::mutex mutex;
    return mutex;
}

inline std::vector<std::future<void>>& background_waiters() {
    static std::vector<std::future<void>> waiters;
    return waiters;
}

inline void reap_finished_waiters() {
    auto& waiters = background_waiters();
    std::erase_if(waiters, [](std::future<void>& waiter) {
        return !waiter.valid() || waiter.wait_for(std::chrono::seconds(0)) == std::future_status::ready;
    });
}

template <typename T>
void finish_in_background(std::future<T>& future) {
    if (!future.valid()) return;
    std::scoped_lock lock(background_wait_mutex());
    reap_finished_waiters();
    background_waiters().push_back(std::async(std::launch::async, [pending = std::move(future)]() mutable {
        try { pending.wait(); } catch (...) {}
    }));
}

inline void drain_background_work() {
    std::scoped_lock lock(background_wait_mutex());
    for (auto& waiter : background_waiters()) {
        if (!waiter.valid()) continue;
        if (waiter.wait_for(std::chrono::seconds(2)) != std::future_status::ready) continue;
    }
    reap_finished_waiters();
}

}
