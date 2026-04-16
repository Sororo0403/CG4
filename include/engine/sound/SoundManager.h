#pragma once
#include "SoundVoiceCallback.h"
#include <string>
#include <vector>
#include <wrl.h>
#include <xaudio2.h>

struct WavData {
    WAVEFORMATEX format{};
    std::vector<BYTE> buffer;
};

class SoundManager {
  public:
    /// <summary>
    /// デストラクタ
    /// </summary>
    ~SoundManager();

    /// <summary>
    /// 初期化処理
    /// </summary>
    void Initialize();

    /// <summary>
    /// 音声を読み込む
    /// </summary>
    /// <param name="path">読み込む音声のファイルパス</param>
    /// <returns>音声id</returns>
    uint32_t Load(const std::wstring &path);

    /// <summary>
    /// 音声を再生する
    /// </summary>
    /// <param name="soundId">再生する音声id</param>
    void Play(uint32_t soundId);

  private:
    /// <summary>
    /// WAVファイルを読み込み
    /// </summary>
    /// <param name="path">読み込むWAVファイルへのファイルパス</param>
    /// <returns>読み込んだWavData</returns>
    static WavData LoadWavPcm16(const std::wstring &path);

  private:
    Microsoft::WRL::ComPtr<IXAudio2> xAudio2_;
    IXAudio2MasteringVoice *masterVoice_ = nullptr;

    struct SoundResource {
        WavData wav;
    };

    std::vector<SoundResource> sounds_;

    SoundVoiceCallback voiceCallback_;
};
