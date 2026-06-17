#include "animation/AssimpAnimationLoader.h"

#include "model/ModelLimits.h"

#include <algorithm>
#include <cmath>
#include <numeric>
#include <unordered_map>
#include <vector>

namespace {

constexpr float kDefaultTicksPerSecond = 25.0f;
constexpr float kMinimumClipDuration = 0.000001f;
constexpr size_t kMaxAssimpNodeTraversal = 65536u;

float ToSeconds(double ticks, float ticksPerSecond) {
    const float safeTicksPerSecond = (std::isfinite(ticksPerSecond) && ticksPerSecond > 0.0f)
                                         ? ticksPerSecond
                                         : kDefaultTicksPerSecond;
    if (!std::isfinite(ticks)) {
        return 0.0f;
    }

    const float seconds = static_cast<float>(ticks) / safeTicksPerSecond;
    if (!std::isfinite(seconds) || seconds < 0.0f) {
        return 0.0f;
    }
    return seconds;
}

template <typename TValue> void NormalizeKeyframes(AnimationCurve<TValue>& curve) {
    curve.keyframes.erase(std::remove_if(curve.keyframes.begin(), curve.keyframes.end(),
                                         [](const Keyframe<TValue>& keyframe) {
                                             return !std::isfinite(keyframe.time) ||
                                                    keyframe.time < 0.0f;
                                         }),
                          curve.keyframes.end());
    std::stable_sort(curve.keyframes.begin(), curve.keyframes.end(),
                     [](const Keyframe<TValue>& lhs, const Keyframe<TValue>& rhs) {
                         return lhs.time < rhs.time;
                     });
}

void NormalizeNodeAnimation(NodeAnimation& nodeAnimation) {
    NormalizeKeyframes(nodeAnimation.translate);
    NormalizeKeyframes(nodeAnimation.rotate);
    NormalizeKeyframes(nodeAnimation.scale);
}

bool HasKeyframes(const NodeAnimation& nodeAnimation) {
    return !nodeAnimation.translate.keyframes.empty() || !nodeAnimation.rotate.keyframes.empty() ||
           !nodeAnimation.scale.keyframes.empty();
}

template <typename TValue> float MaxKeyTime(const AnimationCurve<TValue>& curve) {
    return curve.keyframes.empty() ? 0.0f : curve.keyframes.back().time;
}

float MaxNodeAnimationTime(const NodeAnimation& nodeAnimation) {
    return (std::max)({MaxKeyTime(nodeAnimation.translate), MaxKeyTime(nodeAnimation.rotate),
                       MaxKeyTime(nodeAnimation.scale)});
}

float MaxClipKeyTime(const AnimationClip& clip) {
    return std::accumulate(
        clip.nodeAnimations.begin(), clip.nodeAnimations.end(), 0.0f,
        [](float maxTime, const auto &entry) {
            return (std::max)(maxTime, MaxNodeAnimationTime(entry.second));
        });
}

const aiNode* FindNearestAnimatedNode(
    const aiNode* node, const std::unordered_map<std::string, NodeAnimation>& nodeAnimations) {
    if (!node) {
        return nullptr;
    }

    std::vector<const aiNode*> stack;
    try {
        stack.reserve(256u);
        stack.push_back(node);
    } catch (...) {
        return nullptr;
    }
    size_t visited = 0;
    while (!stack.empty()) {
        const aiNode* current = stack.back();
        stack.pop_back();
        if (!current) {
            continue;
        }
        if (++visited > kMaxAssimpNodeTraversal) {
            return nullptr;
        }

        if (nodeAnimations.find(current->mName.C_Str()) != nodeAnimations.end()) {
            return current;
        }

        if (current->mNumChildren > 0 && current->mChildren == nullptr) {
            return nullptr;
        }
        for (unsigned int childIndex = current->mNumChildren; childIndex > 0; --childIndex) {
            try {
                stack.push_back(current->mChildren[childIndex - 1u]);
            } catch (...) {
                return nullptr;
            }
        }
    }

    return nullptr;
}

} // namespace

