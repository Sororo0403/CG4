#include "sprite/SpriteManager.h"
#include "graphics/DirectXCommon.h"
#include "sprite/Sprite.h"
#include "texture/TextureManager.h"
#include <algorithm>
#include <cmath>
#include <limits>
#include <numeric>

namespace {
float FiniteOr(float value, float fallback) {
    return std::isfinite(value) ? value : fallback;
}

Sprite &FallbackSprite() {
    static Sprite fallback{};
    return fallback;
}
} // namespace

SpriteManager &SpriteManager::GetInstance() {
    static SpriteManager instance;
    return instance;
}

void SpriteManager::Initialize(DirectXCommon *dxCommon,
                               TextureManager *textureManager,
                               SrvManager *srvManager, int width, int height) {
    if (!dxCommon || !textureManager || !srvManager) {
        dxCommon_ = nullptr;
        textureManager_ = nullptr;
        sprites_.clear();
        return;
    }

    spriteRenderer_.Initialize(dxCommon, textureManager, srvManager, width,
                               height);
    dxCommon_ = dxCommon;
    textureManager_ = textureManager;
    sprites_.clear();
}

void SpriteManager::Draw(uint32_t id) {
    if (!IsValidSpriteId(id)) {
        return;
    }
    spriteRenderer_.Draw(sprites_[id]);
}

void SpriteManager::DrawAllSorted(bool backToFront) {
    std::vector<size_t> indices(sprites_.size());
    std::iota(indices.begin(), indices.end(), size_t{0});
    std::stable_sort(indices.begin(), indices.end(),
                     [&](size_t lhs, size_t rhs) {
                         const float lhsZ = FiniteOr(sprites_[lhs].zOrder, 0.0f);
                         const float rhsZ = FiniteOr(sprites_[rhs].zOrder, 0.0f);
                         return backToFront
                                    ? lhsZ > rhsZ
                                    : lhsZ < rhsZ;
                     });

    for (size_t index : indices) {
        spriteRenderer_.Draw(sprites_[index]);
    }
}

void SpriteManager::DrawSprite(const Sprite &sprite) {
    spriteRenderer_.Draw(sprite);
}

uint32_t SpriteManager::Create(const std::wstring &filePath) {
    if (textureManager_ == nullptr) {
        return UINT32_MAX;
    }

    uint32_t texId = textureManager_->Load(filePath);

    Sprite sprite{};
    sprite.textureId = texId;
    sprite.position = {0.0f, 0.0f};
    sprite.size = {static_cast<float>(textureManager_->GetWidth(texId)),
                   static_cast<float>(textureManager_->GetHeight(texId))};
    sprite.uvLeftTop = {0.0f, 0.0f};
    sprite.uvSize = {1.0f, 1.0f};
    sprite.color = {1.0f, 1.0f, 1.0f, 1.0f};

    if (sprites_.size() >=
        static_cast<size_t>((std::numeric_limits<uint32_t>::max)())) {
        return UINT32_MAX;
    }
    sprites_.push_back(sprite);
    return static_cast<uint32_t>(sprites_.size() - 1);
}

void SpriteManager::BeginFrame() { spriteRenderer_.BeginFrame(); }

void SpriteManager::PreDraw(bool backBufferTarget) {
    spriteRenderer_.PreDraw(backBufferTarget);
}

void SpriteManager::PostDraw() { spriteRenderer_.PostDraw(); }

void SpriteManager::Resize(int width, int height) {
    spriteRenderer_.UpdateProjection(width, height);
}

bool SpriteManager::IsValidSpriteId(uint32_t id) const {
    return id < sprites_.size();
}

Sprite &SpriteManager::GetSprite(uint32_t id) {
    if (!IsValidSpriteId(id)) {
        return FallbackSprite();
    }
    return sprites_[id];
}

const Sprite &SpriteManager::GetSprite(uint32_t id) const {
    if (!IsValidSpriteId(id)) {
        return FallbackSprite();
    }
    return sprites_[id];
}
