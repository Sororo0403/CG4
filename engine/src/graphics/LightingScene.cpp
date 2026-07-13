#include "graphics/LightingScene.h"

void LightingScene::BeginFrame() {
    lighting_ = SceneLighting{};
    stats_ = {};
}

void LightingScene::SetSceneLighting(const SceneLighting& lighting) {
    lighting_ = lighting;
    RefreshStats();
}

void LightingScene::RefreshStats() {
    stats_ = {};
    stats_.hasSun = lighting_.keyLightColor.w > 0.5f &&
                    (lighting_.keyLightColor.x > 0.0001f || lighting_.keyLightColor.y > 0.0001f ||
                     lighting_.keyLightColor.z > 0.0001f);
    for (const PointLight& pointLight : lighting_.pointLights) {
        if (pointLight.colorIntensity.w > 0.0001f && pointLight.positionRange.w > 0.0001f) {
            ++stats_.pointLightCount;
        }
    }

    const SpotLight& spotLight = lighting_.spotLight;
    if (spotLight.angleParams.w > 0.5f && spotLight.colorIntensity.w > 0.0001f &&
        spotLight.positionRange.w > 0.0001f) {
        stats_.spotLightCount = 1u;
    }
}