void AssimpAnimationLoader::LoadAnimations(const aiScene* scene, Model& model) {
    if (!scene || !scene->HasAnimations()) {
        return;
    }
    if (scene->mNumAnimations > ModelLimits::kMaxAnimations) {
        return;
    }
    if (scene->mNumAnimations > 0 && scene->mAnimations == nullptr) {
        return;
    }

    size_t totalChannels = 0;
    size_t totalKeys = 0;
    for (unsigned int a = 0; a < scene->mNumAnimations; a++) {
        aiAnimation* anim = scene->mAnimations[a];
        if (!anim) {
            continue;
        }
        if (anim->mNumChannels > ModelLimits::kMaxAnimationChannels ||
            totalChannels > ModelLimits::kMaxAnimationChannels - anim->mNumChannels) {
            continue;
        }
        totalChannels += anim->mNumChannels;
        if (anim->mNumChannels > 0 && anim->mChannels == nullptr) {
            continue;
        }

        AnimationClip clip{};
        const float ticksPerSecond = static_cast<float>(anim->mTicksPerSecond);
        clip.duration = ToSeconds(anim->mDuration, ticksPerSecond);

        for (unsigned int i = 0; i < anim->mNumChannels; i++) {
            aiNodeAnim* channel = anim->mChannels[i];
            if (!channel) {
                continue;
            }
            if ((channel->mNumPositionKeys > 0 && !channel->mPositionKeys) ||
                (channel->mNumRotationKeys > 0 && !channel->mRotationKeys) ||
                (channel->mNumScalingKeys > 0 && !channel->mScalingKeys)) {
                continue;
            }
            const size_t channelKeyCount = static_cast<size_t>(channel->mNumPositionKeys) +
                                           static_cast<size_t>(channel->mNumRotationKeys) +
                                           static_cast<size_t>(channel->mNumScalingKeys);
            if (channelKeyCount > ModelLimits::kMaxAnimationKeysPerChannel ||
                totalKeys > ModelLimits::kMaxAnimationKeysTotal - channelKeyCount) {
                continue;
            }
            totalKeys += channelKeyCount;

            NodeAnimation nodeAnim;

            for (unsigned int k = 0; k < channel->mNumPositionKeys; k++) {
                const aiVectorKey& key = channel->mPositionKeys[k];
                nodeAnim.translate.keyframes.push_back(
                    {ToSeconds(key.mTime, ticksPerSecond),
                     {key.mValue.x, key.mValue.y, key.mValue.z}});
            }

            for (unsigned int k = 0; k < channel->mNumRotationKeys; k++) {
                const aiQuatKey& key = channel->mRotationKeys[k];
                nodeAnim.rotate.keyframes.push_back(
                    {ToSeconds(key.mTime, ticksPerSecond),
                     {key.mValue.x, key.mValue.y, key.mValue.z, key.mValue.w}});
            }

            for (unsigned int k = 0; k < channel->mNumScalingKeys; k++) {
                const aiVectorKey& key = channel->mScalingKeys[k];
                nodeAnim.scale.keyframes.push_back({ToSeconds(key.mTime, ticksPerSecond),
                                                    {key.mValue.x, key.mValue.y, key.mValue.z}});
            }

            NormalizeNodeAnimation(nodeAnim);
            if (!HasKeyframes(nodeAnim)) {
                continue;
            }
            clip.nodeAnimations[channel->mNodeName.C_Str()] = nodeAnim;
        }

        if (clip.nodeAnimations.empty()) {
            continue;
        }

        const float keyDuration = MaxClipKeyTime(clip);
        clip.duration = (std::max)(clip.duration, keyDuration);
        if (!std::isfinite(clip.duration) || clip.duration <= 0.0f) {
            clip.duration = kMinimumClipDuration;
        }

        if (const aiNode* rootAnimatedNode =
                FindNearestAnimatedNode(scene->mRootNode, clip.nodeAnimations)) {
            clip.rootNodeName = rootAnimatedNode->mName.C_Str();
        } else if (clip.nodeAnimations.size() == 1) {
            clip.rootNodeName = clip.nodeAnimations.begin()->first;
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
