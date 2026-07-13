#pragma once

#include "sound/AudioFileLoader.h"
#include "sound/SoundManager.h"
#include "sound/SoundVoiceCallback.h"

#include <DirectXMath.h>
#include <cmath>
#include <cstdint>
#include <deque>
#include <memory>
#include <mfapi.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#include <string>
#include <unordered_map>
#include <vector>
#include <wrl.h>
#include <xaudio2.h>

struct SoundManager::PlayingVoice {
    IXAudio2SourceVoice* voice = nullptr;
    std::unique_ptr<SoundVoiceCallback> callback;
    uint32_t handle = kInvalidVoiceHandle;
    uint32_t soundId = 0;
    uint32_t startFrame = 0;
    float volume = 1.0f;
    float frequencyRatio = XAUDIO2_DEFAULT_FREQ_RATIO;
    float lowPassCutoffHz = 24000.0f;
    bool manualLowPassCutoff = false;
    bool loop = false;
    bool is3D = false;
    bool isStreaming = false;
    bool streamSourceEnded = false;
    std::vector<BYTE> streamWaveFormat;
    std::deque<std::vector<BYTE>> streamBuffers;
    std::vector<float> outputMatrix;
    Microsoft::WRL::ComPtr<IMFSourceReader> streamReader;
    DirectX::XMFLOAT3 position{0.0f, 0.0f, 0.0f};
    float minDistance = 1.0f;
    float maxDistance = 30.0f;
};

struct SoundManager::SoundResource {
    AudioFileLoader::SoundData data;
};

struct SoundManager::State {
    Microsoft::WRL::ComPtr<IXAudio2> xAudio2;
    IXAudio2MasteringVoice* masterVoice = nullptr;
    float masterVolume = 1.0f;
    bool comInitialized = false;
    bool mediaFoundationStarted = false;
    std::string lastInitializeError;

    std::vector<SoundResource> sounds;
    std::vector<PlayingVoice> playingVoices;
    std::unordered_map<std::wstring, uint32_t> pathToSoundId;
    uint32_t nextVoiceHandle = 1;
    DirectX::XMFLOAT3 listenerPosition{0.0f, 0.0f, 0.0f};
    DirectX::XMFLOAT3 listenerForward{0.0f, 0.0f, 1.0f};
    DirectX::XMFLOAT3 listenerUp{0.0f, 1.0f, 0.0f};
};

namespace SoundManagerInternal {

inline constexpr uint32_t kStreamQueuedBuffers = 3u;

class SourceVoiceGuard {
public:
    explicit SourceVoiceGuard(IXAudio2SourceVoice* voice = nullptr) : voice_(voice) {}
    SourceVoiceGuard(const SourceVoiceGuard&) = delete;
    SourceVoiceGuard& operator=(const SourceVoiceGuard&) = delete;

    ~SourceVoiceGuard() {
        Reset();
    }

    IXAudio2SourceVoice* Get() const {
        return voice_;
    }

    IXAudio2SourceVoice* Release() {
        IXAudio2SourceVoice* voice = voice_;
        voice_ = nullptr;
        return voice;
    }

    void Reset(IXAudio2SourceVoice* voice = nullptr) {
        if (voice_) {
            voice_->DestroyVoice();
        }
        voice_ = voice;
    }

private:
    IXAudio2SourceVoice* voice_ = nullptr;
};

inline DirectX::XMVECTOR LoadFloat3OrDefault(const DirectX::XMFLOAT3& value,
                                             DirectX::FXMVECTOR fallback) {
    if (!std::isfinite(value.x) || !std::isfinite(value.y) || !std::isfinite(value.z)) {
        return fallback;
    }
    DirectX::XMVECTOR v = DirectX::XMLoadFloat3(&value);
    const float lengthSq = DirectX::XMVectorGetX(DirectX::XMVector3LengthSq(v));
    if (!std::isfinite(lengthSq) || lengthSq <= 0.000001f) {
        return fallback;
    }
    return DirectX::XMVector3Normalize(v);
}

inline DirectX::XMVECTOR LoadPositionOrDefault(const DirectX::XMFLOAT3& value,
                                               DirectX::FXMVECTOR fallback) {
    if (!std::isfinite(value.x) || !std::isfinite(value.y) || !std::isfinite(value.z)) {
        return fallback;
    }
    return DirectX::XMLoadFloat3(&value);
}

inline DirectX::XMVECTOR NormalizeVectorOrDefault(DirectX::FXMVECTOR value,
                                                  DirectX::FXMVECTOR fallback) {
    const float lengthSq = DirectX::XMVectorGetX(DirectX::XMVector3LengthSq(value));
    if (!std::isfinite(lengthSq) || lengthSq <= 0.000001f) {
        return fallback;
    }
    return DirectX::XMVector3Normalize(value);
}

} // namespace SoundManagerInternal
