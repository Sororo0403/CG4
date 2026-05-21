#include "sound/AudioFileLoader.h"

#include "core/AssetManager.h"

#include <Objbase.h>
#include <algorithm>
#include <filesystem>
#include <mfapi.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#include <sstream>
#include <stdexcept>
#include <wrl.h>

using Microsoft::WRL::ComPtr;

namespace {

constexpr DWORD kAllStreams = static_cast<DWORD>(MF_SOURCE_READER_ALL_STREAMS);
constexpr DWORD kFirstAudioStream =
    static_cast<DWORD>(MF_SOURCE_READER_FIRST_AUDIO_STREAM);

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

AudioFileLoader::SoundData::Info MakeSoundInfo(const WAVEFORMATEX &format,
                                               size_t decodedBytes) {
    AudioFileLoader::SoundData::Info info{};
    info.sampleRate = format.nSamplesPerSec;
    info.channels = format.nChannels;
    info.bitsPerSample = format.wBitsPerSample;
    info.decodedBytes = decodedBytes;
    if (format.nAvgBytesPerSec > 0) {
        info.durationSeconds =
            static_cast<float>(decodedBytes) /
            static_cast<float>(format.nAvgBytesPerSec);
    }
    return info;
}

ComPtr<IMFSourceReader> CreateSourceReader(const std::filesystem::path &path) {
    ComPtr<IMFSourceReader> reader;
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

ComPtr<IMFMediaType> SetPcmFormat(IMFSourceReader *reader) {
    ComPtr<IMFMediaType> pcmType;
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

    ComPtr<IMFMediaType> currentType;
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

std::vector<BYTE> ReadPcmData(IMFSourceReader *reader) {
    std::vector<BYTE> decodedPcm;

    for (;;) {
        DWORD flags = 0;
        ComPtr<IMFSample> sample;
        ThrowIfFailed(reader->ReadSample(kFirstAudioStream, 0, nullptr, &flags,
                                         nullptr, &sample),
                      "ReadSample failed");

        if ((flags & MF_SOURCE_READERF_ENDOFSTREAM) != 0) {
            break;
        }
        if ((flags & MF_SOURCE_READERF_CURRENTMEDIATYPECHANGED) != 0) {
            throw std::runtime_error(
                "Audio media type changed while reading PCM data");
        }
        if (!sample) {
            continue;
        }

        ComPtr<IMFMediaBuffer> mediaBuffer;
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

AudioFileLoader::SoundData AudioFileLoader::Load(const std::wstring &path) {
    const std::filesystem::path resolvedPath = ResolveAudioPath(path);
    if (!std::filesystem::exists(resolvedPath)) {
        throw std::runtime_error("Audio file not found. requested=" +
                                 std::filesystem::path(path).string() +
                                 " resolved=" + resolvedPath.string());
    }

    ComPtr<IMFSourceReader> reader = CreateSourceReader(resolvedPath);
    ComPtr<IMFMediaType> currentMediaType = SetPcmFormat(reader.Get());

    SoundData data{};
    data.waveFormat = GetWaveFormat(currentMediaType.Get());
    data.decodedPcm = ReadPcmData(reader.Get());

    if (data.waveFormat.empty() || data.decodedPcm.empty()) {
        throw std::runtime_error("Loaded audio data is empty");
    }

    data.info = MakeSoundInfo(*data.GetFormat(), data.decodedPcm.size());
    return data;
}
