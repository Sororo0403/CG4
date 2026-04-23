#include "Animator.h"
#include <DirectXMath.h>
#include <cmath>

using namespace DirectX;

static XMFLOAT3 LerpVec3(const XMFLOAT3 &a, const XMFLOAT3 &b, float t) {
    return {
        a.x + (b.x - a.x) * t,
        a.y + (b.y - a.y) * t,
        a.z + (b.z - a.z) * t,
    };
}

static float SafeInv(float x) {
    if (std::fabs(x) < 0.000001f) {
        return 0.0f;
    }
    return 1.0f / x;
}

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

XMFLOAT3
Animator::SampleVec3(const AnimationCurve<XMFLOAT3> &curve, float time) {
    const std::vector<Keyframe<XMFLOAT3>> &keys = curve.keyframes;
    if (keys.empty()) {
        return {0.0f, 0.0f, 0.0f};
    }

    if (keys.size() == 1 || time <= keys.front().time) {
        return keys.front().value;
    }

    if (time >= keys.back().time) {
        return keys.back().value;
    }

    for (size_t i = 0; i + 1 < keys.size(); i++) {
        const auto &k0 = keys[i];
        const auto &k1 = keys[i + 1];

        if (time >= k0.time && time <= k1.time) {
            float len = k1.time - k0.time;
            float t = (time - k0.time) * SafeInv(len);
            return LerpVec3(k0.value, k1.value, t);
        }
    }

    return keys.back().value;
}

XMFLOAT4
Animator::SampleQuat(const AnimationCurve<XMFLOAT4> &curve, float time) {
    const std::vector<Keyframe<XMFLOAT4>> &keys = curve.keyframes;
    if (keys.empty()) {
        return {0.0f, 0.0f, 0.0f, 1.0f};
    }

    if (keys.size() == 1 || time <= keys.front().time) {
        return keys.front().value;
    }

    if (time >= keys.back().time) {
        return keys.back().value;
    }

    for (size_t i = 0; i + 1 < keys.size(); i++) {
        const auto &k0 = keys[i];
        const auto &k1 = keys[i + 1];

        if (time >= k0.time && time <= k1.time) {
            float len = k1.time - k0.time;
            float t = (time - k0.time) * SafeInv(len);

            XMVECTOR q0 = XMLoadFloat4(&k0.value);
            XMVECTOR q1 = XMLoadFloat4(&k1.value);
            XMVECTOR q = XMQuaternionSlerp(q0, q1, t);
            q = XMQuaternionNormalize(q);

            XMFLOAT4 result;
            XMStoreFloat4(&result, q);
            return result;
        }
    }

    return keys.back().value;
}

XMMATRIX Animator::MakeAnimatedLocalMatrix(const BoneInfo &bone,
                                           const AnimationClip &clip,
                                           float time) {
    auto it = clip.nodeAnimations.find(bone.name);
    if (it == clip.nodeAnimations.end()) {
        return XMLoadFloat4x4(&bone.localBindMatrix);
    }

    const NodeAnimation &anim = it->second;

    XMFLOAT3 pos = anim.translate.keyframes.empty()
                       ? XMFLOAT3{0.0f, 0.0f, 0.0f}
                       : SampleVec3(anim.translate, time);

    XMFLOAT3 scl = anim.scale.keyframes.empty() ? XMFLOAT3{1.0f, 1.0f, 1.0f}
                                                : SampleVec3(anim.scale, time);

    XMFLOAT4 rot = anim.rotate.keyframes.empty()
                       ? XMFLOAT4{0.0f, 0.0f, 0.0f, 1.0f}
                       : SampleQuat(anim.rotate, time);

    XMVECTOR q = XMQuaternionNormalize(XMLoadFloat4(&rot));

    XMMATRIX animatedLocal =
        XMMatrixScaling(scl.x, scl.y, scl.z) *
        XMMatrixRotationQuaternion(q) *
        XMMatrixTranslation(pos.x, pos.y, pos.z);

    XMMATRIX adjustment = XMLoadFloat4x4(&bone.parentAdjustmentMatrix);
    return animatedLocal * adjustment;
}

