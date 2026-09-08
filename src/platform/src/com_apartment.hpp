#pragma once
#include <Windows.h>
#include <objbase.h>

namespace winchisel::platform::detail {
struct ComApartment {
    HRESULT const hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    bool const owned = SUCCEEDED(hr);
    ~ComApartment() { if (owned) CoUninitialize(); }
    explicit operator bool() const { return owned || hr == RPC_E_CHANGED_MODE; }
};
}
