#pragma once
#define DIRECTINPUT_VERSION 0x0800
#include <Windows.h>
#include <array>
#include <dinput.h>
#include <wrl.h>

class Input {
  public:
    void Initialize(HINSTANCE hInstance, HWND hwnd);
    void Update(float deltaTime);

    bool IsKeyPress(int dik) const;
    bool IsKeyTrigger(int dik) const;
    bool IsKeyRelease(int dik) const;

    long GetMouseDX() const { return mouseState_.lX; }
    long GetMouseDY() const { return mouseState_.lY; }
    long GetMouseWheel() const { return mouseState_.lZ; }

    bool IsMousePress(int button) const;
    bool IsMouseTrigger(int button) const;
    bool IsMouseRelease(int button) const;

  private:
    void UpdateKeyboard();
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
