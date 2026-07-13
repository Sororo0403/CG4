#include "font/FontManager.h"

#include "core/AssetManager.h"
#include "core/ResourceHandle.h"
#include "texture/TextureManager.h"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <algorithm>
#include <cmath>
#include <cwctype>
#include <dwrite.h>
#include <exception>
#include <limits>
#include <new>
#include <numeric>
#include <unordered_map>
#include <utility>
#include <vector>
#include <wrl.h>

using Microsoft::WRL::ComPtr;

namespace {

constexpr uint32_t kAtlasSize = 1024u;
constexpr uint32_t kGlyphPadding = 1u;
constexpr float kDefaultPixelSize = 32.0f;
constexpr float kMinPixelSize = 1.0f;
constexpr float kMaxPixelSize = 256.0f;

struct AtlasPage {
    uint32_t textureId = kInvalidResourceId;
    std::vector<uint8_t> pixels;
    uint32_t cursorX = 0;
    uint32_t cursorY = 0;
    uint32_t rowHeight = 0;
    bool dirty = false;
};

struct FontSizeCache {
    FontMetrics metrics{};
    std::unordered_map<char32_t, FontGlyph> glyphs;
    std::vector<AtlasPage> pages;
};

struct FontRecord {
    std::filesystem::path path;
    ComPtr<IDWriteFontFace> fontFace;
    DWRITE_FONT_METRICS designMetrics{};
    std::unordered_map<uint32_t, FontSizeCache> sizes;
};

struct GlyphRasterizationRequest {
    IDWriteFactory* factory = nullptr;
    FontRecord* font = nullptr;
    uint32_t pixelKey = 0u;
    uint16_t glyphIndex = 0u;
    float glyphAdvance = 0.0f;
};

struct RasterizedGlyph {
    std::vector<uint8_t> alphaPixels;
    uint32_t width = 0u;
    uint32_t height = 0u;
};

std::wstring NormalizePathKey(const std::filesystem::path& path) {
    try {
        std::wstring key = path.lexically_normal().wstring();
        std::transform(key.begin(), key.end(), key.begin(),
                       [](wchar_t ch) { return static_cast<wchar_t>(std::towlower(ch)); });
        return key;
    } catch (const std::exception&) {
        return {};
    }
}

std::filesystem::path ResolveFontPath(const std::wstring& filePath) {
    std::filesystem::path requested;
    std::filesystem::path resolved;
    try {
        requested = std::filesystem::path(filePath);
        resolved = requested.is_absolute() ? requested : AssetManager::ResolvePath(requested);
    } catch (const std::exception&) {
        return {};
    }
    std::error_code ec;
    try {
        if (!std::filesystem::exists(resolved, ec) || ec) {
            return {};
        }
        return std::filesystem::weakly_canonical(resolved, ec);
    } catch (const std::exception&) {
        return {};
    }
}

float SanitizePixelSize(float pixelSize) {
    if (!std::isfinite(pixelSize)) {
        return kDefaultPixelSize;
    }
    return std::clamp(pixelSize, kMinPixelSize, kMaxPixelSize);
}

uint32_t PixelKeyFromSize(float pixelSize) {
    const float sanitized = SanitizePixelSize(pixelSize);
    const float rounded = std::round(sanitized);
    const float clamped = std::clamp(rounded, kMinPixelSize, kMaxPixelSize);
    return static_cast<uint32_t>(clamped);
}

float ScaleForPixelSize(const FontRecord& font, uint32_t pixelKey) {
    if (font.designMetrics.designUnitsPerEm == 0u) {
        return 0.0f;
    }
    return static_cast<float>(pixelKey) / static_cast<float>(font.designMetrics.designUnitsPerEm);
}

FontMetrics BuildMetrics(const FontRecord& font, uint32_t pixelKey) {
    const float scale = ScaleForPixelSize(font, pixelKey);
    FontMetrics metrics{};
    metrics.pixelSize = static_cast<float>(pixelKey);
    metrics.ascent = static_cast<float>(font.designMetrics.ascent) * scale;
    metrics.descent = static_cast<float>(font.designMetrics.descent) * scale;
    metrics.lineGap = static_cast<float>(font.designMetrics.lineGap) * scale;
    metrics.lineHeight = (std::max)(1.0f, metrics.ascent + metrics.descent + metrics.lineGap);
    return metrics;
}

bool TryAllocateGlyphRect(AtlasPage& page, uint32_t width, uint32_t height, uint32_t& x,
                          uint32_t& y) {
    if (width == 0u || height == 0u) {
        return false;
    }
    if (width > (std::numeric_limits<uint32_t>::max)() - kGlyphPadding * 2u ||
        height > (std::numeric_limits<uint32_t>::max)() - kGlyphPadding * 2u) {
        return false;
    }
    const uint32_t neededWidth = width + kGlyphPadding * 2u;
    const uint32_t neededHeight = height + kGlyphPadding * 2u;
    if (neededWidth > kAtlasSize || neededHeight > kAtlasSize) {
        return false;
    }

    uint32_t cursorX = page.cursorX;
    uint32_t cursorY = page.cursorY;
    uint32_t rowHeight = page.rowHeight;
    if (cursorX > kAtlasSize - neededWidth) {
        cursorX = 0u;
        if (cursorY > (std::numeric_limits<uint32_t>::max)() - rowHeight) {
            return false;
        }
        cursorY += rowHeight;
        rowHeight = 0u;
    }
    if (cursorY > kAtlasSize - neededHeight) {
        return false;
    }

    x = cursorX + kGlyphPadding;
    y = cursorY + kGlyphPadding;
    page.cursorX = cursorX + neededWidth;
    page.cursorY = cursorY;
    page.rowHeight = (std::max)(rowHeight, neededHeight);
    return true;
}

AtlasPage* CreateAtlasPage(TextureManager* textureManager, FontSizeCache& sizeCache) {
    if (textureManager == nullptr) {
        return nullptr;
    }

    AtlasPage page{};
    const size_t pixelCount = static_cast<size_t>(kAtlasSize) * static_cast<size_t>(kAtlasSize);
    try {
        page.pixels.assign(pixelCount * 4u, 0u);
    } catch (const std::exception&) {
        return nullptr;
    }
    page.textureId =
        textureManager->CreateFromRgbaPixels(kAtlasSize, kAtlasSize, page.pixels.data());
    if (!textureManager->IsValidTextureId(page.textureId) ||
        page.textureId == textureManager->GetWhiteTextureId()) {
        return nullptr;
    }
    const uint32_t textureId = page.textureId;

    try {
        sizeCache.pages.push_back(std::move(page));
    } catch (const std::exception&) {
        textureManager->ReleaseTexture(textureId);
        return nullptr;
    }
    return &sizeCache.pages.back();
}

void UploadDirtyPages(TextureManager* textureManager, FontSizeCache& sizeCache) {
    if (textureManager == nullptr) {
        return;
    }

    const size_t rowPitch = static_cast<size_t>(kAtlasSize) * 4u;
    for (AtlasPage& page : sizeCache.pages) {
        if (!page.dirty || !textureManager->IsValidTextureId(page.textureId)) {
            continue;
        }
        textureManager->UpdateTexture2D(page.textureId, page.pixels.data(), rowPitch);
        page.dirty = false;
    }
}

uint8_t AverageClearTypeCoverage(const uint8_t* coverage) {
    const uint32_t sum = static_cast<uint32_t>(coverage[0]) + static_cast<uint32_t>(coverage[1]) +
                         static_cast<uint32_t>(coverage[2]);
    return static_cast<uint8_t>((sum + 1u) / 3u);
}

bool CreateGlyphRunAnalysis(const GlyphRasterizationRequest& request,
                            ComPtr<IDWriteGlyphRunAnalysis>& analysis) {
    if (request.factory == nullptr || request.font == nullptr ||
        request.font->fontFace == nullptr) {
        return false;
    }

    DWRITE_GLYPH_OFFSET glyphOffset{};
    FLOAT advance = request.glyphAdvance;
    DWRITE_GLYPH_RUN glyphRun{};
    glyphRun.fontFace = request.font->fontFace.Get();
    glyphRun.fontEmSize = static_cast<FLOAT>(request.pixelKey);
    glyphRun.glyphCount = 1u;
    glyphRun.glyphIndices = &request.glyphIndex;
    glyphRun.glyphAdvances = &advance;
    glyphRun.glyphOffsets = &glyphOffset;
    glyphRun.isSideways = FALSE;
    glyphRun.bidiLevel = 0u;

    const HRESULT hr = request.factory->CreateGlyphRunAnalysis(
        &glyphRun, 1.0f, nullptr, DWRITE_RENDERING_MODE_NATURAL_SYMMETRIC,
        DWRITE_MEASURING_MODE_NATURAL, 0.0f, 0.0f, &analysis);
    return SUCCEEDED(hr) && analysis != nullptr;
}

bool GetGlyphTextureBounds(IDWriteGlyphRunAnalysis* analysis, FontGlyph& glyph,
                           RasterizedGlyph& rasterized, RECT& bounds) {
    const HRESULT hr = analysis->GetAlphaTextureBounds(DWRITE_TEXTURE_CLEARTYPE_3x1, &bounds);
    if (FAILED(hr)) {
        return false;
    }

    const int32_t rawWidth = bounds.right - bounds.left;
    const int32_t rawHeight = bounds.bottom - bounds.top;
    if (rawWidth <= 0 || rawHeight <= 0) {
        glyph.visible = false;
        rasterized.width = 0u;
        rasterized.height = 0u;
        return true;
    }

    rasterized.width = static_cast<uint32_t>(rawWidth);
    rasterized.height = static_cast<uint32_t>(rawHeight);
    glyph.visible = true;
    return true;
}

bool BuildGlyphAlphaPixels(IDWriteGlyphRunAnalysis* analysis, const RECT& bounds,
                           RasterizedGlyph& rasterized) {
    const uint64_t coverageBytes =
        static_cast<uint64_t>(rasterized.width) * static_cast<uint64_t>(rasterized.height) * 3ull;
    if (coverageBytes > static_cast<uint64_t>((std::numeric_limits<uint32_t>::max)())) {
        return false;
    }

    std::vector<uint8_t> clearTypeCoverage;
    try {
        clearTypeCoverage.resize(static_cast<size_t>(coverageBytes));
    } catch (const std::exception&) {
        return false;
    }
    const HRESULT hr = analysis->CreateAlphaTexture(
        DWRITE_TEXTURE_CLEARTYPE_3x1, &bounds, clearTypeCoverage.data(),
        static_cast<uint32_t>(clearTypeCoverage.size()));
    if (FAILED(hr)) {
        return false;
    }

    const size_t pixelCount =
        static_cast<size_t>(rasterized.width) * static_cast<size_t>(rasterized.height);
    try {
        rasterized.alphaPixels.resize(pixelCount);
    } catch (const std::exception&) {
        return false;
    }
    for (size_t index = 0u; index < pixelCount; ++index) {
        rasterized.alphaPixels[index] = AverageClearTypeCoverage(&clearTypeCoverage[index * 3u]);
    }
    return true;
}

void ApplyGlyphRasterBounds(FontGlyph& glyph, const RECT& bounds,
                            const RasterizedGlyph& rasterized) {
    glyph.visible = true;
    glyph.size = {static_cast<float>(rasterized.width), static_cast<float>(rasterized.height)};
    glyph.offset = {static_cast<float>(bounds.left), static_cast<float>(bounds.top)};
}

bool RasterizeGlyph(const GlyphRasterizationRequest& request, FontGlyph& glyph,
                    RasterizedGlyph& rasterized) {
    ComPtr<IDWriteGlyphRunAnalysis> analysis;
    if (!CreateGlyphRunAnalysis(request, analysis)) {
        return false;
    }

    RECT bounds{};
    if (!GetGlyphTextureBounds(analysis.Get(), glyph, rasterized, bounds)) {
        return false;
    }
    if (!glyph.visible) {
        return true;
    }
    if (!BuildGlyphAlphaPixels(analysis.Get(), bounds, rasterized)) {
        return false;
    }

    ApplyGlyphRasterBounds(glyph, bounds, rasterized);
    return true;
}

FontSizeCache& GetSizeCache(FontRecord& font, uint32_t pixelKey) {
    auto [it, inserted] = font.sizes.try_emplace(pixelKey);
    if (inserted) {
        it->second.metrics = BuildMetrics(font, pixelKey);
    }
    return it->second;
}

FontRecord* GetFontRecord(std::vector<FontRecord>& fonts, FontHandle handle) {
    const uint32_t index = handle.Get();
    if (index >= fonts.size()) {
        return nullptr;
    }
    if (fonts[index].fontFace == nullptr) {
        return nullptr;
    }
    return &fonts[index];
}

const FontRecord* GetFontRecord(const std::vector<FontRecord>& fonts, FontHandle handle) {
    const uint32_t index = handle.Get();
    if (index >= fonts.size()) {
        return nullptr;
    }
    if (fonts[index].fontFace == nullptr) {
        return nullptr;
    }
    return &fonts[index];
}

} // namespace

