#pragma once
#include "graphics/PostProcessSettings.h"

#include <cstdint>
#include <memory>

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
    PostEffectManager();
    ~PostEffectManager();

    /// <summary>
    /// 必要なリソースを初期化する
    /// </summary>
    void Initialize(PostProcessSystem* system);

    PostEffectLayerId CreateLayer(const PostEffectLayerDesc& desc = {});
    /// <summary>
    /// DestroyLayerを実行する
    /// </summary>
    void DestroyLayer(PostEffectLayerId id);

    void SetBaseProfile(const PostProcessProfile& profile);
    void SetLayerProfile(PostEffectLayerId id, const PostProcessProfile& profile);
    /// <summary>
    /// ClearLayerを実行する
    /// </summary>
    void ClearLayer(PostEffectLayerId id);
    void ClearLayers();
    void SetLayerEnabled(PostEffectLayerId id, bool enabled);

    const PostProcessProfile& GetBaseProfile() const;
    const PostProcessProfile& GetComposedProfile() const;
    bool IsReady() const;

private:
    struct Layer;
    struct State;

    Layer* FindLayer(PostEffectLayerId id);
    const Layer* FindLayer(PostEffectLayerId id) const;
    PostEffectLayerId AllocateLayerId();
    /// <summary>
    /// Rebuildを実行する
    /// </summary>
    void Rebuild();

    std::unique_ptr<State> state_;
};
