#include "core/MathUtils.h"
#include "core/Numeric.h"
#include "internal/SoundManagerInternal.h"
#include "sound/SoundManager.h"

#include <algorithm>
#include <cmath>
#include <exception>
#include <limits>
#include <vector>

using namespace DirectX;

namespace {

using SoundManagerInternal::LoadFloat3OrDefault;
using SoundManagerInternal::LoadPositionOrDefault;
using SoundManagerInternal::NormalizeVectorOrDefault;

constexpr float kPi = MathUtils::kPi;

} // namespace

uint32_t SoundManager::Play3D(uint32_t soundId, const DirectX::XMFLOAT3& sourcePosition,
                              float volume, bool loop) {
    const uint32_t handle = Play(soundId, volume, loop);
    if (handle == kInvalidVoiceHandle) {
        return handle;
    }

    auto it = std::find_if(
        state_->playingVoices.begin(), state_->playingVoices.end(),
        [handle](const PlayingVoice& playingVoice) { return playingVoice.handle == handle; });
    if (it != state_->playingVoices.end()) {
        it->is3D = true;
        it->position = sourcePosition;
        Apply3D(*it);
    }
    return handle;
}

void SoundManager::SetListener(const DirectX::XMFLOAT3& position, const DirectX::XMFLOAT3& forward,
                               const DirectX::XMFLOAT3& up) {
    state_->listenerPosition = position;
    XMStoreFloat3(&state_->listenerForward,
                  LoadFloat3OrDefault(forward, XMVectorSet(0.0f, 0.0f, 1.0f, 0.0f)));
    XMStoreFloat3(&state_->listenerUp,
                  LoadFloat3OrDefault(up, XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f)));

    for (PlayingVoice& playingVoice : state_->playingVoices) {
        if (playingVoice.is3D) {
            Apply3D(playingVoice);
        }
    }
}

void SoundManager::SetVoicePosition(uint32_t voiceHandle, const DirectX::XMFLOAT3& sourcePosition) {
    auto it = std::find_if(state_->playingVoices.begin(), state_->playingVoices.end(),
                           [voiceHandle](const PlayingVoice& playingVoice) {
                               return playingVoice.handle == voiceHandle;
                           });
    if (it != state_->playingVoices.end()) {
        it->is3D = true;
        it->position = sourcePosition;
        Apply3D(*it);
    }
}

void SoundManager::SetVoice3DRange(uint32_t voiceHandle, float minDistance, float maxDistance) {
    auto it = std::find_if(state_->playingVoices.begin(), state_->playingVoices.end(),
                           [voiceHandle](const PlayingVoice& playingVoice) {
                               return playingVoice.handle == voiceHandle;
                           });
    if (it != state_->playingVoices.end()) {
        it->minDistance = std::isfinite(minDistance) ? (std::max)(minDistance, 0.001f) : 0.001f;
        it->maxDistance = std::isfinite(maxDistance)
                              ? (std::max)(maxDistance, it->minDistance + 0.001f)
                              : it->minDistance + 0.001f;
        Apply3D(*it);
    }
}

bool SoundManager::ApplyVoiceLowPass(PlayingVoice& playingVoice, float cutoffHz) {
    if (!playingVoice.voice) {
        return false;
    }

    XAUDIO2_VOICE_DETAILS sourceDetails{};
    playingVoice.voice->GetVoiceDetails(&sourceDetails);
    const float sampleRate = sourceDetails.InputSampleRate > 0u
                                 ? static_cast<float>(sourceDetails.InputSampleRate)
                                 : 48000.0f;
    const float maxStableCutoff = sampleRate * 0.49f;
    const float safeCutoff =
        std::clamp(std::isfinite(cutoffHz) ? cutoffHz : maxStableCutoff, 80.0f, maxStableCutoff);
    const float normalizedFrequency = std::clamp(2.0f * std::sin(kPi * safeCutoff / sampleRate),
                                                 0.001f, XAUDIO2_MAX_FILTER_FREQUENCY);

    XAUDIO2_FILTER_PARAMETERS filter{};
    filter.Type = LowPassFilter;
    filter.Frequency = normalizedFrequency;
    filter.OneOverQ = 1.0f;
    if (FAILED(playingVoice.voice->SetFilterParameters(&filter))) {
        return false;
    }
    playingVoice.lowPassCutoffHz = safeCutoff;
    return true;
}

void SoundManager::SetVoiceLowPassCutoff(uint32_t voiceHandle, float cutoffHz) {
    auto it = std::find_if(state_->playingVoices.begin(), state_->playingVoices.end(),
                           [voiceHandle](const PlayingVoice& playingVoice) {
                               return playingVoice.handle == voiceHandle;
                           });
    if (it != state_->playingVoices.end() && ApplyVoiceLowPass(*it, cutoffHz)) {
        it->manualLowPassCutoff = true;
    }
}

