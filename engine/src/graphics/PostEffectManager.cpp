#include "graphics/PostEffectManager.h"
#include "graphics/PostProcessSystem.h"
#include <algorithm>
#include <limits>

namespace {
bool HasSpecial(const PostProcessProfile &profile) {
    return profile.special.mode == PostProcessSpecialMode::Vignette ||
           profile.special.mode == PostProcessSpecialMode::Dissolve;
}

bool HasVignette(const PostProcessProfile &profile) {
    return (profile.vignette.enabled && profile.vignette.strength > 0.0f) ||
           profile.vignette.primaryTintStrength > 0.0f ||
           profile.vignette.secondaryTintStrength > 0.0f;
}

bool HasRandomNoise(const PostProcessProfile &profile) {
    return profile.randomNoise.mode != PostProcessRandomMode::None &&
           profile.randomNoise.strength > 0.0f;
}

bool HasToon(const PostProcessProfile &profile) {
    return profile.toon.enabled && profile.toon.strength > 0.0f;
}

void MergeOverlay(PostProcessProfile &dst, const PostProcessProfile &overlay) {
    if (overlay.colorGrade.mode != PostProcessColorMode::None) {
        dst.colorGrade = overlay.colorGrade;
    }
    if (overlay.filter.mode != PostProcessFilterMode::None) {
        dst.filter = overlay.filter;
    }
    if (overlay.edge.mode != PostProcessEdgeMode::None) {
        dst.edge = overlay.edge;
    }
    if (overlay.tonemap.enabled) {
        dst.tonemap = overlay.tonemap;
    }
    if (overlay.bloom.enabled) {
        dst.bloom = overlay.bloom;
    }
    if (overlay.noise.enabled) {
        dst.noise = overlay.noise;
    }
    if (HasSpecial(overlay)) {
        dst.special = overlay.special;
        if (overlay.special.mode == PostProcessSpecialMode::Dissolve) {
            dst.dissolve = overlay.dissolve;
        }
    }
    if (overlay.lensFlare.enabled) {
        dst.lensFlare = overlay.lensFlare;
    }

    if (overlay.radialBlur.strength > dst.radialBlur.strength) {
        dst.radialBlur = overlay.radialBlur;
    }
    if (overlay.sceneDim.strength > dst.sceneDim.strength) {
        dst.sceneDim = overlay.sceneDim;
    }
    if (HasRandomNoise(overlay) &&
        overlay.randomNoise.strength >= dst.randomNoise.strength) {
        dst.randomNoise = overlay.randomNoise;
    }
    if (HasToon(overlay)) {
        dst.toon = overlay.toon;
    }

    if (HasVignette(overlay)) {
        if (!dst.vignette.enabled ||
            overlay.vignette.strength >= dst.vignette.strength) {
            dst.vignette.radius = overlay.vignette.radius;
            dst.vignette.scale = overlay.vignette.scale;
            dst.vignette.power = overlay.vignette.power;
        }
        dst.vignette.enabled = true;
        dst.vignette.strength =
            (std::max)(dst.vignette.strength, overlay.vignette.strength);
        if (overlay.vignette.primaryTintStrength >=
            dst.vignette.primaryTintStrength) {
            dst.vignette.primaryTintStrength =
                overlay.vignette.primaryTintStrength;
            std::copy(std::begin(overlay.vignette.primaryTintColor),
                      std::end(overlay.vignette.primaryTintColor),
                      std::begin(dst.vignette.primaryTintColor));
        }
        if (overlay.vignette.secondaryTintStrength >=
            dst.vignette.secondaryTintStrength) {
            dst.vignette.secondaryTintStrength =
                overlay.vignette.secondaryTintStrength;
            std::copy(std::begin(overlay.vignette.secondaryTintColor),
                      std::end(overlay.vignette.secondaryTintColor),
                      std::begin(dst.vignette.secondaryTintColor));
        }
    }
}

void MergeOverride(PostProcessProfile &dst, const PostProcessProfile &overlay) {
    if (overlay.colorGrade.mode != PostProcessColorMode::None) {
        dst.colorGrade = overlay.colorGrade;
    }
    if (overlay.filter.mode != PostProcessFilterMode::None) {
        dst.filter = overlay.filter;
    }
    if (overlay.edge.mode != PostProcessEdgeMode::None) {
        dst.edge = overlay.edge;
    }
    if (overlay.tonemap.enabled) {
        dst.tonemap = overlay.tonemap;
    }
    if (overlay.bloom.enabled) {
        dst.bloom = overlay.bloom;
    }
    if (overlay.noise.enabled) {
        dst.noise = overlay.noise;
    }
    if (HasSpecial(overlay)) {
        dst.special = overlay.special;
        if (overlay.special.mode == PostProcessSpecialMode::Dissolve) {
            dst.dissolve = overlay.dissolve;
        }
    }
    if (overlay.lensFlare.enabled) {
        dst.lensFlare = overlay.lensFlare;
    }
    if (overlay.radialBlur.strength > 0.0f) {
        dst.radialBlur = overlay.radialBlur;
    }
    if (overlay.sceneDim.strength > 0.0f) {
        dst.sceneDim = overlay.sceneDim;
    }
    if (HasRandomNoise(overlay)) {
        dst.randomNoise = overlay.randomNoise;
    }
    if (HasToon(overlay)) {
        dst.toon = overlay.toon;
    }
    if (HasVignette(overlay)) {
        dst.vignette = overlay.vignette;
    }
}

void MergeLayer(PostProcessProfile &dst, const PostProcessProfile &overlay,
                PostEffectLayerBlendMode blendMode) {
    switch (blendMode) {
    case PostEffectLayerBlendMode::Override:
        MergeOverride(dst, overlay);
        break;
    case PostEffectLayerBlendMode::Overlay:
    default:
        MergeOverlay(dst, overlay);
        break;
    }
}
} // namespace

