#include "sound/SoundManager.h"

#include "core/Numeric.h"
#include "internal/SoundFormatUtils.h"
#include "internal/SoundManagerInternal.h"

#include <algorithm>
#include <cmath>
#include <exception>
#include <limits>
#include <new>
#include <utility>

namespace {

using SoundManagerInternal::kStreamQueuedBuffers;

} // namespace

uint32_t SoundManager::Play(uint32_t soundId, float volume, bool loop) {
    Update();
    if (soundId >= state_->sounds.size() || !state_->xAudio2 || GetInfo(soundId) == nullptr) {
        return kInvalidVoiceHandle;
    }

    return CreateSourceVoice(soundId, volume, loop);
}

uint32_t SoundManager::PlayFrom(uint32_t soundId, float startSeconds, float volume, bool loop) {
    Update();
    if (soundId >= state_->sounds.size() || !state_->xAudio2 || GetInfo(soundId) == nullptr) {
        return kInvalidVoiceHandle;
    }

    return CreateSourceVoice(soundId, volume, loop, startSeconds);
}

void SoundManager::Stop(uint32_t voiceHandle) {
    auto it = std::find_if(state_->playingVoices.begin(), state_->playingVoices.end(),
                           [voiceHandle](const PlayingVoice& playingVoice) {
                               return playingVoice.handle == voiceHandle;
                           });
    if (it != state_->playingVoices.end()) {
        DestroyVoice(*it);
        state_->playingVoices.erase(it);
    }
}

void SoundManager::Pause(uint32_t voiceHandle) {
    auto it = std::find_if(state_->playingVoices.begin(), state_->playingVoices.end(),
                           [voiceHandle](const PlayingVoice& playingVoice) {
                               return playingVoice.handle == voiceHandle && playingVoice.voice;
                           });
    if (it != state_->playingVoices.end()) {
        it->voice->Stop(0);
    }
}

void SoundManager::Resume(uint32_t voiceHandle) {
    auto it = std::find_if(state_->playingVoices.begin(), state_->playingVoices.end(),
                           [voiceHandle](const PlayingVoice& playingVoice) {
                               return playingVoice.handle == voiceHandle && playingVoice.voice;
                           });
    if (it != state_->playingVoices.end()) {
        it->voice->Start(0);
    }
}

void SoundManager::SetVoiceVolume(uint32_t voiceHandle, float volume) {
    const float clampedVolume = Numeric::ClampFinite(volume, 0.0f, 1.0f, 0.0f);
    auto it = std::find_if(state_->playingVoices.begin(), state_->playingVoices.end(),
                           [voiceHandle](const PlayingVoice& playingVoice) {
                               return playingVoice.handle == voiceHandle && playingVoice.voice;
                           });
    if (it != state_->playingVoices.end()) {
        it->volume = clampedVolume;
        it->voice->SetVolume(clampedVolume);
    }
}

void SoundManager::SetVoiceFrequencyRatio(uint32_t voiceHandle, float frequencyRatio) {
    const float clampedRatio = Numeric::ClampFinite(
        frequencyRatio, XAUDIO2_MIN_FREQ_RATIO, XAUDIO2_MAX_FREQ_RATIO, XAUDIO2_DEFAULT_FREQ_RATIO);
    auto it = std::find_if(state_->playingVoices.begin(), state_->playingVoices.end(),
                           [voiceHandle](const PlayingVoice& playingVoice) {
                               return playingVoice.handle == voiceHandle && playingVoice.voice;
                           });
    if (it != state_->playingVoices.end()) {
        it->frequencyRatio = clampedRatio;
        it->voice->SetFrequencyRatio(clampedRatio);
    }
}

float SoundManager::GetVoiceFrequencyRatio(uint32_t voiceHandle) const {
    const auto it = std::find_if(state_->playingVoices.begin(), state_->playingVoices.end(),
                                 [voiceHandle](const PlayingVoice& playingVoice) {
                                     return playingVoice.handle == voiceHandle;
                                 });
    return it != state_->playingVoices.end() ? it->frequencyRatio : XAUDIO2_DEFAULT_FREQ_RATIO;
}

