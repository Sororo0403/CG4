#include "sound/SoundManager.h"
#include "core/Numeric.h"
#include "core/PathUtils.h"
#include "sound/AudioLimits.h"
#include "SoundFormatUtils.h"

#include <Objbase.h>
#include <algorithm>
#include <cwctype>
#include <cstring>
#include <cmath>
#include <filesystem>
#include <mfapi.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#include <utility>

using namespace DirectX;

namespace {

constexpr UINT32 kStreamQueuedBuffers = 3;

using SoundFormatUtils::BuildPcmWaveFormat;
using SoundFormatUtils::IsSupportedPcmReadFormat;
using SoundFormatUtils::MakeHResultMessage;

} // namespace

SoundManager &SoundManager::GetInstance() {
    static SoundManager instance;
    return instance;
}

SoundManager::~SoundManager() {
    Finalize();
}

void SoundManager::Finalize() {
    StopAll();
    sounds_.clear();
    pathToSoundId_.clear();

    if (masterVoice_) {
        masterVoice_->DestroyVoice();
        masterVoice_ = nullptr;
    }
    xAudio2_.Reset();

    if (mediaFoundationStarted_) {
        MFShutdown();
        mediaFoundationStarted_ = false;
    }
    if (comInitialized_) {
        CoUninitialize();
        comInitialized_ = false;
    }
    lastInitializeError_.clear();
    nextVoiceHandle_ = 1;
    listenerPosition_ = {0.0f, 0.0f, 0.0f};
    listenerForward_ = {0.0f, 0.0f, 1.0f};
    listenerUp_ = {0.0f, 1.0f, 0.0f};
}

void SoundManager::Initialize() {
    if (xAudio2_ && masterVoice_) {
        lastInitializeError_.clear();
        return;
    }
    if (xAudio2_ || masterVoice_ || mediaFoundationStarted_ ||
        comInitialized_) {
        Finalize();
    }
    lastInitializeError_.clear();

    const HRESULT coResult = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    if (SUCCEEDED(coResult)) {
        comInitialized_ = true;
    } else if (coResult != RPC_E_CHANGED_MODE) {
        lastInitializeError_ = MakeHResultMessage(coResult, "CoInitializeEx failed");
        return;
    }

    const HRESULT mfResult = MFStartup(MF_VERSION);
    if (FAILED(mfResult)) {
        lastInitializeError_ = MakeHResultMessage(mfResult, "MFStartup failed");
        if (comInitialized_) {
            CoUninitialize();
            comInitialized_ = false;
        }
        return;
    }
    mediaFoundationStarted_ = true;

    HRESULT audioResult = XAudio2Create(&xAudio2_, 0);
    if (SUCCEEDED(audioResult)) {
        audioResult = xAudio2_->CreateMasteringVoice(&masterVoice_);
    }
    if (FAILED(audioResult)) {
        lastInitializeError_ =
            MakeHResultMessage(audioResult, "XAudio2 initialization failed");
        if (masterVoice_) {
            masterVoice_->DestroyVoice();
            masterVoice_ = nullptr;
        }
        xAudio2_.Reset();
        MFShutdown();
        mediaFoundationStarted_ = false;
        if (comInitialized_) {
            CoUninitialize();
            comInitialized_ = false;
        }
        return;
    }

    SetMasterVolume(masterVolume_);
    lastInitializeError_.clear();
}
uint32_t SoundManager::Load(const std::wstring &path) {
    return LoadOrCreateSilent(path);
}

