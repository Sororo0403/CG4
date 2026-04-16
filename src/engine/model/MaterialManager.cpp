#include "MaterialManager.h"
#include "DirectXCommon.h"
#include "DxHelpers.h"
#include "DxUtils.h"
#include <stdexcept>

using namespace DirectX;
using namespace DxUtils;
using Microsoft::WRL::ComPtr;

void MaterialManager::Initialize(DirectXCommon *dxCommon) {
    dxCommon_ = dxCommon;
}

uint32_t MaterialManager::CreateMaterial(const Material &material) {
    if (!dxCommon_) {
        throw std::runtime_error("MaterialManager is not initialized");
    }

    MaterialResource matRes;
    matRes.material = material;

    UINT size = Align256(sizeof(Material));

    CD3DX12_HEAP_PROPERTIES heapProp(D3D12_HEAP_TYPE_UPLOAD);
    auto resourceDesc = CD3DX12_RESOURCE_DESC::Buffer(size);

    ThrowIfFailed(dxCommon_->GetDevice()->CreateCommittedResource(
                      &heapProp, D3D12_HEAP_FLAG_NONE, &resourceDesc,
                      D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
                      IID_PPV_ARGS(&matRes.resource)),
                  "CreateCommittedResource(Material) failed");

    ThrowIfFailed(
        matRes.resource->Map(0, nullptr,
                             reinterpret_cast<void **>(&matRes.mappedData)),
        "Material resource Map failed");

    std::memcpy(matRes.mappedData, &material, sizeof(Material));

    materials_.push_back(std::move(matRes));
    uint32_t materialId = static_cast<uint32_t>(materials_.size() - 1);

    return materialId;
}

void MaterialManager::SetMaterial(uint32_t materialId,
                                  const Material &material) {
    if (materialId >= materials_.size()) {
        return;
    }

    materials_[materialId].material = material;
    std::memcpy(materials_[materialId].mappedData, &material, sizeof(Material));
}

D3D12_GPU_VIRTUAL_ADDRESS
MaterialManager::GetGPUVirtualAddress(uint32_t materialId) const {
    if (materialId >= materials_.size()) {
        return 0;
    }

    return materials_[materialId].resource->GetGPUVirtualAddress();
}

const Material &MaterialManager::GetMaterial(uint32_t materialId) const {
    return materials_[materialId].material;
}