struct FontManager::State {
    TextureManager* textureManager = nullptr;
    ComPtr<IDWriteFactory> dwriteFactory;
    std::vector<FontRecord> fonts;
    std::unordered_map<std::wstring, uint32_t> pathToFontId;
    FontHandle defaultFont{};
};

FontHandle FontManager::RegisterLoadedFont(const std::wstring& key,
                                           const std::filesystem::path& path,
                                           IDWriteFontFace* fontFace) {
    if (fontFace == nullptr) {
        return {};
    }

    FontRecord record{};
    try {
        record.path = path;
        record.fontFace = fontFace;
    } catch (const std::exception&) {
        return {};
    }
    record.fontFace->GetMetrics(&record.designMetrics);
    if (record.designMetrics.designUnitsPerEm == 0u) {
        return {};
    }

    if (state_->fonts.size() >= static_cast<size_t>((std::numeric_limits<uint32_t>::max)())) {
        return {};
    }
    const uint32_t fontId = static_cast<uint32_t>(state_->fonts.size());
    try {
        state_->fonts.push_back(std::move(record));
        state_->pathToFontId[key] = fontId;
    } catch (const std::exception&) {
        if (state_->fonts.size() > fontId) {
            state_->fonts.pop_back();
        }
        return {};
    }
    return FontHandle(fontId);
}

