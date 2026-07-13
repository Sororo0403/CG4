#pragma once

#include "graphics/DirectXCommon.h"
#include "input/InputReplayTypes.h"

#include <Windows.h>
#include <algorithm>
#include <array>
#include <chrono>
#include <ctime>
#include <iomanip>
#include <limits>
#include <sstream>
#include <string>

namespace EngineRuntimeInternal {

class FrameAbortScope {
public:
    explicit FrameAbortScope(DirectXCommon& dxCommon) : dxCommon_(&dxCommon) {}
    ~FrameAbortScope() {
        if (!completed_ && dxCommon_ != nullptr) {
            dxCommon_->AbortFrame();
        }
    }

    FrameAbortScope(const FrameAbortScope&) = delete;
    FrameAbortScope& operator=(const FrameAbortScope&) = delete;

    void Complete() noexcept {
        completed_ = true;
    }

private:
    DirectXCommon* dxCommon_ = nullptr;
    bool completed_ = false;
};

inline std::string BoolText(bool value) {
    return value ? "true" : "false";
}

inline std::string MakeTimestamp() {
    const auto now = std::chrono::system_clock::now();
    const std::time_t time = std::chrono::system_clock::to_time_t(now);

    std::tm localTime{};
    localtime_s(&localTime, &time);

    std::ostringstream stream;
    stream << std::put_time(&localTime, "%Y-%m-%d %H:%M:%S");
    return stream.str();
}

inline std::string WideToUtf8(const std::wstring& value) {
    if (value.empty()) {
        return {};
    }

    if (value.size() > static_cast<size_t>((std::numeric_limits<int>::max)())) {
        return {};
    }

    const int sourceLength = static_cast<int>(value.size());
    const int byteCount =
        WideCharToMultiByte(CP_UTF8, 0, value.data(), sourceLength, nullptr, 0, nullptr, nullptr);
    if (byteCount <= 0) {
        return {};
    }

    std::string result(static_cast<size_t>(byteCount), '\0');
    WideCharToMultiByte(CP_UTF8, 0, value.data(), sourceLength, result.data(), byteCount, nullptr,
                        nullptr);
    return result;
}

inline const char* ReplayModeName(InputReplayMode mode) {
    struct ReplayModeNameEntry {
        InputReplayMode mode;
        const char* name;
    };
    static constexpr std::array<ReplayModeNameEntry, 3> kNames{{
        {InputReplayMode::Live, "Live"},
        {InputReplayMode::Record, "Record"},
        {InputReplayMode::Replay, "Replay"},
    }};

    const auto it =
        std::find_if(kNames.begin(), kNames.end(),
                     [mode](const ReplayModeNameEntry& entry) { return entry.mode == mode; });
    return it != kNames.end() ? it->name : "Unknown";
}

} // namespace EngineRuntimeInternal
