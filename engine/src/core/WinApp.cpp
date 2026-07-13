#include "core/WinApp.h"
#ifdef _DEBUG
#include "imgui_impl_win32.h"
#endif

#include <algorithm>
#include <array>

#ifdef _DEBUG
extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam,
                                                             LPARAM lParam);
#endif

bool WinApp::cursorVisible_ = true;
bool WinApp::requestedCursorVisible_ = true;
HWND WinApp::cursorWindow_ = nullptr;

LRESULT WinApp::HandleSetCursorMessage(HWND hwnd, WPARAM, LPARAM lParam, bool& handled) {
    handled = true;
    if (!requestedCursorVisible_ && LOWORD(lParam) == HTCLIENT && ShouldHideCursor(hwnd)) {
        SetCursor(nullptr);
        return TRUE;
    }
    SetCursor(LoadCursor(nullptr, IDC_ARROW));
    return TRUE;
}

LRESULT WinApp::HandleActivateAppMessage(HWND, WPARAM wParam, LPARAM, bool& handled) {
    if (wParam == FALSE) {
        ApplyVisibleCursorState();
    } else {
        RestoreCursorForAppInteraction();
    }
    handled = false;
    return 0;
}

LRESULT WinApp::HandleFocusMessage(HWND, WPARAM, LPARAM, bool& handled) {
    RestoreCursorForAppInteraction();
    handled = false;
    return 0;
}

LRESULT WinApp::HandleKillFocusMessage(HWND, WPARAM, LPARAM, bool& handled) {
    ApplyVisibleCursorState();
    handled = false;
    return 0;
}

LRESULT WinApp::HandleKeyDownMessage(HWND, WPARAM wParam, LPARAM, bool& handled) {
    if ((wParam == VK_LWIN || wParam == VK_RWIN || wParam == VK_MENU || wParam == VK_APPS) &&
        requestedCursorVisible_) {
        ApplyVisibleCursorState();
    }
    handled = false;
    return 0;
}

LRESULT WinApp::HandleSysCommandMessage(HWND, WPARAM wParam, LPARAM, bool& handled) {
    if ((wParam & 0xFFF0) == SC_MINIMIZE || (wParam & 0xFFF0) == SC_TASKLIST) {
        ApplyVisibleCursorState();
    }
    handled = false;
    return 0;
}

LRESULT WinApp::HandleDestroyMessage(HWND hwnd, WPARAM, LPARAM, bool& handled) {
    handled = true;
    if (cursorWindow_ == hwnd) {
        requestedCursorVisible_ = true;
        ApplyVisibleCursorState();
        cursorWindow_ = nullptr;
    }
    PostQuitMessage(0);
    return 0;
}

bool WinApp::TryHandleWindowMessage(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam,
                                    LRESULT& result) {
    struct WindowMessageHandler {
        UINT message = 0u;
        LRESULT (*handler)(HWND hwnd, WPARAM wParam, LPARAM lParam, bool& handled) = nullptr;
    };
    static constexpr std::array<WindowMessageHandler, 8> kHandlers{{
        {WM_SETCURSOR, HandleSetCursorMessage},
        {WM_ACTIVATEAPP, HandleActivateAppMessage},
        {WM_SETFOCUS, HandleFocusMessage},
        {WM_KILLFOCUS, HandleKillFocusMessage},
        {WM_KEYDOWN, HandleKeyDownMessage},
        {WM_SYSKEYDOWN, HandleKeyDownMessage},
        {WM_SYSCOMMAND, HandleSysCommandMessage},
        {WM_DESTROY, HandleDestroyMessage},
    }};

    const auto it = std::ranges::find_if(
        kHandlers, [msg](const WindowMessageHandler& entry) { return entry.message == msg; });
    if (it == kHandlers.end()) {
        return false;
    }

    bool handled = false;
    result = it->handler(hwnd, wParam, lParam, handled);
    return handled;
}

WinApp::~WinApp() {
    if (cursorWindow_ == hwnd_) {
        requestedCursorVisible_ = true;
        ApplyVisibleCursorState();
        cursorWindow_ = nullptr;
    }

    if (hwnd_ != nullptr) {
        HWND hwnd = hwnd_;
        hwnd_ = nullptr;
        if (IsWindow(hwnd)) {
            DestroyWindow(hwnd);
        }
    }

    width_ = 0;
    height_ = 0;
    fullscreen_ = false;
}

LRESULT CALLBACK WinApp::WindowProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
#ifdef _DEBUG
    if (ImGui_ImplWin32_WndProcHandler(hwnd, msg, wParam, lParam)) {
        return true;
    }
#endif

    LRESULT result = 0;
    if (TryHandleWindowMessage(hwnd, msg, wParam, lParam, result)) {
        return result;
    }

    return DefWindowProc(hwnd, msg, wParam, lParam);
}

