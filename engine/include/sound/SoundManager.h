#pragma once
#include "core/ResourceHandle.h"
#include "sound/AudioFileLoader.h"
#include "sound/ISoundService.h"

#include <DirectXMath.h>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

/// <summary>
/// Media Foundationで音声を読み込み、XAudio2で再生を管理する
/// </summary>
class SoundManager : public ISoundService {
public:
    using SoundInfo = AudioFileLoader::SoundData::Info;

    /// <summary>
    /// 音声リソースとMedia Foundationを破棄する
    /// </summary>
    SoundManager();
    ~SoundManager() override;

    /// <summary>
    /// Media Foundation、COM、XAudio2エンジンとマスターボイスを初期化する
    /// </summary>
    void Initialize();
    void Finalize();
    bool IsInitialized() const;
    const std::string& GetLastInitializeError() const;

    /// <summary>
    /// 音声ファイルを読み込み、再利用できる音声IDとして登録する
    /// </summary>
    /// <param name="path">読み込む音声のファイルパス</param>
    /// <returns>音声id</returns>
    uint32_t Load(const std::wstring& path);
    SoundHandle LoadHandle(const std::wstring& path) {
        return SoundHandle(Load(path));
    }

    /// <summary>
    /// 音声ファイルを読み込み、失敗時はfalseを返す
    /// </summary>
    bool TryLoad(const std::wstring& path, uint32_t& soundId);
    bool TryLoadHandle(const std::wstring& path, SoundHandle& sound) {
        uint32_t soundId = kInvalidSoundId;
        const bool loaded = TryLoad(path, soundId);
        sound = SoundHandle(soundId);
        return loaded;
    }

    /// <summary>
    /// 読み込みに失敗した場合、無音データを登録して返す
    /// </summary>
    uint32_t LoadOrCreateSilent(const std::wstring& path);
    SoundHandle LoadOrCreateSilentHandle(const std::wstring& path) {
        return SoundHandle(LoadOrCreateSilent(path));
    }

    /// <summary>
    /// メモリ上で生成した16bit PCM音声を登録する
    /// </summary>
    uint32_t CreatePcm16Sound(const std::wstring& cacheKey, const std::vector<int16_t>& pcmSamples,
                              uint32_t sampleRate = 48000, uint16_t channels = 1) override;
    uint32_t FindPcm16Sound(const std::wstring& cacheKey) const override;
    bool RemoveSound(uint32_t soundId);
    SoundHandle CreatePcm16SoundHandle(const std::wstring& cacheKey,
                                       const std::vector<int16_t>& pcmSamples,
                                       uint32_t sampleRate = 48000, uint16_t channels = 1) {
        return SoundHandle(CreatePcm16Sound(cacheKey, pcmSamples, sampleRate, channels));
    }

    /// <summary>
    /// 登録済み音声IDのデコード済みデータをソースボイスで再生する
    /// </summary>
    /// <param name="soundId">再生する音声id</param>
    /// <param name="volume">この再生だけに適用する音量</param>
    /// <param name="loop">末尾まで到達したら先頭へ戻すか</param>
    /// <returns>再生中のボイスを操作するためのハンドル</returns>
    /// <summary>
    /// Playを実行する
    /// </summary>
    uint32_t Play(uint32_t soundId, float volume = 1.0f, bool loop = false) override;
    VoiceHandle Play(SoundHandle sound, float volume = 1.0f, bool loop = false) {
        return VoiceHandle(Play(sound.Get(), volume, loop));
    }

    /// <summary>
    /// 登録済み音声IDのデコード済みデータを指定秒から再生する
    /// </summary>
    uint32_t PlayFrom(uint32_t soundId, float startSeconds, float volume = 1.0f,
                      bool loop = false) override;
    VoiceHandle PlayFrom(SoundHandle sound, float startSeconds, float volume = 1.0f,
                         bool loop = false) {
        return VoiceHandle(PlayFrom(sound.Get(), startSeconds, volume, loop));
    }

    /// <summary>
    /// 3D位置をもつ音声として再生する
    /// </summary>
    uint32_t Play3D(uint32_t soundId, const DirectX::XMFLOAT3& sourcePosition, float volume = 1.0f,
                    bool loop = false) override;
    VoiceHandle Play3D(SoundHandle sound, const DirectX::XMFLOAT3& sourcePosition,
                       float volume = 1.0f, bool loop = false) {
        return VoiceHandle(Play3D(sound.Get(), sourcePosition, volume, loop));
    }

