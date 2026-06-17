#include "InputInternal.h"
#include "input/Input.h"
#include "input/InputReplayLimits.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <limits>
#include <sstream>
#include <string>
#include <system_error>
#include <type_traits>
#include <utility>
#include <vector>

#pragma warning(push, 0)
#include "nlohmann/json.hpp"
#pragma warning(pop)

namespace {

std::string EncodeKeys(const std::array<BYTE, 256>& keys) {
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

std::array<BYTE, 256> DecodeKeys(const std::string& encoded) {
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
            keys[index * 4 + bit] = (nibble & (1u << bit)) != 0 ? 0x80 : 0;
        }
    }
    return keys;
}

template <typename T> bool TryConvertInteger(uint64_t value, T& outValue) {
    static_assert(std::is_integral_v<T>);
    if constexpr (std::is_signed_v<T>) {
        if (value > static_cast<uint64_t>((std::numeric_limits<T>::max)())) {
            return false;
        }
    } else {
        if (value > static_cast<uint64_t>((std::numeric_limits<T>::max)())) {
            return false;
        }
    }

    outValue = static_cast<T>(value);
    return true;
}

template <typename T> bool TryConvertInteger(int64_t value, T& outValue) {
    static_assert(std::is_integral_v<T>);
    if constexpr (std::is_signed_v<T>) {
        if (value < static_cast<int64_t>((std::numeric_limits<T>::lowest)()) ||
            value > static_cast<int64_t>((std::numeric_limits<T>::max)())) {
            return false;
        }
    } else {
        if (value < 0 ||
            static_cast<uint64_t>(value) > static_cast<uint64_t>((std::numeric_limits<T>::max)())) {
            return false;
        }
    }

    outValue = static_cast<T>(value);
    return true;
}

template <typename T>
T JsonValueOr(const nlohmann::json& object, const char* key,
              const T& fallback) {
    const auto it = object.find(key);
    if (it == object.end() || it->is_null()) {
        return fallback;
    }
    if constexpr (std::is_same_v<T, std::string>) {
        const std::string* value = it->get_ptr<const std::string*>();
        return value != nullptr ? *value : fallback;
    } else if constexpr (std::is_same_v<T, bool>) {
        const bool* value = it->get_ptr<const bool*>();
        return value != nullptr ? *value : fallback;
    } else if constexpr (std::is_floating_point_v<T>) {
        if (it->is_number()) {
            const double value = it->get<double>();
            if (std::isfinite(value) &&
                value >= static_cast<double>((std::numeric_limits<T>::lowest)()) &&
                value <= static_cast<double>((std::numeric_limits<T>::max)())) {
                return static_cast<T>(value);
            }
        }
        return fallback;
    } else if constexpr (std::is_integral_v<T>) {
        T converted{};
        if (it->is_number_unsigned()) {
            return TryConvertInteger(it->get<uint64_t>(), converted) ? converted : fallback;
        }
        if (it->is_number_integer()) {
            return TryConvertInteger(it->get<int64_t>(), converted) ? converted : fallback;
        }
        return fallback;
    } else {
        return fallback;
    }
}

float JsonClampedFloat(const nlohmann::json& object, const char* key, float fallback,
                       float minValue, float maxValue) {
    return std::clamp(JsonValueOr<float>(object, key, fallback), minValue, maxValue);
}

} // namespace

