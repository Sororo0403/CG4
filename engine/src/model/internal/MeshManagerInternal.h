#pragma once

#include "model/MeshManager.h"

#include <vector>
#include <wrl.h>

struct MeshManager::State {
    std::vector<Mesh> meshes;
    std::vector<Microsoft::WRL::ComPtr<ID3D12Resource>> uploadBuffers;
    std::vector<Mesh> deferredDestroyedMeshes;
    std::vector<std::vector<Microsoft::WRL::ComPtr<ID3D12Resource>>> frameUploadBuffers;
    std::vector<std::vector<Mesh>> frameDeferredDestroyedMeshes;
};