bool FontManager::EnsureGlyph(FontHandle handle, uint32_t pixelKey, char32_t codepoint) {
    FontRecord* font = GetFontRecord(state_->fonts, handle);
    if (font == nullptr) {
        return false;
    }

    FontSizeCache* sizeCachePtr = nullptr;
    try {
        sizeCachePtr = &GetSizeCache(*font, pixelKey);
    } catch (const std::exception&) {
        return false;
    }
    FontSizeCache& sizeCache = *sizeCachePtr;
    if (sizeCache.glyphs.contains(codepoint)) {
        return true;
    }

    const UINT32 directWriteCodepoint = static_cast<UINT32>(codepoint);
    UINT16 glyphIndex = 0u;
    HRESULT hr = font->fontFace->GetGlyphIndices(&directWriteCodepoint, 1u, &glyphIndex);
    if (FAILED(hr)) {
        return false;
    }

    DWRITE_GLYPH_METRICS designGlyphMetrics{};
    hr = font->fontFace->GetDesignGlyphMetrics(&glyphIndex, 1u, &designGlyphMetrics, FALSE);
    if (FAILED(hr)) {
        return false;
    }

    const float scale = ScaleForPixelSize(*font, pixelKey);
    FontGlyph glyph{};
    glyph.advanceX = static_cast<float>(designGlyphMetrics.advanceWidth) * scale;

    RasterizedGlyph rasterized;
    const GlyphRasterizationRequest rasterizationRequest{.factory = state_->dwriteFactory.Get(),
                                                         .font = font,
                                                         .pixelKey = pixelKey,
                                                         .glyphIndex = glyphIndex,
                                                         .glyphAdvance = glyph.advanceX};
    if (!RasterizeGlyph(rasterizationRequest, glyph, rasterized)) {
        return false;
    }

    if (glyph.visible) {
        AtlasPage* targetPage = nullptr;
        uint32_t atlasX = 0u;
        uint32_t atlasY = 0u;
        for (AtlasPage& page : sizeCache.pages) {
            if (TryAllocateGlyphRect(page, rasterized.width, rasterized.height, atlasX, atlasY)) {
                targetPage = &page;
                break;
            }
        }
        if (targetPage == nullptr) {
            targetPage = CreateAtlasPage(state_->textureManager, sizeCache);
            if (targetPage == nullptr || !TryAllocateGlyphRect(*targetPage, rasterized.width,
                                                               rasterized.height, atlasX, atlasY)) {
                return false;
            }
        }

        for (uint32_t y = 0u; y < rasterized.height; ++y) {
            for (uint32_t x = 0u; x < rasterized.width; ++x) {
                const size_t srcIndex = static_cast<size_t>(y) * rasterized.width + x;
                const size_t dstPixel = (static_cast<size_t>(atlasY + y) * kAtlasSize) +
                                        static_cast<size_t>(atlasX + x);
                const size_t dstIndex = dstPixel * 4u;
                targetPage->pixels[dstIndex + 0u] = 255u;
                targetPage->pixels[dstIndex + 1u] = 255u;
                targetPage->pixels[dstIndex + 2u] = 255u;
                targetPage->pixels[dstIndex + 3u] = rasterized.alphaPixels[srcIndex];
            }
        }
        targetPage->dirty = true;

        glyph.textureId = targetPage->textureId;
        glyph.uvLeftTop = {static_cast<float>(atlasX) / static_cast<float>(kAtlasSize),
                           static_cast<float>(atlasY) / static_cast<float>(kAtlasSize)};
        glyph.uvSize = {static_cast<float>(rasterized.width) / static_cast<float>(kAtlasSize),
                        static_cast<float>(rasterized.height) / static_cast<float>(kAtlasSize)};
    }

    try {
        sizeCache.glyphs.emplace(codepoint, glyph);
    } catch (const std::exception&) {
        return false;
    }
    return true;
}

