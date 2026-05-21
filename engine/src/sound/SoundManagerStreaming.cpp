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



uint32_t SoundManager::PlayStream(const std::wstring &path, float volume,
                                  bool loop) {
    try {
        return CreateStreamingVoice(path, volume, loop);
    } catch (const std::exception &e) {
        DebugLog::Get().Write("Sound", "SoundManager", "stream_failed",
                              e.what());
    } catch (...) {
        DebugLog::Get().Write("Sound", "SoundManager", "stream_failed",
                              "unknown error");
    }

    const uint32_t soundId = LoadOrCreateSilent(path);
    const uint32_t handle = Play(soundId, volume, loop);
    for (PlayingVoice &playingVoice : playingVoices_) {
        if (playingVoice.handle == handle) {
            playingVoice.isStreaming = false;
            break;
        }
    }
    return handle;
}

uint32_t SoundManager::CreateStreamingVoice(const std::wstring &path,
                                            float volume, bool loop) {
    Update();
    if (!xAudio2_) {
        return kInvalidVoiceHandle;
    }

    const std::filesystem::path resolvedPath = ResolveAudioPath(path);
    if (!std::filesystem::exists(resolvedPath)) {
        throw std::runtime_error("Audio stream file not found. requested=" +
                                 std::filesystem::path(path).string() +
                                 " resolved=" + resolvedPath.string());
    }

    Microsoft::WRL::ComPtr<IMFSourceReader> reader =
        CreateStreamReader(resolvedPath);
    Microsoft::WRL::ComPtr<IMFMediaType> mediaType =
        SetStreamPcmFormat(reader.Get());
    std::vector<BYTE> waveFormat = GetWaveFormat(mediaType.Get());
    const WAVEFORMATEX *format =
        reinterpret_cast<const WAVEFORMATEX *>(waveFormat.data());
    if (!format) {
        throw std::runtime_error("Streaming audio format is empty");
    }

    IXAudio2SourceVoice *voice = nullptr;
    auto callback = std::make_unique<SoundVoiceCallback>();
    ThrowIfFailed(xAudio2_->CreateSourceVoice(
                      &voice, format, 0, XAUDIO2_DEFAULT_FREQ_RATIO,
                      callback.get()),
                  "CreateSourceVoice for streaming failed");

    if (nextVoiceHandle_ == kInvalidVoiceHandle) {
        nextVoiceHandle_ = 1;
    }

    PlayingVoice playingVoice{};
    playingVoice.voice = voice;
    playingVoice.callback = std::move(callback);
    playingVoice.handle = nextVoiceHandle_++;
    playingVoice.soundId = kInvalidSoundId;
    playingVoice.volume = std::clamp(volume, 0.0f, 1.0f);
    playingVoice.loop = loop;
    playingVoice.isStreaming = true;
    playingVoice.streamReader = reader;
    playingVoice.streamWaveFormat = std::move(waveFormat);

    for (UINT32 i = 0; i < kStreamQueuedBuffers; ++i) {
        if (!SubmitNextStreamBuffer(playingVoice)) {
            break;
        }
    }

    if (playingVoice.streamBuffers.empty()) {
        playingVoice.voice->DestroyVoice();
        throw std::runtime_error("Streaming audio data is empty");
    }

    playingVoice.voice->SetVolume(playingVoice.volume);
    ThrowIfFailed(playingVoice.voice->Start(),
                  "Start streaming source voice failed");

    playingVoices_.push_back(std::move(playingVoice));
    return playingVoices_.back().handle;
}

bool SoundManager::SubmitNextStreamBuffer(PlayingVoice &playingVoice) {
    if (!playingVoice.voice || !playingVoice.streamReader) {
        return false;
    }

    bool reachedEnd = false;
    std::vector<BYTE> pcm =
        ReadNextStreamChunk(playingVoice.streamReader.Get(), reachedEnd);

    if (pcm.empty() && reachedEnd && playingVoice.loop) {
        if (!SeekStreamToStart(playingVoice.streamReader.Get())) {
            playingVoice.streamSourceEnded = true;
            return false;
        }
        reachedEnd = false;
        pcm = ReadNextStreamChunk(playingVoice.streamReader.Get(), reachedEnd);
    }

    if (pcm.empty()) {
        playingVoice.streamSourceEnded = reachedEnd || !playingVoice.loop;
        return false;
    }

    if (reachedEnd && playingVoice.loop) {
        SeekStreamToStart(playingVoice.streamReader.Get());
        reachedEnd = false;
    }

    playingVoice.streamBuffers.push_back(std::move(pcm));

    XAUDIO2_BUFFER buffer{};
    buffer.pAudioData = playingVoice.streamBuffers.back().data();
    buffer.AudioBytes =
        static_cast<UINT32>(playingVoice.streamBuffers.back().size());
    if (reachedEnd && !playingVoice.loop) {
        buffer.Flags = XAUDIO2_END_OF_STREAM;
        playingVoice.streamSourceEnded = true;
    }

    const HRESULT hr = playingVoice.voice->SubmitSourceBuffer(&buffer);
    if (FAILED(hr)) {
        playingVoice.streamBuffers.pop_back();
        ThrowIfFailed(hr, "Submit streaming source buffer failed");
    }

    return true;
}

void SoundManager::ReleaseFinishedStreamBuffers(PlayingVoice &playingVoice) {
    if (!playingVoice.callback) {
        return;
    }

    uint32_t completedBuffers =
        playingVoice.callback->ConsumeEndedBufferCount();
    while (completedBuffers > 0 && !playingVoice.streamBuffers.empty()) {
        playingVoice.streamBuffers.pop_front();
        --completedBuffers;
    }
}
