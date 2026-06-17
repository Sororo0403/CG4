#pragma once

#include "graphics/Lighting.h"

#include <DirectXMath.h>
#include <cstdint>
#include <span>
#include <vector>

struct DirectionalLightDesc {
    DirectX::XMFLOAT3 direction{0.35f, 1.0f, -0.25f};
    float intensity = 1.0f;
    DirectX::XMFLOAT3 color{1.0f, 1.0f, 1.0f};
    bool castsShadow = true;
};

struct LocalLightDesc {
    DirectX::XMFLOAT3 position{0.0f, 0.0f, 0.0f};
    float range = 1.0f;
    DirectX::XMFLOAT3 color{1.0f, 1.0f, 1.0f};
    float intensity = 1.0f;
};

struct SpotLightDesc {
    DirectX::XMFLOAT3 position{0.0f, 0.0f, 0.0f};
    float range = 1.0f;
    DirectX::XMFLOAT3 direction{0.0f, -1.0f, 0.0f};
    float innerConeCos = 0.94f;
    DirectX::XMFLOAT3 color{1.0f, 0.86f, 0.58f};
    float intensity = 0.0f;
    float outerConeCos = 0.72f;
    float falloff = 2.4f;
    bool enabled = false;
};

struct ReflectionProbeDesc {
    DirectX::XMFLOAT3 position{0.0f, 0.0f, 0.0f};
    float radius = 8.0f;
    uint32_t resolution = 128u;
    bool dynamic = false;
};

struct LightingSceneStats {
    uint32_t pointLightCount = 0;
    uint32_t spotLightCount = 0;
    uint32_t reflectionProbeCount = 0;
    bool hasSun = false;
    bool hasLegacyBase = false;
};

class LightingScene {
  public:
    void BeginFrame();

    void SetLegacyBase(const SceneLighting &lighting);
    void SetSun(const DirectionalLightDesc &light);
    void AddPointLight(const LocalLightDesc &light);
    void AddSpotLight(const SpotLightDesc &light);
    void AddReflectionProbe(const ReflectionProbeDesc &probe);

    SceneLighting BuildLegacySceneLighting() const;

    std::span<const LocalLightDesc> PointLights() const;
    std::span<const SpotLightDesc> SpotLights() const;
    std::span<const ReflectionProbeDesc> ReflectionProbes() const;
    const LightingSceneStats &GetStats() const { return stats_; }

  private:
    SceneLighting legacyBase_{};
    DirectionalLightDesc sun_{};
    std::vector<LocalLightDesc> pointLights_;
    std::vector<SpotLightDesc> spotLights_;
    std::vector<ReflectionProbeDesc> reflectionProbes_;
    LightingSceneStats stats_{};
};
