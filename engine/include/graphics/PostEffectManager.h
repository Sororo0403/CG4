#pragma once
#include "graphics/PostProcessSettings.h"
#include <cstdint>
#include <vector>

class PostProcessSystem;

using PostEffectLayerId = uint32_t;

enum class PostEffectLayerBlendMode : uint8_t {
    Override,
    Overlay,
};

struct PostEffectLayerDesc {
    int priority = 0;
    PostEffectLayerBlendMode blendMode = PostEffectLayerBlendMode::Overlay;
};

class PostEffectManager {
  public:
    /// <summary>
    /// 必要なリソースを初期化する
    /// </summary>
    void Initialize(PostProcessSystem *system);

    PostEffectLayerId CreateLayer(const PostEffectLayerDesc &desc = {});
    /// <summary>
    /// DestroyLayerを実行する
    /// </summary>
    void DestroyLayer(PostEffectLayerId id);

    void SetBaseProfile(const PostProcessProfile &profile);
    void SetLayerProfile(PostEffectLayerId id,
                         const PostProcessProfile &profile);
    /// <summary>
    /// ClearLayerを実行する
    /// </summary>
    void ClearLayer(PostEffectLayerId id);
    void ClearLayers();
    void SetLayerEnabled(PostEffectLayerId id, bool enabled);

    const PostProcessProfile &GetBaseProfile() const { return baseProfile_; }
    const PostProcessProfile &GetComposedProfile() const {
        return composedProfile_;
    }

  private:
    struct Layer {
        PostEffectLayerId id = 0;
        int priority = 0;
        PostEffectLayerBlendMode blendMode = PostEffectLayerBlendMode::Overlay;
        bool enabled = true;
        bool hasProfile = false;
        PostProcessProfile profile{};
    };

    Layer *FindLayer(PostEffectLayerId id);
    const Layer *FindLayer(PostEffectLayerId id) const;
    PostEffectLayerId AllocateLayerId();
    /// <summary>
    /// Rebuildを実行する
    /// </summary>
    void Rebuild();

    PostProcessSystem *system_ = nullptr;
    PostProcessProfile baseProfile_{};
    PostProcessProfile composedProfile_{};
    std::vector<Layer> layers_;
    PostEffectLayerId nextLayerId_ = 1;
};