void Animator::ApplyBindPose(Model &model) {
    const size_t boneCount = model.bones.size();

    if (model.skeletonSpaceMatrices.size() != boneCount) {
        model.skeletonSpaceMatrices.resize(boneCount);
    }

    if (model.finalBoneMatrices.size() != boneCount) {
        model.finalBoneMatrices.resize(boneCount);
    }

    std::vector<XMMATRIX> localMatrices(boneCount);
    std::vector<XMMATRIX> globalMatrices(boneCount);

    for (size_t i = 0; i < boneCount; i++) {
        localMatrices[i] = XMLoadFloat4x4(&model.bones[i].localBindMatrix);
    }

    for (size_t i = 0; i < boneCount; i++) {
        int parent = model.bones[i].parentIndex;

        if (parent < 0) {
            globalMatrices[i] = localMatrices[i];
        } else {
            globalMatrices[i] = localMatrices[i] * globalMatrices[parent];
        }

        XMStoreFloat4x4(&model.skeletonSpaceMatrices[i], globalMatrices[i]);
    }

    for (size_t i = 0; i < boneCount; i++) {
        XMMATRIX offset = XMLoadFloat4x4(&model.bones[i].offsetMatrix);
        XMMATRIX final = offset * globalMatrices[i];
        XMStoreFloat4x4(&model.finalBoneMatrices[i], final);
    }
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

    if (clip.duration <= 0.0f) {
        model.hasRootAnimation = false;
        XMStoreFloat4x4(&model.rootAnimationMatrix, XMMatrixIdentity());
        if (!model.bones.empty()) {
            ApplyBindPose(model);
        }
        return;
    }

    if (model.isPlaying) {
        model.animationTime += deltaTime;

        if (model.isLoop) {
            while (model.animationTime >= clip.duration) {
                model.animationTime -= clip.duration;
            }
        } else {
            if (model.animationTime >= clip.duration) {
                model.animationTime = clip.duration;
                model.isPlaying = false;
                model.animationFinished = true;
            }
        }
    }

    if (model.bones.empty()) {
        model.hasRootAnimation = false;
        XMStoreFloat4x4(&model.rootAnimationMatrix, XMMatrixIdentity());

        if (!clip.nodeAnimations.empty()) {
            const NodeAnimation &rootAnim = clip.nodeAnimations.begin()->second;

            XMFLOAT3 pos =
                rootAnim.translate.keyframes.empty()
                    ? XMFLOAT3{0.0f, 0.0f, 0.0f}
                    : SampleVec3(rootAnim.translate, model.animationTime);
            XMFLOAT3 scl = rootAnim.scale.keyframes.empty()
                               ? XMFLOAT3{1.0f, 1.0f, 1.0f}
                               : SampleVec3(rootAnim.scale, model.animationTime);
            XMFLOAT4 rot =
                rootAnim.rotate.keyframes.empty()
                    ? XMFLOAT4{0.0f, 0.0f, 0.0f, 1.0f}
                    : SampleQuat(rootAnim.rotate, model.animationTime);

            XMMATRIX local =
                XMMatrixScaling(scl.x, scl.y, scl.z) *
                XMMatrixRotationQuaternion(XMQuaternionNormalize(XMLoadFloat4(&rot))) *
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

    std::vector<XMMATRIX> localMatrices(boneCount);
    std::vector<XMMATRIX> globalMatrices(boneCount);

    for (size_t i = 0; i < boneCount; i++) {
        localMatrices[i] =
            MakeAnimatedLocalMatrix(model.bones[i], clip, model.animationTime);
    }

    for (size_t i = 0; i < boneCount; i++) {
        int parent = model.bones[i].parentIndex;

        if (parent < 0) {
            globalMatrices[i] = localMatrices[i];
        } else {
            globalMatrices[i] = localMatrices[i] * globalMatrices[parent];
        }

        XMStoreFloat4x4(&model.skeletonSpaceMatrices[i], globalMatrices[i]);
    }

    for (size_t i = 0; i < boneCount; i++) {
        XMMATRIX offset = XMLoadFloat4x4(&model.bones[i].offsetMatrix);
        XMMATRIX final = offset * globalMatrices[i];
        XMStoreFloat4x4(&model.finalBoneMatrices[i], final);
    }
}
