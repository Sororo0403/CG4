#pragma once
#define DIRECTINPUT_VERSION 0x0800
#include "input/InputReplayTypes.h"
#include <Windows.h>
#include <Xinput.h>
#include <array>
#include <dinput.h>
#include <string>
#include <vector>
#include <wrl.h>

/// <summary>
/// キーボードとマウスの入力状態を管理する
/// </summary>
class Input {
  public:
    using ReplayMode = InputReplayMode;
    using ReplayStartupOptions = InputReplayStartupOptions;

    ~Input();

    /// <summary>
    /// DirectInputのキーボードとマウス、XInputのゲームパッドを準備する
    /// </summary>
    /// <param name="hInstance">アプリケーションインスタンス</param>
    /// <param name="hwnd">入力を受け取るウィンドウハンドル</param>
    void Initialize(HINSTANCE hInstance, HWND hwnd);

    /// <summary>
    /// 各入力デバイスの現在状態を取得し、前フレーム状態と入れ替える
    /// </summary>
    /// <param name="deltaTime">前フレームからの経過時間</param>
    void Update(float deltaTime);

    /// <summary>
    /// 現在の入力をフレーム単位でJSONへ保存する録画を開始する
    /// </summary>
    bool StartRecording(const std::wstring &path, float fixedDeltaTime);

    /// <summary>
    /// JSONから読み込んだ入力フレームを実入力の代わりに再生する
    /// </summary>
    bool StartReplay(const std::wstring &path);

    /// <summary>
    /// 録画を保存して通常入力へ戻る
    /// </summary>
    bool StopRecording();

    /// <summary>
    /// 録画中なら現在までの入力フレームをファイルへ書き出す
    /// </summary>
    bool FinishRecording();

    /// <summary>
    /// 起動時の録画・再生設定をInputへ適用する
    /// </summary>
    bool ApplyReplayStartupOptions(const ReplayStartupOptions &options,
                                   float fixedDeltaTime);

    ReplayMode GetReplayMode() const { return replayMode_; }
    bool IsReplayFinished() const { return replayFinished_; }
    size_t GetReplayFrameIndex() const { return replayFrameIndex_; }
    size_t GetReplayFrameCount() const { return replayFrames_.size(); }
    const std::wstring &GetReplayPath() const { return replayPath_; }

    /// <summary>
    /// 指定キーが押下中かを判定する
    /// </summary>
    /// <param name="dik">DirectInputのキーコード</param>
    /// <returns>押下中ならtrue</returns>
    bool IsKeyPress(int dik) const;

    /// <summary>
    /// 指定キーがこのフレームで押されたかを判定する
    /// </summary>
    /// <param name="dik">DirectInputのキーコード</param>
    /// <returns>押下開始ならtrue</returns>
    bool IsKeyTrigger(int dik) const;

    /// <summary>
    /// 指定キーがこのフレームで離されたかを判定する
    /// </summary>
    /// <param name="dik">DirectInputのキーコード</param>
    /// <returns>離された瞬間ならtrue</returns>
    bool IsKeyRelease(int dik) const;

    /// <summary>
    /// マウスのX方向移動量を取得する
    /// </summary>
    /// <returns>X方向の移動量</returns>
    long GetMouseDX() const { return mouseState_.lX; }

    /// <summary>
    /// マウスのY方向移動量を取得する
    /// </summary>
    /// <returns>Y方向の移動量</returns>
    long GetMouseDY() const { return mouseState_.lY; }

    /// <summary>
    /// マウスホイールの移動量を取得する
    /// </summary>
    /// <returns>ホイールの移動量</returns>
    long GetMouseWheel() const { return mouseState_.lZ; }

    /// <summary>
    /// 指定マウスボタンが押下中かを判定する
    /// </summary>
    /// <param name="button">マウスボタン番号</param>
    /// <returns>押下中ならtrue</returns>
    bool IsMousePress(int button) const;

    /// <summary>
    /// 指定マウスボタンがこのフレームで押されたかを判定する
    /// </summary>
    /// <param name="button">マウスボタン番号</param>
    /// <returns>押下開始ならtrue</returns>
    bool IsMouseTrigger(int button) const;

    /// <summary>
    /// 指定マウスボタンがこのフレームで離されたかを判定する
    /// </summary>
    /// <param name="button">マウスボタン番号</param>
    /// <returns>離された瞬間ならtrue</returns>
    bool IsMouseRelease(int button) const;

    /// <summary>
    /// Xboxコントローラーが接続されているかを取得する
    /// </summary>
    bool IsGamepadConnected() const { return gamepadConnected_; }

