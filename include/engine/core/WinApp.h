#pragma once
#include <Windows.h>
#include <string>

class WinApp {
  public:
    /// <summary>
    /// デストラクタ
    /// </summary>
    ~WinApp() = default;

    /// <summary>
    /// ウィンドウおよびWin32アプリケーションの初期化を行う
    /// </summary>
    /// <param name="hInstance">アプリケーションインスタンスハンドル</param>
    /// <param name="nCmdShow">ウィンドウの表示状態</param>
    /// <param name="width">クライアント領域の幅</param>
    /// <param name="height">クライアント領域の高さ</param>
    /// <param name="title">ウィンドウタイトル</param>
    void Initialize(HINSTANCE hInstance, int nCmdShow, int width, int height,
                    const std::wstring &title);

    /// <summary>
    /// Windowsメッセージを処理する
    /// </summary>
    /// <returns>true: アプリケーション継続 / false: 終了要求あり</returns>
    bool ProcessMessage();

    // Getter
    int GetWidth() const { return width_; }
    int GetHeight() const { return height_; }
    HWND GetHwnd() const { return hwnd_; }

  private:
    // ウィンドウプロシージャ
    static LRESULT CALLBACK WindowProc(HWND hwnd, UINT msg, WPARAM wParam,
                                       LPARAM lParam);

  private:
    static constexpr const wchar_t *kClassName = L"WindowClass";

    int width_;
    int height_;

    HWND hwnd_ = nullptr;
};
