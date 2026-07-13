#pragma once

#include "sound/ISoundService.h"

class NullSoundService final : public ISoundService {
public:
    uint32_t CreatePcm16Sound(const std::wstring&, const std::vector<int16_t>&, uint32_t = 48000,
                              uint16_t = 1) override {
        return kInvalidSoundId;
    }

    uint32_t FindPcm16Sound(const std::wstring&) const override {
        return kInvalidSoundId;
    }

    uint32_t Play(uint32_t, float = 1.0f, bool = false) override {
        return kInvalidVoiceHandle;
    }

    uint32_t PlayFrom(uint32_t, float, float = 1.0f, bool = false) override {
        return kInvalidVoiceHandle;
    }

    uint32_t Play3D(uint32_t, const DirectX::XMFLOAT3&, float = 1.0f, bool = false) override {
        return kInvalidVoiceHandle;
    }

    void Stop(uint32_t) override {}
    void StopAll() override {}
    bool IsPlaying(uint32_t) const override {
        return false;
    }
    const SoundInfo* GetInfo(uint32_t) const override {
        return nullptr;
    }
    void SetVoiceVolume(uint32_t, float) override {}
    void SetVoiceFrequencyRatio(uint32_t, float) override {}
    void SetVoicePosition(uint32_t, const DirectX::XMFLOAT3&) override {}
    void SetVoice3DRange(uint32_t, float, float) override {}
    void SetListener(const DirectX::XMFLOAT3&, const DirectX::XMFLOAT3&,
                     const DirectX::XMFLOAT3& = {0.0f, 1.0f, 0.0f}) override {}
};
