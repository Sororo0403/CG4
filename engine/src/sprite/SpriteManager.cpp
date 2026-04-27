#include "SpriteManager.h"
#include "DirectXCommon.h"
#include "Sprite.h"
#include "TextureManager.h"

void SpriteManager::Initialize(DirectXCommon *dxCommon,
                               TextureManager *textureManager,
                               SrvManager *srvManager, int width, int height) {
    dxCommon_ = dxCommon;
    textureManager_ = textureManager;

    spriteRenderer_.Initialize(dxCommon, textureManager, srvManager, width,
                               height);
}

uint32_t SpriteManager::Create(const std::wstring &filePath) {
    // テクスチャ取得
    uint32_t texId = textureManager_->Load(filePath);

    Sprite sprite{};
    sprite.textureId = texId;
    sprite.position = {0.0f, 0.0f};
    sprite.size = {static_cast<float>(textureManager_->GetWidth(texId)),
                   static_cast<float>(textureManager_->GetHeight(texId))};
    sprite.color = {1.0f, 1.0f, 1.0f, 1.0f};

    sprites_.push_back(sprite);
    return static_cast<uint32_t>(sprites_.size() - 1);
}

void SpriteManager::Resize(int width, int height) {
    spriteRenderer_.UpdateProjection(width, height);
}

Sprite &SpriteManager::GetSprite(uint32_t id) { return sprites_.at(id); }

const Sprite &SpriteManager::GetSprite(uint32_t id) const {
    return sprites_.at(id);
}