float SoundManager::GetVoiceVolume(uint32_t voiceHandle) const {
    const auto it = std::find_if(state_->playingVoices.begin(), state_->playingVoices.end(),
                                 [voiceHandle](const PlayingVoice& playingVoice) {
                                     return playingVoice.handle == voiceHandle;
                                 });
    return it != state_->playingVoices.end() ? it->volume : 0.0f;
}

float SoundManager::GetPlaybackPosition(uint32_t voiceHandle) const {
    const auto it =
        std::find_if(state_->playingVoices.begin(), state_->playingVoices.end(),
                     [voiceHandle](const PlayingVoice& playingVoice) {
                         return playingVoice.handle == voiceHandle && playingVoice.voice;
                     });
    if (it != state_->playingVoices.end()) {
        const PlayingVoice& playingVoice = *it;

        XAUDIO2_VOICE_STATE state{};
        playingVoice.voice->GetState(&state);
        WAVEFORMATEX format{};
        bool hasFormat = false;
        uint64_t totalFrames = 0;
        if (playingVoice.isStreaming) {
            hasFormat =
                SoundFormatUtils::CopyWaveFormatHeader(playingVoice.streamWaveFormat, format);
        } else if (playingVoice.soundId < state_->sounds.size()) {
            const AudioFileLoader::SoundData& sound = state_->sounds[playingVoice.soundId].data;
            hasFormat = sound.CopyFormat(format);
            if (hasFormat && format.nBlockAlign != 0) {
                totalFrames = sound.decodedPcm.size() / static_cast<uint64_t>(format.nBlockAlign);
            }
        }
        if (!hasFormat || format.nSamplesPerSec == 0) {
            return 0.0f;
        }

        uint64_t playbackFrame = state.SamplesPlayed;
        if (!playingVoice.isStreaming) {
            const uint64_t startFrame = playingVoice.startFrame;
            if (playingVoice.loop && totalFrames > startFrame) {
                const uint64_t loopLength = totalFrames - startFrame;
                playbackFrame = startFrame + (playbackFrame % loopLength);
            } else if (playbackFrame > (std::numeric_limits<uint64_t>::max)() - startFrame) {
                playbackFrame = (std::numeric_limits<uint64_t>::max)();
            } else {
                playbackFrame += startFrame;
            }

            if (totalFrames > 0) {
                playbackFrame = (std::min)(playbackFrame, totalFrames);
            }
        }

        return static_cast<float>(playbackFrame) / static_cast<float>(format.nSamplesPerSec);
    }

    return 0.0f;
}

bool SoundManager::IsStreaming(uint32_t voiceHandle) const {
    const auto it = std::find_if(state_->playingVoices.begin(), state_->playingVoices.end(),
                                 [voiceHandle](const PlayingVoice& playingVoice) {
                                     return playingVoice.handle == voiceHandle;
                                 });
    return it != state_->playingVoices.end() && it->isStreaming;
}

void SoundManager::StopAll() {
    for (PlayingVoice& playingVoice : state_->playingVoices) {
        DestroyVoice(playingVoice);
    }
    state_->playingVoices.clear();
}

void SoundManager::Update() {
    for (auto it = state_->playingVoices.begin(); it != state_->playingVoices.end();) {
        if (it->isStreaming && it->voice) {
            ReleaseFinishedStreamBuffers(*it);

            XAUDIO2_VOICE_STATE state{};
            it->voice->GetState(&state, XAUDIO2_VOICE_NOSAMPLESPLAYED);
            while (!it->streamSourceEnded && state.BuffersQueued < kStreamQueuedBuffers) {
                if (!SubmitNextStreamBuffer(*it)) {
                    break;
                }
                it->voice->GetState(&state, XAUDIO2_VOICE_NOSAMPLESPLAYED);
            }
        }

        if (IsVoiceActive(*it)) {
            ++it;
            continue;
        }

        DestroyVoice(*it);
        it = state_->playingVoices.erase(it);
    }
}

bool SoundManager::IsPlaying(uint32_t voiceHandle) const {
    const auto it = std::find_if(state_->playingVoices.begin(), state_->playingVoices.end(),
                                 [voiceHandle](const PlayingVoice& playingVoice) {
                                     return playingVoice.handle == voiceHandle;
                                 });
    return it != state_->playingVoices.end() && IsVoiceActive(*it);
}