FontManager::FontManager() : state_(std::make_unique<State>()) {}

FontManager::~FontManager() {
    Finalize(true);
}

void FontManager::Initialize(TextureManager* textureManager) {
    if (!Finalize()) {
        return;
    }
    if (textureManager == nullptr) {
        return;
    }

    state_->textureManager = textureManager;
    HRESULT hr =
        DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory),
                            reinterpret_cast<IUnknown**>(state_->dwriteFactory.GetAddressOf()));
    if (FAILED(hr) || state_->dwriteFactory == nullptr) {
        Finalize();
        return;
    }

    state_->defaultFont = LoadFont(L"engine/resources/fonts/MPLUS1/MPLUS1-ExtraBold.ttf");
    if (!state_->defaultFont.IsValid()) {
        static constexpr const wchar_t* kFallbackFamilies[] = {L"Yu Gothic UI", L"Meiryo UI",
                                                               L"Segoe UI", L"Arial"};
        for (const wchar_t* family : kFallbackFamilies) {
            state_->defaultFont = LoadSystemFontFamily(family);
            if (state_->defaultFont.IsValid()) {
                break;
            }
        }
    }
}

bool FontManager::Finalize(bool allowFrameAbort) {
    if (!ReleaseAtlasTextures(allowFrameAbort)) {
        return false;
    }
    state_->pathToFontId.clear();
    state_->fonts.clear();
    state_->defaultFont = {};
    state_->dwriteFactory.Reset();
    state_->textureManager = nullptr;
    return true;
}

