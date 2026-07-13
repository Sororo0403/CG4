#pragma once

#include "core/ResourceHandle.h"
#include "sound/AudioFileLoader.h"

#include <DirectXMath.h>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

class ISoundService {
public:
    static constexpr uint32_t kInvalidVoiceHandle = kInvalidResourceId;
    static constexpr uint32_t kInvalidSoundId = kInvalidResourceId;

    using SoundInfo = AudioFileLoader::SoundData::Info;

    virtual ~ISoundService() = default;

    virtual uint32_t CreatePcm16Sound(const std::wstring& cacheKey,
                                      const std::vector<int16_t>& pcmSamples,
                                      uint32_t sampleRate = 48000, uint16_t channels = 1) = 0;
    virtual uint32_t FindPcm16Sound(const std::wstring& cacheKey) const = 0;
    virtual uint32_t Play(uint32_t soundId, float volume = 1.0f, bool loop = false) = 0;
    virtual uint32_t PlayFrom(uint32_t soundId, float startSeconds, float volume = 1.0f,
                              bool loop = false) = 0;
    virtual uint32_t Play3D(uint32_t soundId, const DirectX::XMFLOAT3& sourcePosition,
                            float volume = 1.0f, bool loop = false) = 0;
    virtual void Stop(uint32_t voiceHandle) = 0;
    virtual void StopAll() = 0;
    virtual bool IsPlaying(uint32_t voiceHandle) const = 0;
    virtual const SoundInfo* GetInfo(uint32_t soundId) const = 0;
    virtual void SetVoiceVolume(uint32_t voiceHandle, float volume) = 0;
    virtual void SetVoiceFrequencyRatio(uint32_t voiceHandle, float frequencyRatio) = 0;
    virtual void SetVoicePosition(uint32_t voiceHandle,
                                  const DirectX::XMFLOAT3& sourcePosition) = 0;
    virtual void SetVoice3DRange(uint32_t voiceHandle, float minDistance, float maxDistance) = 0;
    virtual void SetListener(const DirectX::XMFLOAT3& position, const DirectX::XMFLOAT3& forward,
                             const DirectX::XMFLOAT3& up = {0.0f, 1.0f, 0.0f}) = 0;
};
