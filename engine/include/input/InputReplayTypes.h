#pragma once

#include <string>

enum class InputReplayMode {
    Live,
    Record,
    Replay,
};

struct InputReplayStartupOptions {
    std::wstring recordPath;
    std::wstring replayPath;
    std::wstring replayDirectory;
    bool autoRecord = false;
    bool enableHotkeys = true;
};
