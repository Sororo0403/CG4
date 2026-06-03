#pragma once

#include <Windows.h>

namespace DirectXCommonInternal {

inline bool LogIfFailed(HRESULT hr, const char *message) {
    if (SUCCEEDED(hr)) {
        return false;
    }
    OutputDebugStringA("DirectXCommon: ");
    OutputDebugStringA(message != nullptr ? message : "HRESULT failed");
    OutputDebugStringA("\n");
    return true;
}

} // namespace DirectXCommonInternal