void WinApp::Initialize(HINSTANCE hInstance, int nCmdShow, int width, int height,
                        const std::wstring& title, bool fullscreen) {
    hwnd_ = nullptr;
    cursorWindow_ = nullptr;
    width_ = 0;
    height_ = 0;
    fullscreen_ = false;

    WNDCLASS wc{};
    wc.lpfnWndProc = WindowProc;
    wc.hInstance = hInstance;
    wc.lpszClassName = kClassName;
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);

    ATOM atom = RegisterClass(&wc);
    if (!atom && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
        OutputDebugStringA("WinApp: RegisterClass failed\n");
        return;
    }

    DWORD style = WS_OVERLAPPEDWINDOW;
    DWORD exStyle = 0;
    int windowX = CW_USEDEFAULT;
    int windowY = CW_USEDEFAULT;
    const int clientWidth = width > 0 ? width : 1;
    const int clientHeight = height > 0 ? height : 1;
    int windowWidth = clientWidth;
    int windowHeight = clientHeight;

    if (fullscreen) {
        RECT restoredRect{0, 0, clientWidth, clientHeight};
        AdjustWindowRect(&restoredRect, WS_OVERLAPPEDWINDOW, FALSE);
        const int restoredW = restoredRect.right - restoredRect.left;
        const int restoredH = restoredRect.bottom - restoredRect.top;
        MONITORINFO monitorInfo{};
        monitorInfo.cbSize = sizeof(monitorInfo);
        const HMONITOR monitor = MonitorFromPoint({0, 0}, MONITOR_DEFAULTTOPRIMARY);
        if (GetMonitorInfo(monitor, &monitorInfo)) {
            const RECT& monitorRect = monitorInfo.rcMonitor;
            windowedRect_ = {
                monitorRect.left + (monitorRect.right - monitorRect.left - restoredW) / 2,
                monitorRect.top + (monitorRect.bottom - monitorRect.top - restoredH) / 2,
                monitorRect.left + (monitorRect.right - monitorRect.left + restoredW) / 2,
                monitorRect.top + (monitorRect.bottom - monitorRect.top + restoredH) / 2,
            };
            windowX = monitorRect.left;
            windowY = monitorRect.top;
            windowWidth = monitorRect.right - monitorRect.left;
            windowHeight = monitorRect.bottom - monitorRect.top;
            style = WS_POPUP;
            fullscreen_ = true;
        }
    } else {
        RECT windowRect{0, 0, clientWidth, clientHeight};
        AdjustWindowRect(&windowRect, style, FALSE);
        windowWidth = windowRect.right - windowRect.left;
        windowHeight = windowRect.bottom - windowRect.top;
        MONITORINFO monitorInfo{};
        monitorInfo.cbSize = sizeof(monitorInfo);
        const HMONITOR monitor = MonitorFromPoint({0, 0}, MONITOR_DEFAULTTOPRIMARY);
        if (GetMonitorInfo(monitor, &monitorInfo)) {
            const RECT& workRect = monitorInfo.rcWork;
            const int workWidth = workRect.right - workRect.left;
            const int workHeight = workRect.bottom - workRect.top;
            windowX = workRect.left + (workWidth - windowWidth) / 2;
            windowY = workRect.top + (workHeight - windowHeight) / 2;
            if (windowX < workRect.left || windowWidth > workWidth) {
                windowX = workRect.left;
            }
            if (windowY < workRect.top || windowHeight > workHeight) {
                windowY = workRect.top;
            }
        }
        windowedRect_ = {windowX, windowY, windowX + windowWidth, windowY + windowHeight};
        fullscreen_ = false;
    }
    windowedStyle_ = WS_OVERLAPPEDWINDOW;

    hwnd_ = CreateWindowEx(exStyle, kClassName, title.c_str(), style, windowX, windowY, windowWidth,
                           windowHeight, nullptr, nullptr, hInstance, nullptr);

    if (!hwnd_) {
        OutputDebugStringA("WinApp: CreateWindowEx failed\n");
        return;
    }
    cursorWindow_ = hwnd_;

    ShowWindow(hwnd_, nCmdShow);
    UpdateClientSize();
}

bool WinApp::ProcessMessage() const {
    if (hwnd_ == nullptr) {
        return false;
    }

    MSG msg{};
    while (PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE)) {
        if (msg.message == WM_QUIT) {
            return false;
        }
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }
    UpdateClientSize();
    if (!requestedCursorVisible_) {
        ApplyRequestedCursorState(hwnd_);
    }
    return true;
}

void WinApp::RequestClose() {
    if (hwnd_) {
        PostMessage(hwnd_, WM_CLOSE, 0, 0);
    } else {
        PostQuitMessage(0);
    }
}

void WinApp::SetCursorVisible(bool visible) const {
    requestedCursorVisible_ = visible;
    ApplyRequestedCursorState(hwnd_);
}

