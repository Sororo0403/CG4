#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>
#include <wrl.h>
#include <xaudio2.h>

struct IMFMediaType;
struct IMFSourceReader;

namespace SoundFormatUtils {

struct AlignedWaveFormat {
    WAVEFORMATEXTENSIBLE format{};

    const WAVEFORMATEX* Get() const {
        return &format.Format;
    }
};

std::string MakeHResultMessage(HRESULT hr, const char* message);
bool BuildPcmWaveFormat(uint32_t sampleRate, uint16_t channels, uint16_t bitsPerSample,
                        WAVEFORMATEX& format);
bool IsSupportedPcmReadFormat(const WAVEFORMATEX& format);
bool CopyWaveFormatHeader(const std::vector<BYTE>& bytes, WAVEFORMATEX& outFormat);
bool CopyAlignedWaveFormat(const std::vector<BYTE>& bytes, AlignedWaveFormat& outFormat);
bool CreatePcmSourceReader(const std::filesystem::path& path,
                           Microsoft::WRL::ComPtr<IMFSourceReader>& reader,
                           Microsoft::WRL::ComPtr<IMFMediaType>* currentType = nullptr);
bool GetWaveFormatBytes(IMFMediaType* mediaType, std::vector<BYTE>& result);
bool SeekSourceReaderToStart(IMFSourceReader* reader);
bool ReadNextPcmChunk(IMFSourceReader* reader, size_t targetBytes, size_t maxDecodedBytes,
                      bool& sourceEnded, std::vector<BYTE>& decodedPcm);
bool ReadAllPcmData(IMFSourceReader* reader, size_t maxBytes, std::vector<BYTE>& decodedPcm);

} // namespace SoundFormatUtils
