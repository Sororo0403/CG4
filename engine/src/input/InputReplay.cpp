#include "input/Input.h"
#include "debug/DebugLog.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <string>

#pragma warning(push, 0)
#include "nlohmann/json.hpp"
#pragma warning(pop)

namespace {
std::string EncodeKeys(const std::array<BYTE, 256> &keys) {
    std::ostringstream stream;
    stream << std::hex << std::setfill('0');
    for (size_t index = 0; index < keys.size(); index += 4) {
        unsigned int nibble = 0;
        for (size_t bit = 0; bit < 4; ++bit) {
            if ((keys[index + bit] & 0x80) != 0) {
                nibble |= 1u << bit;
            }
        }
        stream << std::setw(1) << nibble;
    }
    return stream.str();
}

std::array<BYTE, 256> DecodeKeys(const std::string &encoded) {
    std::array<BYTE, 256> keys{};
    const size_t count = (std::min)(encoded.size(), keys.size() / 4);
    for (size_t index = 0; index < count; ++index) {
        const char ch = encoded[index];
        unsigned int nibble = 0;
        if (ch >= '0' && ch <= '9') {
            nibble = static_cast<unsigned int>(ch - '0');
        } else if (ch >= 'a' && ch <= 'f') {
            nibble = static_cast<unsigned int>(10 + ch - 'a');
        } else if (ch >= 'A' && ch <= 'F') {
            nibble = static_cast<unsigned int>(10 + ch - 'A');
        }

        for (size_t bit = 0; bit < 4; ++bit) {
            keys[index * 4 + bit] =
                (nibble & (1u << bit)) != 0 ? 0x80 : 0;
        }
    }
    return keys;
}

std::string NarrowPath(const std::wstring &path) {
    return std::filesystem::path(path).generic_string();
}

std::string ReplayModeName(Input::ReplayMode mode) {
    switch (mode) {
    case Input::ReplayMode::Live:
        return "Live";
    case Input::ReplayMode::Record:
        return "Record";
    case Input::ReplayMode::Replay:
        return "Replay";
    }
    return "Unknown";
}

} // namespace

bool Input::StartRecording(const std::wstring &path, float fixedDeltaTime) {
    if (path.empty() || replayMode_ == ReplayMode::Replay) {
        DebugLog::Get().Write(
            "Input", "ReplayRecorder", "start_recording", "failed",
            {{"reason", replayMode_ == ReplayMode::Replay
                            ? "replay_mode_active"
                            : "empty_path"}});
        return false;
    }

    replayPath_ = path;
    replayFixedDeltaTime_ = fixedDeltaTime;
    recordedFrames_.clear();
    recordingDirty_ = true;
    replayMode_ = ReplayMode::Record;
    const std::string narrowPath = NarrowPath(replayPath_);
    DebugLog::Get().Write(
        "Input", "ReplayRecorder", "start_recording", "ok",
        {{"path", narrowPath}, {"fixedDeltaTime", std::to_string(fixedDeltaTime)}});
    return true;
}

bool Input::StartReplay(const std::wstring &path) {
    if (path.empty() || replayMode_ == ReplayMode::Record) {
        DebugLog::Get().Write(
            "Input", "ReplayPlayer", "start_replay", "failed",
            {{"reason", replayMode_ == ReplayMode::Record
                            ? "record_mode_active"
                            : "empty_path"}});
        return false;
    }

    if (!LoadReplay(path)) {
        const std::string narrowPath = NarrowPath(path);
        DebugLog::Get().Write("Input", "ReplayPlayer", "load_replay",
                              "failed", {{"path", narrowPath}});
        return false;
    }

    replayPath_ = path;
    replayFrameIndex_ = 0;
    replayFinished_ = replayFrames_.empty();
    replayMode_ = ReplayMode::Replay;
    const std::string narrowPath = NarrowPath(replayPath_);
    DebugLog::Get().Write(
        "Input", "ReplayPlayer", "start_replay", "ok",
        {{"path", narrowPath}, {"frames", std::to_string(replayFrames_.size())}});
    return true;
}

