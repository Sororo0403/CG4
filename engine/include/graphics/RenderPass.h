#pragma once
#include <cstdint>

enum class RenderPass : uint8_t {
    None,
    Shadow,
    SceneColor,
    Foreground3D,
    Transparent,
    VolumetricLighting,
    PostProcess,
    Debug,
    UI,
    BackBuffer,

    Scene = SceneColor,
    DebugUi = UI,
};