float SoundManager::GetVoiceLowPassCutoff(uint32_t voiceHandle) const {
    const auto it = std::find_if(state_->playingVoices.begin(), state_->playingVoices.end(),
                                 [voiceHandle](const PlayingVoice& playingVoice) {
                                     return playingVoice.handle == voiceHandle;
                                 });
    return it != state_->playingVoices.end() ? it->lowPassCutoffHz : 0.0f;
}

void SoundManager::Apply3D(PlayingVoice& playingVoice) {
    if (!playingVoice.voice || !state_->masterVoice) {
        return;
    }

    XMVECTOR listener = LoadPositionOrDefault(state_->listenerPosition, XMVectorZero());
    XMVECTOR source = LoadPositionOrDefault(playingVoice.position, listener);
    XMVECTOR toSource = source - listener;
    const float distance = XMVectorGetX(XMVector3Length(toSource));
    if (!std::isfinite(distance)) {
        return;
    }

    const float minDistance = std::isfinite(playingVoice.minDistance)
                                  ? (std::max)(playingVoice.minDistance, 0.001f)
                                  : 0.001f;
    const float maxDistance = std::isfinite(playingVoice.maxDistance)
                                  ? (std::max)(playingVoice.maxDistance, minDistance + 0.001f)
                                  : minDistance + 0.001f;
    float volume = 1.0f;
    if (distance > minDistance) {
        const float excessDistance = distance - minDistance;
        const float distanceRolloff = minDistance / (minDistance + excessDistance * 0.18f);
        const float rangeFade =
            1.0f - MathUtils::SmoothStep(maxDistance * 0.72f, maxDistance, distance);
        volume = distanceRolloff * rangeFade;
    }
    volume = Numeric::ClampFinite(volume, 0.0f, 1.0f, 0.0f);

    if (!playingVoice.manualLowPassCutoff) {
        const float lowPassAmount =
            MathUtils::SmoothStep(maxDistance * 0.35f, maxDistance, distance);
        const float lowPassCutoffHz = MathUtils::LerpClamped(24000.0f, 1350.0f, lowPassAmount);
        ApplyVoiceLowPass(playingVoice, lowPassCutoffHz);
    }

    XAUDIO2_VOICE_DETAILS sourceDetails{};
    XAUDIO2_VOICE_DETAILS masterDetails{};
    playingVoice.voice->GetVoiceDetails(&sourceDetails);
    state_->masterVoice->GetVoiceDetails(&masterDetails);

    const UINT32 sourceChannels = (std::max)(sourceDetails.InputChannels, UINT32{1});
    const UINT32 destinationChannels = (std::max)(masterDetails.InputChannels, UINT32{1});
    if (destinationChannels > (std::numeric_limits<UINT32>::max)() / sourceChannels) {
        return;
    }
    const size_t matrixSize =
        static_cast<size_t>(sourceChannels) * static_cast<size_t>(destinationChannels);
    std::vector<float>& matrix = playingVoice.outputMatrix;
    if (matrix.size() != matrixSize) {
        try {
            matrix.resize(matrixSize);
        } catch (const std::exception&) {
            matrix.clear();
            return;
        }
    }
    std::fill(matrix.begin(), matrix.end(), volume);

    if (destinationChannels >= 2 && distance > 0.0001f) {
        XMVECTOR forward =
            LoadFloat3OrDefault(state_->listenerForward, XMVectorSet(0.0f, 0.0f, 1.0f, 0.0f));
        XMVECTOR up = LoadFloat3OrDefault(state_->listenerUp, XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f));
        XMVECTOR right = NormalizeVectorOrDefault(XMVector3Cross(up, forward),
                                                  XMVectorSet(1.0f, 0.0f, 0.0f, 0.0f));
        XMVECTOR direction = NormalizeVectorOrDefault(toSource, forward);
        const float pan =
            Numeric::ClampFinite(XMVectorGetX(XMVector3Dot(direction, right)), -1.0f, 1.0f, 0.0f);
        const float left = volume * std::sqrt((1.0f - pan) * 0.5f);
        const float rightVolume = volume * std::sqrt((1.0f + pan) * 0.5f);

        for (UINT32 sourceChannel = 0; sourceChannel < sourceChannels; ++sourceChannel) {
            matrix[sourceChannel * destinationChannels + 0] = left;
            matrix[sourceChannel * destinationChannels + 1] = rightVolume;
        }
    }

    playingVoice.voice->SetOutputMatrix(state_->masterVoice, sourceChannels, destinationChannels,
                                        matrix.data());
}