void WinApp::SetFullscreen(bool fullscreen) {
    if (hwnd_ == nullptr || fullscreen_ == fullscreen) {
        return;
    }

    if (fullscreen) {
        RECT rect{};
        if (GetWindowRect(hwnd_, &rect)) {
            windowedRect_ = rect;
        }
        windowedStyle_ = static_cast<DWORD>(GetWindowLongPtr(hwnd_, GWL_STYLE));

        MONITORINFO monitorInfo{};
        monitorInfo.cbSize = sizeof(monitorInfo);
        const HMONITOR monitor = MonitorFromWindow(hwnd_, MONITOR_DEFAULTTONEAREST);
        if (!GetMonitorInfo(monitor, &monitorInfo)) {
            return;
        }

        const RECT& monitorRect = monitorInfo.rcMonitor;
        SetWindowLongPtr(hwnd_, GWL_STYLE, WS_POPUP);
        SetWindowPos(hwnd_, HWND_TOP, monitorRect.left, monitorRect.top,
                     monitorRect.right - monitorRect.left, monitorRect.bottom - monitorRect.top,
                     SWP_FRAMECHANGED | SWP_SHOWWINDOW);
        fullscreen_ = true;
    } else {
        SetWindowLongPtr(hwnd_, GWL_STYLE, windowedStyle_);
        SetWindowPos(hwnd_, nullptr, windowedRect_.left, windowedRect_.top,
                     windowedRect_.right - windowedRect_.left,
                     windowedRect_.bottom - windowedRect_.top,
                     SWP_FRAMECHANGED | SWP_NOZORDER | SWP_SHOWWINDOW);
        fullscreen_ = false;
    }

    UpdateClientSize();
}

void WinApp::ApplyHiddenCursorState(HWND hwnd, bool lockToClient) {
    CURSORINFO cursorInfo{};
    cursorInfo.cbSize = sizeof(cursorInfo);
    const bool osCursorVisible =
        GetCursorInfo(&cursorInfo) && (cursorInfo.flags & CURSOR_SHOWING) == CURSOR_SHOWING;
    const bool shouldUpdateShowCount = cursorVisible_ || osCursorVisible;

    cursorVisible_ = false;
    SetCursor(nullptr);
    if (shouldUpdateShowCount) {
        while (ShowCursor(FALSE) >= 0) {
        }
    }
    if (lockToClient) {
        LockCursorToClient(hwnd);
        CenterCursorInClient(hwnd);
    } else {
        ReleaseCursorLock();
    }
}

void WinApp::ApplyVisibleCursorState() {
    cursorVisible_ = true;
    ReleaseCursorLock();
    while (ShowCursor(TRUE) < 0) {
    }
    SetCursor(LoadCursor(nullptr, IDC_ARROW));
}

void WinApp::ApplyRequestedCursorState(HWND hwnd) {
    if (requestedCursorVisible_ || !ShouldHideCursor(hwnd)) {
        ApplyVisibleCursorState();
        return;
    }

    ApplyHiddenCursorState(hwnd, ShouldLockHiddenCursor(hwnd));
}

void WinApp::LockCursorToClient(HWND hwnd) {
    if (hwnd == nullptr) {
        return;
    }

    RECT clientRect{};
    if (!GetClientRect(hwnd, &clientRect)) {
        return;
    }

    POINT topLeft{clientRect.left, clientRect.top};
    POINT bottomRight{clientRect.right, clientRect.bottom};
    if (!ClientToScreen(hwnd, &topLeft) || !ClientToScreen(hwnd, &bottomRight)) {
        return;
    }

    RECT screenRect{topLeft.x, topLeft.y, bottomRight.x, bottomRight.y};
    ClipCursor(&screenRect);
}

void WinApp::CenterCursorInClient(HWND hwnd) {
    if (hwnd == nullptr) {
        return;
    }

    RECT clientRect{};
    if (!GetClientRect(hwnd, &clientRect)) {
        return;
    }

    POINT center{(clientRect.left + clientRect.right) / 2,
                 (clientRect.top + clientRect.bottom) / 2};
    if (!ClientToScreen(hwnd, &center)) {
        return;
    }

    SetCursorPos(center.x, center.y);
}

void WinApp::ReleaseCursorLock() {
    ClipCursor(nullptr);
}

bool WinApp::ShouldLockHiddenCursor(HWND hwnd) {
    return ShouldHideCursor(hwnd);
}

bool WinApp::ShouldHideCursor(HWND hwnd) {
    return hwnd != nullptr && GetActiveWindow() == hwnd && !IsIconic(hwnd);
}

void WinApp::RestoreCursorForAppInteraction() {
    ApplyRequestedCursorState(cursorWindow_);
}

int WinApp::GetWidth() const {
    UpdateClientSize();
    return width_;
}

int WinApp::GetHeight() const {
    UpdateClientSize();
    return height_;
}

void WinApp::UpdateClientSize() const {
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
