#pragma once
#include <atomic>
#include <xaudio2.h>

/// <summary>
/// XAudio2 の再生コールバックを受ける
/// </summary>
class SoundVoiceCallback : public IXAudio2VoiceCallback {
  public:
    bool IsEnded() const {
        return streamEnded_.load(std::memory_order_acquire) ||
               endedBufferCount_.load(std::memory_order_acquire) > 0;
    }

    uint32_t ConsumeEndedBufferCount() {
        return endedBufferCount_.exchange(0, std::memory_order_acq_rel);
    }

    void Reset() {
        endedBufferCount_.store(0, std::memory_order_release);
        streamEnded_.store(false, std::memory_order_release);
    }

    /// <summary>
    /// バッファ再生終了通知を受け取る
    /// </summary>
    void STDMETHODCALLTYPE OnBufferEnd(void *) override {
        endedBufferCount_.fetch_add(1, std::memory_order_acq_rel);
    }

    /// <summary>
    /// ストリーム終了通知を受け取る
    /// </summary>
    void STDMETHODCALLTYPE OnStreamEnd() override {
        streamEnded_.store(true, std::memory_order_release);
    }

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
    void STDMETHODCALLTYPE OnVoiceError(void *, HRESULT) override {
        streamEnded_.store(true, std::memory_order_release);
    }

  private:
    std::atomic_uint32_t endedBufferCount_{0};
    std::atomic_bool streamEnded_{false};
};
