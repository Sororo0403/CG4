#pragma once
#include "graphics/PostEffectManager.h"

#include <vector>

struct PostEffectManager::Layer {
    PostEffectLayerId id = 0;
    int priority = 0;
    PostEffectLayerBlendMode blendMode = PostEffectLayerBlendMode::Overlay;
    bool enabled = true;
    bool hasProfile = false;
    PostProcessProfile profile{};
};

struct PostEffectManager::State {
    PostProcessSystem* system = nullptr;
    PostProcessProfile baseProfile{};
    PostProcessProfile composedProfile{};
    std::vector<Layer> layers;
    PostEffectLayerId nextLayerId = 1;
};
