#include "internal/SoundFormatUtils.h"
#include "internal/SoundManagerInternal.h"
#include "core/PathUtils.h"
#include "sound/AudioLimits.h"
#include "sound/SoundManager.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <limits>
#include <utility>

namespace {

using SoundFormatUtils::BuildPcmWaveFormat;

} // namespace

uint32_t SoundManager::Load(const std::wstring& path) {
    return LoadOrCreateSilent(path);
}

bool SoundManager::TryLoad(const std::wstring& path, uint32_t& soundId) {
    soundId = kInvalidSoundId;

    const std::filesystem::path resolvedPath = PathUtils::ResolveAssetPath(path);
    std::error_code ec;
    if (!std::filesystem::exists(resolvedPath, ec)) {
        return false;
    }

    const std::wstring key = PathUtils::NormalizePathKey(resolvedPath);
    const auto cached = state_->pathToSoundId.find(key);
    if (cached != state_->pathToSoundId.end()) {
        soundId = cached->second;
        return true;
    }

    AudioFileLoader::SoundData data{};
    if (!AudioFileLoader::TryLoad(resolvedPath.wstring(), data)) {
        return false;
    }

    SoundResource resource{};
    resource.data = std::move(data);
    soundId = AppendSoundResource(std::move(resource));
    if (soundId == kInvalidSoundId) {
        return false;
    }
    state_->pathToSoundId[key] = soundId;
    return true;
}

uint32_t SoundManager::LoadOrCreateSilent(const std::wstring& path) {
    const std::filesystem::path resolvedPath = PathUtils::ResolveAssetPath(path);
    const std::wstring key = PathUtils::NormalizePathKey(resolvedPath);
    const auto cached = state_->pathToSoundId.find(key);
    if (cached != state_->pathToSoundId.end()) {
        return cached->second;
    }

    uint32_t soundId = kInvalidSoundId;
    if (TryLoad(path, soundId)) {
        return soundId;
    }

    return CreateSilentSound(key);
}

uint32_t SoundManager::CreatePcm16Sound(const std::wstring& cacheKey,
                                        const std::vector<int16_t>& pcmSamples, uint32_t sampleRate,
                                        uint16_t channels) {
    const std::wstring key = PathUtils::NormalizeKey(L"procedural:" + cacheKey);
    const auto cached = state_->pathToSoundId.find(key);
    if (cached != state_->pathToSoundId.end()) {
        return cached->second;
    }

    if (pcmSamples.empty() || sampleRate == 0u || channels == 0u ||
        (pcmSamples.size() % channels) != 0u) {
        return CreateSilentSound(key);
    }

    WAVEFORMATEX format{};
    if (!BuildPcmWaveFormat(sampleRate, channels, 16, format)) {
        return CreateSilentSound(key);
    }
    if (pcmSamples.size() > (std::numeric_limits<size_t>::max)() / sizeof(int16_t) ||
        pcmSamples.size() > AudioLimits::kMaxDecodedPcmBytes / sizeof(int16_t)) {
        return CreateSilentSound(key);
    }

    SoundResource resource{};
    resource.data.waveFormat.resize(sizeof(WAVEFORMATEX));
    std::memcpy(resource.data.waveFormat.data(), &format, sizeof(format));
    resource.data.decodedPcm.resize(pcmSamples.size() * sizeof(int16_t));
    std::memcpy(resource.data.decodedPcm.data(), pcmSamples.data(),
                resource.data.decodedPcm.size());
    resource.data.info.sampleRate = sampleRate;
    resource.data.info.channels = channels;
    resource.data.info.bitsPerSample = 16;
    resource.data.info.durationSeconds =
        static_cast<float>(pcmSamples.size() / channels) / static_cast<float>(sampleRate);
    resource.data.info.decodedBytes = resource.data.decodedPcm.size();

    const uint32_t soundId = AppendSoundResource(std::move(resource));
    if (soundId == kInvalidSoundId) {
        return soundId;
    }
    state_->pathToSoundId[key] = soundId;
    return soundId;
}

