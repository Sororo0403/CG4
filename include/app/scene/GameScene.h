#pragma once
#include "BaseScene.h"
#include "DebugCamera.h"
#include "LevelLoader.h"
#include "SkyboxRenderer.h"
#include "Transform.h"
#include <DirectXMath.h>
#include <cstdint>
#include <filesystem>
#include <string>
#include <unordered_map>
#include <vector>

class GameScene : public BaseScene {
  public:
    void Initialize(const SceneContext &ctx) override;
    void Update() override;
    void Draw() override;

  private:
    uint32_t LoadSkyboxTexture();
    void LoadLevel(const std::filesystem::path &path);
    void InstantiateLevelObject(const LevelObjectData &object,
                                const std::filesystem::path &baseDirectory,
                                const DirectX::XMMATRIX &parentWorld);

    struct PlacedObject {
        std::string name;
        std::wstring sourcePath;
        uint32_t modelId = 0;
        Transform transform;
    };

  private:
    DebugCamera camera_;
    SkyboxRenderer skyboxRenderer_;
    uint32_t skyboxTextureId_ = 0;
    std::vector<PlacedObject> placedObjects_;
    std::unordered_map<std::wstring, uint32_t> modelCache_;
};
