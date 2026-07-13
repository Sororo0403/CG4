#include "input/Input.h"

#include "input/InputReplayLimits.h"
#include "internal/InputInternal.h"

#include <algorithm>
#include <cassert>
#include <cmath>
#include <exception>
#include <filesystem>
#include <new>
#include <string>
#include <utility>

#pragma comment(lib, "xinput.lib")

namespace {
float NormalizeThumbAxis(SHORT value, SHORT deadZone) {
    const int intValue = static_cast<int>(value);
    const int absValue = std::abs(intValue);
    if (absValue <= deadZone) {
        return 0.0f;
    }

    const int maxValue = intValue < 0 ? 32768 : 32767;
    const float normalized =
        static_cast<float>(absValue - deadZone) / static_cast<float>(maxValue - deadZone);
    return std::clamp(normalized, 0.0f, 1.0f) * (intValue < 0 ? -1.0f : 1.0f);
}

float NormalizeTrigger(BYTE value) {
    if (value <= XINPUT_GAMEPAD_TRIGGER_THRESHOLD) {
        return 0.0f;
    }

    constexpr float maxValue = 255.0f - XINPUT_GAMEPAD_TRIGGER_THRESHOLD;
    return std::clamp(static_cast<float>(value - XINPUT_GAMEPAD_TRIGGER_THRESHOLD) / maxValue, 0.0f,
                      1.0f);
}

std::wstring GetDefaultReplayDirectory() {
    std::array<wchar_t, MAX_PATH> pathBuffer{};
    const DWORD length =
        GetModuleFileNameW(nullptr, pathBuffer.data(), static_cast<DWORD>(pathBuffer.size()));
    if (length == 0 || length >= pathBuffer.size()) {
        return L"replays";
    }

    try {
        const std::filesystem::path executablePath(std::wstring(pathBuffer.data(), length));
        return (executablePath.parent_path() / L"replays").wstring();
    } catch (const std::exception&) {
        return L"replays";
    }
}

} // namespace

Input::Input() : state_(std::make_unique<State>()) {}

Input::~Input() {
    FinishRecording();
}

Input::ReplayMode Input::GetReplayMode() const {
    return state_->replayMode;
}

bool Input::IsReplayFinished() const {
    return state_->replayFinished;
}

size_t Input::GetReplayFrameIndex() const {
    return state_->replayFrameIndex;
}

size_t Input::GetReplayFrameCount() const {
    return state_->replayFrames.size();
}

const std::wstring& Input::GetReplayPath() const {
    return state_->replayPath;
}

long Input::GetMouseDX() const {
    return state_->mouseState.lX;
}

long Input::GetMouseDY() const {
    return state_->mouseState.lY;
}

long Input::GetMouseWheel() const {
    return state_->mouseState.lZ;
}

bool Input::IsGamepadConnected() const {
    return state_->gamepadConnected;
}

float Input::GetGamepadLeftStickX() const {
    return state_->gamepadLeftStickX;
}

float Input::GetGamepadLeftStickY() const {
    return state_->gamepadLeftStickY;
}

float Input::GetGamepadRightStickX() const {
    return state_->gamepadRightStickX;
}

float Input::GetGamepadRightStickY() const {
    return state_->gamepadRightStickY;
}

float Input::GetGamepadLeftTrigger() const {
    return state_->gamepadLeftTrigger;
}

float Input::GetGamepadRightTrigger() const {
    return state_->gamepadRightTrigger;
}

void Input::ClearInputState(bool clearPrevious) {
    state_->keyNow.fill(0);
    state_->mouseState = {};
    state_->gamepadState = {};
    state_->gamepadConnected = false;
    state_->gamepadLeftStickX = 0.0f;
    state_->gamepadLeftStickY = 0.0f;
    state_->gamepadRightStickX = 0.0f;
    state_->gamepadRightStickY = 0.0f;
    state_->gamepadLeftTrigger = 0.0f;
    state_->gamepadRightTrigger = 0.0f;

    if (clearPrevious) {
        state_->keyPrev.fill(0);
        state_->mousePrevState = {};
        state_->gamepadPrevState = {};
        state_->gamepadPrevConnected = false;
    }
}

