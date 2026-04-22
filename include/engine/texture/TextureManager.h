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

/// <summary>
/// テクスチャ読み込みとSRV管理を担当する
/// </summary>
class TextureManager {
  private:
    /// <summary>
    /// テクスチャ本体と対応するSRV情報
    /// </summary>
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
    /// デバッグ用のキューブマップを生成する
    /// </summary>
    /// <param name="faceSize">各面の一辺サイズ</param>
    /// <returns>生成されたテクスチャID</returns>
    uint32_t CreateDebugCubemap(uint32_t faceSize = 4);

    /// <summary>
    /// ロード時に使った一時UploadBufferを解放
    /// </summary>
    void ReleaseUploadBuffers();

    /// <summary>
    /// テクスチャのGPUハンドルを取得する
    /// </summary>
    /// <param name="textureId">テクスチャID</param>
    /// <returns>GPUディスクリプタハンドル</returns>
    D3D12_GPU_DESCRIPTOR_HANDLE GetGpuHandle(uint32_t textureId) const;
    /// <summary>
    /// テクスチャリソースを取得する
    /// </summary>
    /// <param name="textureId">テクスチャID</param>
    /// <returns>ID3D12Resourceへのポインタ</returns>
    ID3D12Resource *GetResource(uint32_t textureId) const;
    /// <summary>
    /// テクスチャ幅を取得する
    /// </summary>
    /// <param name="id">テクスチャID</param>
    /// <returns>テクスチャ幅</returns>
    uint32_t GetWidth(uint32_t id) const;
    /// <summary>
    /// テクスチャ高さを取得する
    /// </summary>
    /// <param name="id">テクスチャID</param>
    /// <returns>テクスチャ高さ</returns>
    uint32_t GetHeight(uint32_t id) const;

  private:
    /// <summary>
    /// Image配列からGPUテクスチャを生成する
    /// </summary>
    /// <param name="images">画像配列</param>
    /// <param name="imageCount">画像枚数</param>
    /// <param name="metadata">テクスチャメタデータ</param>
    /// <returns>生成されたテクスチャID</returns>
    uint32_t CreateTexture(const DirectX::Image *images, size_t imageCount,
                           const DirectX::TexMetadata &metadata);

  private:
    DirectXCommon *dxCommon_ = nullptr;
    SrvManager *srvManager_ = nullptr;

    std::vector<Entry> textures_;
    std::vector<Microsoft::WRL::ComPtr<ID3D12Resource>> uploadBuffers_;
    std::unordered_map<std::wstring, uint32_t> filePathToTextureId_;
};
