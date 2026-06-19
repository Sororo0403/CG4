#pragma once

#include "input/Input.h"

#include <array>
#include <vector>
#include <wrl.h>

struct Input::InputFrame {
    std::array<BYTE, 256> keys{};
    DIMOUSESTATE mouse{};
    bool gamepadConnected = false;
    WORD gamepadButtons = 0;
    float gamepadLeftStickX = 0.0f;
    float gamepadLeftStickY = 0.0f;
    float gamepadRightStickX = 0.0f;
    float gamepadRightStickY = 0.0f;
    float gamepadLeftTrigger = 0.0f;
    float gamepadRightTrigger = 0.0f;
};

struct Input::State {
    Microsoft::WRL::ComPtr<IDirectInput8> directInput;
    Microsoft::WRL::ComPtr<IDirectInputDevice8> keyboard;
    Microsoft::WRL::ComPtr<IDirectInputDevice8> mouse;

    std::array<BYTE, 256> keyNow{};
    std::array<BYTE, 256> keyPrev{};

    DIMOUSESTATE mouseState{};
    DIMOUSESTATE mousePrevState{};

    XINPUT_STATE gamepadState{};
    XINPUT_STATE gamepadPrevState{};
    bool gamepadConnected = false;
    bool gamepadPrevConnected = false;
    float gamepadLeftStickX = 0.0f;
    float gamepadLeftStickY = 0.0f;
    float gamepadRightStickX = 0.0f;
    float gamepadRightStickY = 0.0f;
    float gamepadLeftTrigger = 0.0f;
    float gamepadRightTrigger = 0.0f;

    Input::ReplayMode replayMode = Input::ReplayMode::Live;
    std::wstring replayPath;
    float replayFixedDeltaTime = 0.0f;
    std::vector<InputFrame> recordedFrames;
    std::vector<InputFrame> replayFrames;
    size_t replayFrameIndex = 0;
    bool replayFinished = false;
    bool recordingDirty = false;
    std::wstring replayDirectory;
};
