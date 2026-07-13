#include "sound/AudioFileLoader.h"

#include "core/PathUtils.h"
#include "internal/SoundFormatUtils.h"
#include "sound/AudioLimits.h"

#include <exception>
#include <filesystem>
#include <mfapi.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#include <utility>

using Microsoft::WRL::ComPtr;

namespace {

AudioFileLoader::SoundData::Info MakeSoundInfo(const WAVEFORMATEX& format, size_t decodedBytes) {
    AudioFileLoader::SoundData::Info info{};
    info.sampleRate = format.nSamplesPerSec;
    info.channels = format.nChannels;
    info.bitsPerSample = format.wBitsPerSample;
    info.decodedBytes = decodedBytes;
    if (format.nAvgBytesPerSec > 0) {
        info.durationSeconds =
            static_cast<float>(decodedBytes) / static_cast<float>(format.nAvgBytesPerSec);
    }
    return info;
}

} // namespace

AudioFileLoader::SoundData AudioFileLoader::Load(const std::wstring& path) {
    SoundData data{};
    if (TryLoad(path, data)) {
        return data;
    }
    return {};
}

bool AudioFileLoader::TryLoad(const std::wstring& path, SoundData& outData) {
    outData = {};

    std::filesystem::path resolvedPath;
    try {
        resolvedPath = PathUtils::ResolveAssetPath(path);
    } catch (const std::exception&) {
        return false;
    }
    std::error_code ec;
    try {
        if (!std::filesystem::exists(resolvedPath, ec)) {
            return false;
        }
    } catch (const std::exception&) {
        return false;
    }

    ComPtr<IMFSourceReader> reader;
    ComPtr<IMFMediaType> currentMediaType;
    if (!SoundFormatUtils::CreatePcmSourceReader(resolvedPath, reader, &currentMediaType)) {
        return false;
    }

    SoundData data{};
    WAVEFORMATEX format{};
    if (!SoundFormatUtils::GetWaveFormatBytes(currentMediaType.Get(), data.waveFormat) ||
        !SoundFormatUtils::ReadAllPcmData(reader.Get(), AudioLimits::kMaxDecodedPcmBytes,
                                          data.decodedPcm) ||
        data.waveFormat.empty() || data.decodedPcm.empty() || !data.CopyFormat(format)) {
        return false;
    }

    data.info = MakeSoundInfo(format, data.decodedPcm.size());
    outData = std::move(data);
    return true;
}
