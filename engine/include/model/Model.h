#pragma once
#include "animation/AnimationTypes.h"
#include "core/ResourceHandle.h"
#include "model/VertexInfluence.h"

#include <DirectXMath.h>
#include <cstdint>
#include <d3d12.h>
#include <string>
#include <unordered_map>
#include <vector>
#include <wrl.h>

/// <summary>
/// 単一頂点に対するボーンウェイトを表す
/// </summary>
struct VertexWeightData {
    float weight = 0.0f;
    uint32_t vertexIndex = 0;
};

/// <summary>
/// 1ジョイントに紐づく逆バインド行列と頂点ウェイト群
/// </summary>
struct JointWeightData {
    DirectX::XMFLOAT4X4 inverseBindPoseMatrix{};
    std::vector<VertexWeightData> vertexWeights;
};

/// <summary>
/// GPU上で使用するボーン行列セット
/// </summary>
struct WellForGPU {
    DirectX::XMFLOAT4X4 skeletonSpaceMatrix{};
    DirectX::XMFLOAT4X4 skeletonSpaceInverseTransposeMatrix{};
};

struct SkinPaletteFrame {
    Microsoft::WRL::ComPtr<ID3D12Resource> resource;
    WellForGPU* mappedPalette = nullptr;
};

/// <summary>
/// スキンクラスター関連のGPUリソース群
/// </summary>
struct SkinCluster {
    std::vector<DirectX::XMFLOAT4X4> inverseBindPoseMatrices;

    Microsoft::WRL::ComPtr<ID3D12Resource> influenceResource;
    VertexInfluence* mappedInfluence = nullptr;
    uint32_t influenceCount = 0;
    uint32_t influenceSrvIndex = kInvalidResourceId;
    D3D12_CPU_DESCRIPTOR_HANDLE influenceSrvCpuHandle{};
    D3D12_GPU_DESCRIPTOR_HANDLE influenceSrvGpuHandle{};

    uint32_t paletteCount = 0;
    std::vector<WellForGPU> paletteCpuData;
    std::vector<SkinPaletteFrame> paletteFrames;
    mutable std::vector<bool> paletteDirtyFrames;

    Microsoft::WRL::ComPtr<ID3D12Resource> skinnedVertexResource;
    D3D12_VERTEX_BUFFER_VIEW skinnedVertexBufferView{};
    uint32_t inputVertexSrvIndex = kInvalidResourceId;
    D3D12_CPU_DESCRIPTOR_HANDLE inputVertexSrvCpuHandle{};
    D3D12_GPU_DESCRIPTOR_HANDLE inputVertexSrvGpuHandle{};
    uint32_t skinnedVertexUavIndex = kInvalidResourceId;
    D3D12_CPU_DESCRIPTOR_HANDLE skinnedVertexUavCpuHandle{};
    D3D12_GPU_DESCRIPTOR_HANDLE skinnedVertexUavGpuHandle{};
    mutable D3D12_RESOURCE_STATES skinnedVertexState =
        D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER;
    mutable uint64_t lastSkinningFrame = 0;
    mutable bool skinningValid = false;
};

/// <summary>
/// ボーン階層の1ノード分の情報
/// </summary>
struct BoneInfo {
    std::string name;
    int parentIndex = -1;
    DirectX::XMFLOAT4X4 offsetMatrix{};
    DirectX::XMFLOAT4X4 localBindMatrix{};
    DirectX::XMFLOAT4X4 parentAdjustmentMatrix{};
};

/// <summary>
/// モデルを構成するサブメッシュ情報
/// </summary>
struct ModelSubMesh {
    uint32_t meshId = kInvalidResourceId;
    uint32_t textureId = kInvalidResourceId;
    uint32_t normalTextureId = kInvalidResourceId;
    uint32_t materialId = kInvalidResourceId;
    uint32_t vertexCount = 0;
    std::vector<DirectX::XMFLOAT3> sourcePositions;
    DirectX::XMFLOAT3 sourceBoundsMin = {0.0f, 0.0f, 0.0f};
    DirectX::XMFLOAT3 sourceBoundsMax = {0.0f, 0.0f, 0.0f};

    std::unordered_map<std::string, JointWeightData> skinClusterData;
    SkinCluster skinCluster;
};

/// <summary>
/// 描画・アニメーションに必要なモデルデータ一式
/// </summary>
struct Model {
    uint32_t meshId = kInvalidResourceId;
    uint32_t textureId = kInvalidResourceId;
    uint32_t materialId = kInvalidResourceId;

    std::vector<ModelSubMesh> subMeshes;

    std::vector<BoneInfo> bones;
    std::unordered_map<std::string, uint32_t> boneMap;

    std::unordered_map<std::string, AnimationClip> animations;
    std::string rootNodeName;

    std::vector<DirectX::XMFLOAT4X4> skeletonSpaceMatrices;
    std::vector<DirectX::XMFLOAT4X4> finalBoneMatrices;

    std::string currentAnimation;
    float animationTime = 0.0f;
    bool isLoop = true;
    bool isPlaying = true;
    bool animationFinished = false;

    bool hasRootAnimation = false;
    DirectX::XMFLOAT4X4 rootAnimationMatrix = {1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f,
                                               0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f};
};
