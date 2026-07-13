#include "core/Numeric.h"
#include "core/PathUtils.h"
#include "internal/SoundFormatUtils.h"
#include "internal/SoundManagerInternal.h"
#include "sound/SoundManager.h"

#include <algorithm>
#include <exception>
#include <filesystem>
#include <limits>
#include <memory>
#include <utility>
#include <vector>

using namespace DirectX;

namespace {

constexpr size_t kStreamBufferBytes = 64 * 1024;
using SoundManagerInternal::kStreamQueuedBuffers;

} // namespace

uint32_t SoundManager::PlayStream(const std::wstring& path, float volume, bool loop) {
    const uint32_t streamingHandle = CreateStreamingVoice(path, volume, loop);
    if (streamingHandle != kInvalidVoiceHandle) {
        return streamingHandle;
    }

    const uint32_t soundId = LoadOrCreateSilent(path);
    const uint32_t handle = Play(soundId, volume, loop);
    auto it = std::find_if(
        state_->playingVoices.begin(), state_->playingVoices.end(),
        [handle](const PlayingVoice& playingVoice) { return playingVoice.handle == handle; });
    if (it != state_->playingVoices.end()) {
        it->isStreaming = false;
    }
    return handle;
}

uint32_t SoundManager::CreateStreamingVoice(const std::wstring& path, float volume, bool loop) {
    Update();
    if (!state_->xAudio2) {
        return kInvalidVoiceHandle;
    }

    std::filesystem::path resolvedPath;
    try {
        resolvedPath = PathUtils::ResolveAssetPath(path);
    } catch (const std::exception&) {
        return kInvalidVoiceHandle;
    }
    std::error_code ec;
    if (!std::filesystem::exists(resolvedPath, ec)) {
        return kInvalidVoiceHandle;
    }

    Microsoft::WRL::ComPtr<IMFSourceReader> reader;
    Microsoft::WRL::ComPtr<IMFMediaType> mediaType;
    if (!SoundFormatUtils::CreatePcmSourceReader(resolvedPath, reader, &mediaType)) {
        return kInvalidVoiceHandle;
    }

    std::vector<BYTE> waveFormat;
    if (!SoundFormatUtils::GetWaveFormatBytes(mediaType.Get(), waveFormat)) {
        return kInvalidVoiceHandle;
    }
    SoundFormatUtils::AlignedWaveFormat alignedFormat{};
    if (!SoundFormatUtils::CopyAlignedWaveFormat(waveFormat, alignedFormat)) {
        return kInvalidVoiceHandle;
    }
    const WAVEFORMATEX* format = alignedFormat.Get();
    if (format->nSamplesPerSec == 0 || format->nBlockAlign == 0) {
        return kInvalidVoiceHandle;
    }

    PlayingVoice playingVoice{};
    IXAudio2SourceVoice* voice = nullptr;
    std::unique_ptr<SoundVoiceCallback> callback;
    try {
        callback = std::make_unique<SoundVoiceCallback>();
    } catch (const std::exception&) {
        return kInvalidVoiceHandle;
    }
    if (FAILED(state_->xAudio2->CreateSourceVoice(&voice, format, XAUDIO2_VOICE_USEFILTER,
                                                  XAUDIO2_DEFAULT_FREQ_RATIO, callback.get()))) {
        return kInvalidVoiceHandle;
    }
    SoundManagerInternal::SourceVoiceGuard voiceGuard(voice);

    if (state_->nextVoiceHandle == kInvalidVoiceHandle) {
        state_->nextVoiceHandle = 1;
    }

    playingVoice.voice = voiceGuard.Get();
    playingVoice.callback = std::move(callback);
    playingVoice.handle = AllocateVoiceHandle();
    if (playingVoice.handle == kInvalidVoiceHandle) {
        return kInvalidVoiceHandle;
    }
    playingVoice.soundId = kInvalidSoundId;
    playingVoice.volume = Numeric::ClampFinite(volume, 0.0f, 1.0f, 0.0f);
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
        return kInvalidVoiceHandle;
    }

    try {
        state_->playingVoices.push_back(std::move(playingVoice));
    } catch (const std::exception&) {
        return kInvalidVoiceHandle;
    }
    voiceGuard.Release();
    PlayingVoice& storedVoice = state_->playingVoices.back();
    const uint32_t handle = storedVoice.handle;
    storedVoice.voice->SetVolume(storedVoice.volume);
    if (FAILED(storedVoice.voice->Start())) {
        DestroyVoice(storedVoice);
        state_->playingVoices.pop_back();
        return kInvalidVoiceHandle;
    }

    return handle;
}

