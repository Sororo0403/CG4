#include "sound/SoundManager.h"
#include "core/AssetManager.h"
#include "debug/DebugLog.h"

#include <Objbase.h>
#include <algorithm>
#include <cwctype>
#include <cstring>
#include <cmath>
#include <filesystem>
#include <mfapi.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#include <sstream>
#include <stdexcept>
#include <utility>

using namespace DirectX;

namespace {

constexpr DWORD kAllStreams = static_cast<DWORD>(MF_SOURCE_READER_ALL_STREAMS);
constexpr DWORD kFirstAudioStream =
    static_cast<DWORD>(MF_SOURCE_READER_FIRST_AUDIO_STREAM);
constexpr UINT32 kStreamQueuedBuffers = 3;
constexpr size_t kStreamBufferBytes = 64 * 1024;

std::string MakeHResultMessage(HRESULT hr, const char *message) {
    std::ostringstream oss;
    oss << message << " HRESULT=0x" << std::hex << static_cast<unsigned long>(hr);
    return oss.str();
}

void ThrowIfFailed(HRESULT hr, const char *message) {
    if (FAILED(hr)) {
        throw std::runtime_error(MakeHResultMessage(hr, message));
    }
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

XMVECTOR LoadFloat3OrDefault(const XMFLOAT3 &value, FXMVECTOR fallback) {
    XMVECTOR v = XMLoadFloat3(&value);
    if (XMVectorGetX(XMVector3LengthSq(v)) <= 0.000001f) {
        return fallback;
    }
    return XMVector3Normalize(v);
}

class MediaBufferLock {
  public:
    explicit MediaBufferLock(IMFMediaBuffer *buffer) : buffer_(buffer) {
        ThrowIfFailed(buffer_->Lock(&data_, &maxLength_, &currentLength_),
                      "IMFMediaBuffer::Lock failed");
    }

    ~MediaBufferLock() {
        if (buffer_) {
            buffer_->Unlock();
        }
    }

    const BYTE *Data() const { return data_; }
    DWORD Size() const { return currentLength_; }

  private:
    IMFMediaBuffer *buffer_ = nullptr;
    BYTE *data_ = nullptr;
    DWORD maxLength_ = 0;
    DWORD currentLength_ = 0;
};

Microsoft::WRL::ComPtr<IMFSourceReader>
CreateStreamReader(const std::filesystem::path &path) {
    Microsoft::WRL::ComPtr<IMFSourceReader> reader;
    const std::string message =
        "MFCreateSourceReaderFromURL failed: " + path.string();
    ThrowIfFailed(MFCreateSourceReaderFromURL(path.c_str(), nullptr, &reader),
                  message.c_str());

    ThrowIfFailed(reader->SetStreamSelection(kAllStreams, FALSE),
                  "SetStreamSelection all streams failed");
    ThrowIfFailed(reader->SetStreamSelection(kFirstAudioStream, TRUE),
                  "SetStreamSelection first audio stream failed");
    return reader;
}

Microsoft::WRL::ComPtr<IMFMediaType>
SetStreamPcmFormat(IMFSourceReader *reader) {
    Microsoft::WRL::ComPtr<IMFMediaType> pcmType;
    ThrowIfFailed(MFCreateMediaType(&pcmType), "MFCreateMediaType failed");
    ThrowIfFailed(pcmType->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Audio),
                  "Set audio major type failed");
    ThrowIfFailed(pcmType->SetGUID(MF_MT_SUBTYPE, MFAudioFormat_PCM),
                  "Set PCM subtype failed");
    ThrowIfFailed(pcmType->SetUINT32(MF_MT_AUDIO_BITS_PER_SAMPLE, 16),
                  "Set PCM bits per sample failed");

    ThrowIfFailed(reader->SetCurrentMediaType(kFirstAudioStream, nullptr,
                                              pcmType.Get()),
                  "SetCurrentMediaType PCM failed");

    Microsoft::WRL::ComPtr<IMFMediaType> currentType;
    ThrowIfFailed(reader->GetCurrentMediaType(kFirstAudioStream, &currentType),
                  "GetCurrentMediaType failed");
    return currentType;
}

std::vector<BYTE> GetWaveFormat(IMFMediaType *mediaType) {
    WAVEFORMATEX *waveFormat = nullptr;
    UINT32 waveFormatSize = 0;
    ThrowIfFailed(MFCreateWaveFormatExFromMFMediaType(
                      mediaType, &waveFormat, &waveFormatSize),
                  "MFCreateWaveFormatExFromMFMediaType failed");

    std::vector<BYTE> result(waveFormatSize);
    std::copy_n(reinterpret_cast<const BYTE *>(waveFormat), waveFormatSize,
                result.data());
    CoTaskMemFree(waveFormat);
    return result;
}

bool SeekStreamToStart(IMFSourceReader *reader) {
    PROPVARIANT position;
    PropVariantInit(&position);
    position.vt = VT_I8;
    position.hVal.QuadPart = 0;
    const HRESULT hr =
        reader->SetCurrentPosition(GUID_NULL, position);
    PropVariantClear(&position);
    return SUCCEEDED(hr);
}

std::vector<BYTE> ReadNextStreamChunk(IMFSourceReader *reader,
                                      bool &sourceEnded) {
    std::vector<BYTE> decodedPcm;
    sourceEnded = false;

    while (decodedPcm.size() < kStreamBufferBytes) {
        DWORD flags = 0;
        Microsoft::WRL::ComPtr<IMFSample> sample;
        ThrowIfFailed(reader->ReadSample(kFirstAudioStream, 0, nullptr, &flags,
                                         nullptr, &sample),
                      "ReadSample failed");

        if ((flags & MF_SOURCE_READERF_CURRENTMEDIATYPECHANGED) != 0) {
            throw std::runtime_error(
                "Audio media type changed while streaming PCM data");
        }
        if ((flags & MF_SOURCE_READERF_ENDOFSTREAM) != 0) {
            sourceEnded = true;
            break;
        }
        if (!sample) {
            continue;
        }

        Microsoft::WRL::ComPtr<IMFMediaBuffer> mediaBuffer;
        ThrowIfFailed(sample->ConvertToContiguousBuffer(&mediaBuffer),
                      "ConvertToContiguousBuffer failed");

        const MediaBufferLock locked(mediaBuffer.Get());
        const size_t oldSize = decodedPcm.size();
        decodedPcm.resize(oldSize + locked.Size());
        std::copy_n(locked.Data(), locked.Size(), decodedPcm.data() + oldSize);
    }

    return decodedPcm;
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
        return;
    }

