#include "sound/AudioFileLoader.h"

#include "core/AssetManager.h"

#include <Objbase.h>
#include <algorithm>
#include <filesystem>
#include <limits>
#include <mfapi.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#include <utility>
#include <wrl.h>

using Microsoft::WRL::ComPtr;

namespace {

constexpr DWORD kAllStreams = static_cast<DWORD>(MF_SOURCE_READER_ALL_STREAMS);
constexpr DWORD kFirstAudioStream =
    static_cast<DWORD>(MF_SOURCE_READER_FIRST_AUDIO_STREAM);

std::filesystem::path ResolveAudioPath(const std::wstring &path) {
    return AssetManager::ResolvePath(std::filesystem::path(path));
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

bool CreateSourceReader(const std::filesystem::path &path,
                        ComPtr<IMFSourceReader> &reader) {
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

bool SetPcmFormat(IMFSourceReader *reader, ComPtr<IMFMediaType> &currentType) {
    if (reader == nullptr) {
        return false;
    }

    ComPtr<IMFMediaType> pcmType;
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

bool ReadPcmData(IMFSourceReader *reader, std::vector<BYTE> &decodedPcm) {
    decodedPcm.clear();
    if (reader == nullptr) {
        return false;
    }

    for (;;) {
        DWORD flags = 0;
        ComPtr<IMFSample> sample;
        if (FAILED(reader->ReadSample(kFirstAudioStream, 0, nullptr, &flags,
                                      nullptr, &sample))) {
            decodedPcm.clear();
            return false;
        }

        if ((flags & MF_SOURCE_READERF_ENDOFSTREAM) != 0) {
            break;
        }
        if ((flags & MF_SOURCE_READERF_CURRENTMEDIATYPECHANGED) != 0) {
            decodedPcm.clear();
            return false;
        }
        if (!sample) {
            continue;
        }

        ComPtr<IMFMediaBuffer> mediaBuffer;
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

    return !decodedPcm.empty();
}

} // namespace

AudioFileLoader::SoundData AudioFileLoader::Load(const std::wstring &path) {
    SoundData data{};
    if (TryLoad(path, data)) {
        return data;
    }
    return {};
}

bool AudioFileLoader::TryLoad(const std::wstring &path, SoundData &outData) {
    outData = {};

    const std::filesystem::path resolvedPath = ResolveAudioPath(path);
    std::error_code ec;
    if (!std::filesystem::exists(resolvedPath, ec)) {
        return false;
    }

    ComPtr<IMFSourceReader> reader;
    if (!CreateSourceReader(resolvedPath, reader)) {
        return false;
    }

    ComPtr<IMFMediaType> currentMediaType;
    if (!SetPcmFormat(reader.Get(), currentMediaType)) {
        return false;
    }

    SoundData data{};
    if (!GetWaveFormat(currentMediaType.Get(), data.waveFormat) ||
        !ReadPcmData(reader.Get(), data.decodedPcm) ||
        data.waveFormat.empty() || data.decodedPcm.empty() ||
        data.GetFormat() == nullptr) {
        return false;
    }

    data.info = MakeSoundInfo(*data.GetFormat(), data.decodedPcm.size());
    outData = std::move(data);
    return true;
}