bool Input::StopRecording() {
    if (replayMode_ != ReplayMode::Record) {
        return true;
    }

    const bool saved = FinishRecording();
    if (saved) {
        replayMode_ = ReplayMode::Live;
        DebugLog::Get().Write("Input", "ReplayRecorder", "stop_recording",
                              "ok");
    }
    return saved;
}

bool Input::FinishRecording() {
    if (replayMode_ != ReplayMode::Record || !recordingDirty_) {
        return true;
    }

    const bool saved = SaveRecording();
    if (saved) {
        recordingDirty_ = false;
        const std::string narrowPath = NarrowPath(replayPath_);
        DebugLog::Get().Write(
            "Input", "ReplayRecorder", "save_recording", "ok",
            {{"path", narrowPath},
             {"frames", std::to_string(recordedFrames_.size())}});
    } else {
        const std::string narrowPath = NarrowPath(replayPath_);
        DebugLog::Get().Write("Input", "ReplayRecorder", "save_recording",
                              "failed", {{"path", narrowPath}});
    }
    return saved;
}

bool Input::ApplyReplayStartupOptions(const ReplayStartupOptions &options,
                                      float fixedDeltaTime) {
    if (!options.replayDirectory.empty()) {
        replayDirectory_ = options.replayDirectory;
    }
    replayHotkeysEnabled_ = options.enableHotkeys;

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
    frame.keys = keyNow_;
    frame.mouse = mouseState_;
    frame.gamepadConnected = gamepadConnected_;
    frame.gamepadButtons = gamepadState_.Gamepad.wButtons;
    frame.gamepadLeftStickX = gamepadLeftStickX_;
    frame.gamepadLeftStickY = gamepadLeftStickY_;
    frame.gamepadRightStickX = gamepadRightStickX_;
    frame.gamepadRightStickY = gamepadRightStickY_;
    frame.gamepadLeftTrigger = gamepadLeftTrigger_;
    frame.gamepadRightTrigger = gamepadRightTrigger_;
    return frame;
}

void Input::ApplyReplayFrame(const InputFrame &frame) {
    keyNow_ = frame.keys;
    mouseState_ = frame.mouse;
    gamepadConnected_ = frame.gamepadConnected;
    ZeroMemory(&gamepadState_, sizeof(XINPUT_STATE));
    gamepadState_.Gamepad.wButtons = frame.gamepadButtons;
    gamepadLeftStickX_ = frame.gamepadLeftStickX;
    gamepadLeftStickY_ = frame.gamepadLeftStickY;
    gamepadRightStickX_ = frame.gamepadRightStickX;
    gamepadRightStickY_ = frame.gamepadRightStickY;
    gamepadLeftTrigger_ = frame.gamepadLeftTrigger;
    gamepadRightTrigger_ = frame.gamepadRightTrigger;
    gamepadState_.Gamepad.bLeftTrigger =
        static_cast<BYTE>(std::clamp(gamepadLeftTrigger_, 0.0f, 1.0f) *
                          255.0f);
    gamepadState_.Gamepad.bRightTrigger =
        static_cast<BYTE>(std::clamp(gamepadRightTrigger_, 0.0f, 1.0f) *
                          255.0f);
}

void Input::UpdateReplayHotkeys(float fixedDeltaTime) {
    if (!replayHotkeysEnabled_ || replayMode_ == ReplayMode::Replay) {
        return;
    }

    if (!IsKeyTrigger(DIK_F9)) {
        return;
    }

    DebugLog::Get().Write("Input", "Keyboard", "hotkey", "F9",
                          {{"mode", ReplayModeName(replayMode_)}});

    if (replayMode_ == ReplayMode::Record) {
        StopRecording();
        return;
    }

    StartRecording(MakeAutoReplayPath(), fixedDeltaTime);
}

std::wstring Input::MakeAutoReplayPath() const {
    const auto now = std::chrono::system_clock::now();
    const std::time_t time = std::chrono::system_clock::to_time_t(now);

    std::tm localTime{};
    localtime_s(&localTime, &time);

    std::wostringstream name;
    name << L"replay_" << std::put_time(&localTime, L"%Y%m%d_%H%M%S");

    std::filesystem::path directory(replayDirectory_);
    std::filesystem::path path = directory / (name.str() + L".json");
    for (int index = 1; std::filesystem::exists(path); ++index) {
        std::wostringstream numberedName;
        numberedName << name.str() << L"_" << std::setw(2) << std::setfill(L'0')
                     << index << L".json";
        path = directory / numberedName.str();
    }

    return path.wstring();
}