    const HRESULT coResult = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    if (SUCCEEDED(coResult)) {
        comInitialized_ = true;
    } else if (coResult != RPC_E_CHANGED_MODE) {
        ThrowIfFailed(coResult, "CoInitializeEx failed");
    }

    ThrowIfFailed(MFStartup(MF_VERSION), "MFStartup failed");
    mediaFoundationStarted_ = true;

    ThrowIfFailed(XAudio2Create(&xAudio2_, 0), "XAudio2Create failed");
    ThrowIfFailed(xAudio2_->CreateMasteringVoice(&masterVoice_),
                  "CreateMasteringVoice failed");

    SetMasterVolume(masterVolume_);
}

uint32_t SoundManager::Load(const std::wstring &path) {
    const std::filesystem::path resolvedPath = ResolveAudioPath(path);
    if (!std::filesystem::exists(resolvedPath)) {
        throw std::runtime_error("Audio file not found. requested=" +
                                 std::filesystem::path(path).string() +
                                 " resolved=" + resolvedPath.string());
    }

    const std::wstring key = NormalizePathKey(resolvedPath);
    const auto cached = pathToSoundId_.find(key);
    if (cached != pathToSoundId_.end()) {
        return cached->second;
    }

    SoundResource resource{};
    resource.data = AudioFileLoader::Load(resolvedPath.wstring());

    sounds_.push_back(std::move(resource));
    const uint32_t soundId = static_cast<uint32_t>(sounds_.size() - 1);
    pathToSoundId_[key] = soundId;

    return soundId;
}

