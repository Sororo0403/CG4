#include "animation/AssimpAnimationLoader.h"

#include "model/ModelLimits.h"

#include <algorithm>
#include <cmath>
#include <exception>
#include <numeric>
#include <unordered_map>
#include <vector>

namespace {

constexpr float kDefaultTicksPerSecond = 25.0f;
constexpr float kMinimumClipDuration = 0.000001f;
constexpr size_t kMaxAssimpNodeTraversal = 65536u;

struct AnimationLoadCounters {
    size_t totalChannels = 0;
    size_t totalKeys = 0;
};

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
    return std::accumulate(clip.nodeAnimations.begin(), clip.nodeAnimations.end(), 0.0f,
                           [](float maxTime, const auto& entry) {
                               return (std::max)(maxTime, MaxNodeAnimationTime(entry.second));
                           });
}

const aiNode* FindNearestAnimatedNode(
    const aiNode* node, const std::unordered_map<std::string, NodeAnimation>& nodeAnimations) {
    if (node == nullptr) {
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
        if (current == nullptr) {
            continue;
        }
        if (++visited > kMaxAssimpNodeTraversal) {
            return nullptr;
        }

        if (nodeAnimations.contains(current->mName.C_Str())) {
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

bool IsSceneAnimationListUsable(const aiScene* scene) {
    if (scene == nullptr || !scene->HasAnimations()) {
        return false;
    }
    if (scene->mNumAnimations > ModelLimits::kMaxAnimations) {
        return false;
    }
    return scene->mNumAnimations == 0 || scene->mAnimations != nullptr;
}

bool TryReserveAnimationChannels(const aiAnimation* animation, AnimationLoadCounters& counters) {
    if (animation->mNumChannels > ModelLimits::kMaxAnimationChannels ||
        counters.totalChannels > ModelLimits::kMaxAnimationChannels - animation->mNumChannels) {
        return false;
    }
    counters.totalChannels += animation->mNumChannels;
    return true;
}

bool HasUsableAnimationChannels(const aiAnimation* animation) {
    return animation->mNumChannels == 0 || animation->mChannels != nullptr;
}

bool HasUsableChannelKeyArrays(const aiNodeAnim* channel) {
    return (channel->mNumPositionKeys == 0 || channel->mPositionKeys != nullptr) &&
           (channel->mNumRotationKeys == 0 || channel->mRotationKeys != nullptr) &&
           (channel->mNumScalingKeys == 0 || channel->mScalingKeys != nullptr);
}

size_t ChannelKeyCount(const aiNodeAnim* channel) {
    return static_cast<size_t>(channel->mNumPositionKeys) +
           static_cast<size_t>(channel->mNumRotationKeys) +
           static_cast<size_t>(channel->mNumScalingKeys);
}

bool TryReserveChannelKeys(const aiNodeAnim* channel, AnimationLoadCounters& counters) {
    const size_t keyCount = ChannelKeyCount(channel);
    if (keyCount > ModelLimits::kMaxAnimationKeysPerChannel ||
        counters.totalKeys > ModelLimits::kMaxAnimationKeysTotal - keyCount) {
        return false;
    }
    counters.totalKeys += keyCount;
    return true;
}

void AppendPositionKeys(const aiNodeAnim* channel, float ticksPerSecond,
                        NodeAnimation& nodeAnimation) {
    for (unsigned int k = 0; k < channel->mNumPositionKeys; k++) {
        const aiVectorKey& key = channel->mPositionKeys[k];
        nodeAnimation.translate.keyframes.push_back(
            {.time = ToSeconds(key.mTime, ticksPerSecond),
             .value = {key.mValue.x, key.mValue.y, key.mValue.z}});
    }
}

void AppendRotationKeys(const aiNodeAnim* channel, float ticksPerSecond,
                        NodeAnimation& nodeAnimation) {
    for (unsigned int k = 0; k < channel->mNumRotationKeys; k++) {
        const aiQuatKey& key = channel->mRotationKeys[k];
        nodeAnimation.rotate.keyframes.push_back(
            {.time = ToSeconds(key.mTime, ticksPerSecond),
             .value = {key.mValue.x, key.mValue.y, key.mValue.z, key.mValue.w}});
    }
}

void AppendScalingKeys(const aiNodeAnim* channel, float ticksPerSecond,
                       NodeAnimation& nodeAnimation) {
    for (unsigned int k = 0; k < channel->mNumScalingKeys; k++) {
        const aiVectorKey& key = channel->mScalingKeys[k];
        nodeAnimation.scale.keyframes.push_back(
            {.time = ToSeconds(key.mTime, ticksPerSecond),
             .value = {key.mValue.x, key.mValue.y, key.mValue.z}});
    }
}

bool TryLoadNodeAnimation(const aiNodeAnim* channel, float ticksPerSecond,
                          AnimationLoadCounters& counters, NodeAnimation& nodeAnimation) {
    if (channel == nullptr || !HasUsableChannelKeyArrays(channel) ||
        !TryReserveChannelKeys(channel, counters)) {
        return false;
    }

    AppendPositionKeys(channel, ticksPerSecond, nodeAnimation);
    AppendRotationKeys(channel, ticksPerSecond, nodeAnimation);
    AppendScalingKeys(channel, ticksPerSecond, nodeAnimation);
    NormalizeNodeAnimation(nodeAnimation);
    return HasKeyframes(nodeAnimation);
}

void FinalizeClipDuration(AnimationClip& clip) {
    clip.duration = (std::max)(clip.duration, MaxClipKeyTime(clip));
    if (!std::isfinite(clip.duration) || clip.duration <= 0.0f) {
        clip.duration = kMinimumClipDuration;
    }
}

void ResolveClipRootNode(const aiScene* scene, AnimationClip& clip) {
    if (const aiNode* rootAnimatedNode =
            FindNearestAnimatedNode(scene->mRootNode, clip.nodeAnimations)) {
        clip.rootNodeName = rootAnimatedNode->mName.C_Str();
    } else if (clip.nodeAnimations.size() == 1) {
        clip.rootNodeName = clip.nodeAnimations.begin()->first;
    }
}

std::string ResolveAnimationName(const aiAnimation* animation, unsigned int index) {
    std::string animationName = animation->mName.C_Str();
    if (animationName.empty()) {
        animationName = "Anim_" + std::to_string(index);
    }
    return animationName;
}

bool TryLoadAnimationClip(const aiScene* scene, const aiAnimation* animation,
                          AnimationLoadCounters& counters, AnimationClip& clip) {
    if (animation == nullptr || !TryReserveAnimationChannels(animation, counters) ||
        !HasUsableAnimationChannels(animation)) {
        return false;
    }

    const auto ticksPerSecond = static_cast<float>(animation->mTicksPerSecond);
    clip.duration = ToSeconds(animation->mDuration, ticksPerSecond);
    for (unsigned int i = 0; i < animation->mNumChannels; i++) {
        NodeAnimation nodeAnimation;
        const aiNodeAnim* channel = animation->mChannels[i];
        if (!TryLoadNodeAnimation(channel, ticksPerSecond, counters, nodeAnimation)) {
            continue;
        }
        clip.nodeAnimations[channel->mNodeName.C_Str()] = nodeAnimation;
    }

    if (clip.nodeAnimations.empty()) {
        return false;
    }
    FinalizeClipDuration(clip);
    ResolveClipRootNode(scene, clip);
    return true;
}

void EnableModelAnimationPlayback(Model& model) {
    if (model.animations.empty()) {
        return;
    }
    model.currentAnimation = model.animations.begin()->first;
    model.animationTime = 0.0f;
    model.isLoop = true;
    model.isPlaying = true;
}

} // namespace

void AssimpAnimationLoader::LoadAnimations(const aiScene* scene, Model& model) {
    if (!IsSceneAnimationListUsable(scene)) {
        return;
    }

    try {
        AnimationLoadCounters counters;
        for (unsigned int a = 0; a < scene->mNumAnimations; a++) {
            AnimationClip clip{};
            const aiAnimation* animation = scene->mAnimations[a];
            if (TryLoadAnimationClip(scene, animation, counters, clip)) {
                model.animations[ResolveAnimationName(animation, a)] = clip;
            }
        }
        EnableModelAnimationPlayback(model);
    } catch (const std::exception&) {
        model.animations.clear();
        model.currentAnimation.clear();
        model.isPlaying = false;
    }
}
