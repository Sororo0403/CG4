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
#include <utility>

using namespace DirectX;

namespace {

constexpr DWORD kAllStreams = static_cast<DWORD>(MF_SOURCE_READER_ALL_STREAMS);
constexpr DWORD kFirstAudioStream =
    static_cast<DWORD>(MF_SOURCE_READER_FIRST_AUDIO_STREAM);
constexpr UINT32 kStreamQueuedBuffers = 3;
constexpr size_t kStreamBufferBytes = 64 * 1024;

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

class MediaBufferLock {
  public:
    explicit MediaBufferLock(IMFMediaBuffer *buffer) : buffer_(buffer) {
        if (buffer_ != nullptr &&
            FAILED(buffer_->Lock(&data_, &maxLength_, &currentLength_))) {
            buffer_ = nullptr;
            data_ = nullptr;
            maxLength_ = 0;
            currentLength_ = 0;
        }
    }

    ~MediaBufferLock() {
        if (buffer_) {
            buffer_->Unlock();
        }
    }

    const BYTE *Data() const { return data_; }
    DWORD Size() const { return currentLength_; }
    bool IsValid() const { return buffer_ != nullptr && data_ != nullptr; }

  private:
    IMFMediaBuffer *buffer_ = nullptr;
    BYTE *data_ = nullptr;
    DWORD maxLength_ = 0;
    DWORD currentLength_ = 0;
};

bool CreateStreamReader(const std::filesystem::path &path,
                        Microsoft::WRL::ComPtr<IMFSourceReader> &reader) {
    reader.Reset();
    if (FAILED(MFCreateSourceReaderFromURL(path.c_str(), nullptr, &reader))) {
        return false;
    }

    if (FAILED(reader->SetStreamSelection(kAllStreams, FALSE)) ||
        FAILED(reader->SetStreamSelection(kFirstAudioStream, TRUE))) {
        reader.Reset();
        return false;
    }
    return true;
}

bool SetStreamPcmFormat(IMFSourceReader *reader,
                        Microsoft::WRL::ComPtr<IMFMediaType> &currentType) {
    currentType.Reset();
    if (reader == nullptr) {
        return false;
    }

    Microsoft::WRL::ComPtr<IMFMediaType> pcmType;
    if (FAILED(MFCreateMediaType(&pcmType)) ||
        FAILED(pcmType->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Audio)) ||
        FAILED(pcmType->SetGUID(MF_MT_SUBTYPE, MFAudioFormat_PCM)) ||
        FAILED(pcmType->SetUINT32(MF_MT_AUDIO_BITS_PER_SAMPLE, 16)) ||
        FAILED(reader->SetCurrentMediaType(kFirstAudioStream, nullptr,
                                           pcmType.Get())) ||
        FAILED(reader->GetCurrentMediaType(kFirstAudioStream, &currentType))) {
        currentType.Reset();
        return false;
    }
    return true;
}

bool GetWaveFormat(IMFMediaType *mediaType, std::vector<BYTE> &result) {
    result.clear();
    if (mediaType == nullptr) {
        return false;
    }

    WAVEFORMATEX *waveFormat = nullptr;
    UINT32 waveFormatSize = 0;
    if (FAILED(MFCreateWaveFormatExFromMFMediaType(
            mediaType, &waveFormat, &waveFormatSize)) ||
        waveFormat == nullptr || waveFormatSize == 0) {
        if (waveFormat != nullptr) {
            CoTaskMemFree(waveFormat);
        }
        return false;
    }

    result.resize(waveFormatSize);
    std::copy_n(reinterpret_cast<const BYTE *>(waveFormat), waveFormatSize,
                result.data());
    CoTaskMemFree(waveFormat);
    return true;
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

bool ReadNextStreamChunk(IMFSourceReader *reader, bool &sourceEnded,
                         std::vector<BYTE> &decodedPcm) {
    decodedPcm.clear();
    sourceEnded = false;
    if (reader == nullptr) {
        return false;
    }

    while (decodedPcm.size() < kStreamBufferBytes) {
        DWORD flags = 0;
        Microsoft::WRL::ComPtr<IMFSample> sample;
        if (FAILED(reader->ReadSample(kFirstAudioStream, 0, nullptr, &flags,
                                      nullptr, &sample))) {
            decodedPcm.clear();
            return false;
        }

        if ((flags & MF_SOURCE_READERF_CURRENTMEDIATYPECHANGED) != 0) {
            decodedPcm.clear();
            return false;
        }
        if ((flags & MF_SOURCE_READERF_ENDOFSTREAM) != 0) {
            sourceEnded = true;
            break;
        }
        if (!sample) {
            continue;
        }

        Microsoft::WRL::ComPtr<IMFMediaBuffer> mediaBuffer;
        if (FAILED(sample->ConvertToContiguousBuffer(&mediaBuffer))) {
            decodedPcm.clear();
            return false;
        }

        const MediaBufferLock locked(mediaBuffer.Get());
        if (!locked.IsValid()) {
            decodedPcm.clear();
            return false;
        }
        const size_t oldSize = decodedPcm.size();
        if (locked.Size() >
            (std::numeric_limits<size_t>::max)() - oldSize) {
            decodedPcm.clear();
            return false;
        }
        decodedPcm.resize(oldSize + locked.Size());
        std::copy_n(locked.Data(), locked.Size(), decodedPcm.data() + oldSize);
    }

    return true;
}

} // namespace



