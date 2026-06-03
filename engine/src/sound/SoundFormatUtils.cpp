#include "SoundFormatUtils.h"

#include <limits>
#include <sstream>

namespace SoundFormatUtils {

std::string MakeHResultMessage(HRESULT hr, const char *message) {
    std::ostringstream oss;
    oss << message << " HRESULT=0x" << std::hex
        << static_cast<unsigned long>(hr);
    return oss.str();
}

bool BuildPcmWaveFormat(uint32_t sampleRate, uint16_t channels,
                        uint16_t bitsPerSample, WAVEFORMATEX &format) {
    if (sampleRate == 0u || channels == 0u || bitsPerSample == 0u) {
        return false;
    }

    const uint32_t frameBits =
        static_cast<uint32_t>(channels) * static_cast<uint32_t>(bitsPerSample);
    if (frameBits == 0u || (frameBits % 8u) != 0u) {
        return false;
    }

    const uint32_t blockAlign = frameBits / 8u;
    if (blockAlign == 0u ||
        blockAlign >
            static_cast<uint32_t>((std::numeric_limits<WORD>::max)())) {
        return false;
    }
    if (sampleRate >
        (std::numeric_limits<DWORD>::max)() / static_cast<DWORD>(blockAlign)) {
        return false;
    }

    format = {};
    format.wFormatTag = WAVE_FORMAT_PCM;
    format.nChannels = channels;
    format.nSamplesPerSec = sampleRate;
    format.wBitsPerSample = bitsPerSample;
    format.nBlockAlign = static_cast<WORD>(blockAlign);
    format.nAvgBytesPerSec = sampleRate * format.nBlockAlign;
    return true;
}

bool IsSupportedPcmReadFormat(const WAVEFORMATEX &format) {
    if (format.nSamplesPerSec == 0 || format.nChannels == 0 ||
        format.nBlockAlign == 0) {
        return false;
    }
    if (format.wBitsPerSample != 8 && format.wBitsPerSample != 16) {
        return false;
    }

    const uint32_t bytesPerSample =
        static_cast<uint32_t>(format.wBitsPerSample) / 8u;
    const uint32_t minBlockAlign =
        static_cast<uint32_t>(format.nChannels) * bytesPerSample;
    return minBlockAlign != 0u && minBlockAlign <= format.nBlockAlign;
}

} // namespace SoundFormatUtils
