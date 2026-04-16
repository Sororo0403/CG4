#include "Input.h"
#include <cassert>

void Input::Initialize(HINSTANCE hInstance, HWND hwnd) {
    HRESULT hr;

    hr = DirectInput8Create(
        hInstance, DIRECTINPUT_VERSION, IID_IDirectInput8,
        reinterpret_cast<void **>(directInput_.GetAddressOf()), nullptr);
    assert(SUCCEEDED(hr));

    hr = directInput_->CreateDevice(GUID_SysKeyboard, keyboard_.GetAddressOf(),
                                    nullptr);
    assert(SUCCEEDED(hr));

    hr = keyboard_->SetDataFormat(&c_dfDIKeyboard);
    assert(SUCCEEDED(hr));

    hr = keyboard_->SetCooperativeLevel(hwnd,
                                        DISCL_FOREGROUND | DISCL_NONEXCLUSIVE);
    assert(SUCCEEDED(hr));

    keyboard_->Acquire();

    hr = directInput_->CreateDevice(GUID_SysMouse, mouse_.GetAddressOf(),
                                    nullptr);
    assert(SUCCEEDED(hr));

    hr = mouse_->SetDataFormat(&c_dfDIMouse);
    assert(SUCCEEDED(hr));

    hr = mouse_->SetCooperativeLevel(hwnd,
                                     DISCL_FOREGROUND | DISCL_NONEXCLUSIVE);
    assert(SUCCEEDED(hr));

    mouse_->Acquire();
}

void Input::Update(float deltaTime) {
    (void)deltaTime;
    UpdateKeyboard();
    UpdateMouse();
}

void Input::UpdateKeyboard() {
    keyPrev_ = keyNow_;

    HRESULT hr = keyboard_->GetDeviceState(256, keyNow_.data());

    if (FAILED(hr)) {
        hr = keyboard_->Acquire();

        if (SUCCEEDED(hr)) {
            keyboard_->GetDeviceState(256, keyNow_.data());
        }
    }
}

void Input::UpdateMouse() {
    mousePrevState_ = mouseState_;

    HRESULT hr = mouse_->GetDeviceState(sizeof(DIMOUSESTATE), &mouseState_);

    if (FAILED(hr)) {
        hr = mouse_->Acquire();

        if (SUCCEEDED(hr)) {
            mouse_->GetDeviceState(sizeof(DIMOUSESTATE), &mouseState_);
        }
    }
}

bool Input::IsKeyPress(int dik) const {
    return (keyNow_[dik] & kPressMask) != 0;
}

bool Input::IsKeyTrigger(int dik) const {
    return (keyNow_[dik] & kPressMask) && !(keyPrev_[dik] & kPressMask);
}

bool Input::IsKeyRelease(int dik) const {
    return !(keyNow_[dik] & kPressMask) && (keyPrev_[dik] & kPressMask);
}

bool Input::IsMousePress(int button) const {
    return (mouseState_.rgbButtons[button] & 0x80) != 0;
}

bool Input::IsMouseTrigger(int button) const {
    return (mouseState_.rgbButtons[button] & 0x80) &&
           !(mousePrevState_.rgbButtons[button] & 0x80);
}

bool Input::IsMouseRelease(int button) const {
    return !(mouseState_.rgbButtons[button] & 0x80) &&
           (mousePrevState_.rgbButtons[button] & 0x80);
}
