#pragma once
#include "Texture.h"
#include <DirectXTex.h>
#include <cstdint>
#include <d3d12.h>
#include <string>
#include <unordered_map>
#include <vector>
#include <wrl.h>

class DirectXCommon;
class SrvManager;

class TextureManager {
  private:
    struct Entry {
        Texture texture;
        uint32_t srvIndex = 0;
    };

  public:
    /// <summary>
    /// 初期化処理
    /// </summary>
    /// <param name="dxCommon">DirectXCommonインスタンス</param>
    /// <param name="srvManager">SrvManagerインスタンス</param>
    void Initialize(DirectXCommon *dxCommon, SrvManager *srvManager);

    /// <summary>
    /// ファイルからテクスチャをロードしてidを返す
    /// </summary>
    /// <param name="filePath">ロードするテクスチャのファイルパス</param>
    /// <returns>テクスチャid</returns>
    uint32_t Load(const std::wstring &filePath);

    /// <summary>
    /// メモリからテクスチャをロードしてidを返す
    /// </summary>
    /// <param name="data">画像データの先頭アドレス</param>
    /// <param name="size">画像データのバイトサイズ</param>
    /// <returns>生成されたテクスチャのID</returns>
    uint32_t LoadFromMemory(const uint8_t *data, size_t size);

    /// <summary>
    /// ロード時に使った一時UploadBufferを解放
    /// </summary>
    void ReleaseUploadBuffers();

    // Getter
    D3D12_GPU_DESCRIPTOR_HANDLE GetGpuHandle(uint32_t textureId) const;
    ID3D12Resource *GetResource(uint32_t textureId) const;
    uint32_t GetWidth(uint32_t id) const;
    uint32_t GetHeight(uint32_t id) const;

  private:
    // Create
    uint32_t CreateTexture(const DirectX::Image *image,
                           const DirectX::TexMetadata &metadata);

  private:
    DirectXCommon *dxCommon_ = nullptr;
    SrvManager *srvManager_ = nullptr;

    std::vector<Entry> textures_;
    std::vector<Microsoft::WRL::ComPtr<ID3D12Resource>> uploadBuffers_;
    std::unordered_map<std::wstring, uint32_t> filePathToTextureId_;
};