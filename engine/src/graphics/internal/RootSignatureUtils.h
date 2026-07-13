#pragma once

#include <d3d12.h>
#include <d3dcompiler.h>
#include <wrl/client.h>

namespace RootSignatureUtils {

inline bool CreateRootSignature(ID3D12Device* device, const D3D12_ROOT_SIGNATURE_DESC& desc,
                                Microsoft::WRL::ComPtr<ID3D12RootSignature>& rootSignature) {
    rootSignature.Reset();
    if (device == nullptr) {
        return false;
    }

    Microsoft::WRL::ComPtr<ID3DBlob> blob;
    Microsoft::WRL::ComPtr<ID3DBlob> error;
    if (FAILED(D3D12SerializeRootSignature(&desc, D3D_ROOT_SIGNATURE_VERSION_1, &blob, &error)) ||
        !blob) {
        return false;
    }
    if (FAILED(device->CreateRootSignature(0, blob->GetBufferPointer(), blob->GetBufferSize(),
                                           IID_PPV_ARGS(&rootSignature))) ||
        !rootSignature) {
        rootSignature.Reset();
        return false;
    }
    return true;
}

} // namespace RootSignatureUtils
