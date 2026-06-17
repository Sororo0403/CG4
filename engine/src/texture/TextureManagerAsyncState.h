#pragma once

#include "core/ResourceHandle.h"

#include <DirectXTex.h>
#include <atomic>
#include <cstdint>
#include <memory>
#include <string>
#include <thread>
#include <vector>

struct TextureManagerDecodedTexture {
    std::wstring pathKey;
    DirectX::ScratchImage scratch;
    DirectX::TexMetadata metadata{};
    bool succeeded = false;
};

struct TextureManagerAsyncJob {
    TextureManagerDecodedTexture decoded;
    std::atomic_bool ready = false;
};

struct TextureManagerAsyncRequest {
    uint32_t requestId = 0;
    std::wstring filePath;
    std::shared_ptr<TextureManagerAsyncJob> job;
    std::thread worker;
    uint32_t textureId = kInvalidResourceId;
    bool completed = false;
    bool failed = false;
};

struct TextureManagerAsyncState {
    std::vector<TextureManagerAsyncRequest> requests;
    uint32_t nextRequestId = 1;

    void Reset() {
        requests.clear();
        nextRequestId = 1;
    }
};
