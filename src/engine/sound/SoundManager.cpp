#include "SoundManager.h"
#include <cassert>
#include <cstring>
#include <filesystem>
#include <fstream>

SoundManager::~SoundManager() {
    if (masterVoice_) {
        masterVoice_->DestroyVoice();
        masterVoice_ = nullptr;
    }
    xAudio2_.Reset();
}

void SoundManager::Initialize() {
    HRESULT hr = XAudio2Create(&xAudio2_, 0);
    assert(SUCCEEDED(hr));

    hr = xAudio2_->CreateMasteringVoice(&masterVoice_);
    assert(SUCCEEDED(hr));
}

uint32_t SoundManager::Load(const std::wstring &path) {
    SoundResource res{};
    res.wav = LoadWavPcm16(path);

    sounds_.push_back(std::move(res));
    uint32_t soundId = static_cast<uint32_t>(sounds_.size() - 1);

    return soundId;
}

void SoundManager::Play(uint32_t soundId) {
    if (soundId >= sounds_.size()) {
        return;
    }

    const WavData &wav = sounds_[soundId].wav;
    assert(!wav.buffer.empty());

    IXAudio2SourceVoice *voice = nullptr;

    HRESULT hr = xAudio2_->CreateSourceVoice(
        &voice, &wav.format, 0, XAUDIO2_DEFAULT_FREQ_RATIO, &voiceCallback_);
    assert(SUCCEEDED(hr));

    XAUDIO2_BUFFER buf{};
    buf.pAudioData = wav.buffer.data();
    buf.AudioBytes = static_cast<UINT32>(wav.buffer.size());
    buf.Flags = XAUDIO2_END_OF_STREAM;

    buf.pContext = voice;

    hr = voice->SubmitSourceBuffer(&buf);
    assert(SUCCEEDED(hr));

    hr = voice->Start();
    assert(SUCCEEDED(hr));
}
WavData SoundManager::LoadWavPcm16(const std::wstring &path) {
    std::ifstream f(std::filesystem::path(path), std::ios::binary);
    assert(f && "Failed to open wav file");

    auto readU32 = [&f]() {
        uint32_t v{};
        f.read(reinterpret_cast<char *>(&v), 4);
        return v;
    };
    auto readU16 = [&f]() {
        uint16_t v{};
        f.read(reinterpret_cast<char *>(&v), 2);
        return v;
    };

    char riff[4]{};
    f.read(riff, 4);
    assert(std::memcmp(riff, "RIFF", 4) == 0);

    readU32();

    char wave[4]{};
    f.read(wave, 4);
    assert(std::memcmp(wave, "WAVE", 4) == 0);

    WavData out{};
    bool foundFmt = false;
    bool foundData = false;

    while (f && (!foundFmt || !foundData)) {
        char chunkId[4]{};
        f.read(chunkId, 4);
        if (!f)
            break;

        uint32_t chunkSize = readU32();
        if (!f)
            break;

        if (std::memcmp(chunkId, "fmt ", 4) == 0) {
            uint16_t wFormatTag = readU16();
            uint16_t nChannels = readU16();
            uint32_t nSamplesPerSec = readU32();
            uint32_t nAvgBytesPerSec = readU32();
            uint16_t nBlockAlign = readU16();
            uint16_t wBitsPerSample = readU16();

            // PCM 16bit のみ対応
            assert(wFormatTag == WAVE_FORMAT_PCM);
            assert(wBitsPerSample == 16);

            out.format.wFormatTag = wFormatTag;
            out.format.nChannels = nChannels;
            out.format.nSamplesPerSec = nSamplesPerSec;
            out.format.nAvgBytesPerSec = nAvgBytesPerSec;
            out.format.nBlockAlign = nBlockAlign;
            out.format.wBitsPerSample = wBitsPerSample;
            out.format.cbSize = 0;

            // fmt チャンク拡張分をスキップ
            const uint32_t consumed = 16;
            if (chunkSize > consumed) {
                f.seekg(chunkSize - consumed, std::ios::cur);
            }

            foundFmt = true;
        } else if (std::memcmp(chunkId, "data", 4) == 0) {
            out.buffer.resize(chunkSize);
            if (chunkSize > 0) {
                f.read(reinterpret_cast<char *>(out.buffer.data()), chunkSize);
            }
            foundData = true;
        } else {
            f.seekg(chunkSize, std::ios::cur);
        }

        // チャンクは 2byte 境界
        if (chunkSize & 1) {
            f.seekg(1, std::ios::cur);
        }
    }

    assert(foundFmt && foundData);
    assert(!out.buffer.empty());

    // 整合性チェック
    assert(out.format.nBlockAlign ==
           out.format.nChannels * (out.format.wBitsPerSample / 8));

    return out;
}