void Input::Initialize(HINSTANCE hInstance, HWND hwnd) {
    if (state_->replayDirectory.empty()) {
        state_->replayDirectory = GetDefaultReplayDirectory();
    }

    state_->directInput.Reset();
    state_->keyboard.Reset();
    state_->mouse.Reset();
    ClearInputState(true);

    HRESULT hr =
        DirectInput8Create(hInstance, DIRECTINPUT_VERSION, IID_IDirectInput8,
                           reinterpret_cast<void**>(state_->directInput.GetAddressOf()), nullptr);
    if (FAILED(hr) || !state_->directInput) {
        return;
    }

    hr = state_->directInput->CreateDevice(GUID_SysKeyboard, state_->keyboard.GetAddressOf(),
                                           nullptr);
    if (SUCCEEDED(hr) && state_->keyboard) {
        hr = state_->keyboard->SetDataFormat(&c_dfDIKeyboard);
        if (SUCCEEDED(hr)) {
            hr = state_->keyboard->SetCooperativeLevel(hwnd, DISCL_FOREGROUND | DISCL_NONEXCLUSIVE);
        }
        if (SUCCEEDED(hr)) {
            state_->keyboard->Acquire();
        } else {
            state_->keyboard.Reset();
        }
    } else {
        state_->keyboard.Reset();
    }

    hr = state_->directInput->CreateDevice(GUID_SysMouse, state_->mouse.GetAddressOf(), nullptr);
    if (SUCCEEDED(hr) && state_->mouse) {
        hr = state_->mouse->SetDataFormat(&c_dfDIMouse);
        if (SUCCEEDED(hr)) {
            hr = state_->mouse->SetCooperativeLevel(hwnd, DISCL_FOREGROUND | DISCL_NONEXCLUSIVE);
        }
        if (SUCCEEDED(hr)) {
            state_->mouse->Acquire();
        } else {
            state_->mouse.Reset();
        }
    } else {
        state_->mouse.Reset();
    }
}

void Input::Update(float deltaTime) {
    (void)deltaTime;

    if (state_->replayMode == ReplayMode::Replay) {
        state_->keyPrev = state_->keyNow;
        state_->mousePrevState = state_->mouseState;
        state_->gamepadPrevConnected = state_->gamepadConnected;
        state_->gamepadPrevState = state_->gamepadState;

        if (state_->replayFrameIndex < state_->replayFrames.size()) {
            ApplyReplayFrame(state_->replayFrames[state_->replayFrameIndex]);
            ++state_->replayFrameIndex;
            state_->replayFinished = state_->replayFrameIndex >= state_->replayFrames.size();
        } else {
            state_->replayFinished = true;
            ClearInputState(false);
        }
        return;
    }

    UpdateKeyboard();
    UpdateMouse();
    UpdateGamepad();

    if (state_->replayMode == ReplayMode::Record &&
        state_->recordedFrames.size() < InputReplayLimits::kMaxFrames) {
        try {
            state_->recordedFrames.push_back(CaptureFrame());
            state_->recordingDirty = true;
        } catch (const std::exception&) {
        }
    }
}

void Input::UpdateKeyboard() {
    state_->keyPrev = state_->keyNow;
    if (!state_->keyboard) {
        state_->keyNow.fill(0);
        return;
    }

    HRESULT hr = state_->keyboard->GetDeviceState(256, state_->keyNow.data());

    if (FAILED(hr)) {
        hr = state_->keyboard->Acquire();

        if (SUCCEEDED(hr)) {
            hr = state_->keyboard->GetDeviceState(256, state_->keyNow.data());
        }
        if (FAILED(hr)) {
            state_->keyNow.fill(0);
        }
    }
}

void Input::UpdateMouse() {
    state_->mousePrevState = state_->mouseState;
    if (!state_->mouse) {
        state_->mouseState = {};
        return;
    }

    HRESULT hr = state_->mouse->GetDeviceState(sizeof(DIMOUSESTATE), &state_->mouseState);

    if (FAILED(hr)) {
        hr = state_->mouse->Acquire();

        if (SUCCEEDED(hr)) {
            hr = state_->mouse->GetDeviceState(sizeof(DIMOUSESTATE), &state_->mouseState);
        }
        if (FAILED(hr)) {
            state_->mouseState = {};
        }
    }
}

