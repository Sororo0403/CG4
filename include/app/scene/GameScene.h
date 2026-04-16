#pragma once
#include "BaseScene.h"
#include "DebugCamera.h"
#include "SkyboxRenderer.h"
#include <cstdint>

class GameScene : public BaseScene {
  public:
    void Initialize(const SceneContext &ctx) override;
    void Update() override;
    void Draw() override;

  private:
    uint32_t LoadSkyboxTexture();

  private:
    DebugCamera camera_;
    SkyboxRenderer skyboxRenderer_;
    uint32_t skyboxTextureId_ = 0;
};
