#include "core/Numeric.h"
#include "internal/SoundFormatUtils.h"
#include "internal/SoundManagerInternal.h"
#include "sound/SoundManager.h"

#include <Objbase.h>
#include <mfapi.h>

namespace {

using SoundFormatUtils::MakeHResultMessage;

} // namespace

SoundManager::SoundManager() : state_(std::make_unique<State>()) {}

SoundManager::~SoundManager() {
    Finalize();
}

bool SoundManager::IsInitialized() const {
    return state_->xAudio2 != nullptr && state_->masterVoice != nullptr;
}

const std::string& SoundManager::GetLastInitializeError() const {
    return state_->lastInitializeError;
}

void SoundManager::Finalize() {
    SoundManager::StopAll();
    state_->sounds.clear();
    state_->pathToSoundId.clear();

    ShutdownAudioBackend();
    state_->lastInitializeError.clear();
    state_->nextVoiceHandle = 1;
    state_->listenerPosition = {0.0f, 0.0f, 0.0f};
    state_->listenerForward = {0.0f, 0.0f, 1.0f};
    state_->listenerUp = {0.0f, 1.0f, 0.0f};
}

void SoundManager::Initialize() {
    if (state_->xAudio2 && state_->masterVoice) {
        state_->lastInitializeError.clear();
        return;
    }
    if (state_->xAudio2 || state_->masterVoice || state_->mediaFoundationStarted ||
        state_->comInitialized) {
        Finalize();
    }
    state_->lastInitializeError.clear();

    if (!InitializeComAndMediaFoundation()) {
        return;
    }
    if (!InitializeXAudioBackend()) {
        ShutdownAudioBackend();
        return;
    }

    SetMasterVolume(state_->masterVolume);
    state_->lastInitializeError.clear();
}

bool SoundManager::InitializeComAndMediaFoundation() {
    const HRESULT coResult = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    if (SUCCEEDED(coResult)) {
        state_->comInitialized = true;
    } else if (coResult != RPC_E_CHANGED_MODE) {
        state_->lastInitializeError = MakeHResultMessage(coResult, "CoInitializeEx failed");
        return false;
    }

    const HRESULT mfResult = MFStartup(MF_VERSION);
    if (FAILED(mfResult)) {
        state_->lastInitializeError = MakeHResultMessage(mfResult, "MFStartup failed");
        ShutdownAudioBackend();
        return false;
    }
    state_->mediaFoundationStarted = true;
    return true;
}

bool SoundManager::InitializeXAudioBackend() {
    HRESULT audioResult = XAudio2Create(&state_->xAudio2, 0);
    if (SUCCEEDED(audioResult)) {
        audioResult = state_->xAudio2->CreateMasteringVoice(&state_->masterVoice);
    }
    if (FAILED(audioResult)) {
        state_->lastInitializeError =
            MakeHResultMessage(audioResult, "XAudio2 initialization failed");
        return false;
    }
    return true;
}

void SoundManager::ShutdownAudioBackend() {
    if (state_->masterVoice) {
        state_->masterVoice->DestroyVoice();
        state_->masterVoice = nullptr;
    }
    state_->xAudio2.Reset();

    if (state_->mediaFoundationStarted) {
        MFShutdown();
        state_->mediaFoundationStarted = false;
    }
    if (state_->comInitialized) {
        CoUninitialize();
        state_->comInitialized = false;
    }
}

void SoundManager::SetMasterVolume(float volume) {
    state_->masterVolume = Numeric::ClampFinite(volume, 0.0f, 1.0f, 0.0f);
    if (state_->masterVoice) {
        state_->masterVoice->SetVolume(state_->masterVolume);
    }
}

float SoundManager::GetMasterVolume() const {
    return state_->masterVolume;
}