bool SoundManager::TryLoad(const std::wstring &path, uint32_t &soundId) {
    soundId = kInvalidSoundId;

    const std::filesystem::path resolvedPath = PathUtils::ResolveAssetPath(path);
    std::error_code ec;
    if (!std::filesystem::exists(resolvedPath, ec)) {
        return false;
    }

    const std::wstring key = PathUtils::NormalizePathKey(resolvedPath);
    const auto cached = pathToSoundId_.find(key);
    if (cached != pathToSoundId_.end()) {
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
    try {
        pathToSoundId_[key] = soundId;
    } catch (...) {
        return true;
    }
    return true;
}

uint32_t SoundManager::LoadOrCreateSilent(const std::wstring &path) {
    const std::filesystem::path resolvedPath = PathUtils::ResolveAssetPath(path);
    const std::wstring key = PathUtils::NormalizePathKey(resolvedPath);
    const auto cached = pathToSoundId_.find(key);
    if (cached != pathToSoundId_.end()) {
        return cached->second;
    }

    uint32_t soundId = kInvalidSoundId;
    if (TryLoad(path, soundId)) {
        return soundId;
    }

    return CreateSilentSound(key);
}

uint32_t SoundManager::CreatePcm16Sound(const std::wstring &cacheKey,
                                        const std::vector<int16_t> &pcmSamples,
                                        uint32_t sampleRate,
                                        uint16_t channels) {
    const std::wstring key = PathUtils::NormalizeKey(L"procedural:" + cacheKey);
    const auto cached = pathToSoundId_.find(key);
    if (cached != pathToSoundId_.end()) {
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
    if (pcmSamples.size() >
            (std::numeric_limits<size_t>::max)() / sizeof(int16_t) ||
        pcmSamples.size() >
            AudioLimits::kMaxDecodedPcmBytes / sizeof(int16_t)) {
        return CreateSilentSound(key);
    }

    SoundResource resource{};
    try {
        resource.data.waveFormat.resize(sizeof(WAVEFORMATEX));
        std::memcpy(resource.data.waveFormat.data(), &format, sizeof(format));
        resource.data.decodedPcm.resize(pcmSamples.size() * sizeof(int16_t));
        std::memcpy(resource.data.decodedPcm.data(), pcmSamples.data(),
                    resource.data.decodedPcm.size());
    } catch (...) {
        return kInvalidSoundId;
    }
    resource.data.info.sampleRate = sampleRate;
    resource.data.info.channels = channels;
    resource.data.info.bitsPerSample = 16;
    resource.data.info.durationSeconds =
        static_cast<float>(pcmSamples.size() / channels) /
        static_cast<float>(sampleRate);
    resource.data.info.decodedBytes = resource.data.decodedPcm.size();

    const uint32_t soundId = AppendSoundResource(std::move(resource));
    if (soundId == kInvalidSoundId) {
        return soundId;
    }
    try {
        pathToSoundId_[key] = soundId;
    } catch (...) {
        return soundId;
    }
    return soundId;
}
uint32_t SoundManager::Play(uint32_t soundId, float volume, bool loop) {
    Update();
    if (soundId >= sounds_.size() || !xAudio2_) {
        return kInvalidVoiceHandle;
    }

    return CreateSourceVoice(soundId, volume, loop);
}

uint32_t SoundManager::PlayFrom(uint32_t soundId, float startSeconds,
                                float volume, bool loop) {
    Update();
    if (soundId >= sounds_.size() || !xAudio2_) {
        return kInvalidVoiceHandle;
    }

    return CreateSourceVoice(soundId, volume, loop, startSeconds);
}

void SoundManager::Stop(uint32_t voiceHandle) {
    for (auto it = playingVoices_.begin(); it != playingVoices_.end(); ++it) {
        if (it->handle == voiceHandle) {
            DestroyVoice(*it);
            playingVoices_.erase(it);
            return;
        }
    }
}

void SoundManager::Pause(uint32_t voiceHandle) {
    for (PlayingVoice &playingVoice : playingVoices_) {
        if (playingVoice.handle == voiceHandle && playingVoice.voice) {
            playingVoice.voice->Stop(0);
            return;
        }
    }
}

void SoundManager::Resume(uint32_t voiceHandle) {
    for (PlayingVoice &playingVoice : playingVoices_) {
        if (playingVoice.handle == voiceHandle && playingVoice.voice) {
            playingVoice.voice->Start(0);
            return;
        }
    }
}

void SoundManager::SetVoiceVolume(uint32_t voiceHandle, float volume) {
    const float clampedVolume =
        Numeric::ClampFinite(volume, 0.0f, 1.0f, 0.0f);
    for (PlayingVoice &playingVoice : playingVoices_) {
        if (playingVoice.handle == voiceHandle && playingVoice.voice) {
            playingVoice.volume = clampedVolume;
            playingVoice.voice->SetVolume(clampedVolume);
            return;
        }
    }
}

void SoundManager::SetVoiceFrequencyRatio(uint32_t voiceHandle,
                                          float frequencyRatio) {
    const float clampedRatio =
        Numeric::ClampFinite(frequencyRatio, XAUDIO2_MIN_FREQ_RATIO,
                             XAUDIO2_MAX_FREQ_RATIO,
                             XAUDIO2_DEFAULT_FREQ_RATIO);
    for (PlayingVoice &playingVoice : playingVoices_) {
        if (playingVoice.handle == voiceHandle && playingVoice.voice) {
            playingVoice.frequencyRatio = clampedRatio;
            playingVoice.voice->SetFrequencyRatio(clampedRatio);
            return;
        }
    }
}

float SoundManager::GetVoiceFrequencyRatio(uint32_t voiceHandle) const {
    for (const PlayingVoice &playingVoice : playingVoices_) {
        if (playingVoice.handle == voiceHandle) {
            return playingVoice.frequencyRatio;
        }
    }
    return XAUDIO2_DEFAULT_FREQ_RATIO;
}

float SoundManager::GetVoiceVolume(uint32_t voiceHandle) const {
    for (const PlayingVoice &playingVoice : playingVoices_) {
        if (playingVoice.handle == voiceHandle) {
            return playingVoice.volume;
        }
    }

    return 0.0f;
}

float SoundManager::GetPlaybackPosition(uint32_t voiceHandle) const {
    for (const PlayingVoice &playingVoice : playingVoices_) {
        if (playingVoice.handle != voiceHandle || !playingVoice.voice) {
            continue;
        }

        XAUDIO2_VOICE_STATE state{};
        playingVoice.voice->GetState(&state);
        const WAVEFORMATEX *format = nullptr;
        uint64_t totalFrames = 0;
        if (playingVoice.isStreaming) {
            if (playingVoice.streamWaveFormat.size() >=
                sizeof(WAVEFORMATEX)) {
                format = reinterpret_cast<const WAVEFORMATEX *>(
                    playingVoice.streamWaveFormat.data());
            }
        } else if (playingVoice.soundId < sounds_.size()) {
            const AudioFileLoader::SoundData &sound =
                sounds_[playingVoice.soundId].data;
            format = sound.GetFormat();
            if (format && format->nBlockAlign != 0) {
                totalFrames =
                    sound.decodedPcm.size() /
                    static_cast<uint64_t>(format->nBlockAlign);
            }
        }
        if (!format || format->nSamplesPerSec == 0) {
            return 0.0f;
        }

        uint64_t playbackFrame = state.SamplesPlayed;
        if (!playingVoice.isStreaming) {
            const uint64_t startFrame = playingVoice.startFrame;
            if (playingVoice.loop && totalFrames > startFrame) {
                const uint64_t loopLength = totalFrames - startFrame;
                playbackFrame = startFrame + (playbackFrame % loopLength);
            } else if (playbackFrame >
                       (std::numeric_limits<uint64_t>::max)() -
                           startFrame) {
                playbackFrame = (std::numeric_limits<uint64_t>::max)();
            } else {
                playbackFrame += startFrame;
            }

            if (totalFrames > 0) {
                playbackFrame = (std::min)(playbackFrame, totalFrames);
            }
        }

        return static_cast<float>(playbackFrame) /
               static_cast<float>(format->nSamplesPerSec);
    }

    return 0.0f;
}

bool SoundManager::IsStreaming(uint32_t voiceHandle) const {
    for (const PlayingVoice &playingVoice : playingVoices_) {
        if (playingVoice.handle == voiceHandle) {
            return playingVoice.isStreaming;
        }
    }
    return false;
}

void SoundManager::StopAll() {
    for (PlayingVoice &playingVoice : playingVoices_) {
        DestroyVoice(playingVoice);
    }
    playingVoices_.clear();
}

void SoundManager::Update() {
    for (auto it = playingVoices_.begin(); it != playingVoices_.end();) {
        if (it->isStreaming && it->voice) {
            ReleaseFinishedStreamBuffers(*it);

            XAUDIO2_VOICE_STATE state{};
            it->voice->GetState(&state, XAUDIO2_VOICE_NOSAMPLESPLAYED);
            while (!it->streamSourceEnded &&
                   state.BuffersQueued < kStreamQueuedBuffers) {
                if (!SubmitNextStreamBuffer(*it)) {
                    break;
                }
                it->voice->GetState(&state, XAUDIO2_VOICE_NOSAMPLESPLAYED);
            }
        }

        if (IsVoiceActive(*it)) {
            ++it;
            continue;
        }

        DestroyVoice(*it);
        it = playingVoices_.erase(it);
    }
}

bool SoundManager::IsPlaying(uint32_t voiceHandle) const {
    for (const PlayingVoice &playingVoice : playingVoices_) {
        if (playingVoice.handle == voiceHandle) {
            return IsVoiceActive(playingVoice);
        }
    }

    return false;
}

const SoundManager::SoundInfo *SoundManager::GetInfo(uint32_t soundId) const {
    if (soundId >= sounds_.size()) {
        return nullptr;
    }

    return &sounds_[soundId].data.info;
}
void SoundManager::SetMasterVolume(float volume) {
    masterVolume_ = Numeric::ClampFinite(volume, 0.0f, 1.0f, 0.0f);
    if (masterVoice_) {
        masterVoice_->SetVolume(masterVolume_);
    }
}
uint32_t SoundManager::CreateSourceVoice(uint32_t soundId, float volume,
                                         bool loop, float startSeconds) {
    const AudioFileLoader::SoundData &sound = sounds_[soundId].data;
    const WAVEFORMATEX *format = sound.GetFormat();
    if (!format || format->nSamplesPerSec == 0 || format->nBlockAlign == 0 ||
        sound.decodedPcm.empty()) {
        return kInvalidVoiceHandle;
    }
    if (sound.decodedPcm.size() >
        (std::numeric_limits<UINT32>::max)()) {
        return kInvalidVoiceHandle;
    }

    IXAudio2SourceVoice *voice = nullptr;
    auto callback = std::make_unique<SoundVoiceCallback>();
    HRESULT hr = xAudio2_->CreateSourceVoice(
        &voice, format, 0, XAUDIO2_DEFAULT_FREQ_RATIO,
        callback.get());
    if (FAILED(hr)) {
        return kInvalidVoiceHandle;
    }

    const size_t totalFramesSize =
        sound.decodedPcm.size() / static_cast<size_t>(format->nBlockAlign);
    if (totalFramesSize == 0 ||
        totalFramesSize > (std::numeric_limits<UINT32>::max)()) {
        voice->DestroyVoice();
        return kInvalidVoiceHandle;
    }
    const UINT32 totalFrames = static_cast<UINT32>(totalFramesSize);
    if (totalFrames == 0u) {
        voice->DestroyVoice();
        return kInvalidVoiceHandle;
    }
    const float safeVolume = Numeric::ClampFinite(volume, 0.0f, 1.0f, 0.0f);
    const float clampedStartSeconds =
        std::isfinite(startSeconds) ? (std::max)(startSeconds, 0.0f) : 0.0f;
    const double requestedStartFrame =
        static_cast<double>(clampedStartSeconds) *
        static_cast<double>(format->nSamplesPerSec);
    const UINT32 startFrame =
        !std::isfinite(requestedStartFrame) ||
                requestedStartFrame >= static_cast<double>(totalFrames)
            ? totalFrames - 1u
            : static_cast<UINT32>(requestedStartFrame);

    XAUDIO2_BUFFER buffer{};
    buffer.pAudioData = sound.decodedPcm.data();
    if (sound.decodedPcm.size() >
        static_cast<size_t>((std::numeric_limits<UINT32>::max)())) {
        voice->DestroyVoice();
        return kInvalidVoiceHandle;
    }
    buffer.AudioBytes = static_cast<UINT32>(sound.decodedPcm.size());
    buffer.Flags = XAUDIO2_END_OF_STREAM;
    buffer.PlayBegin = startFrame;
    buffer.PlayLength = totalFrames - startFrame;
    if (loop) {
        buffer.LoopBegin = startFrame;
        buffer.LoopLength = totalFrames - startFrame;
        buffer.LoopCount = XAUDIO2_LOOP_INFINITE;
    }

    hr = voice->SubmitSourceBuffer(&buffer);
    if (FAILED(hr)) {
        voice->DestroyVoice();
        return kInvalidVoiceHandle;
    }

    if (nextVoiceHandle_ == kInvalidVoiceHandle) {
        nextVoiceHandle_ = 1;
    }

    PlayingVoice playingVoice{};
    playingVoice.voice = voice;
    playingVoice.callback = std::move(callback);
    playingVoice.handle = AllocateVoiceHandle();
    if (playingVoice.handle == kInvalidVoiceHandle) {
        voice->DestroyVoice();
        return kInvalidVoiceHandle;
    }
    playingVoice.soundId = soundId;
    playingVoice.startFrame = startFrame;
    playingVoice.volume = safeVolume;
    playingVoice.loop = loop;
    try {
        playingVoices_.push_back(std::move(playingVoice));
    } catch (...) {
        voice->DestroyVoice();
        return kInvalidVoiceHandle;
    }

    PlayingVoice &storedVoice = playingVoices_.back();
    const uint32_t handle = storedVoice.handle;
    storedVoice.voice->SetVolume(safeVolume);

    hr = storedVoice.voice->Start();
    if (FAILED(hr)) {
        DestroyVoice(storedVoice);
        playingVoices_.pop_back();
        return kInvalidVoiceHandle;
    }

    return handle;
}

uint32_t SoundManager::CreateSilentSound(const std::wstring &cacheKey,
                                         uint32_t sampleRate,
                                         uint16_t channels,
                                         uint16_t bitsPerSample,
                                         float durationSeconds) {
    const auto cached = pathToSoundId_.find(cacheKey);
    if (cached != pathToSoundId_.end()) {
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
        std::isfinite(durationSeconds)
            ? std::clamp(durationSeconds, 0.01f, 10.0f)
            : 0.01f;
    const double decodedBytesDouble =
        static_cast<double>(safeDuration) *
        static_cast<double>(format.nAvgBytesPerSec);
    if (decodedBytesDouble >
        static_cast<double>((std::numeric_limits<size_t>::max)())) {
        return kInvalidSoundId;
    }
    const size_t decodedBytes = static_cast<size_t>(decodedBytesDouble);

    SoundResource resource{};
    try {
        resource.data.waveFormat.resize(sizeof(WAVEFORMATEX));
        std::memcpy(resource.data.waveFormat.data(), &format, sizeof(format));
        resource.data.decodedPcm.assign(decodedBytes, 0);
    } catch (...) {
        return kInvalidSoundId;
    }
    resource.data.info.sampleRate = sampleRate;
    resource.data.info.channels = channels;
    resource.data.info.bitsPerSample = bitsPerSample;
    resource.data.info.durationSeconds =
        static_cast<float>(decodedBytes) /
        static_cast<float>(format.nAvgBytesPerSec);
    resource.data.info.decodedBytes = decodedBytes;

    const uint32_t soundId = AppendSoundResource(std::move(resource));
    if (soundId == kInvalidSoundId) {
        return soundId;
    }
    try {
        pathToSoundId_[cacheKey] = soundId;
    } catch (...) {
        return soundId;
    }

    return soundId;
}

uint32_t SoundManager::AppendSoundResource(SoundResource resource) {
    if (sounds_.size() >=
        static_cast<size_t>((std::numeric_limits<uint32_t>::max)())) {
        return kInvalidSoundId;
    }
    try {
        sounds_.push_back(std::move(resource));
    } catch (...) {
        return kInvalidSoundId;
    }
    return static_cast<uint32_t>(sounds_.size() - 1);
}

uint32_t SoundManager::AllocateVoiceHandle() {
    if (playingVoices_.size() >=
        static_cast<size_t>((std::numeric_limits<uint32_t>::max)()) - 1u) {
        return kInvalidVoiceHandle;
    }

    for (;;) {
        if (nextVoiceHandle_ == kInvalidVoiceHandle) {
            nextVoiceHandle_ = 1;
        }
        const uint32_t candidate = nextVoiceHandle_++;
        const auto it = std::find_if(
            playingVoices_.begin(), playingVoices_.end(),
            [candidate](const PlayingVoice &voice) {
                return voice.handle == candidate;
            });
        if (it == playingVoices_.end()) {
            return candidate;
        }
    }
}

void SoundManager::DestroyVoice(PlayingVoice &playingVoice) {
    if (!playingVoice.voice) {
        return;
    }

    playingVoice.voice->Stop(0);
    playingVoice.voice->FlushSourceBuffers();
    playingVoice.voice->DestroyVoice();
    playingVoice.voice = nullptr;
    playingVoice.streamBuffers.clear();
    playingVoice.streamReader.Reset();
}

bool SoundManager::IsVoiceActive(const PlayingVoice &playingVoice) const {
    if (!playingVoice.voice) {
        return false;
    }
    if (playingVoice.isStreaming) {
        XAUDIO2_VOICE_STATE state{};
        playingVoice.voice->GetState(&state, XAUDIO2_VOICE_NOSAMPLESPLAYED);
        return !playingVoice.streamSourceEnded || state.BuffersQueued > 0;
    }
    if (playingVoice.loop) {
        return true;
    }
    if (playingVoice.callback && playingVoice.callback->IsEnded()) {
        return false;
    }

    XAUDIO2_VOICE_STATE state{};
    playingVoice.voice->GetState(&state, XAUDIO2_VOICE_NOSAMPLESPLAYED);
    return state.BuffersQueued > 0;
}
