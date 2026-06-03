#pragma once

#include <DirectXMath.h>
#include <filesystem>
#include <string>

#include "nlohmann/json.hpp"

struct WorldState {
    std::string sceneName;
    uint32_t seed = 0;
    DirectX::XMFLOAT3 playerPosition{0.0f, 0.0f, 0.0f};
    DirectX::XMFLOAT3 cameraPosition{0.0f, 0.0f, 0.0f};
    DirectX::XMFLOAT3 cameraRotation{0.0f, 0.0f, 0.0f};
    nlohmann::json userData = nlohmann::json::object();
};

class WorldStateStore {
  public:
    static bool Save(const std::filesystem::path &path,
                     const WorldState &state);
    static bool Load(const std::filesystem::path &path, WorldState &state);
};
