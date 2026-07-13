#include "internal/AssimpMeshLoaderUtils.h"
#include "model/AssimpMeshLoader.h"

#include <exception>
#include <limits>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace {

using AssimpMeshLoaderUtils::CheckedIntSize;
using AssimpMeshLoaderUtils::CheckedUint32Size;
using AssimpMeshLoaderUtils::ToMatrix;

constexpr size_t kMaxAssimpNodeTraversal = 65536u;

bool BuildNodeMap(const aiNode* root, std::unordered_map<std::string, const aiNode*>& nodes) {
    if (!root) {
        return false;
    }

    std::vector<const aiNode*> stack;
    stack.reserve(256u);
    nodes.reserve(256u);
    stack.push_back(root);
    size_t visited = 0;
    while (!stack.empty()) {
        const aiNode* node = stack.back();
        stack.pop_back();
        if (!node) {
            continue;
        }
        if (++visited > kMaxAssimpNodeTraversal) {
            return false;
        }

        nodes.emplace(node->mName.C_Str(), node);
        if (node->mNumChildren > 0 && node->mChildren == nullptr) {
            return false;
        }
        for (unsigned int i = node->mNumChildren; i > 0; --i) {
            stack.push_back(node->mChildren[i - 1u]);
        }
    }

    return true;
}

} // namespace

void AssimpMeshLoader::BuildBoneHierarchy(const aiScene* scene, Model& model) {
    if (!scene || !scene->mRootNode) {
        return;
    }

    std::unordered_map<std::string, const aiNode*> nodes;
    try {
        if (!BuildNodeMap(scene->mRootNode, nodes)) {
            return;
        }
    } catch (const std::exception&) {
        return;
    }

    std::vector<BoneInfo> originalBones;
    try {
        originalBones = model.bones;
        for (size_t i = 0; i < model.bones.size(); i++) {
            const std::string& boneName = model.bones[i].name;

            const auto nodeIt = nodes.find(boneName);
            const aiNode* node = nodeIt != nodes.end() ? nodeIt->second : nullptr;
            if (!node) {
                model.bones[i].parentIndex = -1;
                model.bones[i].localBindMatrix = ToMatrix(aiMatrix4x4());
                model.bones[i].parentAdjustmentMatrix = ToMatrix(aiMatrix4x4());
                continue;
            }

            aiMatrix4x4 adjustment{};
            int parentIndex = -1;
            const aiNode* parent = node->mParent;
            size_t parentDepth = 0;

            while (parent) {
                if (++parentDepth > kMaxAssimpNodeTraversal) {
                    parentIndex = -1;
                    break;
                }
                auto it = model.boneMap.find(parent->mName.C_Str());
                if (it != model.boneMap.end()) {
                    parentIndex = static_cast<int>(it->second);
                    break;
                }

                adjustment *= parent->mTransformation;
                parent = parent->mParent;
            }

            model.bones[i].parentIndex = parentIndex;
            model.bones[i].parentAdjustmentMatrix = ToMatrix(adjustment);
            model.bones[i].localBindMatrix = ToMatrix(node->mTransformation * adjustment);
        }

        ReorderBonesParentFirst(model);
    } catch (const std::exception&) {
        model.bones = std::move(originalBones);
    }
}

void AssimpMeshLoader::ReorderBonesParentFirst(Model& model) {
    const size_t boneCount = model.bones.size();
    if (boneCount <= 1) {
        return;
    }
    if (boneCount > static_cast<size_t>((std::numeric_limits<int>::max)())) {
        return;
    }

    std::vector<std::vector<size_t>> children;
    std::vector<size_t> roots;
    children.resize(boneCount);
    roots.reserve(boneCount);
    for (size_t boneIndex = 0; boneIndex < boneCount; ++boneIndex) {
        const int parentIndex = model.bones[boneIndex].parentIndex;
        if (parentIndex >= 0 && static_cast<size_t>(parentIndex) < boneCount) {
            children[static_cast<size_t>(parentIndex)].push_back(boneIndex);
        } else {
            roots.push_back(boneIndex);
        }
    }

    std::vector<BoneInfo> orderedBones;
    std::vector<int> oldToNew;
    oldToNew.assign(boneCount, -1);
    orderedBones.reserve(boneCount);
    auto visit = [&](size_t rootIndex, int rootParentIndex) {
        std::vector<std::pair<size_t, int>> stack;
        stack.reserve(64u);
        stack.push_back({rootIndex, rootParentIndex});
        while (!stack.empty()) {
            const auto entry = stack.back();
            stack.pop_back();
            const size_t oldIndex = entry.first;
            const int newParentIndex = entry.second;

            if (oldIndex >= boneCount || oldToNew[oldIndex] >= 0) {
                continue;
            }

            const int newIndex = CheckedIntSize(orderedBones.size(),
                                                "AssimpMeshLoader reordered bone count overflow");
            BoneInfo bone = model.bones[oldIndex];
            bone.parentIndex = newParentIndex;
            orderedBones.push_back(std::move(bone));
            oldToNew[oldIndex] = newIndex;

            const std::vector<size_t>& childList = children[oldIndex];
            for (size_t i = childList.size(); i > 0; --i) {
                stack.push_back({childList[i - 1u], newIndex});
            }
        }
        return true;
    };

    for (size_t rootIndex : roots) {
        if (!visit(rootIndex, -1)) {
            return;
        }
    }

    for (size_t boneIndex = 0; boneIndex < boneCount; ++boneIndex) {
        if (oldToNew[boneIndex] < 0) {
            if (!visit(boneIndex, -1)) {
                return;
            }
        }
    }

    std::unordered_map<std::string, uint32_t> reorderedBoneMap;
    reorderedBoneMap.reserve(orderedBones.size());
    for (size_t boneIndex = 0; boneIndex < orderedBones.size(); ++boneIndex) {
        reorderedBoneMap.emplace(
            orderedBones[boneIndex].name,
            CheckedUint32Size(boneIndex, "AssimpMeshLoader reordered bone count overflow"));
    }
    model.bones = std::move(orderedBones);
    model.boneMap = std::move(reorderedBoneMap);
}
