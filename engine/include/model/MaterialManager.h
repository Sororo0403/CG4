#pragma once
#include "core/ResourceHandle.h"
#include "model/Material.h"

#include <cstdint>
#include <d3d12.h>
#include <memory>

class DirectXCommon;

/// <summary>
/// マテリアル定数バッファの生成と参照を管理する
/// </summary>
class MaterialManager {
public:
    MaterialManager();
    ~MaterialManager();

    /// <summary>
    /// マテリアル用GPUリソースを生成できるようDirectX参照を設定する
    /// </summary>
    /// <param name="dxCommon">DirectXCommonインスタンス</param>
    void Initialize(DirectXCommon* dxCommon);

    /// <summary>
    /// マテリアル定数バッファを明示的に解放する
    /// </summary>
    bool Finalize();
    bool Finalize(bool allowFrameAbort);

    /// <summary>
    /// マテリアルを作成してIDを返す
    /// </summary>
    /// <param name="material">Material構造体</param>
    /// <returns>マテリアルのID</returns>
    uint32_t CreateMaterial(const Material& material);
    MaterialHandle CreateMaterialHandle(const Material& material) {
        return MaterialHandle(CreateMaterial(material));
    }

    void DestroyMaterial(uint32_t materialId);
    void DestroyMaterial(MaterialHandle material) {
        DestroyMaterial(material.Get());
    }

    /// <summary>
    /// 既存マテリアルの内容を更新する
    /// </summary>
    /// <param name="materialId">更新対象のマテリアルID</param>
    /// <param name="material">設定するマテリアル値</param>
    void SetMaterial(uint32_t materialId, const Material& material);
    void SetMaterial(MaterialHandle materialId, const Material& material) {
        SetMaterial(materialId.Get(), material);
    }

    /// <summary>
    /// GPU仮想アドレスを取得する
    /// </summary>
    /// <param name="materialId">対象マテリアルID</param>
    /// <returns>定数バッファのGPU仮想アドレス</returns>
    D3D12_GPU_VIRTUAL_ADDRESS GetGPUVirtualAddress(uint32_t materialId);
    D3D12_GPU_VIRTUAL_ADDRESS
    GetGPUVirtualAddress(MaterialHandle materialId) {
        return GetGPUVirtualAddress(materialId.Get());
    }

    /// <summary>
    /// マテリアル情報を取得する
    /// </summary>
    /// <param name="materialId">対象マテリアルID</param>
    /// <returns>マテリアル情報</returns>
    const Material& GetMaterial(uint32_t materialId) const;
    const Material& GetMaterial(MaterialHandle materialId) const {
        return GetMaterial(materialId.Get());
    }

    /// <summary>
    /// 指定IDが有効なマテリアルを指しているかを取得する
    /// </summary>
    bool IsValidMaterialId(uint32_t materialId) const;
    bool IsValidMaterial(MaterialHandle materialId) const {
        return IsValidMaterialId(materialId.Get());
    }

    void ReleaseDeferredResources();
    void ReleaseCompletedFrameResources();

private:
    struct MaterialResource;
    struct State;

private:
    DirectXCommon* dxCommon_ = nullptr;
    std::unique_ptr<State> state_;
};
