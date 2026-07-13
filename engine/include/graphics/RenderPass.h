#pragma once
#include <cstdint>

enum class RenderPass : uint8_t {
    None = 0,
    Shadow = 1,
    SceneColor = 2,
    Foreground3D = 3,
    Transparent = 4,
    VolumetricLighting = 5,
    PostProcess = 6,
    Debug = 7,
    UI = 8,
    BackBuffer = 9,

    Scene = SceneColor,
    DebugUi = UI,
};