uint32_t SoundManager::PlayStream(const std::wstring &path, float volume,
                                  bool loop) {
    const uint32_t streamingHandle = CreateStreamingVoice(path, volume, loop);
    if (streamingHandle != kInvalidVoiceHandle) {
        return streamingHandle;
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
    std::error_code ec;
    if (!std::filesystem::exists(resolvedPath, ec)) {
        return kInvalidVoiceHandle;
    }

    Microsoft::WRL::ComPtr<IMFSourceReader> reader;
    if (!CreateStreamReader(resolvedPath, reader)) {
        return kInvalidVoiceHandle;
    }

    Microsoft::WRL::ComPtr<IMFMediaType> mediaType;
    if (!SetStreamPcmFormat(reader.Get(), mediaType)) {
        return kInvalidVoiceHandle;
    }

    std::vector<BYTE> waveFormat;
    if (!GetWaveFormat(mediaType.Get(), waveFormat)) {
        return kInvalidVoiceHandle;
    }
    if (waveFormat.size() < sizeof(WAVEFORMATEX)) {
        return kInvalidVoiceHandle;
    }
    const WAVEFORMATEX *format =
        reinterpret_cast<const WAVEFORMATEX *>(waveFormat.data());
    if (!format || format->nSamplesPerSec == 0 || format->nBlockAlign == 0) {
        return kInvalidVoiceHandle;
    }

    IXAudio2SourceVoice *voice = nullptr;
    auto callback = std::make_unique<SoundVoiceCallback>();
    if (FAILED(xAudio2_->CreateSourceVoice(
            &voice, format, 0, XAUDIO2_DEFAULT_FREQ_RATIO, callback.get()))) {
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
    playingVoice.soundId = kInvalidSoundId;
    playingVoice.volume = ClampFinite(volume, 0.0f, 1.0f, 0.0f);
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
        return kInvalidVoiceHandle;
    }

    playingVoice.voice->SetVolume(playingVoice.volume);
    if (FAILED(playingVoice.voice->Start())) {
        playingVoice.voice->DestroyVoice();
        return kInvalidVoiceHandle;
    }

    playingVoices_.push_back(std::move(playingVoice));
    return playingVoices_.back().handle;
}

bool SoundManager::SubmitNextStreamBuffer(PlayingVoice &playingVoice) {
    if (!playingVoice.voice || !playingVoice.streamReader) {
        return false;
    }

    bool reachedEnd = false;
    std::vector<BYTE> pcm;
    if (!ReadNextStreamChunk(playingVoice.streamReader.Get(), reachedEnd, pcm)) {
        playingVoice.streamSourceEnded = true;
        return false;
    }

    if (pcm.empty() && reachedEnd && playingVoice.loop) {
        if (!SeekStreamToStart(playingVoice.streamReader.Get())) {
            playingVoice.streamSourceEnded = true;
            return false;
        }
        reachedEnd = false;
        if (!ReadNextStreamChunk(playingVoice.streamReader.Get(), reachedEnd,
                                 pcm)) {
            playingVoice.streamSourceEnded = true;
            return false;
        }
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
    if (playingVoice.streamBuffers.back().size() >
        static_cast<size_t>((std::numeric_limits<UINT32>::max)())) {
        playingVoice.streamBuffers.pop_back();
        playingVoice.streamSourceEnded = true;
        return false;
    }
    buffer.AudioBytes =
        static_cast<UINT32>(playingVoice.streamBuffers.back().size());
    if (reachedEnd && !playingVoice.loop) {
        buffer.Flags = XAUDIO2_END_OF_STREAM;
        playingVoice.streamSourceEnded = true;
    }

    const HRESULT hr = playingVoice.voice->SubmitSourceBuffer(&buffer);
    if (FAILED(hr)) {
        playingVoice.streamBuffers.pop_back();
        playingVoice.streamSourceEnded = true;
        return false;
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
