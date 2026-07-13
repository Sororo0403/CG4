#pragma once
#include "core/ResourceHandle.h"

#include <cstddef>
#include <cstdint>
#include <d3d12.h>
#include <memory>
#include <wrl.h>

class DirectXCommon;

/// <summary>
/// 頂点バッファとインデックスバッファをまとめたメッシュ情報
/// </summary>
struct Mesh {
    Microsoft::WRL::ComPtr<ID3D12Resource> vertexBuffer;
    Microsoft::WRL::ComPtr<ID3D12Resource> indexBuffer;

    D3D12_VERTEX_BUFFER_VIEW vbView{};
    D3D12_INDEX_BUFFER_VIEW ibView{};

    uint32_t indexCount = 0;
    uint32_t vertexStride = 0;
    uint64_t vertexBytes = 0;
    uint64_t indexBytes = 0;
    D3D12_PRIMITIVE_TOPOLOGY primitiveTopology = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
};

/// <summary>
/// GPUメッシュリソースの生成と参照を管理する
/// </summary>
class MeshManager {
public:
    MeshManager();
    ~MeshManager();

    /// <summary>
    /// メッシュ用GPUリソースを生成できるようDirectX参照を設定する
    /// </summary>
    /// <param name="dxCommon">DirectXCommonインスタンス</param>
    void Initialize(DirectXCommon* dxCommon);

    /// <summary>
    /// 管理中のメッシュGPUリソースを解放する
    /// </summary>
    bool Finalize();
    bool Finalize(bool allowFrameAbort);

    /// <summary>
    /// GPU転送完了後にメッシュ作成用の一時UploadBufferを解放する
    /// </summary>
    void ReleaseUploadBuffers();
    void ReleaseCompletedFrameResources();

    /// <summary>
    /// 頂点配列とインデックス配列からGPUメッシュを作成して登録する
    /// </summary>
    /// <param name="vertexData">頂点データへのポインタ</param>
    /// <param name="vertexStride">1頂点あたりのバイトサイズ</param>
    /// <param name="vertexCount">頂点数</param>
    /// <param name="indexData">16bitインデックス配列</param>
    /// <param name="indexCount">インデックス数</param>
    /// <returns>登録されたMeshのID</returns>
    uint32_t CreateMesh(
        const void* vertexData, uint32_t vertexStride, uint32_t vertexCount,
        const uint32_t* indexData, uint32_t indexCount,
        D3D12_PRIMITIVE_TOPOLOGY primitiveTopology = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    MeshHandle CreateMeshHandle(
        const void* vertexData, uint32_t vertexStride, uint32_t vertexCount,
        const uint32_t* indexData, uint32_t indexCount,
        D3D12_PRIMITIVE_TOPOLOGY primitiveTopology = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST) {
        return MeshHandle(CreateMesh(vertexData, vertexStride, vertexCount, indexData, indexCount,
                                     primitiveTopology));
    }

    /// <summary>
    /// 登録済みメッシュを破棄してIDを無効化する
    /// </summary>
    void DestroyMesh(uint32_t meshId);
    void DestroyMesh(MeshHandle mesh) {
        DestroyMesh(mesh.Get());
    }

    /// <summary>
    /// メッシュ情報を取得する
    /// </summary>
    /// <param name="meshId">メッシュID</param>
    /// <returns>メッシュ情報</returns>
    const Mesh& GetMesh(uint32_t meshId) const;
    const Mesh& GetMesh(MeshHandle mesh) const {
        return GetMesh(mesh.Get());
    }

    /// <summary>
    /// 指定IDが有効なメッシュを指しているかを取得する
    /// </summary>
    bool IsValidMeshId(uint32_t meshId) const;
    bool IsValidMesh(MeshHandle mesh) const {
        return IsValidMeshId(mesh.Get());
    }
    size_t GetActiveMeshCount() const;
    uint64_t GetActiveGpuBytes() const;
    uint64_t GetDeferredGpuBytes() const;
    uint64_t GetUploadBytes() const;

private:
    struct State;

    bool ReserveMeshStorage();
    bool StoreFrameUploadBuffers(const Microsoft::WRL::ComPtr<ID3D12Resource>& vertexUploadBuffer,
                                 const Microsoft::WRL::ComPtr<ID3D12Resource>& indexUploadBuffer);
    bool StoreFallbackUploadBuffers(
        const Microsoft::WRL::ComPtr<ID3D12Resource>& vertexUploadBuffer,
        const Microsoft::WRL::ComPtr<ID3D12Resource>& indexUploadBuffer);
    bool StoreMesh(Mesh&& mesh, uint32_t& meshId);
    void RollBackStoredMesh(uint32_t meshId);
    void RemoveLastStoredUploadBuffers();
    bool RegisterMeshFrameRollback(uint32_t meshId);
    bool KeepSubmittedUploadBuffers(
        uint32_t meshId, const Microsoft::WRL::ComPtr<ID3D12Resource>& vertexUploadBuffer,
        const Microsoft::WRL::ComPtr<ID3D12Resource>& indexUploadBuffer);
    ID3D12GraphicsCommandList* BeginMeshUpload(bool ownsUploadPass);
    bool StoreMeshForUpload(bool ownsUploadPass, Mesh&& mesh,
                            const Microsoft::WRL::ComPtr<ID3D12Resource>& vertexUploadBuffer,
                            const Microsoft::WRL::ComPtr<ID3D12Resource>& indexUploadBuffer,
                            uint32_t& meshId);
    bool RegisterMeshRollbackIfNeeded(bool ownsUploadPass, uint32_t meshId);
    bool FinishMeshUpload(bool ownsUploadPass, uint32_t meshId,
                          const Microsoft::WRL::ComPtr<ID3D12Resource>& vertexUploadBuffer,
                          const Microsoft::WRL::ComPtr<ID3D12Resource>& indexUploadBuffer);

    DirectXCommon* dxCommon_ = nullptr;
    std::unique_ptr<State> state_;
};
