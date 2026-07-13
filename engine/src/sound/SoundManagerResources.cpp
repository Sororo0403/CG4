#include "core/PathUtils.h"
#include "internal/SoundFormatUtils.h"
#include "internal/SoundManagerInternal.h"
#include "sound/AudioLimits.h"
#include "sound/SoundManager.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <exception>
#include <filesystem>
#include <limits>
#include <new>
#include <utility>

namespace {

using SoundFormatUtils::BuildPcmWaveFormat;

} // namespace

uint32_t SoundManager::Load(const std::wstring& path) {
    return LoadOrCreateSilent(path);
}

bool SoundManager::TryLoad(const std::wstring& path, uint32_t& soundId) {
    soundId = kInvalidSoundId;

    std::filesystem::path resolvedPath;
    try {
        resolvedPath = PathUtils::ResolveAssetPath(path);
    } catch (const std::exception&) {
        return false;
    }
    std::error_code ec;
    try {
        if (!std::filesystem::exists(resolvedPath, ec)) {
            return false;
        }
    } catch (const std::exception&) {
        return false;
    }

    std::wstring key;
    try {
        key = PathUtils::NormalizePathKey(resolvedPath);
    } catch (const std::exception&) {
        return false;
    }
    const auto cached = state_->pathToSoundId.find(key);
    if (cached != state_->pathToSoundId.end()) {
        soundId = cached->second;
        return true;
    }

    std::wstring resolvedPathText;
    try {
        resolvedPathText = resolvedPath.wstring();
    } catch (const std::exception&) {
        return false;
    }

    AudioFileLoader::SoundData data{};
    if (!AudioFileLoader::TryLoad(resolvedPathText, data)) {
        return false;
    }

    SoundResource resource{};
    resource.data = std::move(data);
    soundId = AppendSoundResource(std::move(resource));
    if (soundId == kInvalidSoundId) {
        return false;
    }
    try {
        state_->pathToSoundId[key] = soundId;
    } catch (const std::exception&) {
        RemoveSound(soundId);
        soundId = kInvalidSoundId;
        return false;
    }
    return true;
}

uint32_t SoundManager::LoadOrCreateSilent(const std::wstring& path) {
    std::wstring key;
    try {
        const std::filesystem::path resolvedPath = PathUtils::ResolveAssetPath(path);
        key = PathUtils::NormalizePathKey(resolvedPath);
    } catch (const std::exception&) {
        key = L"silent:fallback";
    }
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
    std::wstring key;
    try {
        key = PathUtils::NormalizeKey(L"procedural:" + cacheKey);
    } catch (const std::exception&) {
        return CreateSilentSound(L"procedural:fallback");
    }
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
    try {
        resource.data.waveFormat.resize(sizeof(WAVEFORMATEX));
        resource.data.decodedPcm.resize(pcmSamples.size() * sizeof(int16_t));
    } catch (const std::exception&) {
        return CreateSilentSound(key);
    }
    std::memcpy(resource.data.waveFormat.data(), &format, sizeof(format));
    std::memcpy(resource.data.decodedPcm.data(), pcmSamples.data(),
                resource.data.decodedPcm.size());
    resource.data.info.sampleRate = sampleRate;
    resource.data.info.channels = channels;
    resource.data.info.bitsPerSample = 16;
    resource.data.info.durationSeconds =
        static_cast<float>(pcmSamples.size()) /
        (static_cast<float>(channels) * static_cast<float>(sampleRate));
    resource.data.info.decodedBytes = resource.data.decodedPcm.size();

    const uint32_t soundId = AppendSoundResource(std::move(resource));
    if (soundId == kInvalidSoundId) {
        return soundId;
    }
    try {
        state_->pathToSoundId[key] = soundId;
    } catch (const std::exception&) {
        RemoveSound(soundId);
        return kInvalidSoundId;
    }
    return soundId;
}

uint32_t SoundManager::FindPcm16Sound(const std::wstring& cacheKey) const {
    std::wstring key;
    try {
        key = PathUtils::NormalizeKey(L"procedural:" + cacheKey);
    } catch (const std::exception&) {
        return kInvalidSoundId;
    }
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
    try {
        resource.data.waveFormat.resize(sizeof(WAVEFORMATEX));
        resource.data.decodedPcm.assign(decodedBytes, 0);
    } catch (const std::exception&) {
        return kInvalidSoundId;
    }
    std::memcpy(resource.data.waveFormat.data(), &format, sizeof(format));
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
    try {
        state_->pathToSoundId[cacheKey] = soundId;
    } catch (const std::exception&) {
        RemoveSound(soundId);
        return kInvalidSoundId;
    }
    return soundId;
}

uint32_t SoundManager::AppendSoundResource(SoundResource resource) {
    if (state_->sounds.size() >= static_cast<size_t>((std::numeric_limits<uint32_t>::max)())) {
        return kInvalidSoundId;
    }
    try {
        state_->sounds.push_back(std::move(resource));
    } catch (const std::exception&) {
        return kInvalidSoundId;
    }
    return static_cast<uint32_t>(state_->sounds.size() - 1);
}
