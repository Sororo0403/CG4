#pragma once
#include <xaudio2.h>

#ifdef _WIN32
#include <Windows.h>
#endif

/// <summary>
/// XAudio2 の再生コールバックを処理する
/// </summary>
class SoundVoiceCallback : public IXAudio2VoiceCallback {
  public:
    /// <summary>
    /// バッファ再生終了時にソースボイスを破棄する
    /// </summary>
    /// <param name="context">再生終了したソースボイス</param>
    void STDMETHODCALLTYPE OnBufferEnd(void *context) override {
#ifdef _WIN32
        if (!context) {
            OutputDebugStringA("[SoundVoiceCallback] OnBufferEnd context=nullptr\n");
        }
#endif
        auto *voice = reinterpret_cast<IXAudio2SourceVoice *>(context);
        if (voice) {
            voice->DestroyVoice();
        }
    }

    /// <summary>
    /// ストリーム終了通知を受け取る
    /// </summary>
    void STDMETHODCALLTYPE OnStreamEnd() override {}

    /// <summary>
    /// 音声処理パス終了通知を受け取る
    /// </summary>
    void STDMETHODCALLTYPE OnVoiceProcessingPassEnd() override {}

    /// <summary>
    /// 音声処理パス開始通知を受け取る
    /// </summary>
    void STDMETHODCALLTYPE OnVoiceProcessingPassStart(UINT32) override {}

    /// <summary>
    /// バッファ再生開始通知を受け取る
    /// </summary>
    void STDMETHODCALLTYPE OnBufferStart(void *) override {}

    /// <summary>
    /// ループ再生終了通知を受け取る
    /// </summary>
    void STDMETHODCALLTYPE OnLoopEnd(void *) override {}

    /// <summary>
    /// 音声再生エラー通知を受け取る
    /// </summary>
    void STDMETHODCALLTYPE OnVoiceError(void *, HRESULT) override {}
};