bool FontManager::IsReady() const {
    return state_->textureManager != nullptr && state_->dwriteFactory != nullptr;
}

FontHandle FontManager::LoadFont(const std::wstring& filePath) {
    if (state_->dwriteFactory == nullptr) {
        return {};
    }

    std::filesystem::path resolvedPath;
    try {
        resolvedPath = ResolveFontPath(filePath);
    } catch (const std::exception&) {
        return {};
    }
    if (resolvedPath.empty()) {
        return {};
    }
    std::error_code existsError;
    try {
        if (!std::filesystem::exists(resolvedPath, existsError) || existsError) {
            return {};
        }
    } catch (const std::exception&) {
        return {};
    }

    std::wstring pathKey;
    try {
        pathKey = NormalizePathKey(resolvedPath);
    } catch (const std::exception&) {
        return {};
    }
    const auto cached = state_->pathToFontId.find(pathKey);
    if (cached != state_->pathToFontId.end()) {
        return FontHandle(cached->second);
    }

    ComPtr<IDWriteFontFile> fontFile;
    HRESULT hr =
        state_->dwriteFactory->CreateFontFileReference(resolvedPath.c_str(), nullptr, &fontFile);
    if (FAILED(hr) || fontFile == nullptr) {
        return {};
    }

    BOOL supported = FALSE;
    DWRITE_FONT_FILE_TYPE fileType = DWRITE_FONT_FILE_TYPE_UNKNOWN;
    DWRITE_FONT_FACE_TYPE faceType = DWRITE_FONT_FACE_TYPE_UNKNOWN;
    UINT32 faceCount = 0u;
    hr = fontFile->Analyze(&supported, &fileType, &faceType, &faceCount);
    (void)fileType;
    if (FAILED(hr) || !supported || faceCount == 0u || faceType == DWRITE_FONT_FACE_TYPE_UNKNOWN) {
        return {};
    }

    IDWriteFontFile* fontFiles[] = {fontFile.Get()};
    ComPtr<IDWriteFontFace> fontFace;
    hr = state_->dwriteFactory->CreateFontFace(faceType, 1u, fontFiles, 0u,
                                               DWRITE_FONT_SIMULATIONS_NONE, &fontFace);
    if (FAILED(hr) || fontFace == nullptr) {
        return {};
    }

    return RegisterLoadedFont(pathKey, resolvedPath, fontFace.Get());
}