    /// <summary>
    /// 指定ゲームパッドボタンが押下中かを判定する
    /// </summary>
    bool IsGamepadButtonPress(WORD button) const;

    /// <summary>
    /// 指定ゲームパッドボタンがこのフレームで押されたかを判定する
    /// </summary>
    bool IsGamepadButtonTrigger(WORD button) const;

    /// <summary>
    /// 指定ゲームパッドボタンがこのフレームで離されたかを判定する
    /// </summary>
    bool IsGamepadButtonRelease(WORD button) const;

    /// <summary>
    /// 左トリガーが閾値を超えた瞬間かを判定する
    /// </summary>
    bool IsGamepadLeftTriggerTrigger(float threshold = 0.2f) const;

    /// <summary>
    /// 右トリガーが閾値を超えた瞬間かを判定する
    /// </summary>
    bool IsGamepadRightTriggerTrigger(float threshold = 0.2f) const;

    /// <summary>
    /// 左スティックのX入力を取得する
    /// </summary>
    float GetGamepadLeftStickX() const { return gamepadLeftStickX_; }

    /// <summary>
    /// 左スティックのY入力を取得する
    /// </summary>
    float GetGamepadLeftStickY() const { return gamepadLeftStickY_; }

    /// <summary>
    /// 右スティックのX入力を取得する
    /// </summary>
    float GetGamepadRightStickX() const { return gamepadRightStickX_; }

    /// <summary>
    /// 右スティックのY入力を取得する
    /// </summary>
    float GetGamepadRightStickY() const { return gamepadRightStickY_; }

    /// <summary>
    /// 左トリガーの入力値を取得する
    /// </summary>
    float GetGamepadLeftTrigger() const { return gamepadLeftTrigger_; }

    /// <summary>
    /// 右トリガーの入力値を取得する
    /// </summary>
    float GetGamepadRightTrigger() const { return gamepadRightTrigger_; }

  private:
    /// <summary>
    /// DirectInputからキーボードの現在状態を読み込む
    /// </summary>
    void UpdateKeyboard();

    /// <summary>
    /// DirectInputからマウス移動量とボタン状態を読み込む
    /// </summary>
    void UpdateMouse();

    /// <summary>
    /// XInputからゲームパッドの接続状態と各入力値を読み込む
    /// </summary>
    void UpdateGamepad();

    struct InputFrame {
        std::array<BYTE, 256> keys{};
        DIMOUSESTATE mouse{};
        bool gamepadConnected = false;
        WORD gamepadButtons = 0;
        float gamepadLeftStickX = 0.0f;
        float gamepadLeftStickY = 0.0f;
        float gamepadRightStickX = 0.0f;
        float gamepadRightStickY = 0.0f;
        float gamepadLeftTrigger = 0.0f;
        float gamepadRightTrigger = 0.0f;
    };

    InputFrame CaptureFrame() const;
    void ApplyReplayFrame(const InputFrame &frame);
    void UpdateReplayHotkeys(float fixedDeltaTime);
    std::wstring MakeAutoReplayPath() const;
    bool SaveRecording() const;
    bool LoadReplay(const std::wstring &path);

  private:
    static constexpr BYTE kPressMask = 0x80;

    Microsoft::WRL::ComPtr<IDirectInput8> directInput_;
    Microsoft::WRL::ComPtr<IDirectInputDevice8> keyboard_;
    Microsoft::WRL::ComPtr<IDirectInputDevice8> mouse_;

    std::array<BYTE, 256> keyNow_{};
    std::array<BYTE, 256> keyPrev_{};

    DIMOUSESTATE mouseState_{};
    DIMOUSESTATE mousePrevState_{};

    XINPUT_STATE gamepadState_{};
    XINPUT_STATE gamepadPrevState_{};
    bool gamepadConnected_ = false;
    float gamepadLeftStickX_ = 0.0f;
    float gamepadLeftStickY_ = 0.0f;
    float gamepadRightStickX_ = 0.0f;
    float gamepadRightStickY_ = 0.0f;
    float gamepadLeftTrigger_ = 0.0f;
    float gamepadRightTrigger_ = 0.0f;

    ReplayMode replayMode_ = ReplayMode::Live;
    std::wstring replayPath_;
    float replayFixedDeltaTime_ = 0.0f;
    std::vector<InputFrame> recordedFrames_;
    std::vector<InputFrame> replayFrames_;
    size_t replayFrameIndex_ = 0;
    bool replayFinished_ = false;
    bool recordingDirty_ = false;
    std::wstring replayDirectory_;
    bool replayHotkeysEnabled_ = true;
};
