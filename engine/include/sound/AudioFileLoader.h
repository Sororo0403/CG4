#pragma once
#include <Windows.h>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>
#include <xaudio2.h>

namespace AudioFileLoader {

struct SoundData {
    std::vector<BYTE> waveFormat;
    std::vector<BYTE> decodedPcm;

    struct Info {
        uint32_t sampleRate = 0;
        uint16_t channels = 0;
        uint16_t bitsPerSample = 0;
        float durationSeconds = 0.0f;
        size_t decodedBytes = 0;
    } info{};

    bool CopyFormat(WAVEFORMATEX& outFormat) const {
        if (waveFormat.size() < sizeof(WAVEFORMATEX)) {
            outFormat = {};
            return false;
        }
        std::memcpy(&outFormat, waveFormat.data(), sizeof(outFormat));
        return true;
    }
};

/// <summary>
/// Loadを実行する
/// </summary>
SoundData Load(const std::wstring& path);
bool TryLoad(const std::wstring& path, SoundData& outData);

} // namespace AudioFileLoader
