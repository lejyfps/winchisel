#pragma once
#include <atomic>

namespace winchisel::core {
// The application has one window; all ContentDialogs share its slot.
class DialogSlot {
    inline static std::atomic_bool busy_{};
    bool acquired_ = !busy_.exchange(true);
public:
    DialogSlot() = default;
    DialogSlot(DialogSlot const&) = delete;
    DialogSlot& operator=(DialogSlot const&) = delete;
    ~DialogSlot() { if (acquired_) busy_ = false; }
    explicit operator bool() const { return acquired_; }
};
}