    /// <summary>
    /// ストリーミング再生用のボイスとして音声ファイルを再生する
    /// </summary>
    uint32_t PlayStream(const std::wstring& path, float volume = 1.0f, bool loop = false);
    VoiceHandle PlayStreamHandle(const std::wstring& path, float volume = 1.0f, bool loop = false) {
        return VoiceHandle(PlayStream(path, volume, loop));
    }

    /// <summary>
    /// 指定した再生中ボイスを停止する
    /// </summary>
    void Stop(uint32_t voiceHandle) override;
    void Stop(VoiceHandle voice) {
        Stop(voice.Get());
    }

    /// <summary>
    /// 指定した再生中ボイスを一時停止する
    /// </summary>
    void Pause(uint32_t voiceHandle);
    void Pause(VoiceHandle voice) {
        Pause(voice.Get());
    }

    /// <summary>
    /// 一時停止したボイスを再開する
    /// </summary>
    void Resume(uint32_t voiceHandle);
    void Resume(VoiceHandle voice) {
        Resume(voice.Get());
    }

    /// <summary>
    /// 指定した再生中ボイスの音量を設定する
    /// </summary>
    void SetVoiceVolume(uint32_t voiceHandle, float volume) override;
    void SetVoiceVolume(VoiceHandle voice, float volume) {
        SetVoiceVolume(voice.Get(), volume);
    }

    /// <summary>
    /// 指定した再生中ボイスの音量を取得する
    /// </summary>
    float GetVoiceVolume(uint32_t voiceHandle) const;
    float GetVoiceVolume(VoiceHandle voice) const {
        return GetVoiceVolume(voice.Get());
    }

    /// <summary>
    /// 指定した再生中ボイスの再生位置を秒単位で取得する
    /// </summary>
    float GetPlaybackPosition(uint32_t voiceHandle) const;
    float GetPlaybackPosition(VoiceHandle voice) const {
        return GetPlaybackPosition(voice.Get());
    }

    /// <summary>
    /// 指定した再生中ボイスの周波数比率（ピッチ）を設定する
    /// </summary>
    void SetVoiceFrequencyRatio(uint32_t voiceHandle, float frequencyRatio) override;
    void SetVoiceFrequencyRatio(VoiceHandle voice, float frequencyRatio) {
        SetVoiceFrequencyRatio(voice.Get(), frequencyRatio);
    }

    /// <summary>
    /// 指定した再生中ボイスの周波数比率（ピッチ）を取得する
    /// </summary>
    float GetVoiceFrequencyRatio(uint32_t voiceHandle) const;
    float GetVoiceFrequencyRatio(VoiceHandle voice) const {
        return GetVoiceFrequencyRatio(voice.Get());
    }

    /// <summary>
    /// 指定した再生中ボイスのローパスカットオフ周波数を設定する
    /// </summary>
    void SetVoiceLowPassCutoff(uint32_t voiceHandle, float cutoffHz);
    void SetVoiceLowPassCutoff(VoiceHandle voice, float cutoffHz) {
        SetVoiceLowPassCutoff(voice.Get(), cutoffHz);
    }

    /// <summary>
    /// 指定した再生中ボイスのローパスカットオフ周波数を取得する
    /// </summary>
    float GetVoiceLowPassCutoff(uint32_t voiceHandle) const;
    float GetVoiceLowPassCutoff(VoiceHandle voice) const {
        return GetVoiceLowPassCutoff(voice.Get());
    }

    /// <summary>
    /// 3Dサウンドのリスナー位置と向きを設定する
    /// </summary>
    void SetListener(const DirectX::XMFLOAT3& position, const DirectX::XMFLOAT3& forward,
                     const DirectX::XMFLOAT3& up = {0.0f, 1.0f, 0.0f}) override;

    /// <summary>
    /// 3Dボイスの音源位置を更新する
    /// </summary>
    void SetVoicePosition(uint32_t voiceHandle, const DirectX::XMFLOAT3& sourcePosition) override;
    void SetVoicePosition(VoiceHandle voice, const DirectX::XMFLOAT3& sourcePosition) {
        SetVoicePosition(voice.Get(), sourcePosition);
    }

    /// <summary>
    /// 3Dボイスの距離減衰範囲を設定する
    /// </summary>
    void SetVoice3DRange(uint32_t voiceHandle, float minDistance, float maxDistance) override;
    void SetVoice3DRange(VoiceHandle voice, float minDistance, float maxDistance) {
        SetVoice3DRange(voice.Get(), minDistance, maxDistance);
    }

