#include "core/MathUtils.h"
#include "core/Numeric.h"
#include "internal/SoundFormatUtils.h"
#include "internal/SoundManagerInternal.h"
#include "sound/SoundManager.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace {

struct PcmAnalysisView {
    const BYTE* data = nullptr;
    size_t frameCount = 0;
    WAVEFORMATEX format{};
    uint16_t channels = 1;
    uint16_t bits = 0;
    size_t bytesPerSample = 0;
    float duration = 0.0f;
};

bool TryBuildPcmAnalysisView(const AudioFileLoader::SoundData& sound, PcmAnalysisView& view) {
    if (!sound.CopyFormat(view.format) ||
        !SoundFormatUtils::IsSupportedPcmReadFormat(view.format) || sound.decodedPcm.empty()) {
        return false;
    }

    view.frameCount = sound.decodedPcm.size() / static_cast<size_t>(view.format.nBlockAlign);
    if (view.frameCount == 0) {
        return false;
    }
    view.duration =
        static_cast<float>(view.frameCount) / static_cast<float>(view.format.nSamplesPerSec);
    if (!std::isfinite(view.duration) || view.duration <= 0.0f) {
        return false;
    }

    view.data = sound.decodedPcm.data();
    view.channels = (std::max<uint16_t>)(view.format.nChannels, 1);
    view.bits = view.format.wBitsPerSample;
    view.bytesPerSample = static_cast<size_t>(view.bits) / 8u;
    return true;
}

float NormalizePcmPlaybackTime(float playbackSeconds, float duration) {
    const float safePlaybackSeconds = std::isfinite(playbackSeconds) ? playbackSeconds : 0.0f;
    float sampleTime = std::fmod(safePlaybackSeconds, duration);
    if (!std::isfinite(sampleTime)) {
        return 0.0f;
    }
    return sampleTime < 0.0f ? sampleTime + duration : sampleTime;
}

float ReadPcmFrameAverage(const PcmAnalysisView& view, size_t frame) {
    const BYTE* base = view.data + frame * view.format.nBlockAlign;
    float total = 0.0f;
    size_t count = 0;
    for (uint16_t ch = 0; ch < view.channels; ++ch) {
        const size_t byteOffset = static_cast<size_t>(ch) * view.bytesPerSample;
        if (byteOffset + view.bytesPerSample > view.format.nBlockAlign) {
            continue;
        }

        if (view.bits == 16) {
            int16_t sample = 0;
            std::memcpy(&sample, base + byteOffset, sizeof(sample));
            total += static_cast<float>(sample) / 32768.0f;
            ++count;
        } else if (view.bits == 8) {
            const uint8_t sample = *(base + byteOffset);
            total += (static_cast<float>(sample) - 128.0f) / 128.0f;
            ++count;
        }
    }
    return count > 0 ? total / static_cast<float>(count) : 0.0f;
}

float ComputeSpectrumBand(const PcmAnalysisView& view, size_t centerFrame, size_t band,
                          size_t bandCount) {
    constexpr size_t kWindowFrames = 768;
    const float t =
        bandCount > 1 ? static_cast<float>(band) / static_cast<float>(bandCount - 1) : 0.0f;
    const float frequency = 45.0f * std::pow(12000.0f / 45.0f, t);
    const float omega =
        MathUtils::kTwoPi * frequency / static_cast<float>(view.format.nSamplesPerSec);
    double real = 0.0;
    double imag = 0.0;

    for (size_t i = 0; i < kWindowFrames; ++i) {
        const size_t frame = (centerFrame + (i % view.frameCount) + view.frameCount -
                              ((kWindowFrames / 2) % view.frameCount)) %
                             view.frameCount;
        const float window = 0.5f - 0.5f * std::cos(MathUtils::kTwoPi * static_cast<float>(i) /
                                                    static_cast<float>(kWindowFrames - 1));
        const float sample = ReadPcmFrameAverage(view, frame) * window;
        const float phase = omega * static_cast<float>(i);
        real += static_cast<double>(sample * std::cos(phase));
        imag -= static_cast<double>(sample * std::sin(phase));
    }

    const float magnitude = static_cast<float>(std::sqrt(real * real + imag * imag)) /
                            static_cast<float>(kWindowFrames);
    const float bassLift = 1.35f - 0.45f * t;
    const float bandValue = std::pow(magnitude * bassLift * 32.0f, 0.55f);
    return std::isfinite(bandValue) ? std::clamp(bandValue, 0.0f, 1.0f) : 0.0f;
}

} // namespace