bool SoundManager::TryLoad(const std::wstring &path, uint32_t &soundId) {
    try {
        soundId = Load(path);
        return true;
    } catch (const std::exception &e) {
        DebugLog::Get().Write("Sound", "SoundManager", "load_failed",
                              e.what());
    } catch (...) {
        DebugLog::Get().Write("Sound", "SoundManager", "load_failed",
                              "unknown error");
    }

    soundId = kInvalidSoundId;
    return false;
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
    format.wFormatTag = WAVE_FORMAT_PCM;
    format.nChannels = channels;
    format.nSamplesPerSec = sampleRate;
    format.wBitsPerSample = 16;
    format.nBlockAlign =
        static_cast<WORD>((format.nChannels * format.wBitsPerSample) / 8u);
    format.nAvgBytesPerSec = format.nSamplesPerSec * format.nBlockAlign;

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

    sounds_.push_back(std::move(resource));
    const uint32_t soundId = static_cast<uint32_t>(sounds_.size() - 1);
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

void SoundManager::Stop(uint32_t voiceHandle) {
    for (auto it = playingVoices_.begin(); it != playingVoices_.end(); ++it) {
        if (it->handle == voiceHandle) {
            DestroyVoice(*it);
            playingVoices_.erase(it);
            return;
        }
    }
}

void SoundManager::SetVoiceVolume(uint32_t voiceHandle, float volume) {
    const float clampedVolume = std::clamp(volume, 0.0f, 1.0f);
    for (PlayingVoice &playingVoice : playingVoices_) {
        if (playingVoice.handle == voiceHandle && playingVoice.voice) {
            playingVoice.volume = clampedVolume;
            playingVoice.voice->SetVolume(clampedVolume);
            return;
        }
    }
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
            format = reinterpret_cast<const WAVEFORMATEX *>(
                playingVoice.streamWaveFormat.data());
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
        if (it->isStreaming) {
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
    masterVolume_ = std::clamp(volume, 0.0f, 1.0f);
    if (masterVoice_) {
        masterVoice_->SetVolume(masterVolume_);
    }
}

uint32_t SoundManager::CreateSourceVoice(uint32_t soundId, float volume,
                                         bool loop) {
    const AudioFileLoader::SoundData &sound = sounds_[soundId].data;

    IXAudio2SourceVoice *voice = nullptr;
    auto callback = std::make_unique<SoundVoiceCallback>();
    HRESULT hr = xAudio2_->CreateSourceVoice(
        &voice, sound.GetFormat(), 0, XAUDIO2_DEFAULT_FREQ_RATIO,
        callback.get());
    if (FAILED(hr)) {
        return kInvalidVoiceHandle;
    }

    XAUDIO2_BUFFER buffer{};
    buffer.pAudioData = sound.decodedPcm.data();
    buffer.AudioBytes = static_cast<UINT32>(sound.decodedPcm.size());
    buffer.Flags = XAUDIO2_END_OF_STREAM;
    buffer.LoopCount = loop ? XAUDIO2_LOOP_INFINITE : 0;

    hr = voice->SubmitSourceBuffer(&buffer);
    if (FAILED(hr)) {
        voice->DestroyVoice();
        return kInvalidVoiceHandle;
    }

    voice->SetVolume(std::clamp(volume, 0.0f, 1.0f));

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
    playingVoice.handle = nextVoiceHandle_++;
    playingVoice.soundId = soundId;
    playingVoice.volume = std::clamp(volume, 0.0f, 1.0f);
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

    WAVEFORMATEX format{};
    format.wFormatTag = WAVE_FORMAT_PCM;
    format.nChannels = channels;
    format.nSamplesPerSec = sampleRate;
    format.wBitsPerSample = bitsPerSample;
    format.nBlockAlign =
        static_cast<WORD>((channels * bitsPerSample) / 8u);
    format.nAvgBytesPerSec = sampleRate * format.nBlockAlign;

    const size_t decodedBytes = static_cast<size_t>(
        (std::max)(durationSeconds, 0.01f) *
        static_cast<float>(format.nAvgBytesPerSec));

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

    sounds_.push_back(std::move(resource));
    const uint32_t soundId = static_cast<uint32_t>(sounds_.size() - 1);
    pathToSoundId_[cacheKey] = soundId;

    DebugLog::Get().Write("Sound", "SoundManager", "fallback", "silent");
    return soundId;
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
