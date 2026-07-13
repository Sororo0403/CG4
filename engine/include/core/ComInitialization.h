#pragma once

#include <objbase.h>

class ScopedComInitialization {
public:
    explicit ScopedComInitialization(DWORD coInit = COINIT_MULTITHREADED) noexcept
        : result_(CoInitializeEx(nullptr, coInit)), ownsInitialization_(SUCCEEDED(result_)) {}

    ~ScopedComInitialization() {
        if (ownsInitialization_) {
            CoUninitialize();
        }
    }

    ScopedComInitialization(const ScopedComInitialization&) = delete;
    ScopedComInitialization& operator=(const ScopedComInitialization&) = delete;

    bool IsUsable() const noexcept {
        return SUCCEEDED(result_) || result_ == RPC_E_CHANGED_MODE;
    }

    HRESULT Result() const noexcept {
        return result_;
    }

private:
    HRESULT result_ = E_FAIL;
    bool ownsInitialization_ = false;
};