FontHandle FontManager::LoadSystemFontFamily(std::wstring_view familyName) {
    if (state_->dwriteFactory == nullptr || familyName.empty()) {
        return {};
    }

    std::wstring family;
    std::wstring key;
    try {
        family = std::wstring(familyName);
        key = L"system:";
        key.append(family);
        std::transform(key.begin(), key.end(), key.begin(),
                       [](wchar_t ch) { return static_cast<wchar_t>(std::towlower(ch)); });
    } catch (const std::exception&) {
        return {};
    }

    const auto cached = state_->pathToFontId.find(key);
    if (cached != state_->pathToFontId.end()) {
        return FontHandle(cached->second);
    }

    ComPtr<IDWriteFontCollection> collection;
    HRESULT hr = state_->dwriteFactory->GetSystemFontCollection(&collection, FALSE);
    if (FAILED(hr) || collection == nullptr) {
        return {};
    }

    UINT32 familyIndex = 0u;
    BOOL exists = FALSE;
    hr = collection->FindFamilyName(family.c_str(), &familyIndex, &exists);
    if (FAILED(hr) || !exists) {
        return {};
    }

    ComPtr<IDWriteFontFamily> fontFamily;
    hr = collection->GetFontFamily(familyIndex, &fontFamily);
    if (FAILED(hr) || fontFamily == nullptr) {
        return {};
    }

    ComPtr<IDWriteFont> font;
    hr = fontFamily->GetFirstMatchingFont(DWRITE_FONT_WEIGHT_EXTRA_BOLD, DWRITE_FONT_STRETCH_NORMAL,
                                          DWRITE_FONT_STYLE_NORMAL, &font);
    if (FAILED(hr) || font == nullptr) {
        return {};
    }

    ComPtr<IDWriteFontFace> fontFace;
    hr = font->CreateFontFace(&fontFace);
    if (FAILED(hr) || fontFace == nullptr) {
        return {};
    }

    return RegisterLoadedFont(key, std::filesystem::path(key), fontFace.Get());
}

FontHandle FontManager::GetDefaultFont() const {
    return state_->defaultFont;
}

