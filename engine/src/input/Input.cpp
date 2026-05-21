#include "input/Input.h"
#include "debug/DebugLog.h"
#include <algorithm>
#include <cassert>
#include <cmath>
#include <filesystem>
#include <string>

#pragma comment(lib, "xinput.lib")

namespace {
float NormalizeThumbAxis(SHORT value, SHORT deadZone) {
    const int intValue = static_cast<int>(value);
    const int absValue = std::abs(intValue);
    if (absValue <= deadZone) {
        return 0.0f;
    }

    const int maxValue = intValue < 0 ? 32768 : 32767;
    const float normalized = static_cast<float>(absValue - deadZone) /
                             static_cast<float>(maxValue - deadZone);
    return std::clamp(normalized, 0.0f, 1.0f) * (intValue < 0 ? -1.0f : 1.0f);
}

float NormalizeTrigger(BYTE value) {
    if (value <= XINPUT_GAMEPAD_TRIGGER_THRESHOLD) {
        return 0.0f;
    }

    constexpr float maxValue = 255.0f - XINPUT_GAMEPAD_TRIGGER_THRESHOLD;
    return std::clamp(
        static_cast<float>(value - XINPUT_GAMEPAD_TRIGGER_THRESHOLD) / maxValue,
        0.0f, 1.0f);
}

std::wstring GetDefaultReplayDirectory() {
    std::array<wchar_t, MAX_PATH> pathBuffer{};
    const DWORD length = GetModuleFileNameW(
        nullptr, pathBuffer.data(), static_cast<DWORD>(pathBuffer.size()));
    if (length == 0 || length >= pathBuffer.size()) {
        return L"replays";
    }

    const std::filesystem::path executablePath(
        std::wstring(pathBuffer.data(), length));
    return (executablePath.parent_path() / L"replays").wstring();
}

} // namespace

Input::~Input() { FinishRecording(); }

void Input::Initialize(HINSTANCE hInstance, HWND hwnd) {
    HRESULT hr;

    if (replayDirectory_.empty()) {
        replayDirectory_ = GetDefaultReplayDirectory();
    }

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
    if (replayMode_ == ReplayMode::Replay) {
        keyPrev_ = keyNow_;
        mousePrevState_ = mouseState_;
        gamepadPrevState_ = gamepadState_;

        if (replayFrameIndex_ < replayFrames_.size()) {
            ApplyReplayFrame(replayFrames_[replayFrameIndex_]);
            ++replayFrameIndex_;
            replayFinished_ = replayFrameIndex_ >= replayFrames_.size();
            if (replayFinished_) {
                DebugLog::Get().Write(
                    "Input", "ReplayPlayer", "finished", "ok",
                    {{"frames", std::to_string(replayFrames_.size())}});
            }
        } else {
            if (!replayFinished_) {
                DebugLog::Get().Write(
                    "Input", "ReplayPlayer", "finished", "ok",
                    {{"frames", std::to_string(replayFrames_.size())}});
            }
            replayFinished_ = true;
        }
        return;
    }

    UpdateKeyboard();
    UpdateMouse();
    UpdateGamepad();
    UpdateReplayHotkeys(deltaTime);

    if (replayMode_ == ReplayMode::Record) {
        recordedFrames_.push_back(CaptureFrame());
        recordingDirty_ = true;
    }
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

void Input::UpdateGamepad() {
    gamepadPrevState_ = gamepadState_;
    ZeroMemory(&gamepadState_, sizeof(XINPUT_STATE));

    const DWORD result = XInputGetState(0, &gamepadState_);
    gamepadConnected_ = result == ERROR_SUCCESS;

    if (!gamepadConnected_) {
        gamepadLeftStickX_ = 0.0f;
        gamepadLeftStickY_ = 0.0f;
        gamepadRightStickX_ = 0.0f;
        gamepadRightStickY_ = 0.0f;
        gamepadLeftTrigger_ = 0.0f;
        gamepadRightTrigger_ = 0.0f;
        return;
    }

    const XINPUT_GAMEPAD &pad = gamepadState_.Gamepad;
    gamepadLeftStickX_ =
        NormalizeThumbAxis(pad.sThumbLX, XINPUT_GAMEPAD_LEFT_THUMB_DEADZONE);
    gamepadLeftStickY_ =
        NormalizeThumbAxis(pad.sThumbLY, XINPUT_GAMEPAD_LEFT_THUMB_DEADZONE);
    gamepadRightStickX_ =
        NormalizeThumbAxis(pad.sThumbRX, XINPUT_GAMEPAD_RIGHT_THUMB_DEADZONE);
    gamepadRightStickY_ =
        NormalizeThumbAxis(pad.sThumbRY, XINPUT_GAMEPAD_RIGHT_THUMB_DEADZONE);
    gamepadLeftTrigger_ = NormalizeTrigger(pad.bLeftTrigger);
    gamepadRightTrigger_ = NormalizeTrigger(pad.bRightTrigger);
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

bool Input::IsGamepadButtonPress(WORD button) const {
    return gamepadConnected_ && (gamepadState_.Gamepad.wButtons & button) != 0;
}

bool Input::IsGamepadButtonTrigger(WORD button) const {
    return gamepadConnected_ &&
           (gamepadState_.Gamepad.wButtons & button) != 0 &&
           (gamepadPrevState_.Gamepad.wButtons & button) == 0;
}

bool Input::IsGamepadButtonRelease(WORD button) const {
    return gamepadConnected_ &&
           (gamepadState_.Gamepad.wButtons & button) == 0 &&
           (gamepadPrevState_.Gamepad.wButtons & button) != 0;
}

bool Input::IsGamepadLeftTriggerTrigger(float threshold) const {
    return gamepadConnected_ && gamepadLeftTrigger_ > threshold &&
           NormalizeTrigger(gamepadPrevState_.Gamepad.bLeftTrigger) <=
               threshold;
}

bool Input::IsGamepadRightTriggerTrigger(float threshold) const {
    return gamepadConnected_ && gamepadRightTrigger_ > threshold &&
           NormalizeTrigger(gamepadPrevState_.Gamepad.bRightTrigger) <=
               threshold;
}
