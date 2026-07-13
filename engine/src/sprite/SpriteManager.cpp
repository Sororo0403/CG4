#include "sprite/SpriteManager.h"

#include "core/Numeric.h"
#include "core/ResourceHandle.h"
#include "graphics/DirectXCommon.h"
#include "sprite/Sprite.h"
#include "texture/TextureManager.h"

#include <algorithm>
#include <cmath>
#include <exception>
#include <limits>
#include <new>
#include <numeric>

namespace {
using Numeric::FiniteOr;

Sprite& FallbackSprite() {
    static Sprite fallback{};
    return fallback;
}

bool SpriteSortCacheMatches(const std::vector<Sprite>& sprites, const std::vector<float>& zOrders) {
    if (sprites.size() != zOrders.size()) {
        return false;
    }
    for (size_t index = 0; index < sprites.size(); ++index) {
        if (FiniteOr(sprites[index].zOrder, 0.0f) != zOrders[index]) {
            return false;
        }
    }
    return true;
}
} // namespace

void SpriteManager::Initialize(DirectXCommon* dxCommon, TextureManager* textureManager,
                               SrvManager* srvManager, int width, int height) {
    if (!dxCommon || !textureManager || !srvManager) {
        Finalize();
        return;
    }

    Finalize();
    spriteRenderer_.Initialize(dxCommon, textureManager, srvManager, width, height);
    dxCommon_ = dxCommon;
    textureManager_ = textureManager;
    sprites_.clear();
    sortedIndices_.clear();
    sortedZOrders_.clear();
    sortedCacheValid_ = false;
}

void SpriteManager::Finalize() {
    spriteRenderer_.Finalize();
    dxCommon_ = nullptr;
    textureManager_ = nullptr;
    sprites_.clear();
    sortedIndices_.clear();
    sortedZOrders_.clear();
    sortedCacheValid_ = false;
}

void SpriteManager::Draw(uint32_t id) {
    if (!IsValidSpriteId(id)) {
        return;
    }
    spriteRenderer_.Draw(sprites_[id]);
}

void SpriteManager::DrawAllSorted(bool backToFront) {
    const bool needsSort = !sortedCacheValid_ || sortedBackToFront_ != backToFront ||
                           !SpriteSortCacheMatches(sprites_, sortedZOrders_);
    if (needsSort) {
        try {
            sortedIndices_.resize(sprites_.size());
            sortedZOrders_.resize(sprites_.size());
        } catch (const std::exception&) {
            sortedIndices_.clear();
            sortedZOrders_.clear();
            sortedCacheValid_ = false;
            for (const Sprite& sprite : sprites_) {
                spriteRenderer_.Draw(sprite);
            }
            return;
        }
        std::iota(sortedIndices_.begin(), sortedIndices_.end(), size_t{0});
        for (size_t index = 0; index < sprites_.size(); ++index) {
            sortedZOrders_[index] = FiniteOr(sprites_[index].zOrder, 0.0f);
        }
        std::sort(sortedIndices_.begin(), sortedIndices_.end(), [&](size_t lhs, size_t rhs) {
            const float lhsZ = sortedZOrders_[lhs];
            const float rhsZ = sortedZOrders_[rhs];
            if (lhsZ == rhsZ) {
                return lhs < rhs;
            }
            return backToFront ? lhsZ > rhsZ : lhsZ < rhsZ;
        });
        sortedBackToFront_ = backToFront;
        sortedCacheValid_ = true;
    }

    for (size_t index : sortedIndices_) {
        spriteRenderer_.Draw(sprites_[index]);
    }
}

void SpriteManager::DrawSprite(const Sprite& sprite) {
    spriteRenderer_.Draw(sprite);
}

uint32_t SpriteManager::Create(const std::wstring& filePath) {
    if (textureManager_ == nullptr) {
        return kInvalidResourceId;
    }
    if (sprites_.size() >= static_cast<size_t>((std::numeric_limits<uint32_t>::max)())) {
        return kInvalidResourceId;
    }

    uint32_t texId = textureManager_->Load(filePath);
    if (!textureManager_->IsValidTextureId(texId)) {
        return kInvalidResourceId;
    }

    const uint32_t textureWidth = textureManager_->GetWidth(texId);
    const uint32_t textureHeight = textureManager_->GetHeight(texId);
    if (textureWidth == 0 || textureHeight == 0) {
        return kInvalidResourceId;
    }

    Sprite sprite{};
    sprite.textureId = texId;
    sprite.position = {0.0f, 0.0f};
    sprite.size = {static_cast<float>(textureWidth), static_cast<float>(textureHeight)};
    sprite.uvLeftTop = {0.0f, 0.0f};
    sprite.uvSize = {1.0f, 1.0f};
    sprite.color = {1.0f, 1.0f, 1.0f, 1.0f};

    try {
        sprites_.push_back(sprite);
    } catch (const std::exception&) {
        return kInvalidResourceId;
    }
    sortedCacheValid_ = false;
    return static_cast<uint32_t>(sprites_.size() - 1);
}

void SpriteManager::BeginFrame() {
    spriteRenderer_.BeginFrame();
}

void SpriteManager::PreDraw(bool backBufferTarget) {
    spriteRenderer_.PreDraw(backBufferTarget);
}

void SpriteManager::PostDraw() {
    spriteRenderer_.PostDraw();
}

void SpriteManager::Resize(int width, int height) {
    spriteRenderer_.UpdateProjection(width, height);
}

bool SpriteManager::IsValidSpriteId(uint32_t id) const {
    return id < sprites_.size();
}

Sprite& SpriteManager::GetSprite(uint32_t id) {
    if (!IsValidSpriteId(id)) {
        return FallbackSprite();
    }
    return sprites_[id];
}

const Sprite& SpriteManager::GetSprite(uint32_t id) const {
    if (!IsValidSpriteId(id)) {
        return FallbackSprite();
    }
    return sprites_[id];
}
