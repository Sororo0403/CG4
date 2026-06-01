#include "model/MaterialManager.h"
#include "graphics/DirectXCommon.h"
#include "graphics/DxHelpers.h"
#include <limits>

using namespace DirectX;
using Microsoft::WRL::ComPtr;

namespace {
const Material &FallbackMaterial() {
    static const Material fallback{};
    return fallback;
}
} // namespace

MaterialManager::~MaterialManager() {
    Finalize();
}

void MaterialManager::Initialize(DirectXCommon *dxCommon) {
    if (!dxCommon) {
        Finalize();
        return;
    }
    Finalize();
    dxCommon_ = dxCommon;
}

void MaterialManager::Finalize() {
    if (dxCommon_ && !dxCommon_->IsDeviceRemoved() &&
        !dxCommon_->IsCommandListRecording()) {
        dxCommon_->WaitForGpuIfPossible();
    }

    for (MaterialResource &material : materials_) {
        material.Reset();
    }
    materials_.clear();
    dxCommon_ = nullptr;
}

uint32_t MaterialManager::CreateMaterial(const Material &material) {
    if (!dxCommon_ || !dxCommon_->GetDevice()) {
        return UINT32_MAX;
    }

    MaterialResource matRes;
    matRes.material = NormalizeMaterialForDraw(material);

    const UINT size =
        static_cast<UINT>((sizeof(Material) + 0xFFu) & ~size_t{0xFFu});

    CD3DX12_HEAP_PROPERTIES heapProp(D3D12_HEAP_TYPE_UPLOAD);
    auto resourceDesc = CD3DX12_RESOURCE_DESC::Buffer(size);

    const HRESULT resourceResult =
        dxCommon_->GetDevice()->CreateCommittedResource(
            &heapProp, D3D12_HEAP_FLAG_NONE, &resourceDesc,
            D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
            IID_PPV_ARGS(&matRes.resource));
    if (FAILED(resourceResult) || !matRes.resource) {
        return UINT32_MAX;
    }

    const HRESULT mapResult =
        matRes.resource->Map(0, nullptr,
                             reinterpret_cast<void **>(&matRes.mappedData));
    if (FAILED(mapResult) || matRes.mappedData == nullptr) {
        return UINT32_MAX;
    }

    std::memcpy(matRes.mappedData, &matRes.material, sizeof(Material));

    if (materials_.size() >=
        static_cast<size_t>((std::numeric_limits<uint32_t>::max)())) {
        return UINT32_MAX;
    }
    materials_.push_back(std::move(matRes));
    uint32_t materialId = static_cast<uint32_t>(materials_.size() - 1);

    return materialId;
}

void MaterialManager::SetMaterial(uint32_t materialId,
                                  const Material &material) {
    if (!IsValidMaterialId(materialId)) {
        return;
    }

    materials_[materialId].material = NormalizeMaterialForDraw(material);
    std::memcpy(materials_[materialId].mappedData,
                &materials_[materialId].material, sizeof(Material));
}

D3D12_GPU_VIRTUAL_ADDRESS
MaterialManager::GetGPUVirtualAddress(uint32_t materialId) const {
    if (!IsValidMaterialId(materialId)) {
        return 0;
    }

    return materials_[materialId].resource->GetGPUVirtualAddress();
}

const Material &MaterialManager::GetMaterial(uint32_t materialId) const {
    if (!IsValidMaterialId(materialId)) {
        return FallbackMaterial();
    }
    return materials_[materialId].material;
}

bool MaterialManager::IsValidMaterialId(uint32_t materialId) const {
    return materialId < materials_.size() &&
           materials_[materialId].resource != nullptr &&
           materials_[materialId].mappedData != nullptr;
}
