#pragma once
#include <cstdint>

enum class RenderPass : uint8_t {
    None,
    Shadow,
    SceneColor,
    Transparent,
    PostEffect,
    Debug,
    UI,
    BackBuffer,

    Scene = SceneColor,
    PostProcess = PostEffect,
    DebugUi = UI,
};
