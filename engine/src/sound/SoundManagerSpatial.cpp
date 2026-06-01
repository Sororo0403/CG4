#include "sound/SoundManager.h"
#include "core/AssetManager.h"

#include <Objbase.h>
#include <algorithm>
#include <cwctype>
#include <cstring>
#include <cmath>
#include <filesystem>
#include <limits>
#include <mfapi.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#include <sstream>
#include <utility>

using namespace DirectX;

namespace {

float ClampFinite(float value, float minimum, float maximum, float fallback) {
    if (!std::isfinite(value)) {
        return fallback;
    }
    return std::clamp(value, minimum, maximum);
}

XMVECTOR LoadFloat3OrDefault(const XMFLOAT3 &value, FXMVECTOR fallback) {
    if (!std::isfinite(value.x) || !std::isfinite(value.y) ||
        !std::isfinite(value.z)) {
        return fallback;
    }
    XMVECTOR v = XMLoadFloat3(&value);
    const float lengthSq = XMVectorGetX(XMVector3LengthSq(v));
    if (!std::isfinite(lengthSq) || lengthSq <= 0.000001f) {
        return fallback;
    }
    return XMVector3Normalize(v);
}

XMVECTOR LoadPositionOrDefault(const XMFLOAT3 &value, FXMVECTOR fallback) {
    if (!std::isfinite(value.x) || !std::isfinite(value.y) ||
        !std::isfinite(value.z)) {
        return fallback;
    }
    return XMLoadFloat3(&value);
}

XMVECTOR NormalizeVectorOrDefault(FXMVECTOR value, FXMVECTOR fallback) {
    const float lengthSq = XMVectorGetX(XMVector3LengthSq(value));
    if (!std::isfinite(lengthSq) || lengthSq <= 0.000001f) {
        return fallback;
    }
    return XMVector3Normalize(value);
}

} // namespace



uint32_t SoundManager::Play3D(uint32_t soundId,
                              const DirectX::XMFLOAT3 &sourcePosition,
                              float volume, bool loop) {
    const uint32_t handle = Play(soundId, volume, loop);
    if (handle == kInvalidVoiceHandle) {
        return handle;
    }

    for (PlayingVoice &playingVoice : playingVoices_) {
        if (playingVoice.handle == handle) {
            playingVoice.is3D = true;
            playingVoice.position = sourcePosition;
            Apply3D(playingVoice);
            break;
        }
    }
    return handle;
}

void SoundManager::SetListener(const DirectX::XMFLOAT3 &position,
                               const DirectX::XMFLOAT3 &forward,
                               const DirectX::XMFLOAT3 &up) {
    listenerPosition_ = position;
    XMStoreFloat3(&listenerForward_,
                  LoadFloat3OrDefault(forward,
                                      XMVectorSet(0.0f, 0.0f, 1.0f, 0.0f)));
    XMStoreFloat3(&listenerUp_,
                  LoadFloat3OrDefault(up,
                                      XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f)));

    for (PlayingVoice &playingVoice : playingVoices_) {
        if (playingVoice.is3D) {
            Apply3D(playingVoice);
        }
    }
}

void SoundManager::SetVoicePosition(
    uint32_t voiceHandle, const DirectX::XMFLOAT3 &sourcePosition) {
    for (PlayingVoice &playingVoice : playingVoices_) {
        if (playingVoice.handle == voiceHandle) {
            playingVoice.is3D = true;
            playingVoice.position = sourcePosition;
            Apply3D(playingVoice);
            return;
        }
    }
}

void SoundManager::SetVoice3DRange(uint32_t voiceHandle, float minDistance,
                                   float maxDistance) {
    for (PlayingVoice &playingVoice : playingVoices_) {
        if (playingVoice.handle == voiceHandle) {
            playingVoice.minDistance =
                std::isfinite(minDistance)
                    ? (std::max)(minDistance, 0.001f)
                    : 0.001f;
            playingVoice.maxDistance =
                std::isfinite(maxDistance)
                    ? (std::max)(maxDistance,
                                 playingVoice.minDistance + 0.001f)
                    : playingVoice.minDistance + 0.001f;
            Apply3D(playingVoice);
            return;
        }
    }
}

void SoundManager::Apply3D(PlayingVoice &playingVoice) {
    if (!playingVoice.voice || !masterVoice_) {
        return;
    }

    XMVECTOR listener =
        LoadPositionOrDefault(listenerPosition_, XMVectorZero());
    XMVECTOR source = LoadPositionOrDefault(playingVoice.position, listener);
    XMVECTOR toSource = source - listener;
    const float distance = XMVectorGetX(XMVector3Length(toSource));
    if (!std::isfinite(distance)) {
        return;
    }

    const float minDistance =
        std::isfinite(playingVoice.minDistance)
            ? (std::max)(playingVoice.minDistance, 0.001f)
            : 0.001f;
    const float maxDistance =
        std::isfinite(playingVoice.maxDistance)
            ? (std::max)(playingVoice.maxDistance, minDistance + 0.001f)
            : minDistance + 0.001f;
    const float attenuation =
        1.0f - (distance - minDistance) / (maxDistance - minDistance);
    const float voiceVolume =
        ClampFinite(playingVoice.volume, 0.0f, 1.0f, 0.0f);
    const float volume =
        voiceVolume * ClampFinite(attenuation, 0.0f, 1.0f, 0.0f);

    XAUDIO2_VOICE_DETAILS sourceDetails{};
    XAUDIO2_VOICE_DETAILS masterDetails{};
    playingVoice.voice->GetVoiceDetails(&sourceDetails);
    masterVoice_->GetVoiceDetails(&masterDetails);

    const UINT32 sourceChannels =
        (std::max)(sourceDetails.InputChannels, UINT32{1});
    const UINT32 destinationChannels =
        (std::max)(masterDetails.InputChannels, UINT32{1});
    if (destinationChannels >
        (std::numeric_limits<UINT32>::max)() / sourceChannels) {
        return;
    }
    std::vector<float> matrix(sourceChannels * destinationChannels, volume);

    if (destinationChannels >= 2 && distance > 0.0001f) {
        XMVECTOR forward = LoadFloat3OrDefault(
            listenerForward_, XMVectorSet(0.0f, 0.0f, 1.0f, 0.0f));
        XMVECTOR up = LoadFloat3OrDefault(
            listenerUp_, XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f));
        XMVECTOR right = NormalizeVectorOrDefault(
            XMVector3Cross(up, forward),
            XMVectorSet(1.0f, 0.0f, 0.0f, 0.0f));
        XMVECTOR direction = NormalizeVectorOrDefault(toSource, forward);
        const float pan = ClampFinite(
            XMVectorGetX(XMVector3Dot(direction, right)), -1.0f, 1.0f, 0.0f);
        const float left = volume * std::sqrt((1.0f - pan) * 0.5f);
        const float rightVolume = volume * std::sqrt((1.0f + pan) * 0.5f);

        for (UINT32 sourceChannel = 0; sourceChannel < sourceChannels;
             ++sourceChannel) {
            matrix[sourceChannel * destinationChannels + 0] = left;
            matrix[sourceChannel * destinationChannels + 1] = rightVolume;
        }
    }

    playingVoice.voice->SetOutputMatrix(
        masterVoice_, sourceChannels, destinationChannels, matrix.data());
}
