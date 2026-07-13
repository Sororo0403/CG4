#include "font/TextRenderer.h"

#include "core/ResourceHandle.h"
#include "sprite/Sprite.h"
#include "sprite/SpriteRenderer.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <exception>
#include <new>
#include <vector>

namespace {

constexpr char32_t kReplacementCodepoint = 0xFFFDu;

bool IsContinuationByte(uint8_t value) {
    return (value & 0xC0u) == 0x80u;
}

bool AppendCodepoint(std::vector<char32_t>& codepoints, uint32_t value) {
    try {
        if (value > 0x10FFFFu || (value >= 0xD800u && value <= 0xDFFFu)) {
            codepoints.push_back(kReplacementCodepoint);
            return true;
        }
        codepoints.push_back(static_cast<char32_t>(value));
    } catch (const std::exception&) {
        return false;
    }
    return true;
}

std::vector<char32_t> DecodeUtf8(std::string_view text) {
    std::vector<char32_t> codepoints;
    try {
        codepoints.reserve(text.size());
    } catch (const std::exception&) {
        return {};
    }

    size_t index = 0u;
    while (index < text.size()) {
        const uint8_t first = static_cast<uint8_t>(text[index]);
        if (first < 0x80u) {
            try {
                codepoints.push_back(static_cast<char32_t>(first));
            } catch (const std::exception&) {
                return {};
            }
            ++index;
            continue;
        }

        uint32_t value = 0u;
        size_t length = 0u;
        uint32_t minimum = 0u;
        if ((first & 0xE0u) == 0xC0u) {
            value = first & 0x1Fu;
            length = 2u;
            minimum = 0x80u;
        } else if ((first & 0xF0u) == 0xE0u) {
            value = first & 0x0Fu;
            length = 3u;
            minimum = 0x800u;
        } else if ((first & 0xF8u) == 0xF0u) {
            value = first & 0x07u;
            length = 4u;
            minimum = 0x10000u;
        } else {
            try {
                codepoints.push_back(kReplacementCodepoint);
            } catch (const std::exception&) {
                return {};
            }
            ++index;
            continue;
        }

        if (index + length > text.size()) {
            try {
                codepoints.push_back(kReplacementCodepoint);
            } catch (const std::exception&) {
                return {};
            }
            break;
        }

        bool valid = true;
        for (size_t offset = 1u; offset < length; ++offset) {
            const uint8_t next = static_cast<uint8_t>(text[index + offset]);
            if (!IsContinuationByte(next)) {
                valid = false;
                break;
            }
            value = (value << 6u) | static_cast<uint32_t>(next & 0x3Fu);
        }

        if (!valid || value < minimum) {
            try {
                codepoints.push_back(kReplacementCodepoint);
            } catch (const std::exception&) {
                return {};
            }
            ++index;
            continue;
        }

        if (!AppendCodepoint(codepoints, value)) {
            return {};
        }
        index += length;
    }

    return codepoints;
}

std::vector<char32_t> DecodeWide(std::wstring_view text) {
    std::vector<char32_t> codepoints;
    try {
        codepoints.reserve(text.size());
    } catch (const std::exception&) {
        return {};
    }

    size_t index = 0u;
    while (index < text.size()) {
        const uint32_t value = static_cast<uint32_t>(text[index]);
        if constexpr (sizeof(wchar_t) == 2u) {
            if (value >= 0xD800u && value <= 0xDBFFu) {
                if (index + 1u < text.size()) {
                    const uint32_t low = static_cast<uint32_t>(text[index + 1u]);
                    if (low >= 0xDC00u && low <= 0xDFFFu) {
                        const uint32_t codepoint =
                            0x10000u + ((value - 0xD800u) << 10u) + (low - 0xDC00u);
                        try {
                            codepoints.push_back(static_cast<char32_t>(codepoint));
                        } catch (const std::exception&) {
                            return {};
                        }
                        index += 2u;
                        continue;
                    }
                }
                try {
                    codepoints.push_back(kReplacementCodepoint);
                } catch (const std::exception&) {
                    return {};
                }
                ++index;
                continue;
            }
            if (value >= 0xDC00u && value <= 0xDFFFu) {
                try {
                    codepoints.push_back(kReplacementCodepoint);
                } catch (const std::exception&) {
                    return {};
                }
                ++index;
                continue;
            }
        }

        if (!AppendCodepoint(codepoints, value)) {
            return {};
        }
        ++index;
    }

    return codepoints;
}

std::vector<char32_t> CopyCodepoints(std::u32string_view text) {
    try {
        return std::vector<char32_t>(text.begin(), text.end());
    } catch (const std::exception&) {
        return {};
    }
}

float ResolveLineSpacing(float lineSpacing) {
    return std::isfinite(lineSpacing) ? lineSpacing : 0.0f;
}

FontHandle ResolveStyleFont(const FontManager& fontManager, const TextStyle& style) {
    return style.font.IsValid() ? style.font : fontManager.GetDefaultFont();
}

TextLayoutMetrics MeasureCodepoints(FontManager& fontManager,
                                    const std::vector<char32_t>& codepoints,
                                    const TextStyle& style) {
    TextLayoutMetrics result{};
    if (codepoints.empty()) {
        return result;
    }

    const FontHandle font = ResolveStyleFont(fontManager, style);
    if (!font.IsValid()) {
        return result;
    }

    fontManager.PrepareGlyphs(font, style.pixelSize, codepoints);
    const FontMetrics fontMetrics = fontManager.GetMetrics(font, style.pixelSize);
    const float lineAdvance = fontMetrics.lineHeight + ResolveLineSpacing(style.lineSpacing);

    float currentWidth = 0.0f;
    float maxWidth = 0.0f;
    uint32_t lineCount = 1u;
    for (char32_t codepoint : codepoints) {
        if (codepoint == U'\r') {
            continue;
        }
        if (codepoint == U'\n') {
            maxWidth = (std::max)(maxWidth, currentWidth);
            currentWidth = 0.0f;
            ++lineCount;
            continue;
        }

        const FontGlyph* glyph = fontManager.GetGlyph(font, style.pixelSize, codepoint);
        if (glyph != nullptr) {
            currentWidth += glyph->advanceX;
        }
    }
    maxWidth = (std::max)(maxWidth, currentWidth);

    result.lineCount = lineCount;
    result.size.x = maxWidth;
    result.size.y = fontMetrics.lineHeight;
    if (lineCount > 1u) {
        result.size.y += static_cast<float>(lineCount - 1u) * lineAdvance;
    }
    return result;
}

void DrawCodepoints(FontManager& fontManager, SpriteRenderer& spriteRenderer,
                    const std::vector<char32_t>& codepoints, const DirectX::XMFLOAT2& position,
                    const TextStyle& style) {
    if (codepoints.empty()) {
        return;
    }

    const FontHandle font = ResolveStyleFont(fontManager, style);
    if (!font.IsValid()) {
        return;
    }

    fontManager.PrepareGlyphs(font, style.pixelSize, codepoints);
    const FontMetrics fontMetrics = fontManager.GetMetrics(font, style.pixelSize);
    const float lineAdvance = fontMetrics.lineHeight + ResolveLineSpacing(style.lineSpacing);

    float cursorX = position.x;
    float baselineY = position.y + fontMetrics.ascent;
    for (char32_t codepoint : codepoints) {
        if (codepoint == U'\r') {
            continue;
        }
        if (codepoint == U'\n') {
            cursorX = position.x;
            baselineY += lineAdvance;
            continue;
        }

        const FontGlyph* glyph = fontManager.GetGlyph(font, style.pixelSize, codepoint);
        if (glyph == nullptr) {
            continue;
        }

        if (glyph->visible && IsValidResourceId(glyph->textureId)) {
            Sprite sprite{};
            sprite.position = {cursorX + glyph->offset.x, baselineY + glyph->offset.y};
            sprite.size = glyph->size;
            sprite.uvLeftTop = glyph->uvLeftTop;
            sprite.uvSize = glyph->uvSize;
            sprite.color = style.color;
            sprite.textureId = glyph->textureId;
            sprite.zOrder = style.zOrder;
            sprite.blendMode = SpriteBlendMode::Alpha;
            spriteRenderer.Draw(sprite);
        }

        cursorX += glyph->advanceX;
    }
}

} // namespace

