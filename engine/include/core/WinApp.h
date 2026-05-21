#pragma once
#include <Windows.h>
#include <string>

/// <summary>
/// Win32ウィンドウの生成とメッセージ処理を管理する
/// </summary>
class WinApp {
  public:
    /// <summary>
    /// Win32ウィンドウ管理オブジェクトを破棄する
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
    /// <param name="fullscreen">起動時にボーダーレス全画面にする場合はtrue。</param>
    void Initialize(HINSTANCE hInstance, int nCmdShow, int width, int height,
                    const std::wstring &title, bool fullscreen = false);

    /// <summary>
    /// Windowsメッセージを処理する
    /// </summary>
    /// <returns>true: アプリケーション継続 / false: 終了要求あり</returns>
    bool ProcessMessage();

    /// <summary>
    /// 次のメッセージ処理でアプリケーションを終了するよう要求する。
    /// </summary>
    void RequestClose();

    /// <summary>
    /// ウィンドウ上のOSマウスカーソル表示を切り替える。
    /// </summary>
    /// <param name="visible">表示する場合はtrue、隠す場合はfalse。</param>
    void SetCursorVisible(bool visible);

    /// <summary>
    /// クライアント領域の幅を取得する
    /// </summary>
    /// <returns>クライアント領域の幅</returns>
    int GetWidth() const;

    /// <summary>
    /// クライアント領域の高さを取得する
    /// </summary>
    /// <returns>クライアント領域の高さ</returns>
    int GetHeight() const;

    /// <summary>
    /// ウィンドウハンドルを取得する
    /// </summary>
    /// <returns>ウィンドウハンドル</returns>
    HWND GetHwnd() const { return hwnd_; }

  private:
    /// <summary>
    /// 現在のクライアント領域サイズを更新する
    /// </summary>
    void UpdateClientSize();

  private:
    /// <summary>
    /// ウィンドウプロシージャ
    /// </summary>
    static LRESULT CALLBACK WindowProc(HWND hwnd, UINT msg, WPARAM wParam,
                                       LPARAM lParam);

  private:
    static constexpr const wchar_t *kClassName = L"WindowClass";
    static bool cursorVisible_;

    int width_;
    int height_;

    HWND hwnd_ = nullptr;
};