bool SoundManager::SubmitNextStreamBuffer(PlayingVoice& playingVoice) {
    if (!playingVoice.voice || !playingVoice.streamReader) {
        return false;
    }

    bool reachedEnd = false;
    std::vector<BYTE> pcm;
    if (!ReadNextStreamPcm(playingVoice, reachedEnd, pcm)) {
        return false;
    }

    if (pcm.empty()) {
        playingVoice.streamSourceEnded = reachedEnd || !playingVoice.loop;
        return false;
    }

    bool endAfterBuffer = reachedEnd && !playingVoice.loop;
    if (reachedEnd && playingVoice.loop) {
        if (SoundFormatUtils::SeekSourceReaderToStart(playingVoice.streamReader.Get())) {
        } else {
            endAfterBuffer = true;
            playingVoice.streamSourceEnded = true;
        }
    }

    return AppendStreamBuffer(playingVoice, std::move(pcm), endAfterBuffer);
}

bool SoundManager::ReadNextStreamPcm(PlayingVoice& playingVoice, bool& reachedEnd,
                                     std::vector<BYTE>& pcm) {
    if (!SoundFormatUtils::ReadNextPcmChunk(playingVoice.streamReader.Get(), kStreamBufferBytes,
                                            (std::numeric_limits<size_t>::max)(), reachedEnd,
                                            pcm)) {
        playingVoice.streamSourceEnded = true;
        return false;
    }

    if (!pcm.empty() || !reachedEnd || !playingVoice.loop) {
        return true;
    }
    if (!SoundFormatUtils::SeekSourceReaderToStart(playingVoice.streamReader.Get())) {
        playingVoice.streamSourceEnded = true;
        return false;
    }
    reachedEnd = false;
    if (!SoundFormatUtils::ReadNextPcmChunk(playingVoice.streamReader.Get(), kStreamBufferBytes,
                                            (std::numeric_limits<size_t>::max)(), reachedEnd,
                                            pcm)) {
        playingVoice.streamSourceEnded = true;
        return false;
    }
    return true;
}

bool SoundManager::AppendStreamBuffer(PlayingVoice& playingVoice, std::vector<BYTE>&& pcm,
                                      bool endAfterBuffer) {
    try {
        playingVoice.streamBuffers.push_back(std::move(pcm));
    } catch (const std::exception&) {
        playingVoice.streamSourceEnded = true;
        return false;
    }
    XAUDIO2_BUFFER buffer{};
    buffer.pAudioData = playingVoice.streamBuffers.back().data();
    if (playingVoice.streamBuffers.back().size() >
        static_cast<size_t>((std::numeric_limits<UINT32>::max)())) {
        playingVoice.streamBuffers.pop_back();
        playingVoice.streamSourceEnded = true;
        return false;
    }
    buffer.AudioBytes = static_cast<UINT32>(playingVoice.streamBuffers.back().size());
    if (endAfterBuffer) {
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

void SoundManager::ReleaseFinishedStreamBuffers(PlayingVoice& playingVoice) {
    if (!playingVoice.callback) {
        return;
    }

    uint32_t completedBuffers = playingVoice.callback->ConsumeEndedBufferCount();
    while (completedBuffers > 0 && !playingVoice.streamBuffers.empty()) {
        playingVoice.streamBuffers.pop_front();
        --completedBuffers;
    }
}
