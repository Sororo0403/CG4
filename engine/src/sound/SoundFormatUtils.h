#pragma once

#include <cstdint>
#include <string>
#include <xaudio2.h>

namespace SoundFormatUtils {

std::string MakeHResultMessage(HRESULT hr, const char *message);
bool BuildPcmWaveFormat(uint32_t sampleRate, uint16_t channels,
                        uint16_t bitsPerSample, WAVEFORMATEX &format);
bool IsSupportedPcmReadFormat(const WAVEFORMATEX &format);

} // namespace SoundFormatUtils
