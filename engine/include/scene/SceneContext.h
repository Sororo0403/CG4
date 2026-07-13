#pragma once
#include "core/FrameTimer.h"
#include "scene/SceneServices.h"

struct RenderContext;

/// <summary>
/// シーンへ渡すフレーム時間
/// </summary>
struct SceneFrameState {
    FrameTime frameTime{};
    float deltaTime = 0.0f;
};

/// <summary>
/// シーンが参照するサービスとフレーム状態をまとめる
/// </summary>
struct SceneContext {
    SceneSystemServices systems{};
    SceneRenderServices rendering{};
    const RenderContext* render = nullptr;
    SceneFrameState frame{};
};
