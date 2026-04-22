#pragma once
#include <DirectXMath.h>
#include <array>
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
/// 1頂点に影響する最大ジョイント数
/// </summary>
constexpr uint32_t kNumMaxInfluence = 4;

/// <summary>
/// GPUへ渡す頂点スキニング情報
/// </summary>
struct VertexInfluence {
    std::array<float, kNumMaxInfluence> weights{};
    std::array<int32_t, kNumMaxInfluence> jointIndices{};
};

/// <summary>
/// GPU上で使用するボーン行列セット
/// </summary>
struct WellForGPU {
    DirectX::XMFLOAT4X4 skeletonSpaceMatrix{};
    DirectX::XMFLOAT4X4 skeletonSpaceInverseTransposeMatrix{};
};

/// <summary>
/// スキンクラスター関連のGPUリソース群
/// </summary>
struct SkinCluster {
    std::vector<DirectX::XMFLOAT4X4> inverseBindPoseMatrices;

    Microsoft::WRL::ComPtr<ID3D12Resource> influenceResource;
    D3D12_VERTEX_BUFFER_VIEW influenceBufferView{};
    VertexInfluence *mappedInfluence = nullptr;
    uint32_t influenceCount = 0;

    Microsoft::WRL::ComPtr<ID3D12Resource> paletteResource;
    WellForGPU *mappedPalette = nullptr;
    uint32_t paletteCount = 0;
    D3D12_CPU_DESCRIPTOR_HANDLE paletteSrvCpuHandle{};
    D3D12_GPU_DESCRIPTOR_HANDLE paletteSrvGpuHandle{};
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
/// ベクトル値のアニメーションキー
/// </summary>
struct AnimationKeyVec3 {
    float time = 0.0f;
    DirectX::XMFLOAT3 value{};
};

/// <summary>
/// クォータニオン値のアニメーションキー
/// </summary>
struct AnimationKeyQuat {
    float time = 0.0f;
    DirectX::XMFLOAT4 value{};
};

/// <summary>
/// ボーン1本分のアニメーションチャンネル
/// </summary>
struct BoneAnimation {
    std::vector<AnimationKeyVec3> positions;
    std::vector<AnimationKeyQuat> rotations;
    std::vector<AnimationKeyVec3> scales;
};

/// <summary>
/// モデル全体で共有するアニメーションクリップ
/// </summary>
struct AnimationClip {
    float duration = 0.0f;
    float ticksPerSecond = 1.0f;
    std::unordered_map<std::string, BoneAnimation> channels;
};

/// <summary>
/// モデルを構成するサブメッシュ情報
/// </summary>
struct ModelSubMesh {
    uint32_t meshId = 0;
    uint32_t textureId = 0;
    uint32_t materialId = 0;
    uint32_t vertexCount = 0;

    std::unordered_map<std::string, JointWeightData> skinClusterData;
    SkinCluster skinCluster;
};

/// <summary>
/// 描画・アニメーションに必要なモデルデータ一式
/// </summary>
struct Model {
    uint32_t meshId = 0;
    uint32_t textureId = 0;
    uint32_t materialId = 0;

    std::vector<ModelSubMesh> subMeshes;

    std::vector<BoneInfo> bones;
    std::unordered_map<std::string, uint32_t> boneMap;

    std::unordered_map<std::string, AnimationClip> animations;

    std::vector<DirectX::XMFLOAT4X4> skeletonSpaceMatrices;
    std::vector<DirectX::XMFLOAT4X4> finalBoneMatrices;

    std::string currentAnimation = "";
    float animationTime = 0.0f;
    bool isLoop = true;
    bool isPlaying = true;
    bool animationFinished = false;
};