const SoundManager::SoundInfo* SoundManager::GetInfo(uint32_t soundId) const {
    if (soundId >= state_->sounds.size()) {
        return nullptr;
    }

    const AudioFileLoader::SoundData& sound = state_->sounds[soundId].data;
    if (sound.waveFormat.empty() || sound.decodedPcm.empty()) {
        return nullptr;
    }
    return &sound.info;
}

struct SoundManager::SourceVoiceCreateWork {
    uint32_t soundId = kInvalidSoundId;
    float volume = 0.0f;
    bool loop = false;
    float startSeconds = 0.0f;
    const AudioFileLoader::SoundData* sound = nullptr;
    SoundFormatUtils::AlignedWaveFormat alignedFormat{};
    const WAVEFORMATEX* format = nullptr;
    std::unique_ptr<SoundVoiceCallback> callback;
    IXAudio2SourceVoice* voice = nullptr;
    UINT32 totalFrames = 0;
    UINT32 startFrame = 0;
    float safeVolume = 0.0f;
};

bool SoundManager::InitializeSourceVoiceCreateWork(uint32_t soundId, float volume, bool loop,
                                                   float startSeconds,
                                                   SourceVoiceCreateWork& work) {
    work = {};
    work.soundId = soundId;
    work.volume = volume;
    work.loop = loop;
    work.startSeconds = startSeconds;
    work.sound = &state_->sounds[soundId].data;
    if (!SoundFormatUtils::CopyAlignedWaveFormat(work.sound->waveFormat, work.alignedFormat)) {
        return false;
    }
    work.format = work.alignedFormat.Get();
    if (work.format->nSamplesPerSec == 0 || work.format->nBlockAlign == 0 ||
        work.sound->decodedPcm.empty()) {
        return false;
    }
    if (work.sound->decodedPcm.size() > (std::numeric_limits<UINT32>::max)()) {
        return false;
    }

    try {
        work.callback = std::make_unique<SoundVoiceCallback>();
    } catch (const std::exception&) {
        return false;
    }
    return true;
}

bool SoundManager::CreateSourceVoiceObject(SourceVoiceCreateWork& work) {
    HRESULT hr =
        state_->xAudio2->CreateSourceVoice(&work.voice, work.format, XAUDIO2_VOICE_USEFILTER,
                                           XAUDIO2_DEFAULT_FREQ_RATIO, work.callback.get());
    return SUCCEEDED(hr);
}

bool SoundManager::BuildSourceVoiceBuffer(SourceVoiceCreateWork& work, XAUDIO2_BUFFER& buffer) {
    const size_t totalFramesSize =
        work.sound->decodedPcm.size() / static_cast<size_t>(work.format->nBlockAlign);
    if (totalFramesSize == 0 || totalFramesSize > (std::numeric_limits<UINT32>::max)()) {
        return false;
    }
    work.totalFrames = static_cast<UINT32>(totalFramesSize);
    work.safeVolume = Numeric::ClampFinite(work.volume, 0.0f, 1.0f, 0.0f);
    const float clampedStartSeconds =
        std::isfinite(work.startSeconds) ? (std::max)(work.startSeconds, 0.0f) : 0.0f;
    const double requestedStartFrame =
        static_cast<double>(clampedStartSeconds) * static_cast<double>(work.format->nSamplesPerSec);
    work.startFrame = !std::isfinite(requestedStartFrame) ||
                              requestedStartFrame >= static_cast<double>(work.totalFrames)
                          ? work.totalFrames - 1u
                          : static_cast<UINT32>(requestedStartFrame);

    buffer = {};
    buffer.pAudioData = work.sound->decodedPcm.data();
    buffer.AudioBytes = static_cast<UINT32>(work.sound->decodedPcm.size());
    buffer.Flags = XAUDIO2_END_OF_STREAM;
    buffer.PlayBegin = work.startFrame;
    buffer.PlayLength = work.totalFrames - work.startFrame;
    if (work.loop) {
        buffer.LoopBegin = work.startFrame;
        buffer.LoopLength = work.totalFrames - work.startFrame;
        buffer.LoopCount = XAUDIO2_LOOP_INFINITE;
    }
    return true;
}

