#pragma once

#include "RendererObjectConstants.h"
#include "graphics/DirectXCommon.h"
#include "graphics/UploadRingBuffer.h"
#include "model/InstanceData.h"

#include <DirectXMath.h>
#include <cstddef>
#include <cstdint>

namespace RendererUploadUtils {

inline bool InitializeUploadBuffer(UploadRingBuffer& uploadBuffer, const DirectXCommon* dxCommon,
                                   size_t bytesPerFrame, uint32_t frameCount = 2) {
    if (dxCommon == nullptr || dxCommon->GetDevice() == nullptr) {
        uploadBuffer.Reset();
        return false;
    }
    uploadBuffer.Initialize(dxCommon->GetDevice(), bytesPerFrame, frameCount);
    return true;
}

inline bool CanStageInstanceData(size_t bytesPerFrame, uint32_t instanceCount) {
    if (bytesPerFrame == 0 || instanceCount == 0) {
        return false;
    }
    const size_t byteSize = sizeof(InstanceData) * static_cast<size_t>(instanceCount);
    return byteSize <= bytesPerFrame;
}

inline uint64_t HashBytes(const void* data, size_t size) {
    constexpr uint64_t kFnvOffset = 14695981039346656037ull;
    constexpr uint64_t kFnvPrime = 1099511628211ull;
    const auto* bytes = static_cast<const uint8_t*>(data);
    uint64_t hash = kFnvOffset;
    for (size_t index = 0; index < size; ++index) {
        hash ^= static_cast<uint64_t>(bytes[index]);
        hash *= kFnvPrime;
    }
    return hash;
}

inline D3D12_GPU_VIRTUAL_ADDRESS WriteObjectConstants(
    UploadRingBuffer& uploadBuffer, const DirectX::XMMATRIX& wvp, const DirectX::XMMATRIX& world,
    const DirectX::XMMATRIX& worldInverseTranspose) {
    const RendererObjectConstants::PerObjectConstBufferData data =
        RendererObjectConstants::MakePerObjectConstants(wvp, world, worldInverseTranspose);
    return uploadBuffer.Write(data).gpu;
}

} // namespace RendererUploadUtils
