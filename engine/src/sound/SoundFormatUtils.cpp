#include "SoundFormatUtils.h"

#include <Objbase.h>
#include <algorithm>
#include <cstring>
#include <limits>
#include <mfapi.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#include <sstream>
#include <utility>

namespace {

constexpr DWORD kAllStreams = static_cast<DWORD>(MF_SOURCE_READER_ALL_STREAMS);
constexpr DWORD kFirstAudioStream =
    static_cast<DWORD>(MF_SOURCE_READER_FIRST_AUDIO_STREAM);

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

} // namespace

namespace SoundFormatUtils {

std::string MakeHResultMessage(HRESULT hr, const char *message) {
    std::ostringstream oss;
    oss << message << " HRESULT=0x" << std::hex
        << static_cast<unsigned long>(hr);
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

bool CopyWaveFormatHeader(const std::vector<BYTE> &bytes,
                          WAVEFORMATEX &outFormat) {
    if (bytes.size() < sizeof(WAVEFORMATEX)) {
        outFormat = {};
        return false;
    }

    std::memcpy(&outFormat, bytes.data(), sizeof(outFormat));
    return true;
}

bool CopyAlignedWaveFormat(const std::vector<BYTE> &bytes,
                           AlignedWaveFormat &outFormat) {
    WAVEFORMATEX header{};
    if (!CopyWaveFormatHeader(bytes, header)) {
        outFormat = {};
        return false;
    }

    const size_t extraSize = static_cast<size_t>(header.cbSize);
    if (extraSize >
        (std::numeric_limits<size_t>::max)() - sizeof(WAVEFORMATEX)) {
        outFormat = {};
        return false;
    }
    const size_t requiredSize = sizeof(WAVEFORMATEX) + extraSize;
    if (requiredSize > bytes.size() ||
        requiredSize > sizeof(WAVEFORMATEXTENSIBLE)) {
        outFormat = {};
        return false;
    }

    outFormat = {};
    std::memcpy(&outFormat.format, bytes.data(), requiredSize);
    return true;
}

bool CreatePcmSourceReader(
    const std::filesystem::path &path,
    Microsoft::WRL::ComPtr<IMFSourceReader> &reader,
    Microsoft::WRL::ComPtr<IMFMediaType> *currentType) {
    reader.Reset();
    if (currentType != nullptr) {
        currentType->Reset();
    }
    if (FAILED(MFCreateSourceReaderFromURL(path.c_str(), nullptr, &reader))) {
        return false;
    }

    if (FAILED(reader->SetStreamSelection(kAllStreams, FALSE)) ||
        FAILED(reader->SetStreamSelection(kFirstAudioStream, TRUE))) {
        reader.Reset();
        return false;
    }

    Microsoft::WRL::ComPtr<IMFMediaType> pcmType;
    Microsoft::WRL::ComPtr<IMFMediaType> resolvedType;
    if (FAILED(MFCreateMediaType(&pcmType)) ||
        FAILED(pcmType->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Audio)) ||
        FAILED(pcmType->SetGUID(MF_MT_SUBTYPE, MFAudioFormat_PCM)) ||
        FAILED(pcmType->SetUINT32(MF_MT_AUDIO_BITS_PER_SAMPLE, 16)) ||
        FAILED(reader->SetCurrentMediaType(kFirstAudioStream, nullptr,
                                           pcmType.Get())) ||
        FAILED(reader->GetCurrentMediaType(kFirstAudioStream,
                                           &resolvedType))) {
        reader.Reset();
        return false;
    }

    if (currentType != nullptr) {
        *currentType = std::move(resolvedType);
    }
    return true;
}

bool GetWaveFormatBytes(IMFMediaType *mediaType, std::vector<BYTE> &result) {
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

bool SeekSourceReaderToStart(IMFSourceReader *reader) {
    if (reader == nullptr) {
        return false;
    }

    PROPVARIANT position;
    PropVariantInit(&position);
    position.vt = VT_I8;
    position.hVal.QuadPart = 0;
    const HRESULT hr = reader->SetCurrentPosition(GUID_NULL, position);
    PropVariantClear(&position);
    return SUCCEEDED(hr);
}

bool ReadNextPcmChunk(IMFSourceReader *reader, size_t targetBytes,
                      size_t maxDecodedBytes, bool &sourceEnded,
                      std::vector<BYTE> &decodedPcm) {
    decodedPcm.clear();
    sourceEnded = false;
    if (reader == nullptr || targetBytes == 0 || maxDecodedBytes == 0) {
        return false;
    }

    while (decodedPcm.size() < targetBytes) {
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
        if (locked.Size() > (std::numeric_limits<size_t>::max)() - oldSize ||
            oldSize + locked.Size() > maxDecodedBytes) {
            decodedPcm.clear();
            return false;
        }
        decodedPcm.resize(oldSize + locked.Size());
        std::copy_n(locked.Data(), locked.Size(), decodedPcm.data() + oldSize);
    }

    return true;
}

bool ReadAllPcmData(IMFSourceReader *reader, size_t maxDecodedBytes,
                    std::vector<BYTE> &decodedPcm) {
    decodedPcm.clear();
    if (reader == nullptr || maxDecodedBytes == 0) {
        return false;
    }

    for (;;) {
        bool sourceEnded = false;
        std::vector<BYTE> chunk;
        if (!ReadNextPcmChunk(reader, 1u, maxDecodedBytes - decodedPcm.size(),
                              sourceEnded, chunk)) {
            decodedPcm.clear();
            return false;
        }
        if (!chunk.empty()) {
            if (chunk.size() > (std::numeric_limits<size_t>::max)() -
                                   decodedPcm.size() ||
                decodedPcm.size() + chunk.size() > maxDecodedBytes) {
                decodedPcm.clear();
                return false;
            }
            decodedPcm.insert(decodedPcm.end(), chunk.begin(), chunk.end());
        }
        if (sourceEnded) {
            break;
        }
    }

    return !decodedPcm.empty();
}

} // namespace SoundFormatUtils
