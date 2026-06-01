#include "sound/SoundManager.h"
#include "core/AssetManager.h"

#include <Objbase.h>
#include <algorithm>
#include <cwctype>
#include <cstring>
#include <cmath>
#include <filesystem>
#include <mfapi.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#include <limits>
#include <sstream>
#include <utility>

using namespace DirectX;

namespace {

constexpr UINT32 kStreamQueuedBuffers = 3;

std::string MakeHResultMessage(HRESULT hr, const char *message) {
    std::ostringstream oss;
    oss << message << " HRESULT=0x" << std::hex << static_cast<unsigned long>(hr);
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

std::filesystem::path ResolveAudioPath(const std::wstring &path) {
    return AssetManager::ResolvePath(std::filesystem::path(path));
}

std::wstring NormalizePathKey(const std::filesystem::path &path) {
    std::wstring key = path.lexically_normal().wstring();
#ifdef _WIN32
    std::transform(key.begin(), key.end(), key.begin(),
                   [](wchar_t c) { return static_cast<wchar_t>(towlower(c)); });
#endif
    return key;
}

std::wstring NormalizeCacheKey(std::wstring key) {
#ifdef _WIN32
    std::transform(key.begin(), key.end(), key.begin(),
                   [](wchar_t c) { return static_cast<wchar_t>(towlower(c)); });
#endif
    return key;
}

float ClampFinite(float value, float minimum, float maximum, float fallback) {
    if (!std::isfinite(value)) {
        return fallback;
    }
    return std::clamp(value, minimum, maximum);
}

} // namespace

SoundManager &SoundManager::GetInstance() {
    static SoundManager instance;
    return instance;
}

SoundManager::~SoundManager() {
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
}

void SoundManager::Initialize() {
    if (xAudio2_) {
        lastInitializeError_.clear();
        return;
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

    const std::filesystem::path resolvedPath = ResolveAudioPath(path);
    std::error_code ec;
    if (!std::filesystem::exists(resolvedPath, ec)) {
        return false;
    }

    const std::wstring key = NormalizePathKey(resolvedPath);
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
    pathToSoundId_[key] = soundId;
    return true;
}

uint32_t SoundManager::LoadOrCreateSilent(const std::wstring &path) {
    const std::filesystem::path resolvedPath = ResolveAudioPath(path);
    const std::wstring key = NormalizePathKey(resolvedPath);
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
    const std::wstring key = NormalizeCacheKey(L"procedural:" + cacheKey);
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
        (std::numeric_limits<size_t>::max)() / sizeof(int16_t)) {
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
        static_cast<float>(pcmSamples.size() / channels) /
        static_cast<float>(sampleRate);
    resource.data.info.decodedBytes = resource.data.decodedPcm.size();

    const uint32_t soundId = AppendSoundResource(std::move(resource));
    if (soundId == kInvalidSoundId) {
        return soundId;
    }
    pathToSoundId_[key] = soundId;
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
    const float clampedVolume = ClampFinite(volume, 0.0f, 1.0f, 0.0f);
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
        ClampFinite(frequencyRatio, XAUDIO2_MIN_FREQ_RATIO,
                    XAUDIO2_MAX_FREQ_RATIO, XAUDIO2_DEFAULT_FREQ_RATIO);
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
        if (playingVoice.isStreaming) {
            if (playingVoice.streamWaveFormat.size() >=
                sizeof(WAVEFORMATEX)) {
                format = reinterpret_cast<const WAVEFORMATEX *>(
                    playingVoice.streamWaveFormat.data());
            }
        } else if (playingVoice.soundId < sounds_.size()) {
            format = sounds_[playingVoice.soundId].data.GetFormat();
        }
        if (!format || format->nSamplesPerSec == 0) {
            return 0.0f;
        }
        return static_cast<float>(state.SamplesPlayed) /
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

float SoundManager::GetAmplitudeAt(uint32_t soundId, float playbackSeconds,
                                   float windowSeconds) const {
    if (soundId >= sounds_.size()) {
        return 0.0f;
    }

    const AudioFileLoader::SoundData &sound = sounds_[soundId].data;
    const WAVEFORMATEX *format = sound.GetFormat();
    if (!format || !IsSupportedPcmReadFormat(*format) ||
        sound.decodedPcm.empty()) {
        return 0.0f;
    }

    const size_t frameCount =
        sound.decodedPcm.size() / static_cast<size_t>(format->nBlockAlign);
    if (frameCount == 0) {
        return 0.0f;
    }

    const float duration =
        static_cast<float>(frameCount) /
        static_cast<float>(format->nSamplesPerSec);
    if (!std::isfinite(duration) || duration <= 0.0f) {
        return 0.0f;
    }

    const float safePlaybackSeconds =
        std::isfinite(playbackSeconds) ? playbackSeconds : 0.0f;
    float sampleTime = std::fmod(safePlaybackSeconds, duration);
    if (!std::isfinite(sampleTime)) {
        return 0.0f;
    }
    if (sampleTime < 0.0f) {
        sampleTime += duration;
    }

    const float maxWindowSeconds = (std::max)(duration, 0.005f);
    const float safeWindowSeconds =
        ClampFinite(windowSeconds, 0.005f, maxWindowSeconds,
                    (std::min)(0.045f, maxWindowSeconds));
    const double halfWindowFramesDouble =
        static_cast<double>(safeWindowSeconds) *
        static_cast<double>(format->nSamplesPerSec) * 0.5;

    const size_t centerFrame =
        static_cast<size_t>(sampleTime * format->nSamplesPerSec) % frameCount;
    const size_t halfWindowFrames = (std::max<size_t>)(
        1, (std::min)(frameCount,
                      static_cast<size_t>(halfWindowFramesDouble)));
    const size_t sampleFrames =
        (std::min)(frameCount, halfWindowFrames * 2 + 1);
    const uint16_t channels = (std::max<uint16_t>)(format->nChannels, 1);
    const uint16_t bits = format->wBitsPerSample;
    const size_t bytesPerSample = static_cast<size_t>(bits) / 8u;

    double sumSquares = 0.0;
    size_t valueCount = 0;
    for (size_t i = 0; i < sampleFrames; ++i) {
        const size_t frame =
            (centerFrame + frameCount + i - halfWindowFrames) % frameCount;
        const BYTE *base =
            sound.decodedPcm.data() + frame * format->nBlockAlign;
        for (uint16_t ch = 0; ch < channels; ++ch) {
            const size_t byteOffset =
                static_cast<size_t>(ch) * bytesPerSample;
            if (byteOffset + bytesPerSample > format->nBlockAlign) {
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

    return std::clamp(static_cast<float>(std::sqrt(sumSquares / valueCount)),
                      0.0f, 1.0f);
}

void SoundManager::FillSpectrumBands(uint32_t soundId, float playbackSeconds,
                                     float *outBands,
                                     size_t bandCount) const {
    if (outBands == nullptr || bandCount == 0) {
        return;
    }
    std::fill(outBands, outBands + bandCount, 0.0f);

    if (soundId >= sounds_.size()) {
        return;
    }

    const AudioFileLoader::SoundData &sound = sounds_[soundId].data;
    const WAVEFORMATEX *format = sound.GetFormat();
    if (!format || !IsSupportedPcmReadFormat(*format) ||
        sound.decodedPcm.empty()) {
        return;
    }

    const size_t frameCount =
        sound.decodedPcm.size() / static_cast<size_t>(format->nBlockAlign);
    if (frameCount == 0) {
        return;
    }

    const float duration =
        static_cast<float>(frameCount) /
        static_cast<float>(format->nSamplesPerSec);
    if (!std::isfinite(duration) || duration <= 0.0f) {
        return;
    }

    const float safePlaybackSeconds =
        std::isfinite(playbackSeconds) ? playbackSeconds : 0.0f;
    float sampleTime = std::fmod(safePlaybackSeconds, duration);
    if (!std::isfinite(sampleTime)) {
        return;
    }
    if (sampleTime < 0.0f) {
        sampleTime += duration;
    }

    constexpr size_t kWindowFrames = 768;
    const size_t centerFrame =
        static_cast<size_t>(sampleTime * format->nSamplesPerSec) % frameCount;
    const uint16_t channels = (std::max<uint16_t>)(format->nChannels, 1);
    const uint16_t bits = format->wBitsPerSample;
    const size_t bytesPerSample = static_cast<size_t>(bits) / 8u;
    const float sampleRate = static_cast<float>(format->nSamplesPerSec);

    auto readFrame = [&](size_t frame) {
        const BYTE *base =
            sound.decodedPcm.data() + frame * format->nBlockAlign;
        float total = 0.0f;
        size_t count = 0;
        for (uint16_t ch = 0; ch < channels; ++ch) {
            const size_t byteOffset =
                static_cast<size_t>(ch) * bytesPerSample;
            if (byteOffset + bytesPerSample > format->nBlockAlign) {
                continue;
            }

            if (bits == 16) {
                int16_t sample = 0;
                std::memcpy(&sample, base + byteOffset, sizeof(sample));
                total += static_cast<float>(sample) / 32768.0f;
                ++count;
            } else if (bits == 8) {
                const uint8_t sample = *(base + byteOffset);
                total += (static_cast<float>(sample) - 128.0f) / 128.0f;
                ++count;
            }
        }
        return count > 0 ? total / static_cast<float>(count) : 0.0f;
    };

    for (size_t band = 0; band < bandCount; ++band) {
        const float t = bandCount > 1
                            ? static_cast<float>(band) /
                                  static_cast<float>(bandCount - 1)
                            : 0.0f;
        const float frequency =
            45.0f * std::pow(12000.0f / 45.0f, t);
        const float omega = 2.0f * 3.1415926535f * frequency / sampleRate;
        double real = 0.0;
        double imag = 0.0;

        for (size_t i = 0; i < kWindowFrames; ++i) {
            const size_t frame =
                (centerFrame + frameCount + i - kWindowFrames / 2) %
                frameCount;
            const float window =
                0.5f - 0.5f *
                           std::cos(2.0f * 3.1415926535f *
                                    static_cast<float>(i) /
                                    static_cast<float>(kWindowFrames - 1));
            const float sample = readFrame(frame) * window;
            const float phase = omega * static_cast<float>(i);
            real += static_cast<double>(sample * std::cos(phase));
            imag -= static_cast<double>(sample * std::sin(phase));
        }

        const float magnitude =
            static_cast<float>(std::sqrt(real * real + imag * imag)) /
            static_cast<float>(kWindowFrames);
        const float bassLift = 1.35f - 0.45f * t;
        const float bandValue =
            std::pow(magnitude * bassLift * 32.0f, 0.55f);
        outBands[band] =
            std::isfinite(bandValue)
                ? std::clamp(bandValue, 0.0f, 1.0f)
                : 0.0f;
    }
}

void SoundManager::SetMasterVolume(float volume) {
    masterVolume_ = ClampFinite(volume, 0.0f, 1.0f, 0.0f);
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
    const float safeVolume = ClampFinite(volume, 0.0f, 1.0f, 0.0f);
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

    voice->SetVolume(safeVolume);

    hr = voice->Start();
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
    playingVoice.volume = safeVolume;
    playingVoice.loop = loop;
    playingVoices_.push_back(std::move(playingVoice));

    return playingVoices_.back().handle;
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
    resource.data.waveFormat.resize(sizeof(WAVEFORMATEX));
    std::memcpy(resource.data.waveFormat.data(), &format, sizeof(format));
    resource.data.decodedPcm.assign(decodedBytes, 0);
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
    pathToSoundId_[cacheKey] = soundId;

    return soundId;
}

uint32_t SoundManager::AppendSoundResource(SoundResource resource) {
    if (sounds_.size() >=
        static_cast<size_t>((std::numeric_limits<uint32_t>::max)())) {
        return kInvalidSoundId;
    }
    sounds_.push_back(std::move(resource));
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
