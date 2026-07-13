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
    ~WinApp();

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
                    const std::wstring& title, bool fullscreen = false);

    /// <summary>
    /// Windowsメッセージを処理する
    /// </summary>
    /// <returns>true: アプリケーション継続 / false: 終了要求あり</returns>
    bool ProcessMessage() const;

    /// <summary>
    /// 次のメッセージ処理でアプリケーションを終了するよう要求する。
    /// </summary>
    void RequestClose();

    /// <summary>
    /// ウィンドウ上のOSマウスカーソル表示を切り替える。
    /// </summary>
    /// <param name="visible">表示する場合はtrue、隠す場合はfalse。</param>
    void SetCursorVisible(bool visible) const;

    /// <summary>
    /// ボーダーレス全画面と通常ウィンドウを切り替える。
    /// </summary>
    void SetFullscreen(bool fullscreen);

    /// <summary>
    /// 現在ボーダーレス全画面で表示している場合はtrue。
    /// </summary>
    bool IsFullscreen() const {
        return fullscreen_;
    }

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
    HWND GetHwnd() const {
        return hwnd_;
    }

private:
    /// <summary>
    /// 現在のクライアント領域サイズを更新する
    /// </summary>
    void UpdateClientSize() const;

private:
    /// <summary>
    /// ウィンドウプロシージャ
    /// </summary>
    static LRESULT CALLBACK WindowProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
    static bool TryHandleWindowMessage(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam,
                                       LRESULT& result);
    static LRESULT HandleSetCursorMessage(HWND hwnd, WPARAM, LPARAM lParam, bool& handled);
    static LRESULT HandleActivateAppMessage(HWND, WPARAM wParam, LPARAM, bool& handled);
    static LRESULT HandleFocusMessage(HWND, WPARAM, LPARAM, bool& handled);
    static LRESULT HandleKillFocusMessage(HWND, WPARAM, LPARAM, bool& handled);
    static LRESULT HandleKeyDownMessage(HWND, WPARAM wParam, LPARAM, bool& handled);
    static LRESULT HandleSysCommandMessage(HWND, WPARAM wParam, LPARAM, bool& handled);
    static LRESULT HandleDestroyMessage(HWND hwnd, WPARAM, LPARAM, bool& handled);
    static void ApplyHiddenCursorState(HWND hwnd, bool lockToClient);
    static void ApplyVisibleCursorState();
    static void ApplyRequestedCursorState(HWND hwnd);
    static void LockCursorToClient(HWND hwnd);
    static void CenterCursorInClient(HWND hwnd);
    static void ReleaseCursorLock();
    static bool ShouldLockHiddenCursor(HWND hwnd);
    static bool ShouldHideCursor(HWND hwnd);
    /// <summary>
    /// RestoreCursorForAppInteractionを実行する
    /// </summary>
    static void RestoreCursorForAppInteraction();

private:
    static constexpr const wchar_t* kClassName = L"WindowClass";
    static bool cursorVisible_;
    static bool requestedCursorVisible_;
    static HWND cursorWindow_;

    mutable int width_ = 0;
    mutable int height_ = 0;
    bool fullscreen_ = false;
    RECT windowedRect_{100, 100, 1380, 820};
    DWORD windowedStyle_ = WS_OVERLAPPEDWINDOW;

    HWND hwnd_ = nullptr;
};
