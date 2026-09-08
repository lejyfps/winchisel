#include "winchisel/platform/shell.hpp"

#include <Windows.h>
#include <Shellapi.h>

namespace winchisel::platform {

winchisel::core::Result<void> open_https_url(std::wstring_view url) {
    if (!url.starts_with(L"https://")) {
        return std::unexpected(winchisel::core::Error{"Only HTTPS URLs are allowed"});
    }
    const auto result = reinterpret_cast<INT_PTR>(ShellExecuteW(nullptr, L"open", std::wstring(url).c_str(), nullptr, nullptr, SW_SHOWNORMAL));
    if (result <= 32) {
        return std::unexpected(winchisel::core::Error{std::to_string(result)});
    }
    return {};
}

}  // namespace winchisel::platform
