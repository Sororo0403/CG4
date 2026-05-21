#include "core/WinApp.h"
#ifdef _DEBUG
#include "imgui_impl_win32.h"
#endif
#include <stdexcept>

#ifdef _DEBUG
extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd,
                                                             UINT msg,
                                                             WPARAM wParam,
                                                             LPARAM lParam);
#endif

bool WinApp::cursorVisible_ = true;

LRESULT CALLBACK WinApp::WindowProc(HWND hwnd, UINT msg, WPARAM wParam,
                                    LPARAM lParam) {
#ifdef _DEBUG
    if (ImGui_ImplWin32_WndProcHandler(hwnd, msg, wParam, lParam)) {
        return true;
    }
#endif

    switch (msg) {
    case WM_SETCURSOR:
        if (!cursorVisible_) {
            SetCursor(nullptr);
            return TRUE;
        }
        break;
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    }

    return DefWindowProc(hwnd, msg, wParam, lParam);
}

void WinApp::Initialize(HINSTANCE hInstance, int nCmdShow, int width,
                        int height, const std::wstring &title,
                        bool fullscreen) {
    WNDCLASS wc{};
    wc.lpfnWndProc = WindowProc;
    wc.hInstance = hInstance;
    wc.lpszClassName = kClassName;
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);

    ATOM atom = RegisterClass(&wc);
    if (!atom && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
        throw std::runtime_error("RegisterClass failed");
    }

    DWORD style = WS_OVERLAPPEDWINDOW;
    DWORD exStyle = 0;
    int windowX = CW_USEDEFAULT;
    int windowY = CW_USEDEFAULT;
    int windowWidth = width;
    int windowHeight = height;

    if (fullscreen) {
        MONITORINFO monitorInfo{};
        monitorInfo.cbSize = sizeof(monitorInfo);
        const HMONITOR monitor =
            MonitorFromPoint({0, 0}, MONITOR_DEFAULTTOPRIMARY);
        if (GetMonitorInfo(monitor, &monitorInfo)) {
            const RECT &monitorRect = monitorInfo.rcMonitor;
            windowX = monitorRect.left;
            windowY = monitorRect.top;
            windowWidth = monitorRect.right - monitorRect.left;
            windowHeight = monitorRect.bottom - monitorRect.top;
            style = WS_POPUP;
        }
    } else {
        RECT windowRect{0, 0, width, height};
        AdjustWindowRect(&windowRect, style, FALSE);
        windowWidth = windowRect.right - windowRect.left;
        windowHeight = windowRect.bottom - windowRect.top;
    }

    hwnd_ = CreateWindowEx(exStyle, kClassName, title.c_str(), style, windowX,
                           windowY, windowWidth, windowHeight, nullptr,
                           nullptr, hInstance, nullptr);

    if (!hwnd_) {
        throw std::runtime_error("CreateWindowEx failed");
    }

    ShowWindow(hwnd_, nCmdShow);
    UpdateClientSize();
}

bool WinApp::ProcessMessage() {
    MSG msg{};
    if (PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE)) {
        if (msg.message == WM_QUIT) {
            return false;
        }
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }
    UpdateClientSize();
    return true;
}

void WinApp::RequestClose() {
    if (hwnd_) {
        PostMessage(hwnd_, WM_CLOSE, 0, 0);
    } else {
        PostQuitMessage(0);
    }
}

void WinApp::SetCursorVisible(bool visible) {
    cursorVisible_ = visible;
    if (!visible) {
        SetCursor(nullptr);
        while (ShowCursor(FALSE) >= 0) {
        }
        return;
    }

    while (ShowCursor(TRUE) < 0) {
    }
}

int WinApp::GetWidth() const {
    const_cast<WinApp *>(this)->UpdateClientSize();
    return width_;
}

int WinApp::GetHeight() const {
    const_cast<WinApp *>(this)->UpdateClientSize();
    return height_;
}

void WinApp::UpdateClientSize() {
    if (!hwnd_) {
        return;
    }

    RECT clientRect{};
    if (!GetClientRect(hwnd_, &clientRect)) {
        return;
    }

    width_ = clientRect.right - clientRect.left;
    height_ = clientRect.bottom - clientRect.top;
}
