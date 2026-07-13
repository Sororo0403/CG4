#pragma once

#include "TextureManagerAsyncState.h"
#include "core/ResourceHandle.h"
#include "texture/Texture.h"
#include "texture/TextureManager.h"

#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>
#include <wrl.h>

struct TextureManager::Entry {
    Texture texture;
    uint32_t srvIndex = kInvalidResourceId;
};

struct TextureManager::State {
    std::vector<Entry> textures;
    std::vector<Microsoft::WRL::ComPtr<ID3D12Resource>> uploadBuffers;
    std::vector<std::vector<Microsoft::WRL::ComPtr<ID3D12Resource>>> frameUploadBuffers;
    std::unordered_map<std::wstring, uint32_t> filePathToTextureId;
    std::unique_ptr<TextureManagerAsyncState> asyncState =
        std::make_unique<TextureManagerAsyncState>();
    uint32_t whiteTextureId = kInvalidResourceId;
    uint32_t whiteCubeTextureId = kInvalidResourceId;
    uint32_t blackCubeTextureId = kInvalidResourceId;
    uint32_t defaultNormalTextureId = kInvalidResourceId;
    UINT lastDynamicUploadFrameIndex = UINT_MAX;
};
