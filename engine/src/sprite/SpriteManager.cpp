#include "sprite/SpriteManager.h"
#include "graphics/DirectXCommon.h"
#include "sprite/Sprite.h"
#include "texture/TextureManager.h"
#include <algorithm>
#include <numeric>

SpriteManager &SpriteManager::GetInstance() {
    static SpriteManager instance;
    return instance;
}

void SpriteManager::Initialize(DirectXCommon *dxCommon,
                               TextureManager *textureManager,
                               SrvManager *srvManager, int width, int height) {
    dxCommon_ = dxCommon;
    textureManager_ = textureManager;

    spriteRenderer_.Initialize(dxCommon, textureManager, srvManager, width,
                               height);
}

void SpriteManager::Draw(uint32_t id) { spriteRenderer_.Draw(sprites_.at(id)); }

void SpriteManager::DrawAllSorted(bool backToFront) {
    std::vector<size_t> indices(sprites_.size());
    std::iota(indices.begin(), indices.end(), size_t{0});
    std::stable_sort(indices.begin(), indices.end(),
                     [&](size_t lhs, size_t rhs) {
                         return backToFront
                                    ? sprites_[lhs].zOrder > sprites_[rhs].zOrder
                                    : sprites_[lhs].zOrder < sprites_[rhs].zOrder;
                     });

    for (size_t index : indices) {
        spriteRenderer_.Draw(sprites_[index]);
    }
}

void SpriteManager::DrawSprite(const Sprite &sprite) {
    spriteRenderer_.Draw(sprite);
}

uint32_t SpriteManager::Create(const std::wstring &filePath) {

    uint32_t texId = textureManager_->Load(filePath);

    Sprite sprite{};
    sprite.textureId = texId;
    sprite.position = {0.0f, 0.0f};
    sprite.size = {static_cast<float>(textureManager_->GetWidth(texId)),
                   static_cast<float>(textureManager_->GetHeight(texId))};
    sprite.uvLeftTop = {0.0f, 0.0f};
    sprite.uvSize = {1.0f, 1.0f};
    sprite.color = {1.0f, 1.0f, 1.0f, 1.0f};

    sprites_.push_back(sprite);
    return static_cast<uint32_t>(sprites_.size() - 1);
}

void SpriteManager::BeginFrame() { spriteRenderer_.BeginFrame(); }

void SpriteManager::PreDraw() { spriteRenderer_.PreDraw(); }

void SpriteManager::PostDraw() { spriteRenderer_.PostDraw(); }

void SpriteManager::Resize(int width, int height) {
    spriteRenderer_.UpdateProjection(width, height);
}

Sprite &SpriteManager::GetSprite(uint32_t id) { return sprites_.at(id); }

const Sprite &SpriteManager::GetSprite(uint32_t id) const {
    return sprites_.at(id);
}