void PostEffectManager::Initialize(PostProcessSystem *system) {
    system_ = system;
    Rebuild();
}

PostEffectLayerId
PostEffectManager::CreateLayer(const PostEffectLayerDesc &desc) {
    Layer layer{};
    layer.id = AllocateLayerId();
    if (layer.id == 0) {
        return 0;
    }
    layer.priority = desc.priority;
    layer.blendMode = desc.blendMode;
    layers_.push_back(layer);
    Rebuild();
    return layer.id;
}

void PostEffectManager::DestroyLayer(PostEffectLayerId id) {
    const auto newEnd =
        std::remove_if(layers_.begin(), layers_.end(),
                       [id](const Layer &layer) { return layer.id == id; });
    if (newEnd == layers_.end()) {
        return;
    }
    layers_.erase(newEnd, layers_.end());
    Rebuild();
}

void PostEffectManager::SetBaseProfile(const PostProcessProfile &profile) {
    baseProfile_ = profile;
    Rebuild();
}

void PostEffectManager::SetLayerProfile(PostEffectLayerId id,
                                        const PostProcessProfile &profile) {
    Layer *layer = FindLayer(id);
    if (!layer) {
        return;
    }
    layer->profile = profile;
    layer->hasProfile = true;
    Rebuild();
}

void PostEffectManager::ClearLayer(PostEffectLayerId id) {
    Layer *layer = FindLayer(id);
    if (!layer) {
        return;
    }
    layer->profile = {};
    layer->hasProfile = false;
    Rebuild();
}

void PostEffectManager::ClearLayers() {
    for (Layer &layer : layers_) {
        layer.profile = {};
        layer.hasProfile = false;
    }
    Rebuild();
}

void PostEffectManager::SetLayerEnabled(PostEffectLayerId id, bool enabled) {
    Layer *layer = FindLayer(id);
    if (!layer) {
        return;
    }
    layer->enabled = enabled;
    Rebuild();
}

PostEffectManager::Layer *PostEffectManager::FindLayer(PostEffectLayerId id) {
    const auto it =
        std::find_if(layers_.begin(), layers_.end(),
                     [id](const Layer &layer) { return layer.id == id; });
    return it != layers_.end() ? &*it : nullptr;
}

const PostEffectManager::Layer *
PostEffectManager::FindLayer(PostEffectLayerId id) const {
    const auto it =
        std::find_if(layers_.begin(), layers_.end(),
                     [id](const Layer &layer) { return layer.id == id; });
    return it != layers_.end() ? &*it : nullptr;
}

PostEffectLayerId PostEffectManager::AllocateLayerId() {
    if (layers_.size() >=
        static_cast<size_t>((std::numeric_limits<PostEffectLayerId>::max)()) -
            1u) {
        return 0;
    }

    for (;;) {
        if (nextLayerId_ == 0) {
            nextLayerId_ = 1;
        }
        const PostEffectLayerId candidate = nextLayerId_++;
        if (FindLayer(candidate) == nullptr) {
            return candidate;
        }
    }
}

void PostEffectManager::Rebuild() {
    composedProfile_ = baseProfile_;

    std::vector<const Layer *> activeLayers;
    activeLayers.reserve(layers_.size());
    for (const Layer &layer : layers_) {
        if (layer.enabled && layer.hasProfile) {
            activeLayers.push_back(&layer);
        }
    }
    std::sort(activeLayers.begin(), activeLayers.end(),
              [](const Layer *lhs, const Layer *rhs) {
                  if (lhs->priority != rhs->priority) {
                      return lhs->priority < rhs->priority;
                  }
                  return lhs->id < rhs->id;
              });

    for (const Layer *layer : activeLayers) {
        MergeLayer(composedProfile_, layer->profile, layer->blendMode);
    }

    if (system_) {
        system_->SetProfile(composedProfile_);
    }
}
