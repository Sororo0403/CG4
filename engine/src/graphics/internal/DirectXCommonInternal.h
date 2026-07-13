#pragma once

#include <Windows.h>

namespace DirectXCommonInternal {

class ScopedWin32Handle {
public:
    ScopedWin32Handle() = default;
    explicit ScopedWin32Handle(HANDLE handle) noexcept : handle_(handle) {}
    ScopedWin32Handle(const ScopedWin32Handle&) = delete;
    ScopedWin32Handle& operator=(const ScopedWin32Handle&) = delete;
    ScopedWin32Handle(ScopedWin32Handle&& other) noexcept : handle_(other.Release()) {}
    ScopedWin32Handle& operator=(ScopedWin32Handle&& other) noexcept {
        if (this != &other) {
            Reset(other.Release());
        }
        return *this;
    }
    ~ScopedWin32Handle() {
        Reset();
    }

    HANDLE Get() const noexcept {
        return handle_;
    }
    explicit operator bool() const noexcept {
        return handle_ != nullptr;
    }

    void Reset(HANDLE handle = nullptr) noexcept {
        if (handle_ != nullptr) {
            CloseHandle(handle_);
        }
        handle_ = handle;
    }

    HANDLE Release() noexcept {
        HANDLE handle = handle_;
        handle_ = nullptr;
        return handle;
    }

private:
    HANDLE handle_ = nullptr;
};

inline bool LogIfFailed(HRESULT hr, const char* message) {
    if (SUCCEEDED(hr)) {
        return false;
    }
    OutputDebugStringA("DirectXCommon: ");
    OutputDebugStringA(message != nullptr ? message : "HRESULT failed");
    OutputDebugStringA("\n");
    return true;
}

} // namespace DirectXCommonInternal