bool FontManager::PrepareGlyphs(FontHandle font, float pixelSize,
                                const std::vector<char32_t>& codepoints) {
    const FontHandle resolved = ResolveFont(font);
    if (!resolved.IsValid()) {
        return false;
    }
    const uint32_t pixelKey = ResolvePixelKey(pixelSize);
    bool prepared = true;
    for (char32_t codepoint : codepoints) {
        if (codepoint == U'\n' || codepoint == U'\r') {
            continue;
        }
        prepared = EnsureGlyph(resolved, pixelKey, codepoint) && prepared;
    }

    FontRecord* record = GetFontRecord(state_->fonts, resolved);
    if (record != nullptr) {
        FontSizeCache* sizeCache = nullptr;
        try {
            sizeCache = &GetSizeCache(*record, pixelKey);
        } catch (const std::exception&) {
            return prepared;
        }
        UploadDirtyPages(state_->textureManager, *sizeCache);
    }
    return prepared;
}

const FontGlyph* FontManager::GetGlyph(FontHandle font, float pixelSize, char32_t codepoint) {
    const FontHandle resolved = ResolveFont(font);
    if (!resolved.IsValid()) {
        return nullptr;
    }
    const uint32_t pixelKey = ResolvePixelKey(pixelSize);
    if (!EnsureGlyph(resolved, pixelKey, codepoint)) {
        return nullptr;
    }

    FontRecord* record = GetFontRecord(state_->fonts, resolved);
    if (record == nullptr) {
        return nullptr;
    }
    FontSizeCache* sizeCache = nullptr;
    try {
        sizeCache = &GetSizeCache(*record, pixelKey);
    } catch (const std::exception&) {
        return nullptr;
    }
    UploadDirtyPages(state_->textureManager, *sizeCache);
    const auto it = sizeCache->glyphs.find(codepoint);
    return it != sizeCache->glyphs.end() ? &it->second : nullptr;
}

FontMetrics FontManager::GetMetrics(FontHandle font, float pixelSize) {
    const FontHandle resolved = ResolveFont(font);
    if (!resolved.IsValid()) {
        return {};
    }
    FontRecord* record = GetFontRecord(state_->fonts, resolved);
    if (record == nullptr) {
        return {};
    }
    const uint32_t pixelKey = ResolvePixelKey(pixelSize);
    try {
        return GetSizeCache(*record, pixelKey).metrics;
    } catch (const std::exception&) {
        return {};
    }
}

size_t FontManager::GetFontCount() const {
    return state_->fonts.size();
}

size_t FontManager::GetAtlasPageCount() const {
    return std::accumulate(state_->fonts.begin(), state_->fonts.end(), size_t{0},
                           [](size_t count, const FontRecord& font) {
                               return count + std::accumulate(
                                                  font.sizes.begin(), font.sizes.end(), size_t{0},
                                                  [](size_t fontCount, const auto& entry) {
                                                      return fontCount + entry.second.pages.size();
                                                  });
                           });
}

bool FontManager::ReleaseAtlasTextures(bool allowFrameAbort) {
    if (state_->textureManager == nullptr) {
        return true;
    }

    for (FontRecord& font : state_->fonts) {
        for (auto& sizeEntry : font.sizes) {
            for (AtlasPage& page : sizeEntry.second.pages) {
                if (!state_->textureManager->IsValidTextureId(page.textureId)) {
                    page.textureId = kInvalidResourceId;
                    continue;
                }
                if (page.textureId == state_->textureManager->GetWhiteTextureId() ||
                    page.textureId == state_->textureManager->GetWhiteCubeTextureId() ||
                    page.textureId == state_->textureManager->GetBlackCubeTextureId() ||
                    page.textureId == state_->textureManager->GetDefaultNormalTextureId()) {
                    page.textureId = kInvalidResourceId;
                    continue;
                }
                if (!state_->textureManager->ReleaseTexture(page.textureId, allowFrameAbort)) {
                    return false;
                }
                page.textureId = kInvalidResourceId;
            }
        }
    }
    return true;
}

FontHandle FontManager::ResolveFont(FontHandle font) const {
    if (GetFontRecord(state_->fonts, font) != nullptr) {
        return font;
    }
    if (GetFontRecord(state_->fonts, state_->defaultFont) != nullptr) {
        return state_->defaultFont;
    }
    return {};
}

uint32_t FontManager::ResolvePixelKey(float pixelSize) {
    return PixelKeyFromSize(pixelSize);
}
