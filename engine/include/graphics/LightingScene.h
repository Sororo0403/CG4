#pragma once

#include "graphics/Lighting.h"

#include <cstdint>

struct LightingSceneStats {
    uint32_t pointLightCount = 0;
    uint32_t spotLightCount = 0;
    bool hasSun = false;
};

class LightingScene {
public:
    void BeginFrame();

    void SetSceneLighting(const SceneLighting& lighting);
    const SceneLighting& GetSceneLighting() const {
        return lighting_;
    }

    const LightingSceneStats& GetStats() const {
        return stats_;
    }

private:
    void RefreshStats();

    SceneLighting lighting_{};
    LightingSceneStats stats_{};
};
