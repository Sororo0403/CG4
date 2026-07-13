#include "animation/Animator.h"

#include "animation/AnimationSampler.h"
#include "animation/SkeletonPoseBuilder.h"
#include "core/MathUtils.h"

#include <DirectXMath.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <new>

using namespace DirectX;

namespace {

void ResetRootAnimation(Model& model) {
    model.hasRootAnimation = false;
    XMStoreFloat4x4(&model.rootAnimationMatrix, XMMatrixIdentity());
}

struct AnimationPlaybackPolicy {
    bool loop = false;
    void (*finish)(Model&, const AnimationClip&) = nullptr;
};

void FinishLoopingPlayback(Model& model, const AnimationClip& clip) {
    if (model.animationTime >= clip.duration) {
        model.animationTime = std::fmod(model.animationTime, clip.duration);
    }
}

void FinishOneShotPlayback(Model& model, const AnimationClip& clip) {
    if (model.animationTime >= clip.duration) {
        model.animationTime = clip.duration;
        model.isPlaying = false;
        model.animationFinished = true;
    }
}

const std::array<AnimationPlaybackPolicy, 2>& AnimationPlaybackPolicies() {
    static const std::array<AnimationPlaybackPolicy, 2> kPolicies = {{
        {.loop = false, .finish = FinishOneShotPlayback},
        {.loop = true, .finish = FinishLoopingPlayback},
    }};
    return kPolicies;
}

const AnimationPlaybackPolicy& PlaybackPolicyFor(bool loop) {
    const auto& policies = AnimationPlaybackPolicies();
    const auto found = std::ranges::find_if(
        policies, [loop](const AnimationPlaybackPolicy& policy) { return policy.loop == loop; });
    return found != policies.end() ? *found : policies.front();
}

void AdvancePlayback(Model& model, const AnimationClip& clip, float deltaTime) {
    if (!model.isPlaying) {
        return;
    }

    if (!std::isfinite(model.animationTime) || model.animationTime < 0.0f) {
        model.animationTime = 0.0f;
    }
    const float safeDeltaTime = std::isfinite(deltaTime) ? (std::max)(deltaTime, 0.0f) : 0.0f;
    model.animationTime += safeDeltaTime;
    if (!std::isfinite(model.animationTime)) {
        model.animationTime = model.isLoop ? 0.0f : clip.duration;
    }

    PlaybackPolicyFor(model.isLoop).finish(model, clip);
}

} // namespace

void Animator::Play(Model& model, const std::string& animationName, bool loop) {
    auto it = model.animations.find(animationName);
    if (it == model.animations.end()) {
        return;
    }

    model.currentAnimation = animationName;
    model.animationTime = 0.0f;
    model.isLoop = loop;
    model.isPlaying = true;
    model.animationFinished = false;
}

bool Animator::IsFinished(const Model& model) {
    return model.animationFinished;
}

void Animator::ApplyBindPose(Model& model) {
    const size_t boneCount = model.bones.size();

    std::vector<XMMATRIX> localMatrices;
    SkeletonPoseBuilder::BuildBindPoseLocals(model, localMatrices);
    if (localMatrices.size() != boneCount) {
        return;
    }
    SkeletonPoseBuilder::UpdateSkeleton(model, localMatrices);
}

void Animator::Update(Model& model, float deltaTime) {
    const auto applyBindPoseIfPresent = [](Model& target) {
        ResetRootAnimation(target);
        if (!target.bones.empty()) {
            Animator::ApplyBindPose(target);
        }
    };

    if (model.currentAnimation.empty()) {
        applyBindPoseIfPresent(model);
        return;
    }

    auto clipIt = model.animations.find(model.currentAnimation);
    if (clipIt == model.animations.end()) {
        applyBindPoseIfPresent(model);
        return;
    }

    const AnimationClip& clip = clipIt->second;
    if (!std::isfinite(clip.duration) || clip.duration <= 0.0f) {
        applyBindPoseIfPresent(model);
        return;
    }

    AdvancePlayback(model, clip, deltaTime);

    if (model.bones.empty()) {
        ResetRootAnimation(model);

        if (!clip.rootNodeName.empty()) {
            auto rootIt = clip.nodeAnimations.find(clip.rootNodeName);
            if (rootIt == clip.nodeAnimations.end()) {
                return;
            }

            const NodeAnimation& rootAnim = rootIt->second;
            XMFLOAT3 pos =
                rootAnim.translate.keyframes.empty()
                    ? XMFLOAT3{0.0f, 0.0f, 0.0f}
                    : AnimationSampler::SampleVec3(rootAnim.translate, model.animationTime);
            XMFLOAT3 scl = rootAnim.scale.keyframes.empty()
                               ? XMFLOAT3{1.0f, 1.0f, 1.0f}
                               : AnimationSampler::SampleVec3(rootAnim.scale, model.animationTime);
            XMFLOAT4 rot = rootAnim.rotate.keyframes.empty()
                               ? XMFLOAT4{0.0f, 0.0f, 0.0f, 1.0f}
                               : AnimationSampler::SampleQuat(rootAnim.rotate, model.animationTime);

            XMMATRIX local =
                XMMatrixScaling(scl.x, scl.y, scl.z) *
                XMMatrixRotationQuaternion(MathUtils::LoadNormalizedQuaternionOrIdentity(rot)) *
                XMMatrixTranslation(pos.x, pos.y, pos.z);
            XMStoreFloat4x4(&model.rootAnimationMatrix, local);
            model.hasRootAnimation = true;
        }

        return;
    }

    const size_t boneCount = model.bones.size();

    std::vector<XMMATRIX> localMatrices;
    SkeletonPoseBuilder::BuildAnimatedLocals(model, clip, model.animationTime, localMatrices);
    if (localMatrices.size() != boneCount) {
        return;
    }
    SkeletonPoseBuilder::UpdateSkeleton(model, localMatrices);
}