uint32_t SoundManager::FindPcm16Sound(const std::wstring& cacheKey) const {
    const std::wstring key = PathUtils::NormalizeKey(L"procedural:" + cacheKey);
    const auto cached = state_->pathToSoundId.find(key);
    return cached != state_->pathToSoundId.end() ? cached->second : kInvalidSoundId;
}

bool SoundManager::RemoveSound(uint32_t soundId) {
    if (soundId == kInvalidSoundId || soundId >= state_->sounds.size()) {
        return false;
    }

    for (auto it = state_->playingVoices.begin(); it != state_->playingVoices.end();) {
        if (it->soundId == soundId) {
            DestroyVoice(*it);
            it = state_->playingVoices.erase(it);
            continue;
        }
        ++it;
    }

    for (auto it = state_->pathToSoundId.begin(); it != state_->pathToSoundId.end();) {
        if (it->second == soundId) {
            it = state_->pathToSoundId.erase(it);
            continue;
        }
        ++it;
    }

    state_->sounds[soundId] = SoundResource{};
    return true;
}

uint32_t SoundManager::CreateSilentSound(const std::wstring& cacheKey, uint32_t sampleRate,
                                         uint16_t channels, uint16_t bitsPerSample,
                                         float durationSeconds) {
    const auto cached = state_->pathToSoundId.find(cacheKey);
    if (cached != state_->pathToSoundId.end()) {
        return cached->second;
    }

    if (sampleRate == 0 || channels == 0 || bitsPerSample == 0) {
        sampleRate = 48000;
        channels = 1;
        bitsPerSample = 16;
    }
    WAVEFORMATEX format{};
    if (!BuildPcmWaveFormat(sampleRate, channels, bitsPerSample, format)) {
        sampleRate = 48000;
        channels = 1;
        bitsPerSample = 16;
        const bool defaultFormatOk =
            BuildPcmWaveFormat(sampleRate, channels, bitsPerSample, format);
        if (!defaultFormatOk) {
            return kInvalidSoundId;
        }
    }

    const float safeDuration =
        std::isfinite(durationSeconds) ? std::clamp(durationSeconds, 0.01f, 10.0f) : 0.01f;
    const double decodedBytesDouble =
        static_cast<double>(safeDuration) * static_cast<double>(format.nAvgBytesPerSec);
    if (decodedBytesDouble > static_cast<double>((std::numeric_limits<size_t>::max)())) {
        return kInvalidSoundId;
    }
    const size_t decodedBytes = static_cast<size_t>(decodedBytesDouble);

    SoundResource resource{};
    resource.data.waveFormat.resize(sizeof(WAVEFORMATEX));
    std::memcpy(resource.data.waveFormat.data(), &format, sizeof(format));
    resource.data.decodedPcm.assign(decodedBytes, 0);
    resource.data.info.sampleRate = sampleRate;
    resource.data.info.channels = channels;
    resource.data.info.bitsPerSample = bitsPerSample;
    resource.data.info.durationSeconds =
        static_cast<float>(decodedBytes) / static_cast<float>(format.nAvgBytesPerSec);
    resource.data.info.decodedBytes = decodedBytes;

    const uint32_t soundId = AppendSoundResource(std::move(resource));
    if (soundId == kInvalidSoundId) {
        return soundId;
    }
    state_->pathToSoundId[cacheKey] = soundId;
    return soundId;
}

uint32_t SoundManager::AppendSoundResource(SoundResource resource) {
    if (state_->sounds.size() >= static_cast<size_t>((std::numeric_limits<uint32_t>::max)())) {
        return kInvalidSoundId;
    }
    state_->sounds.push_back(std::move(resource));
    return static_cast<uint32_t>(state_->sounds.size() - 1);
}
