#pragma once
#include "sound/AudioFileLoader.h"
#include "sound/SoundVoiceCallback.h"
#include <DirectXMath.h>
#include <cstdint>
#include <deque>
#include <mfidl.h>
#include <mfreadwrite.h>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>
#include <wrl.h>
#include <xaudio2.h>

/// <summary>
/// Media Foundationで音声を読み込み、XAudio2で再生を管理する
/// </summary>
class SoundManager {
  public:
    static constexpr uint32_t kInvalidVoiceHandle = UINT32_MAX;
    static constexpr uint32_t kInvalidSoundId = UINT32_MAX;

    using SoundInfo = AudioFileLoader::SoundData::Info;

    /// <summary>
    /// SoundManagerの共有インスタンスを取得する
    /// </summary>
    static SoundManager &GetInstance();

    /// <summary>
    /// 音声リソースとMedia Foundationを破棄する
    /// </summary>
    ~SoundManager();

    /// <summary>
    /// Media Foundation、COM、XAudio2エンジンとマスターボイスを初期化する
    /// </summary>
    void Initialize();

    /// <summary>
    /// 音声ファイルを読み込み、再利用できる音声IDとして登録する
    /// </summary>
    /// <param name="path">読み込む音声のファイルパス</param>
    /// <returns>音声id</returns>
    uint32_t Load(const std::wstring &path);

    /// <summary>
    /// 音声ファイルを読み込み、失敗時はfalseを返す
    /// </summary>
    bool TryLoad(const std::wstring &path, uint32_t &soundId);

    /// <summary>
    /// 読み込みに失敗した場合、無音データを登録して返す
    /// </summary>
    uint32_t LoadOrCreateSilent(const std::wstring &path);

    /// <summary>
    /// 登録済み音声IDのデコード済みデータをソースボイスで再生する
    /// </summary>
    /// <param name="soundId">再生する音声id</param>
    /// <param name="volume">この再生だけに適用する音量</param>
    /// <param name="loop">末尾まで到達したら先頭へ戻すか</param>
    /// <returns>再生中のボイスを操作するためのハンドル</returns>
    uint32_t Play(uint32_t soundId, float volume = 1.0f, bool loop = false);

    /// <summary>
    /// 3D位置をもつ音声として再生する
    /// </summary>
    uint32_t Play3D(uint32_t soundId,
                    const DirectX::XMFLOAT3 &sourcePosition,
                    float volume = 1.0f, bool loop = false);

    /// <summary>
    /// ストリーミング再生用のボイスとして音声ファイルを再生する
    /// </summary>
    uint32_t PlayStream(const std::wstring &path, float volume = 1.0f,
                        bool loop = false);

    /// <summary>
    /// 指定した再生中ボイスを停止する
    /// </summary>
    void Stop(uint32_t voiceHandle);

    /// <summary>
    /// 指定した再生中ボイスの音量を設定する
    /// </summary>
    void SetVoiceVolume(uint32_t voiceHandle, float volume);

    /// <summary>
    /// 指定した再生中ボイスの音量を取得する
    /// </summary>
    float GetVoiceVolume(uint32_t voiceHandle) const;

    /// <summary>
    /// 指定した再生中ボイスの再生位置を秒単位で取得する
    /// </summary>
    float GetPlaybackPosition(uint32_t voiceHandle) const;

    /// <summary>
    /// 3Dサウンドのリスナー位置と向きを設定する
    /// </summary>
    void SetListener(const DirectX::XMFLOAT3 &position,
                     const DirectX::XMFLOAT3 &forward,
                     const DirectX::XMFLOAT3 &up = {0.0f, 1.0f, 0.0f});

    /// <summary>
    /// 3Dボイスの音源位置を更新する
    /// </summary>
    void SetVoicePosition(uint32_t voiceHandle,
                          const DirectX::XMFLOAT3 &sourcePosition);

    /// <summary>
    /// 3Dボイスの距離減衰範囲を設定する
    /// </summary>
    void SetVoice3DRange(uint32_t voiceHandle, float minDistance,
                         float maxDistance);

    /// <summary>
    /// 指定ボイスがストリーミング再生として作成されたかを取得する
    /// </summary>
    bool IsStreaming(uint32_t voiceHandle) const;

    /// <summary>
    /// すべての再生中ボイスを停止する
    /// </summary>
    void StopAll();

    /// <summary>
    /// 再生済みボイスを破棄する
    /// </summary>
    void Update();

    /// <summary>
    /// 指定したボイスが再生中かを取得する
    /// </summary>
    bool IsPlaying(uint32_t voiceHandle) const;

    /// <summary>
    /// 読み込み済み音声の情報を取得する
    /// </summary>
    const SoundInfo *GetInfo(uint32_t soundId) const;

    /// <summary>
    /// マスター音量を設定する
    /// </summary>
    void SetMasterVolume(float volume);

    /// <summary>
    /// マスター音量を取得する
    /// </summary>
    float GetMasterVolume() const { return masterVolume_; }

  private:
    struct PlayingVoice {
        IXAudio2SourceVoice *voice = nullptr;
        std::unique_ptr<SoundVoiceCallback> callback;
        uint32_t handle = kInvalidVoiceHandle;
        uint32_t soundId = 0;
        float volume = 1.0f;
        bool loop = false;
        bool is3D = false;
        bool isStreaming = false;
        bool streamSourceEnded = false;
        std::vector<BYTE> streamWaveFormat;
        std::deque<std::vector<BYTE>> streamBuffers;
        Microsoft::WRL::ComPtr<IMFSourceReader> streamReader;
        DirectX::XMFLOAT3 position{0.0f, 0.0f, 0.0f};
        float minDistance = 1.0f;
        float maxDistance = 30.0f;
    };

    uint32_t CreateSourceVoice(uint32_t soundId, float volume, bool loop);
    uint32_t CreateStreamingVoice(const std::wstring &path, float volume,
                                  bool loop);
    bool SubmitNextStreamBuffer(PlayingVoice &playingVoice);
    void ReleaseFinishedStreamBuffers(PlayingVoice &playingVoice);
    void Apply3D(PlayingVoice &playingVoice);
    uint32_t CreateSilentSound(const std::wstring &cacheKey,
                               uint32_t sampleRate = 48000,
                               uint16_t channels = 1,
                               uint16_t bitsPerSample = 16,
                               float durationSeconds = 0.05f);
    void DestroyVoice(PlayingVoice &playingVoice);
    bool IsVoiceActive(const PlayingVoice &playingVoice) const;

  private:
    Microsoft::WRL::ComPtr<IXAudio2> xAudio2_;
    IXAudio2MasteringVoice *masterVoice_ = nullptr;
    float masterVolume_ = 1.0f;
    bool comInitialized_ = false;
    bool mediaFoundationStarted_ = false;

    /// <summary>
    /// 読み込み済み音声1件分のデータ
    /// </summary>
    struct SoundResource {
        AudioFileLoader::SoundData data;
    };

    std::vector<SoundResource> sounds_;
    std::vector<PlayingVoice> playingVoices_;
    std::unordered_map<std::wstring, uint32_t> pathToSoundId_;
    uint32_t nextVoiceHandle_ = 1;
    DirectX::XMFLOAT3 listenerPosition_{0.0f, 0.0f, 0.0f};
    DirectX::XMFLOAT3 listenerForward_{0.0f, 0.0f, 1.0f};
    DirectX::XMFLOAT3 listenerUp_{0.0f, 1.0f, 0.0f};
};
