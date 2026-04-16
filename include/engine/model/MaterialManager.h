#pragma once
#include "Material.h"
#include <cstdint>
#include <d3d12.h>
#include <vector>
#include <wrl.h>

class DirectXCommon;

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

    // Setter
    void SetMaterial(uint32_t materialId, const Material &material);

    // Getter
    D3D12_GPU_VIRTUAL_ADDRESS GetGPUVirtualAddress(uint32_t materialId) const;
    const Material &GetMaterial(uint32_t materialId) const;

  private:
    struct MaterialResource {
        Material material{};
        Microsoft::WRL::ComPtr<ID3D12Resource> resource;
        uint8_t *mappedData = nullptr;
    };

  private:
    DirectXCommon *dxCommon_ = nullptr;
    std::vector<MaterialResource> materials_;
};