bool Input::SaveRecording() const {
    if (state_->recordedFrames.empty() ||
        state_->recordedFrames.size() > InputReplayLimits::kMaxFrames) {
        return false;
    }

    const std::filesystem::path path(state_->replayPath);
    if (path.has_parent_path()) {
        std::error_code error;
        std::filesystem::create_directories(path.parent_path(), error);
        if (error) {
            return false;
        }
    }

    const std::filesystem::path tempPath(path.wstring() + L".tmp");
    std::ofstream file(tempPath, std::ios::binary);
    if (!file) {
        return false;
    }

    std::uintmax_t bytesWritten = 0;
    const auto cleanupTemp = [&]() {
        file.close();
        std::error_code removeError;
        std::filesystem::remove(tempPath, removeError);
    };
    const auto writeText = [&](const std::string& text) -> bool {
        const std::uintmax_t textSize = static_cast<std::uintmax_t>(text.size());
        if (bytesWritten > InputReplayLimits::kMaxFileBytes ||
            textSize > InputReplayLimits::kMaxFileBytes - bytesWritten) {
            return false;
        }

        file.write(text.data(), static_cast<std::streamsize>(text.size()));
        if (!file.good()) {
            return false;
        }

        bytesWritten += textSize;
        return true;
    };

    std::string header = "{\n  \"version\": 1,\n  \"fixedDeltaTime\": ";
    header += nlohmann::json(state_->replayFixedDeltaTime).dump();
    header += ",\n  \"frames\": [\n";
    if (!writeText(header)) {
        cleanupTemp();
        return false;
    }

    for (size_t index = 0; index < state_->recordedFrames.size(); ++index) {
        const InputFrame& frame = state_->recordedFrames[index];
        unsigned int mouseButtons = 0;
        for (size_t button = 0; button < 4; ++button) {
            if ((frame.mouse.rgbButtons[button] & 0x80) != 0) {
                mouseButtons |= 1u << button;
            }
        }

        std::string serializedFrame;
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
        serializedFrame = jsonFrame.dump();
        const std::string prefix = index == 0 ? "    " : ",\n    ";
        if (!writeText(prefix) || !writeText(serializedFrame)) {
            cleanupTemp();
            return false;
        }
    }

    if (!writeText("\n  ]\n}\n")) {
        cleanupTemp();
        return false;
    }

    file.close();
    if (!file.good()) {
        cleanupTemp();
        return false;
    }

    std::error_code error;
    std::filesystem::remove(path, error);
    error.clear();
    std::filesystem::rename(tempPath, path, error);
    if (error) {
        std::filesystem::remove(tempPath, error);
        return false;
    }

    return true;
}

bool Input::LoadReplay(const std::wstring& path) {
    std::error_code fileSizeError;
    const std::uintmax_t fileSize =
        std::filesystem::file_size(std::filesystem::path(path), fileSizeError);
    if (fileSizeError || fileSize == 0 || fileSize > InputReplayLimits::kMaxFileBytes) {
        return false;
    }

    std::ifstream file(std::filesystem::path(path), std::ios::binary);
    if (!file) {
        return false;
    }

    nlohmann::json root;
    root = nlohmann::json::parse(file, nullptr, false);
    if (root.is_discarded()) {
        return false;
    }
    if (!root.contains("frames") || !root["frames"].is_array()) {
        return false;
    }
    if (root["frames"].size() == 0 || root["frames"].size() > InputReplayLimits::kMaxFrames) {
        return false;
    }

    std::vector<InputFrame> loadedFrames;
    loadedFrames.reserve(root["frames"].size());
    for (const nlohmann::json& jsonFrame : root["frames"]) {
        if (!jsonFrame.is_object()) {
            return false;
        }
        InputFrame frame{};
        frame.keys = DecodeKeys(JsonValueOr<std::string>(jsonFrame, "keys", std::string{}));

        const unsigned int mouseButtons =
            JsonValueOr<unsigned int>(jsonFrame, "mouseButtons", 0u) & 0x0Fu;
        for (size_t button = 0; button < 4; ++button) {
            frame.mouse.rgbButtons[button] = (mouseButtons & (1u << button)) != 0 ? 0x80 : 0;
        }
        frame.mouse.lX = JsonValueOr<LONG>(jsonFrame, "mouseDX", 0L);
        frame.mouse.lY = JsonValueOr<LONG>(jsonFrame, "mouseDY", 0L);
        frame.mouse.lZ = JsonValueOr<LONG>(jsonFrame, "mouseWheel", 0L);
        frame.gamepadConnected = JsonValueOr<bool>(jsonFrame, "gamepadConnected", false);
        frame.gamepadButtons = JsonValueOr<WORD>(jsonFrame, "gamepadButtons", WORD{0});
        frame.gamepadLeftStickX = JsonClampedFloat(jsonFrame, "leftStickX", 0.0f, -1.0f, 1.0f);
        frame.gamepadLeftStickY = JsonClampedFloat(jsonFrame, "leftStickY", 0.0f, -1.0f, 1.0f);
        frame.gamepadRightStickX = JsonClampedFloat(jsonFrame, "rightStickX", 0.0f, -1.0f, 1.0f);
        frame.gamepadRightStickY = JsonClampedFloat(jsonFrame, "rightStickY", 0.0f, -1.0f, 1.0f);
        frame.gamepadLeftTrigger = JsonClampedFloat(jsonFrame, "leftTrigger", 0.0f, 0.0f, 1.0f);
        frame.gamepadRightTrigger = JsonClampedFloat(jsonFrame, "rightTrigger", 0.0f, 0.0f, 1.0f);
        loadedFrames.push_back(frame);
    }

    state_->replayFrames = std::move(loadedFrames);
    return true;
}
