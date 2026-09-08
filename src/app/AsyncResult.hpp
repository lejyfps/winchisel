#pragma once
#include "winchisel/core/error.hpp"
#include <winrt/base.h>
#include <exception>

namespace winchisel::ui {
inline std::string exception_text() {
    try { throw; }
    catch (winrt::hresult_error const& error) { return winrt::to_string(error.message()); }
    catch (std::exception const& error) { return error.what(); }
    catch (...) { return "The operation failed unexpectedly."; }
}
template<typename Work>
auto result_or_error(Work&& work) -> decltype(work()) {
    try { return work(); }
    catch (...) { return std::unexpected(winchisel::core::Error{exception_text()}); }
}
}
