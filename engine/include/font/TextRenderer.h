#pragma once

#include "font/FontManager.h"

#include <DirectXMath.h>
#include <string_view>

class SpriteRenderer;

struct TextStyle {
    FontHandle font{};
    float pixelSize = 32.0f;
    DirectX::XMFLOAT4 color{1.0f, 1.0f, 1.0f, 1.0f};
    float zOrder = 0.0f;
    float lineSpacing = 0.0f;
};

struct TextLayoutMetrics {
    DirectX::XMFLOAT2 size{0.0f, 0.0f};
    uint32_t lineCount = 0;
};

class TextRenderer {
public:
    void Initialize(FontManager* fontManager, SpriteRenderer* spriteRenderer);
    void Finalize();
    bool IsReady() const;

    void DrawText(std::string_view utf8Text, const DirectX::XMFLOAT2& position,
                  const TextStyle& style = {});
    void DrawText(std::wstring_view text, const DirectX::XMFLOAT2& position,
                  const TextStyle& style = {});
    void DrawText(std::u32string_view text, const DirectX::XMFLOAT2& position,
                  const TextStyle& style = {});

    TextLayoutMetrics MeasureText(std::string_view utf8Text, const TextStyle& style = {});
    TextLayoutMetrics MeasureText(std::wstring_view text, const TextStyle& style = {});
    TextLayoutMetrics MeasureText(std::u32string_view text, const TextStyle& style = {});

private:
    FontManager* fontManager_ = nullptr;
    SpriteRenderer* spriteRenderer_ = nullptr;
};