void Input::UpdateGamepad() {
    state_->gamepadPrevState = state_->gamepadState;
    state_->gamepadPrevConnected = state_->gamepadConnected;
    ZeroMemory(&state_->gamepadState, sizeof(XINPUT_STATE));

    const DWORD result = XInputGetState(0, &state_->gamepadState);
    state_->gamepadConnected = result == ERROR_SUCCESS;

    if (!state_->gamepadConnected) {
        state_->gamepadLeftStickX = 0.0f;
        state_->gamepadLeftStickY = 0.0f;
        state_->gamepadRightStickX = 0.0f;
        state_->gamepadRightStickY = 0.0f;
        state_->gamepadLeftTrigger = 0.0f;
        state_->gamepadRightTrigger = 0.0f;
        return;
    }

    const XINPUT_GAMEPAD& pad = state_->gamepadState.Gamepad;
    state_->gamepadLeftStickX =
        NormalizeThumbAxis(pad.sThumbLX, XINPUT_GAMEPAD_LEFT_THUMB_DEADZONE);
    state_->gamepadLeftStickY =
        NormalizeThumbAxis(pad.sThumbLY, XINPUT_GAMEPAD_LEFT_THUMB_DEADZONE);
    state_->gamepadRightStickX =
        NormalizeThumbAxis(pad.sThumbRX, XINPUT_GAMEPAD_RIGHT_THUMB_DEADZONE);
    state_->gamepadRightStickY =
        NormalizeThumbAxis(pad.sThumbRY, XINPUT_GAMEPAD_RIGHT_THUMB_DEADZONE);
    state_->gamepadLeftTrigger = NormalizeTrigger(pad.bLeftTrigger);
    state_->gamepadRightTrigger = NormalizeTrigger(pad.bRightTrigger);
}

bool Input::IsKeyPress(int dik) const {
    if (dik < 0 || std::cmp_greater_equal(dik, state_->keyNow.size())) {
        return false;
    }
    return (state_->keyNow[dik] & kPressMask) != 0;
}

bool Input::IsKeyTrigger(int dik) const {
    if (dik < 0 || std::cmp_greater_equal(dik, state_->keyNow.size())) {
        return false;
    }
    return (state_->keyNow[dik] & kPressMask) && !(state_->keyPrev[dik] & kPressMask);
}

bool Input::IsKeyRelease(int dik) const {
    if (dik < 0 || std::cmp_greater_equal(dik, state_->keyNow.size())) {
        return false;
    }
    return !(state_->keyNow[dik] & kPressMask) && (state_->keyPrev[dik] & kPressMask);
}

bool Input::IsMousePress(int button) const {
    if (button < 0 || std::cmp_greater_equal(button, _countof(state_->mouseState.rgbButtons))) {
        return false;
    }
    return (state_->mouseState.rgbButtons[button] & 0x80) != 0;
}

bool Input::IsMouseTrigger(int button) const {
    if (button < 0 || std::cmp_greater_equal(button, _countof(state_->mouseState.rgbButtons))) {
        return false;
    }
    return (state_->mouseState.rgbButtons[button] & 0x80) &&
           !(state_->mousePrevState.rgbButtons[button] & 0x80);
}

bool Input::IsMouseRelease(int button) const {
    if (button < 0 || std::cmp_greater_equal(button, _countof(state_->mouseState.rgbButtons))) {
        return false;
    }
    return !(state_->mouseState.rgbButtons[button] & 0x80) &&
           (state_->mousePrevState.rgbButtons[button] & 0x80);
}

bool Input::IsGamepadButtonPress(WORD button) const {
    return state_->gamepadConnected && (state_->gamepadState.Gamepad.wButtons & button) != 0;
}

bool Input::IsGamepadButtonTrigger(WORD button) const {
    return state_->gamepadConnected && (state_->gamepadState.Gamepad.wButtons & button) != 0 &&
           (state_->gamepadPrevState.Gamepad.wButtons & button) == 0;
}

bool Input::IsGamepadButtonRelease(WORD button) const {
    return state_->gamepadPrevConnected && (state_->gamepadState.Gamepad.wButtons & button) == 0 &&
           (state_->gamepadPrevState.Gamepad.wButtons & button) != 0;
}

bool Input::IsGamepadLeftTriggerTrigger(float threshold) const {
    return state_->gamepadConnected && state_->gamepadLeftTrigger > threshold &&
           NormalizeTrigger(state_->gamepadPrevState.Gamepad.bLeftTrigger) <= threshold;
}

bool Input::IsGamepadRightTriggerTrigger(float threshold) const {
    return state_->gamepadConnected && state_->gamepadRightTrigger > threshold &&
           NormalizeTrigger(state_->gamepadPrevState.Gamepad.bRightTrigger) <= threshold;
}