float SoundManager::GetAmplitudeAt(uint32_t soundId, float playbackSeconds,
                                   float windowSeconds) const {
    if (soundId >= state_->sounds.size()) {
        return 0.0f;
    }

    const AudioFileLoader::SoundData& sound = state_->sounds[soundId].data;
    WAVEFORMATEX format{};
    if (!sound.CopyFormat(format) || !SoundFormatUtils::IsSupportedPcmReadFormat(format) ||
        sound.decodedPcm.empty()) {
        return 0.0f;
    }

    const size_t frameCount = sound.decodedPcm.size() / static_cast<size_t>(format.nBlockAlign);
    if (frameCount == 0) {
        return 0.0f;
    }

    const float duration =
        static_cast<float>(frameCount) / static_cast<float>(format.nSamplesPerSec);
    if (!std::isfinite(duration) || duration <= 0.0f) {
        return 0.0f;
    }

    const float safePlaybackSeconds = std::isfinite(playbackSeconds) ? playbackSeconds : 0.0f;
    float sampleTime = std::fmod(safePlaybackSeconds, duration);
    if (!std::isfinite(sampleTime)) {
        return 0.0f;
    }
    if (sampleTime < 0.0f) {
        sampleTime += duration;
    }

    const float maxWindowSeconds = (std::max)(duration, 0.005f);
    const float safeWindowSeconds = Numeric::ClampFinite(windowSeconds, 0.005f, maxWindowSeconds,
                                                         (std::min)(0.045f, maxWindowSeconds));
    const double halfWindowFramesDouble =
        static_cast<double>(safeWindowSeconds) * static_cast<double>(format.nSamplesPerSec) * 0.5;

    const size_t centerFrame = static_cast<size_t>(static_cast<double>(sampleTime) *
                                                   static_cast<double>(format.nSamplesPerSec)) %
                               frameCount;
    const size_t halfWindowFrames =
        (std::max<size_t>)(1, (std::min)(frameCount, static_cast<size_t>(halfWindowFramesDouble)));
    const size_t sampleFrames = (std::min)(frameCount, halfWindowFrames * 2 + 1);
    const uint16_t channels = (std::max<uint16_t>)(format.nChannels, 1);
    const uint16_t bits = format.wBitsPerSample;
    const size_t bytesPerSample = static_cast<size_t>(bits) / 8u;

    double sumSquares = 0.0;
    size_t valueCount = 0;
    for (size_t i = 0; i < sampleFrames; ++i) {
        const size_t frame = (centerFrame + frameCount + i - halfWindowFrames) % frameCount;
        const BYTE* base = sound.decodedPcm.data() + frame * format.nBlockAlign;
        for (uint16_t ch = 0; ch < channels; ++ch) {
            const size_t byteOffset = static_cast<size_t>(ch) * bytesPerSample;
            if (byteOffset + bytesPerSample > format.nBlockAlign) {
                continue;
            }

            float value = 0.0f;
            if (bits == 16) {
                int16_t sample = 0;
                std::memcpy(&sample, base + byteOffset, sizeof(sample));
                value = static_cast<float>(sample) / 32768.0f;
            } else if (bits == 8) {
                const uint8_t sample = *(base + byteOffset);
                value = (static_cast<float>(sample) - 128.0f) / 128.0f;
            } else {
                continue;
            }
            sumSquares += static_cast<double>(value * value);
            ++valueCount;
        }
    }

    if (valueCount == 0) {
        return 0.0f;
    }

    return std::clamp(static_cast<float>(std::sqrt(sumSquares / static_cast<double>(valueCount))),
                      0.0f, 1.0f);
}

void SoundManager::FillSpectrumBands(uint32_t soundId, float playbackSeconds, float* outBands,
                                     size_t bandCount) const {
    if (outBands == nullptr || bandCount == 0) {
        return;
    }
    std::fill(outBands, outBands + bandCount, 0.0f);

    if (soundId >= state_->sounds.size()) {
        return;
    }

    const AudioFileLoader::SoundData& sound = state_->sounds[soundId].data;
    PcmAnalysisView view;
    if (!TryBuildPcmAnalysisView(sound, view)) {
        return;
    }

    const float sampleTime = NormalizePcmPlaybackTime(playbackSeconds, view.duration);
    const size_t centerFrame =
        static_cast<size_t>(static_cast<double>(sampleTime) *
                            static_cast<double>(view.format.nSamplesPerSec)) %
        view.frameCount;

    for (size_t band = 0; band < bandCount; ++band) {
        outBands[band] = ComputeSpectrumBand(view, centerFrame, band, bandCount);
    }
}
