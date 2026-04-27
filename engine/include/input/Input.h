#pragma once
#define DIRECTINPUT_VERSION 0x0800
#include <Windows.h>
#include <array>
#include <dinput.h>
#include <wrl.h>

/// <summary>
/// キーボードとマウスの入力状態を管理する
/// </summary>
class Input {
  public:
    /// <summary>
    /// 入力デバイスを初期化する
    /// </summary>
    /// <param name="hInstance">アプリケーションインスタンス</param>
    /// <param name="hwnd">入力を受け取るウィンドウハンドル</param>
    void Initialize(HINSTANCE hInstance, HWND hwnd);

    /// <summary>
    /// キーボードとマウスの入力状態を更新する
    /// </summary>
    /// <param name="deltaTime">前フレームからの経過時間</param>
    void Update(float deltaTime);

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

  private:
    /// <summary>
    /// キーボード状態を更新する
    /// </summary>
    void UpdateKeyboard();

    /// <summary>
    /// マウス状態を更新する
    /// </summary>
    void UpdateMouse();

  private:
    static constexpr BYTE kPressMask = 0x80;

    Microsoft::WRL::ComPtr<IDirectInput8> directInput_;
    Microsoft::WRL::ComPtr<IDirectInputDevice8> keyboard_;
    Microsoft::WRL::ComPtr<IDirectInputDevice8> mouse_;

    std::array<BYTE, 256> keyNow_{};
    std::array<BYTE, 256> keyPrev_{};

    DIMOUSESTATE mouseState_{};
    DIMOUSESTATE mousePrevState_{};
};
