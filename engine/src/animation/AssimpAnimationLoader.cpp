#include "AssimpAnimationLoader.h"

namespace {

float ToSeconds(double ticks, float ticksPerSecond) {
    const float safeTicksPerSecond =
        (ticksPerSecond > 0.0f) ? ticksPerSecond : 25.0f;
    return static_cast<float>(ticks) / safeTicksPerSecond;
}

} // namespace

void AssimpAnimationLoader::LoadAnimations(const aiScene *scene, Model &model) const {
    if (!scene || !scene->HasAnimations()) {
        return;
    }

    for (unsigned int a = 0; a < scene->mNumAnimations; a++) {
        aiAnimation *anim = scene->mAnimations[a];
        if (!anim) {
            continue;
        }

        AnimationClip clip{};
        const float ticksPerSecond = static_cast<float>(anim->mTicksPerSecond);
        clip.duration = ToSeconds(anim->mDuration, ticksPerSecond);

        for (unsigned int i = 0; i < anim->mNumChannels; i++) {
            aiNodeAnim *channel = anim->mChannels[i];
            if (!channel) {
                continue;
            }

            NodeAnimation nodeAnim;

            for (unsigned int k = 0; k < channel->mNumPositionKeys; k++) {
                const aiVectorKey &key = channel->mPositionKeys[k];
                nodeAnim.translate.keyframes.push_back(
                    {ToSeconds(key.mTime, ticksPerSecond),
                     {key.mValue.x, key.mValue.y, key.mValue.z}});
            }

            for (unsigned int k = 0; k < channel->mNumRotationKeys; k++) {
                const aiQuatKey &key = channel->mRotationKeys[k];
                nodeAnim.rotate.keyframes.push_back(
                    {ToSeconds(key.mTime, ticksPerSecond),
                     {key.mValue.x, key.mValue.y, key.mValue.z, key.mValue.w}});
            }

            for (unsigned int k = 0; k < channel->mNumScalingKeys; k++) {
                const aiVectorKey &key = channel->mScalingKeys[k];
                nodeAnim.scale.keyframes.push_back(
                    {ToSeconds(key.mTime, ticksPerSecond),
                     {key.mValue.x, key.mValue.y, key.mValue.z}});
            }

            clip.nodeAnimations[channel->mNodeName.C_Str()] = nodeAnim;
        }

        std::string animName = anim->mName.C_Str();
        if (animName.empty()) {
            animName = "Anim_" + std::to_string(a);
        }

        model.animations[animName] = clip;
    }

    if (!model.animations.empty()) {
        model.currentAnimation = model.animations.begin()->first;
        model.animationTime = 0.0f;
        model.isLoop = true;
        model.isPlaying = true;
    }
}
