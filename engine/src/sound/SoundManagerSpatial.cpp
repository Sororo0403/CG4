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



uint32_t SoundManager::Play3D(uint32_t soundId,
                              const DirectX::XMFLOAT3 &sourcePosition,
                              float volume, bool loop) {
    const uint32_t handle = Play(soundId, volume, loop);
    if (handle == kInvalidVoiceHandle) {
        return handle;
    }

    for (PlayingVoice &playingVoice : playingVoices_) {
        if (playingVoice.handle == handle) {
            playingVoice.is3D = true;
            playingVoice.position = sourcePosition;
            Apply3D(playingVoice);
            break;
        }
    }
    return handle;
}

void SoundManager::SetListener(const DirectX::XMFLOAT3 &position,
                               const DirectX::XMFLOAT3 &forward,
                               const DirectX::XMFLOAT3 &up) {
    listenerPosition_ = position;
    XMStoreFloat3(&listenerForward_,
                  LoadFloat3OrDefault(forward,
                                      XMVectorSet(0.0f, 0.0f, 1.0f, 0.0f)));
    XMStoreFloat3(&listenerUp_,
                  LoadFloat3OrDefault(up,
                                      XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f)));

    for (PlayingVoice &playingVoice : playingVoices_) {
        if (playingVoice.is3D) {
            Apply3D(playingVoice);
        }
    }
}

void SoundManager::SetVoicePosition(
    uint32_t voiceHandle, const DirectX::XMFLOAT3 &sourcePosition) {
    for (PlayingVoice &playingVoice : playingVoices_) {
        if (playingVoice.handle == voiceHandle) {
            playingVoice.is3D = true;
            playingVoice.position = sourcePosition;
            Apply3D(playingVoice);
            return;
        }
    }
}

void SoundManager::SetVoice3DRange(uint32_t voiceHandle, float minDistance,
                                   float maxDistance) {
    for (PlayingVoice &playingVoice : playingVoices_) {
        if (playingVoice.handle == voiceHandle) {
            playingVoice.minDistance = (std::max)(minDistance, 0.001f);
            playingVoice.maxDistance =
                (std::max)(maxDistance, playingVoice.minDistance + 0.001f);
            Apply3D(playingVoice);
            return;
        }
    }
}

void SoundManager::Apply3D(PlayingVoice &playingVoice) {
    if (!playingVoice.voice || !masterVoice_) {
        return;
    }

    XMVECTOR listener = XMLoadFloat3(&listenerPosition_);
    XMVECTOR source = XMLoadFloat3(&playingVoice.position);
    XMVECTOR toSource = source - listener;
    const float distance = XMVectorGetX(XMVector3Length(toSource));

    const float attenuation =
        1.0f - (distance - playingVoice.minDistance) /
                   (playingVoice.maxDistance - playingVoice.minDistance);
    const float volume = playingVoice.volume *
                         std::clamp(attenuation, 0.0f, 1.0f);

    XAUDIO2_VOICE_DETAILS sourceDetails{};
    XAUDIO2_VOICE_DETAILS masterDetails{};
    playingVoice.voice->GetVoiceDetails(&sourceDetails);
    masterVoice_->GetVoiceDetails(&masterDetails);

    const UINT32 sourceChannels =
        (std::max)(sourceDetails.InputChannels, UINT32{1});
    const UINT32 destinationChannels =
        (std::max)(masterDetails.InputChannels, UINT32{1});
    std::vector<float> matrix(sourceChannels * destinationChannels, volume);

    if (destinationChannels >= 2 && distance > 0.0001f) {
        XMVECTOR forward = LoadFloat3OrDefault(
            listenerForward_, XMVectorSet(0.0f, 0.0f, 1.0f, 0.0f));
        XMVECTOR up = LoadFloat3OrDefault(
            listenerUp_, XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f));
        XMVECTOR right = XMVector3Normalize(XMVector3Cross(up, forward));
        XMVECTOR direction = XMVector3Normalize(toSource);
        const float pan = std::clamp(XMVectorGetX(XMVector3Dot(direction, right)),
                                     -1.0f, 1.0f);
        const float left = volume * std::sqrt((1.0f - pan) * 0.5f);
        const float rightVolume = volume * std::sqrt((1.0f + pan) * 0.5f);

        for (UINT32 sourceChannel = 0; sourceChannel < sourceChannels;
             ++sourceChannel) {
            matrix[sourceChannel * destinationChannels + 0] = left;
            matrix[sourceChannel * destinationChannels + 1] = rightVolume;
        }
    }

    playingVoice.voice->SetOutputMatrix(
        masterVoice_, sourceChannels, destinationChannels, matrix.data());
}
