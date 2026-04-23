#include "SkeletonPoseBuilder.h"
#include "AnimationSampler.h"
#include <DirectXMath.h>

using namespace DirectX;

namespace {

XMMATRIX MakeAnimatedLocalMatrix(const BoneInfo &bone, const AnimationClip &clip,
                                 float time) {
    auto it = clip.nodeAnimations.find(bone.name);
    if (it == clip.nodeAnimations.end()) {
        return XMLoadFloat4x4(&bone.localBindMatrix);
    }

    const NodeAnimation &anim = it->second;

    XMFLOAT3 pos = anim.translate.keyframes.empty()
                       ? XMFLOAT3{0.0f, 0.0f, 0.0f}
                       : AnimationSampler::SampleVec3(anim.translate, time);

    XMFLOAT3 scl = anim.scale.keyframes.empty() ? XMFLOAT3{1.0f, 1.0f, 1.0f}
                                                : AnimationSampler::SampleVec3(
                                                      anim.scale, time);

    XMFLOAT4 rot = anim.rotate.keyframes.empty()
                       ? XMFLOAT4{0.0f, 0.0f, 0.0f, 1.0f}
                       : AnimationSampler::SampleQuat(anim.rotate, time);

    XMVECTOR q = XMQuaternionNormalize(XMLoadFloat4(&rot));
    XMMATRIX animatedLocal =
        XMMatrixScaling(scl.x, scl.y, scl.z) *
        XMMatrixRotationQuaternion(q) *
        XMMatrixTranslation(pos.x, pos.y, pos.z);

    XMMATRIX adjustment = XMLoadFloat4x4(&bone.parentAdjustmentMatrix);
    return animatedLocal * adjustment;
}

} // namespace

void SkeletonPoseBuilder::BuildBindPoseLocals(
    const Model &model, std::vector<XMMATRIX> &localMatrices) {
    const size_t boneCount = model.bones.size();
    localMatrices.resize(boneCount);
    for (size_t i = 0; i < boneCount; i++) {
        localMatrices[i] = XMLoadFloat4x4(&model.bones[i].localBindMatrix);
    }
}

void SkeletonPoseBuilder::BuildAnimatedLocals(
    Model &model, const AnimationClip &clip, float time,
    std::vector<XMMATRIX> &localMatrices) {
    const size_t boneCount = model.bones.size();
    localMatrices.resize(boneCount);
    for (size_t i = 0; i < boneCount; i++) {
        localMatrices[i] = MakeAnimatedLocalMatrix(model.bones[i], clip, time);
    }
}

void SkeletonPoseBuilder::UpdateSkeleton(
    Model &model, const std::vector<XMMATRIX> &localMatrices) {
    const size_t boneCount = model.bones.size();
    std::vector<XMMATRIX> globalMatrices(boneCount);

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

