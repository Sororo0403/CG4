#include "graphics/PostEffectManager.h"

#include "graphics/PostProcessSystem.h"
#include "internal/PostEffectManagerInternal.h"
#include "internal/PostProcessProfileUtils.h"

#include <algorithm>
#include <array>
#include <exception>
#include <limits>
#include <new>

namespace {
using PostProcessProfileUtils::EnsureFinalToneMapEnabled;
using PostProcessProfileUtils::HasRandomNoise;
using PostProcessProfileUtils::HasSpecial;
using PostProcessProfileUtils::HasToon;
using PostProcessProfileUtils::HasVignette;

struct PostEffectCopyRule {
    bool (*enabled)(const PostProcessProfile&) = nullptr;
    void (*copy)(PostProcessProfile&, const PostProcessProfile&) = nullptr;
};

struct SpecialPayloadCopyRule {
    PostProcessSpecialMode mode = PostProcessSpecialMode::None;
    void (*copy)(PostProcessProfile&, const PostProcessProfile&) = nullptr;
};

bool HasColorGrade(const PostProcessProfile& profile) {
    return profile.colorGrade.mode != PostProcessColorMode::None;
}

bool HasFilter(const PostProcessProfile& profile) {
    return profile.filter.mode != PostProcessFilterMode::None;
}

bool HasEdge(const PostProcessProfile& profile) {
    return profile.edge.mode != PostProcessEdgeMode::None;
}

bool HasTonemap(const PostProcessProfile& profile) {
    return profile.tonemap.enabled;
}

bool HasBloom(const PostProcessProfile& profile) {
    return profile.bloom.enabled;
}

bool HasNoise(const PostProcessProfile& profile) {
    return profile.noise.enabled;
}

bool HasLensFlare(const PostProcessProfile& profile) {
    return profile.lensFlare.enabled;
}

void CopyColorGrade(PostProcessProfile& dst, const PostProcessProfile& src) {
    dst.colorGrade = src.colorGrade;
}

void CopyFilter(PostProcessProfile& dst, const PostProcessProfile& src) {
    dst.filter = src.filter;
}

void CopyEdge(PostProcessProfile& dst, const PostProcessProfile& src) {
    dst.edge = src.edge;
}

void CopyTonemap(PostProcessProfile& dst, const PostProcessProfile& src) {
    dst.tonemap = src.tonemap;
}

void CopyBloom(PostProcessProfile& dst, const PostProcessProfile& src) {
    dst.bloom = src.bloom;
}

void CopyNoise(PostProcessProfile& dst, const PostProcessProfile& src) {
    dst.noise = src.noise;
}

void CopyDissolve(PostProcessProfile& dst, const PostProcessProfile& src) {
    dst.dissolve = src.dissolve;
}

void CopySpecial(PostProcessProfile& dst, const PostProcessProfile& src) {
    dst.special = src.special;
    static const std::array<SpecialPayloadCopyRule, 1u> kSpecialPayloadRules = {{
        {PostProcessSpecialMode::Dissolve, CopyDissolve},
    }};
    for (const SpecialPayloadCopyRule& rule : kSpecialPayloadRules) {
        if (rule.mode == src.special.mode && rule.copy) {
            rule.copy(dst, src);
        }
    }
}

void CopyLensFlare(PostProcessProfile& dst, const PostProcessProfile& src) {
    dst.lensFlare = src.lensFlare;
}

const std::array<PostEffectCopyRule, 8>& EnabledPostEffectCopyRules() {
    static const std::array<PostEffectCopyRule, 8> kRules = {{
        {HasColorGrade, CopyColorGrade},
        {HasFilter, CopyFilter},
        {HasEdge, CopyEdge},
        {HasTonemap, CopyTonemap},
        {HasBloom, CopyBloom},
        {HasNoise, CopyNoise},
        {HasSpecial, CopySpecial},
        {HasLensFlare, CopyLensFlare},
    }};
    return kRules;
}

void ApplyEnabledPostEffects(PostProcessProfile& dst, const PostProcessProfile& overlay) {
    for (const PostEffectCopyRule& rule : EnabledPostEffectCopyRules()) {
        if (rule.enabled(overlay)) {
            rule.copy(dst, overlay);
        }
    }
}

void MergeOverlay(PostProcessProfile& dst, const PostProcessProfile& overlay) {
    ApplyEnabledPostEffects(dst, overlay);
    if (overlay.radialBlur.strength > dst.radialBlur.strength) {
        dst.radialBlur = overlay.radialBlur;
    }
    if (overlay.sceneDim.strength > dst.sceneDim.strength) {
        dst.sceneDim = overlay.sceneDim;
    }
    if (HasRandomNoise(overlay) && overlay.randomNoise.strength >= dst.randomNoise.strength) {
        dst.randomNoise = overlay.randomNoise;
    }
    if (HasToon(overlay)) {
        dst.toon = overlay.toon;
    }

    if (HasVignette(overlay)) {
        if (!dst.vignette.enabled || overlay.vignette.strength >= dst.vignette.strength) {
            dst.vignette.radius = overlay.vignette.radius;
            dst.vignette.scale = overlay.vignette.scale;
            dst.vignette.power = overlay.vignette.power;
        }
        dst.vignette.enabled = true;
        dst.vignette.strength = (std::max)(dst.vignette.strength, overlay.vignette.strength);
        if (overlay.vignette.primaryTintStrength >= dst.vignette.primaryTintStrength) {
            dst.vignette.primaryTintStrength = overlay.vignette.primaryTintStrength;
            std::copy(std::begin(overlay.vignette.primaryTintColor),
                      std::end(overlay.vignette.primaryTintColor),
                      std::begin(dst.vignette.primaryTintColor));
        }
        if (overlay.vignette.secondaryTintStrength >= dst.vignette.secondaryTintStrength) {
            dst.vignette.secondaryTintStrength = overlay.vignette.secondaryTintStrength;
            std::copy(std::begin(overlay.vignette.secondaryTintColor),
                      std::end(overlay.vignette.secondaryTintColor),
                      std::begin(dst.vignette.secondaryTintColor));
        }
    }
}

void MergeOverride(PostProcessProfile& dst, const PostProcessProfile& overlay) {
    ApplyEnabledPostEffects(dst, overlay);
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

void MergeLayer(PostProcessProfile& dst, const PostProcessProfile& overlay,
                PostEffectLayerBlendMode blendMode) {
    struct BlendStrategy {
        PostEffectLayerBlendMode mode;
        void (*merge)(PostProcessProfile&, const PostProcessProfile&);
    };
    static const std::array<BlendStrategy, 2> kStrategies = {{
        {PostEffectLayerBlendMode::Override, MergeOverride},
        {PostEffectLayerBlendMode::Overlay, MergeOverlay},
    }};
    const auto strategy =
        std::find_if(kStrategies.begin(), kStrategies.end(),
                     [blendMode](const BlendStrategy& entry) { return entry.mode == blendMode; });
    if (strategy != kStrategies.end()) {
        strategy->merge(dst, overlay);
    }
}
} // namespace

PostEffectManager::PostEffectManager() : state_(std::make_unique<State>()) {}

PostEffectManager::~PostEffectManager() = default;

const PostProcessProfile& PostEffectManager::GetBaseProfile() const {
    return state_->baseProfile;
}

const PostProcessProfile& PostEffectManager::GetComposedProfile() const {
    return state_->composedProfile;
}

bool PostEffectManager::IsReady() const {
    return state_->system != nullptr;
}

void PostEffectManager::Initialize(PostProcessSystem* system) {
    state_->system = system;
    Rebuild();
}

PostEffectLayerId PostEffectManager::CreateLayer(const PostEffectLayerDesc& desc) {
    Layer layer{};
    layer.id = AllocateLayerId();
    if (layer.id == 0) {
        return 0;
    }
    layer.priority = desc.priority;
    layer.blendMode = desc.blendMode;
    try {
        state_->layers.push_back(layer);
    } catch (const std::exception&) {
        return 0;
    }
    Rebuild();
    return layer.id;
}

void PostEffectManager::DestroyLayer(PostEffectLayerId id) {
    const auto newEnd = std::remove_if(state_->layers.begin(), state_->layers.end(),
                                       [id](const Layer& layer) { return layer.id == id; });
    if (newEnd == state_->layers.end()) {
        return;
    }
    state_->layers.erase(newEnd, state_->layers.end());
    Rebuild();
}

void PostEffectManager::SetBaseProfile(const PostProcessProfile& profile) {
    state_->baseProfile = profile;
    Rebuild();
}

void PostEffectManager::SetLayerProfile(PostEffectLayerId id, const PostProcessProfile& profile) {
    Layer* layer = FindLayer(id);
    if (!layer) {
        return;
    }
    layer->profile = profile;
    layer->hasProfile = true;
    Rebuild();
}

void PostEffectManager::ClearLayer(PostEffectLayerId id) {
    Layer* layer = FindLayer(id);
    if (!layer) {
        return;
    }
    layer->profile = {};
    layer->hasProfile = false;
    Rebuild();
}

void PostEffectManager::ClearLayers() {
    for (Layer& layer : state_->layers) {
        layer.profile = {};
        layer.hasProfile = false;
    }
    Rebuild();
}

void PostEffectManager::SetLayerEnabled(PostEffectLayerId id, bool enabled) {
    Layer* layer = FindLayer(id);
    if (!layer) {
        return;
    }
    layer->enabled = enabled;
    Rebuild();
}

PostEffectManager::Layer* PostEffectManager::FindLayer(PostEffectLayerId id) {
    const auto it = std::find_if(state_->layers.begin(), state_->layers.end(),
                                 [id](const Layer& layer) { return layer.id == id; });
    return it != state_->layers.end() ? &*it : nullptr;
}

const PostEffectManager::Layer* PostEffectManager::FindLayer(PostEffectLayerId id) const {
    const auto it = std::find_if(state_->layers.begin(), state_->layers.end(),
                                 [id](const Layer& layer) { return layer.id == id; });
    return it != state_->layers.end() ? &*it : nullptr;
}

PostEffectLayerId PostEffectManager::AllocateLayerId() {
    if (state_->layers.size() >=
        static_cast<size_t>((std::numeric_limits<PostEffectLayerId>::max)()) - 1u) {
        return 0;
    }

    for (;;) {
        if (state_->nextLayerId == 0) {
            state_->nextLayerId = 1;
        }
        const PostEffectLayerId candidate = state_->nextLayerId++;
        if (FindLayer(candidate) == nullptr) {
            return candidate;
        }
    }
}

void PostEffectManager::Rebuild() {
    state_->composedProfile = state_->baseProfile;

    std::vector<const Layer*> activeLayers;
    try {
        activeLayers.reserve(state_->layers.size());
        for (const Layer& layer : state_->layers) {
            if (layer.enabled && layer.hasProfile) {
                activeLayers.push_back(&layer);
            }
        }
    } catch (const std::exception&) {
        EnsureFinalToneMapEnabled(state_->composedProfile);
        if (state_->system) {
            state_->system->SetProfile(state_->composedProfile);
        }
        return;
    }
    std::sort(activeLayers.begin(), activeLayers.end(), [](const Layer* lhs, const Layer* rhs) {
        if (lhs->priority != rhs->priority) {
            return lhs->priority < rhs->priority;
        }
        return lhs->id < rhs->id;
    });

    for (const Layer* layer : activeLayers) {
        MergeLayer(state_->composedProfile, layer->profile, layer->blendMode);
    }

    EnsureFinalToneMapEnabled(state_->composedProfile);

    if (state_->system) {
        state_->system->SetProfile(state_->composedProfile);
    }
}
