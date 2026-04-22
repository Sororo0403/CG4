#pragma once
#include "Material.h"
#include <cstdint>
#include <d3d12.h>
#include <vector>
#include <wrl.h>

class DirectXCommon;

/// <summary>
/// マテリアル定数バッファの生成と参照を管理する
/// </summary>
class MaterialManager {
  public:
    /// <summary>
    /// 初期化処理
    /// </summary>
    /// <param name="dxCommon">DirectXCommonインスタンス</param>
    void Initialize(DirectXCommon *dxCommon);

    /// <summary>
    /// マテリアルを作成してIDを返す
    /// </summary>
    /// <param name="material">Material構造体</param>
    /// <returns>マテリアルのID</returns>
    uint32_t CreateMaterial(const Material &material);

    /// <summary>
    /// 既存マテリアルの内容を更新する
    /// </summary>
    /// <param name="materialId">更新対象のマテリアルID</param>
    /// <param name="material">設定するマテリアル値</param>
    void SetMaterial(uint32_t materialId, const Material &material);

    /// <summary>
    /// GPU仮想アドレスを取得する
    /// </summary>
    /// <param name="materialId">対象マテリアルID</param>
    /// <returns>定数バッファのGPU仮想アドレス</returns>
    D3D12_GPU_VIRTUAL_ADDRESS GetGPUVirtualAddress(uint32_t materialId) const;
    /// <summary>
    /// マテリアル情報を取得する
    /// </summary>
    /// <param name="materialId">対象マテリアルID</param>
    /// <returns>マテリアル情報</returns>
    const Material &GetMaterial(uint32_t materialId) const;

  private:
    /// <summary>
    /// マテリアル1件分のGPUリソースを保持する
    /// </summary>
    struct MaterialResource {
        Material material{};
        Microsoft::WRL::ComPtr<ID3D12Resource> resource;
        uint8_t *mappedData = nullptr;
    };

  private:
    DirectXCommon *dxCommon_ = nullptr;
    std::vector<MaterialResource> materials_;
};
