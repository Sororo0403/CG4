#include "input/Input.h"
#include "internal/InputInternal.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <ctime>
#include <exception>
#include <filesystem>
#include <iomanip>
#include <sstream>

bool Input::StartRecording(const std::wstring& path, float fixedDeltaTime) {
    if (path.empty() || state_->replayMode == ReplayMode::Replay) {
        return false;
    }

    try {
        state_->replayPath = path;
    } catch (const std::exception&) {
        return false;
    }
    state_->replayFixedDeltaTime =
        std::isfinite(fixedDeltaTime) ? (std::max)(fixedDeltaTime, 0.0f) : 0.0f;
    state_->recordedFrames.clear();
    state_->recordingDirty = true;
    state_->replayMode = ReplayMode::Record;
    return true;
}

bool Input::StartReplay(const std::wstring& path) {
    if (path.empty() || state_->replayMode == ReplayMode::Record) {
        return false;
    }

    if (!LoadReplay(path)) {
        return false;
    }

    ClearInputState(true);
    try {
        state_->replayPath = path;
    } catch (const std::exception&) {
        state_->replayFrames.clear();
        return false;
    }
    state_->replayFrameIndex = 0;
    state_->replayFinished = state_->replayFrames.empty();
    state_->replayMode = ReplayMode::Replay;
    return true;
}

bool Input::StopRecording() {
    if (state_->replayMode != ReplayMode::Record) {
        return true;
    }

    const bool saved = FinishRecording();
    if (saved) {
        state_->replayMode = ReplayMode::Live;
    }
    return saved;
}

bool Input::FinishRecording() {
    if (state_->replayMode != ReplayMode::Record || !state_->recordingDirty) {
        return true;
    }

    const bool saved = SaveRecording();
    if (saved) {
        state_->recordingDirty = false;
    }
    return saved;
}

bool Input::ApplyReplayStartupOptions(const ReplayStartupOptions& options, float fixedDeltaTime) {
    if (!options.replayDirectory.empty()) {
        try {
            state_->replayDirectory = options.replayDirectory;
        } catch (const std::exception&) {
            return false;
        }
    }

    if (!options.recordPath.empty() && !options.replayPath.empty()) {
        return false;
    }

    if (!options.replayPath.empty()) {
        return StartReplay(options.replayPath);
    }

    if (!options.recordPath.empty()) {
        return StartRecording(options.recordPath, fixedDeltaTime);
    }

    if (options.autoRecord) {
        return StartRecording(MakeAutoReplayPath(), fixedDeltaTime);
    }

    return true;
}

Input::InputFrame Input::CaptureFrame() const {
    InputFrame frame{};
    frame.keys = state_->keyNow;
    frame.mouse = state_->mouseState;
    frame.gamepadConnected = state_->gamepadConnected;
    frame.gamepadButtons = state_->gamepadState.Gamepad.wButtons;
    frame.gamepadLeftStickX = state_->gamepadLeftStickX;
    frame.gamepadLeftStickY = state_->gamepadLeftStickY;
    frame.gamepadRightStickX = state_->gamepadRightStickX;
    frame.gamepadRightStickY = state_->gamepadRightStickY;
    frame.gamepadLeftTrigger = state_->gamepadLeftTrigger;
    frame.gamepadRightTrigger = state_->gamepadRightTrigger;
    return frame;
}

void Input::ApplyReplayFrame(const InputFrame& frame) {
    state_->keyNow = frame.keys;
    state_->mouseState = frame.mouse;
    state_->gamepadConnected = frame.gamepadConnected;
    ZeroMemory(&state_->gamepadState, sizeof(XINPUT_STATE));
    state_->gamepadState.Gamepad.wButtons = frame.gamepadButtons;
    state_->gamepadLeftStickX = std::clamp(frame.gamepadLeftStickX, -1.0f, 1.0f);
    state_->gamepadLeftStickY = std::clamp(frame.gamepadLeftStickY, -1.0f, 1.0f);
    state_->gamepadRightStickX = std::clamp(frame.gamepadRightStickX, -1.0f, 1.0f);
    state_->gamepadRightStickY = std::clamp(frame.gamepadRightStickY, -1.0f, 1.0f);
    state_->gamepadLeftTrigger = std::clamp(frame.gamepadLeftTrigger, 0.0f, 1.0f);
    state_->gamepadRightTrigger = std::clamp(frame.gamepadRightTrigger, 0.0f, 1.0f);
    state_->gamepadState.Gamepad.bLeftTrigger =
        static_cast<BYTE>(std::clamp(state_->gamepadLeftTrigger, 0.0f, 1.0f) * 255.0f);
    state_->gamepadState.Gamepad.bRightTrigger =
        static_cast<BYTE>(std::clamp(state_->gamepadRightTrigger, 0.0f, 1.0f) * 255.0f);
}

std::wstring Input::MakeAutoReplayPath() const {
    try {
        const auto now = std::chrono::system_clock::now();
        const std::time_t time = std::chrono::system_clock::to_time_t(now);

        std::tm localTime{};
        localtime_s(&localTime, &time);

        std::wostringstream name;
        name << L"replay_" << std::put_time(&localTime, L"%Y%m%d_%H%M%S");

        std::filesystem::path directory(state_->replayDirectory);
        std::filesystem::path path = directory / (name.str() + L".json");
        std::error_code ec;
        for (int index = 1; std::filesystem::exists(path, ec) && !ec; ++index) {
            std::wostringstream numberedName;
            numberedName << name.str() << L"_" << std::setw(2) << std::setfill(L'0') << index
                         << L".json";
            path = directory / numberedName.str();
            ec.clear();
        }

        return path.wstring();
    } catch (const std::exception&) {
        return L"replay.json";
    }
}