void TextRenderer::Initialize(FontManager* fontManager, SpriteRenderer* spriteRenderer) {
    fontManager_ = fontManager;
    spriteRenderer_ = spriteRenderer;
}

void TextRenderer::Finalize() {
    fontManager_ = nullptr;
    spriteRenderer_ = nullptr;
}

bool TextRenderer::IsReady() const {
    return fontManager_ != nullptr && spriteRenderer_ != nullptr && fontManager_->IsReady();
}

void TextRenderer::DrawText(std::string_view utf8Text, const DirectX::XMFLOAT2& position,
                            const TextStyle& style) {
    if (!IsReady()) {
        return;
    }
    DrawCodepoints(*fontManager_, *spriteRenderer_, DecodeUtf8(utf8Text), position, style);
}

void TextRenderer::DrawText(std::wstring_view text, const DirectX::XMFLOAT2& position,
                            const TextStyle& style) {
    if (!IsReady()) {
        return;
    }
    DrawCodepoints(*fontManager_, *spriteRenderer_, DecodeWide(text), position, style);
}

void TextRenderer::DrawText(std::u32string_view text, const DirectX::XMFLOAT2& position,
                            const TextStyle& style) {
    if (!IsReady()) {
        return;
    }
    DrawCodepoints(*fontManager_, *spriteRenderer_, CopyCodepoints(text), position, style);
}

TextLayoutMetrics TextRenderer::MeasureText(std::string_view utf8Text, const TextStyle& style) {
    if (!IsReady()) {
        return {};
    }
    return MeasureCodepoints(*fontManager_, DecodeUtf8(utf8Text), style);
}

TextLayoutMetrics TextRenderer::MeasureText(std::wstring_view text, const TextStyle& style) {
    if (!IsReady()) {
        return {};
    }
    return MeasureCodepoints(*fontManager_, DecodeWide(text), style);
}

TextLayoutMetrics TextRenderer::MeasureText(std::u32string_view text, const TextStyle& style) {
    if (!IsReady()) {
        return {};
    }
    return MeasureCodepoints(*fontManager_, CopyCodepoints(text), style);
}