uint32_t SoundManager::StoreAndStartSourceVoice(SourceVoiceCreateWork& work,
                                                const XAUDIO2_BUFFER& buffer) {
    SoundManagerInternal::SourceVoiceGuard voiceGuard(work.voice);
    HRESULT hr = work.voice->SubmitSourceBuffer(&buffer);
    if (FAILED(hr)) {
        return kInvalidVoiceHandle;
    }

    if (state_->nextVoiceHandle == kInvalidVoiceHandle) {
        state_->nextVoiceHandle = 1;
    }

    PlayingVoice playingVoice{};
    playingVoice.voice = voiceGuard.Get();
    playingVoice.callback = std::move(work.callback);
    playingVoice.handle = AllocateVoiceHandle();
    if (playingVoice.handle == kInvalidVoiceHandle) {
        return kInvalidVoiceHandle;
    }
    playingVoice.soundId = work.soundId;
    playingVoice.startFrame = work.startFrame;
    playingVoice.volume = work.safeVolume;
    playingVoice.loop = work.loop;
    try {
        state_->playingVoices.push_back(std::move(playingVoice));
    } catch (const std::exception&) {
        return kInvalidVoiceHandle;
    }
    voiceGuard.Release();
    PlayingVoice& storedVoice = state_->playingVoices.back();
    const uint32_t handle = storedVoice.handle;
    storedVoice.voice->SetVolume(work.safeVolume);

    hr = storedVoice.voice->Start();
    if (FAILED(hr)) {
        DestroyVoice(storedVoice);
        state_->playingVoices.pop_back();
        return kInvalidVoiceHandle;
    }

    return handle;
}

uint32_t SoundManager::CreateSourceVoice(uint32_t soundId, float volume, bool loop,
                                         float startSeconds) {
    SourceVoiceCreateWork work;
    XAUDIO2_BUFFER buffer{};
    if (!InitializeSourceVoiceCreateWork(soundId, volume, loop, startSeconds, work) ||
        !CreateSourceVoiceObject(work) || !BuildSourceVoiceBuffer(work, buffer)) {
        if (work.voice != nullptr) {
            work.voice->DestroyVoice();
        }
        return kInvalidVoiceHandle;
    }
    return StoreAndStartSourceVoice(work, buffer);
}

uint32_t SoundManager::AllocateVoiceHandle() {
    if (state_->playingVoices.size() >=
        static_cast<size_t>((std::numeric_limits<uint32_t>::max)()) - 1u) {
        return kInvalidVoiceHandle;
    }

    for (;;) {
        if (state_->nextVoiceHandle == kInvalidVoiceHandle) {
            state_->nextVoiceHandle = 1;
        }
        const uint32_t candidate = state_->nextVoiceHandle++;
        const auto it = std::find_if(
            state_->playingVoices.begin(), state_->playingVoices.end(),
            [candidate](const PlayingVoice& voice) { return voice.handle == candidate; });
        if (it == state_->playingVoices.end()) {
            return candidate;
        }
    }
}

void SoundManager::DestroyVoice(PlayingVoice& playingVoice) {
    if (!playingVoice.voice) {
        return;
    }

    playingVoice.voice->Stop(0);
    playingVoice.voice->FlushSourceBuffers();
    playingVoice.voice->DestroyVoice();
    playingVoice.voice = nullptr;
    playingVoice.streamBuffers.clear();
    playingVoice.outputMatrix.clear();
    playingVoice.streamReader.Reset();
}

bool SoundManager::IsVoiceActive(const PlayingVoice& playingVoice) {
    if (!playingVoice.voice) {
        return false;
    }
    if (playingVoice.isStreaming) {
        XAUDIO2_VOICE_STATE state{};
        playingVoice.voice->GetState(&state, XAUDIO2_VOICE_NOSAMPLESPLAYED);
        return !playingVoice.streamSourceEnded || state.BuffersQueued > 0;
    }
    if (playingVoice.loop) {
        return true;
    }
    if (playingVoice.callback && playingVoice.callback->IsEnded()) {
        return false;
    }

    XAUDIO2_VOICE_STATE state{};
    playingVoice.voice->GetState(&state, XAUDIO2_VOICE_NOSAMPLESPLAYED);
    return state.BuffersQueued > 0;
}
