#pragma once

#include <DirectXMath.h>
#include <cstdint>

enum class ParticleFieldType : uint32_t {
    Directional = 0,
    Radial = 1,
    Vortex = 2,
    Drag = 3,
};

struct ParticleFieldSettings {
    ParticleFieldType type = ParticleFieldType::Directional;
    bool enabled = true;
    DirectX::XMFLOAT3 position{0.0f, 0.0f, 0.0f};
    float radius = 2.0f;
    DirectX::XMFLOAT3 direction{0.0f, 1.0f, 0.0f};
    float strength = 1.0f;
    float falloff = 1.0f;
};
