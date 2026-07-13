#pragma once

#include "core/ResourceHandle.h"

#include <DirectXMath.h>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

class TextureManager;
struct IDWriteFontFace;

struct FontGlyph {
    bool visible = false;
    uint32_t textureId = kInvalidResourceId;
    DirectX::XMFLOAT2 uvLeftTop{0.0f, 0.0f};
    DirectX::XMFLOAT2 uvSize{0.0f, 0.0f};
    DirectX::XMFLOAT2 size{0.0f, 0.0f};
    DirectX::XMFLOAT2 offset{0.0f, 0.0f};
    float advanceX = 0.0f;
};

struct FontMetrics {
    float pixelSize = 0.0f;
    float ascent = 0.0f;
    float descent = 0.0f;
    float lineGap = 0.0f;
    float lineHeight = 0.0f;
};

class FontManager {
public:
    FontManager();
    ~FontManager();

    FontManager(const FontManager&) = delete;
    FontManager& operator=(const FontManager&) = delete;

    void Initialize(TextureManager* textureManager);
    bool Finalize(bool allowFrameAbort = false);
    bool IsReady() const;

    FontHandle LoadFont(const std::wstring& filePath);
    FontHandle GetDefaultFont() const;

    bool PrepareGlyphs(FontHandle font, float pixelSize, const std::vector<char32_t>& codepoints);
    const FontGlyph* GetGlyph(FontHandle font, float pixelSize, char32_t codepoint);
    FontMetrics GetMetrics(FontHandle font, float pixelSize);

    size_t GetFontCount() const;
    size_t GetAtlasPageCount() const;

private:
    struct State;

    FontHandle ResolveFont(FontHandle font) const;
    static uint32_t ResolvePixelKey(float pixelSize);
    FontHandle RegisterLoadedFont(const std::wstring& key, const std::filesystem::path& path,
                                  IDWriteFontFace* fontFace);
    FontHandle LoadSystemFontFamily(std::wstring_view familyName);
    bool ReleaseAtlasTextures(bool allowFrameAbort);
    bool EnsureGlyph(FontHandle handle, uint32_t pixelKey, char32_t codepoint);

    std::unique_ptr<State> state_;
};