bool Input::SaveRecording() const {
    try {
        const std::filesystem::path path(replayPath_);
        if (path.has_parent_path()) {
            std::filesystem::create_directories(path.parent_path());
        }

        nlohmann::json root;
        root["version"] = 1;
        root["fixedDeltaTime"] = replayFixedDeltaTime_;
        root["frames"] = nlohmann::json::array();

        for (size_t index = 0; index < recordedFrames_.size(); ++index) {
            const InputFrame &frame = recordedFrames_[index];
            unsigned int mouseButtons = 0;
            for (size_t button = 0; button < 4; ++button) {
                if ((frame.mouse.rgbButtons[button] & 0x80) != 0) {
                    mouseButtons |= 1u << button;
                }
            }

            nlohmann::json jsonFrame;
            jsonFrame["frame"] = index;
            jsonFrame["keys"] = EncodeKeys(frame.keys);
            jsonFrame["mouseButtons"] = mouseButtons;
            jsonFrame["mouseDX"] = frame.mouse.lX;
            jsonFrame["mouseDY"] = frame.mouse.lY;
            jsonFrame["mouseWheel"] = frame.mouse.lZ;
            jsonFrame["gamepadConnected"] = frame.gamepadConnected;
            jsonFrame["gamepadButtons"] = frame.gamepadButtons;
            jsonFrame["leftStickX"] = frame.gamepadLeftStickX;
            jsonFrame["leftStickY"] = frame.gamepadLeftStickY;
            jsonFrame["rightStickX"] = frame.gamepadRightStickX;
            jsonFrame["rightStickY"] = frame.gamepadRightStickY;
            jsonFrame["leftTrigger"] = frame.gamepadLeftTrigger;
            jsonFrame["rightTrigger"] = frame.gamepadRightTrigger;
            root["frames"].push_back(std::move(jsonFrame));
        }

        std::ofstream file(path, std::ios::binary);
        if (!file) {
            return false;
        }

        file << std::setw(2) << root << '\n';
        return true;
    } catch (...) {
        return false;
    }
}

bool Input::LoadReplay(const std::wstring &path) {
    try {
        std::ifstream file(std::filesystem::path(path), std::ios::binary);
        if (!file) {
            return false;
        }

        replayFrames_.clear();
        const nlohmann::json root = nlohmann::json::parse(file);
        if (!root.contains("frames") || !root["frames"].is_array()) {
            return false;
        }

        for (const nlohmann::json &jsonFrame : root["frames"]) {
            InputFrame frame{};
            frame.keys = DecodeKeys(jsonFrame.value("keys", std::string{}));

            const unsigned int mouseButtons =
                jsonFrame.value("mouseButtons", 0u);
            for (size_t button = 0; button < 4; ++button) {
                frame.mouse.rgbButtons[button] =
                    (mouseButtons & (1u << button)) != 0 ? 0x80 : 0;
            }
            frame.mouse.lX = jsonFrame.value("mouseDX", 0L);
            frame.mouse.lY = jsonFrame.value("mouseDY", 0L);
            frame.mouse.lZ = jsonFrame.value("mouseWheel", 0L);
            frame.gamepadConnected =
                jsonFrame.value("gamepadConnected", false);
            frame.gamepadButtons =
                static_cast<WORD>(jsonFrame.value("gamepadButtons", 0u));
            frame.gamepadLeftStickX = jsonFrame.value("leftStickX", 0.0f);
            frame.gamepadLeftStickY = jsonFrame.value("leftStickY", 0.0f);
            frame.gamepadRightStickX = jsonFrame.value("rightStickX", 0.0f);
            frame.gamepadRightStickY = jsonFrame.value("rightStickY", 0.0f);
            frame.gamepadLeftTrigger = jsonFrame.value("leftTrigger", 0.0f);
            frame.gamepadRightTrigger = jsonFrame.value("rightTrigger", 0.0f);
            replayFrames_.push_back(frame);
        }

        return !replayFrames_.empty();
    } catch (...) {
        replayFrames_.clear();
        return false;
    }
}
