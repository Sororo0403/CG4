#include "WinApp.h"
#include "imgui_impl_win32.h"
#include <stdexcept>

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd,
                                                             UINT msg,
                                                             WPARAM wParam,
                                                             LPARAM lParam);

LRESULT CALLBACK WinApp::WindowProc(HWND hwnd, UINT msg, WPARAM wParam,
                                    LPARAM lParam) {
    if (ImGui_ImplWin32_WndProcHandler(hwnd, msg, wParam, lParam)) {
        return true;
    }

    switch (msg) {
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    }

    return DefWindowProc(hwnd, msg, wParam, lParam);
}

void WinApp::Initialize(HINSTANCE hInstance, int nCmdShow, int width,
                        int height, const std::wstring &title) {
    WNDCLASS wc{};
    wc.lpfnWndProc = WindowProc;
    wc.hInstance = hInstance;
    wc.lpszClassName = kClassName;
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);

    // ウィンドウクラス登録
    ATOM atom = RegisterClass(&wc);
    if (!atom && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
        throw std::runtime_error("RegisterClass failed");
    }

    // ウィンドウ生成
    RECT windowRect{0, 0, width, height};
    AdjustWindowRect(&windowRect, WS_OVERLAPPEDWINDOW, FALSE);

    hwnd_ = CreateWindowEx(0, kClassName, title.c_str(), WS_OVERLAPPEDWINDOW,
                           CW_USEDEFAULT, CW_USEDEFAULT,
                           windowRect.right - windowRect.left,
                           windowRect.bottom - windowRect.top, nullptr, nullptr,
                           hInstance, nullptr);

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
