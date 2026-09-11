// pch.h — C++ precompiled header (wird nur als C++ via pch.cpp kompiliert).
#pragma once

#ifndef __cplusplus
#error "pch.h requires C++ (wird nur von .cpp-TUs inkludiert, LanguageStandard=stdcpplatest)."
#endif
// MSVC meldet ohne /Zc:__cplusplus immer 199711L, daher _MSVC_LANG prüfen.
#if defined(_MSVC_LANG)
static_assert(_MSVC_LANG >= 202002L, "pch.h requires at least C++20.");
#else
static_assert(__cplusplus >= 202002L, "pch.h requires at least C++20.");
#endif

// Schlanke Windows-Konfiguration (falls nicht schon via vcxproj definiert).
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif

// --- C-ABI (unvermeidbar): Win32/COM-Aufrufe sind C-ABI, C++/WinRT braucht sie. ---
#include <windows.h>
#undef GetCurrentTime
#include <unknwn.h>
#include <restrictederrorinfo.h>
#include <hstring.h>

// --- C++ Standardbibliothek (im UI-Code überall benutzt) ---
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <exception>
#include <functional>
#include <future>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

// --- C++/WinRT Projektionen (reines C++) ---

#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.ApplicationModel.Activation.h>
#include <winrt/Microsoft.UI.Composition.h>
#include <winrt/Microsoft.UI.Dispatching.h>
#include <winrt/Microsoft.UI.Windowing.h>
#include <winrt/Microsoft.UI.Xaml.h>
#include <winrt/Microsoft.UI.Xaml.Automation.h>
#include <winrt/Microsoft.UI.Xaml.Controls.h>
#include <winrt/Microsoft.UI.Xaml.Controls.Primitives.h>
#include <winrt/Microsoft.UI.Xaml.Input.h>
#include <winrt/Microsoft.UI.Xaml.Data.h>
#include <winrt/Microsoft.UI.Xaml.Documents.h>
#include <winrt/Microsoft.UI.Xaml.Interop.h>
#include <winrt/Microsoft.UI.Xaml.Markup.h>
#include <winrt/Microsoft.UI.Xaml.Media.h>
#include <winrt/Microsoft.UI.Xaml.Navigation.h>
#include <winrt/Microsoft.UI.Xaml.Shapes.h>
#include <winrt/Windows.UI.Text.h>
#include <winrt/Microsoft.UI.Interop.h>
