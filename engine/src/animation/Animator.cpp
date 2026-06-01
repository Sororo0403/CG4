#include "animation/Animator.h"
#include "animation/AnimationSampler.h"
#include "animation/SkeletonPoseBuilder.h"
#include <DirectXMath.h>
#include <algorithm>
#include <cmath>

using namespace DirectX;

namespace {

constexpr float kQuaternionEpsilon = 0.000001f;

XMVECTOR LoadNormalizedQuaternionOrIdentity(const XMFLOAT4 &rotation) {
    if (!std::isfinite(rotation.x) || !std::isfinite(rotation.y) ||
        !std::isfinite(rotation.z) || !std::isfinite(rotation.w)) {
        return XMQuaternionIdentity();
    }
    XMVECTOR q = XMLoadFloat4(&rotation);
    const float lengthSq = XMVectorGetX(XMVector4LengthSq(q));
    if (!std::isfinite(lengthSq) || lengthSq <= kQuaternionEpsilon) {
        return XMQuaternionIdentity();
    }
    return XMQuaternionNormalize(q);
}

} // namespace

void Animator::Play(Model &model, const std::string &animationName, bool loop) {
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

bool Animator::IsFinished(const Model &model) const {
    return model.animationFinished;
}

void Animator::ApplyBindPose(Model &model) {
    const size_t boneCount = model.bones.size();

    if (model.skeletonSpaceMatrices.size() != boneCount) {
        model.skeletonSpaceMatrices.resize(boneCount);
    }

    if (model.finalBoneMatrices.size() != boneCount) {
        model.finalBoneMatrices.resize(boneCount);
    }

    std::vector<XMMATRIX> localMatrices;
    SkeletonPoseBuilder::BuildBindPoseLocals(model, localMatrices);
    SkeletonPoseBuilder::UpdateSkeleton(model, localMatrices);
}

void Animator::Update(Model &model, float deltaTime) {
    if (model.currentAnimation.empty()) {
        model.hasRootAnimation = false;
        XMStoreFloat4x4(&model.rootAnimationMatrix, XMMatrixIdentity());
        if (!model.bones.empty()) {
            ApplyBindPose(model);
        }
        return;
    }

    auto clipIt = model.animations.find(model.currentAnimation);
    if (clipIt == model.animations.end()) {
        model.hasRootAnimation = false;
        XMStoreFloat4x4(&model.rootAnimationMatrix, XMMatrixIdentity());
        if (!model.bones.empty()) {
            ApplyBindPose(model);
        }
        return;
    }

    const AnimationClip &clip = clipIt->second;
    if (!std::isfinite(clip.duration) || clip.duration <= 0.0f) {
        model.hasRootAnimation = false;
        XMStoreFloat4x4(&model.rootAnimationMatrix, XMMatrixIdentity());
        if (!model.bones.empty()) {
            ApplyBindPose(model);
        }
        return;
    }

    if (model.isPlaying) {
        if (!std::isfinite(model.animationTime) || model.animationTime < 0.0f) {
            model.animationTime = 0.0f;
        }
        const float safeDeltaTime =
            std::isfinite(deltaTime) ? (std::max)(deltaTime, 0.0f) : 0.0f;
        model.animationTime += safeDeltaTime;
        if (!std::isfinite(model.animationTime)) {
            model.animationTime = model.isLoop ? 0.0f : clip.duration;
        }
        if (model.isLoop) {
            if (model.animationTime >= clip.duration) {
                model.animationTime = std::fmod(model.animationTime,
                                                clip.duration);
            }
        } else if (model.animationTime >= clip.duration) {
            model.animationTime = clip.duration;
            model.isPlaying = false;
            model.animationFinished = true;
        }
    }

    if (model.bones.empty()) {
        model.hasRootAnimation = false;
        XMStoreFloat4x4(&model.rootAnimationMatrix, XMMatrixIdentity());

        if (!clip.rootNodeName.empty()) {
            auto rootIt = clip.nodeAnimations.find(clip.rootNodeName);
            if (rootIt == clip.nodeAnimations.end()) {
                return;
            }

            const NodeAnimation &rootAnim = rootIt->second;
            XMFLOAT3 pos = rootAnim.translate.keyframes.empty()
                               ? XMFLOAT3{0.0f, 0.0f, 0.0f}
                               : AnimationSampler::SampleVec3(
                                     rootAnim.translate, model.animationTime);
            XMFLOAT3 scl = rootAnim.scale.keyframes.empty()
                               ? XMFLOAT3{1.0f, 1.0f, 1.0f}
                               : AnimationSampler::SampleVec3(
                                     rootAnim.scale, model.animationTime);
            XMFLOAT4 rot = rootAnim.rotate.keyframes.empty()
                               ? XMFLOAT4{0.0f, 0.0f, 0.0f, 1.0f}
                               : AnimationSampler::SampleQuat(
                                     rootAnim.rotate, model.animationTime);

            XMMATRIX local = XMMatrixScaling(scl.x, scl.y, scl.z) *
                             XMMatrixRotationQuaternion(
                                 LoadNormalizedQuaternionOrIdentity(rot)) *
                             XMMatrixTranslation(pos.x, pos.y, pos.z);
            XMStoreFloat4x4(&model.rootAnimationMatrix, local);
            model.hasRootAnimation = true;
        }

        return;
    }

    const size_t boneCount = model.bones.size();
    if (model.skeletonSpaceMatrices.size() != boneCount) {
        model.skeletonSpaceMatrices.resize(boneCount);
    }
    if (model.finalBoneMatrices.size() != boneCount) {
        model.finalBoneMatrices.resize(boneCount);
    }

    std::vector<XMMATRIX> localMatrices;
    SkeletonPoseBuilder::BuildAnimatedLocals(model, clip, model.animationTime,
                                             localMatrices);
    SkeletonPoseBuilder::UpdateSkeleton(model, localMatrices);
}
