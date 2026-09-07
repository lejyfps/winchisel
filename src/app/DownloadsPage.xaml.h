#pragma once

#include "DownloadsPage.g.h"
#include "DownloadsPage.xaml.g.h"

namespace winrt::Winchisel::implementation {

struct DownloadsPage : DownloadsPageT<DownloadsPage> {
    DownloadsPage();
};

}  // namespace winrt::Winchisel::implementation

namespace winrt::Winchisel::factory_implementation {

struct DownloadsPage : DownloadsPageT<DownloadsPage, implementation::DownloadsPage> {};

}  // namespace winrt::Winchisel::factory_implementation