    /// <summary>
    /// 指定ボイスがストリーミング再生として作成されたかを取得する
    /// </summary>
    bool IsStreaming(uint32_t voiceHandle) const;
    bool IsStreaming(VoiceHandle voice) const {
        return IsStreaming(voice.Get());
    }

    /// <summary>
    /// すべての再生中ボイスを停止する
    /// </summary>
    void StopAll() override;

    /// <summary>
    /// 再生済みボイスを破棄する
    /// </summary>
    void Update();

    /// <summary>
    /// 指定したボイスが再生中かを取得する
    /// </summary>
    bool IsPlaying(uint32_t voiceHandle) const override;
    bool IsPlaying(VoiceHandle voice) const {
        return IsPlaying(voice.Get());
    }

    /// <summary>
    /// 読み込み済み音声の情報を取得する
    /// </summary>
    const SoundInfo* GetInfo(uint32_t soundId) const override;
    const SoundInfo* GetInfo(SoundHandle sound) const {
        return GetInfo(sound.Get());
    }

    /// <summary>
    /// 指定秒付近のPCM振幅を0..1で取得する
    /// </summary>
    float GetAmplitudeAt(uint32_t soundId, float playbackSeconds,
                         float windowSeconds = 0.045f) const;
    float GetAmplitudeAt(SoundHandle sound, float playbackSeconds,
                         float windowSeconds = 0.045f) const {
        return GetAmplitudeAt(sound.Get(), playbackSeconds, windowSeconds);
    }

    /// <summary>
    /// 指定秒付近のPCMから簡易スペクトラムを0..1で取得する
    /// </summary>
    void FillSpectrumBands(uint32_t soundId, float playbackSeconds, float* outBands,
                           size_t bandCount) const;
    void FillSpectrumBands(SoundHandle sound, float playbackSeconds, float* outBands,
                           size_t bandCount) const {
        FillSpectrumBands(sound.Get(), playbackSeconds, outBands, bandCount);
    }

    /// <summary>
    /// マスター音量を設定する
    /// </summary>
    void SetMasterVolume(float volume);

    /// <summary>
    /// マスター音量を取得する
    /// </summary>
    float GetMasterVolume() const;

private:
    struct PlayingVoice;
    struct SourceVoiceCreateWork;
    struct SoundResource;
    struct State;

    uint32_t CreateSourceVoice(uint32_t soundId, float volume, bool loop,
                               float startSeconds = 0.0f);
    bool InitializeSourceVoiceCreateWork(uint32_t soundId, float volume, bool loop,
                                         float startSeconds, SourceVoiceCreateWork& work);
    bool CreateSourceVoiceObject(SourceVoiceCreateWork& work);
    bool BuildSourceVoiceBuffer(SourceVoiceCreateWork& work, XAUDIO2_BUFFER& buffer);
    uint32_t StoreAndStartSourceVoice(SourceVoiceCreateWork& work, const XAUDIO2_BUFFER& buffer);
    uint32_t CreateStreamingVoice(const std::wstring& path, float volume, bool loop);
    /// <summary>
    /// SubmitNextStreamBufferを実行する
    /// </summary>
    static bool SubmitNextStreamBuffer(PlayingVoice& playingVoice);
    static bool ReadNextStreamPcm(PlayingVoice& playingVoice, bool& reachedEnd,
                                  std::vector<BYTE>& pcm);
    static bool AppendStreamBuffer(PlayingVoice& playingVoice, std::vector<BYTE>&& pcm,
                                   bool endAfterBuffer);
    static void ReleaseFinishedStreamBuffers(PlayingVoice& playingVoice);
    void Apply3D(PlayingVoice& playingVoice);
    static bool ApplyVoiceLowPass(PlayingVoice& playingVoice, float cutoffHz);
    uint32_t CreateSilentSound(const std::wstring& cacheKey, uint32_t sampleRate = 48000,
                               uint16_t channels = 1, uint16_t bitsPerSample = 16,
                               float durationSeconds = 0.05f);
    /// <summary>
    /// DestroyVoiceを実行する
    /// </summary>
    static void DestroyVoice(PlayingVoice& playingVoice);
    static bool IsVoiceActive(const PlayingVoice& playingVoice);
    bool InitializeComAndMediaFoundation();
    bool InitializeXAudioBackend();
    void ShutdownAudioBackend();

    uint32_t AppendSoundResource(SoundResource resource);
    uint32_t AllocateVoiceHandle();

    std::unique_ptr<State> state_;
};
