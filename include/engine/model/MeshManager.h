#pragma once
#include <cstdint>
#include <d3d12.h>
#include <vector>
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
};

/// <summary>
/// GPUメッシュリソースの生成と参照を管理する
/// </summary>
class MeshManager {
  public:
    /// <summary>
    /// 初期化処理
    /// </summary>
    /// <param name="dxCommon">DirectXCommonインスタンス</param>
    void Initialize(DirectXCommon *dxCommon);

    /// <summary>
    /// Meshを作成する
    /// </summary>
    /// <param name="vertexData">頂点データへのポインタ</param>
    /// <param name="vertexStride">1頂点あたりのバイトサイズ</param>
    /// <param name="vertexCount">頂点数</param>
    /// <param name="indexData">16bitインデックス配列</param>
    /// <param name="indexCount">インデックス数</param>
    /// <returns>登録されたMeshのID</returns>
    uint32_t CreateMesh(const void *vertexData, uint32_t vertexStride,
                        uint32_t vertexCount, const uint32_t *indexData,
                        uint32_t indexCount);

    /// <summary>
    /// メッシュ情報を取得する
    /// </summary>
    /// <param name="meshId">メッシュID</param>
    /// <returns>メッシュ情報</returns>
    const Mesh &GetMesh(uint32_t meshId) const;

  private:
    DirectXCommon *dxCommon_ = nullptr;
    std::vector<Mesh> meshes_;
};
