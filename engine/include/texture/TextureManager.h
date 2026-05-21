#pragma once
#include "texture/Texture.h"
#include <DirectXTex.h>
#include <cstdint>
#include <d3d12.h>
#include <future>
#include <optional>
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
    /// TextureManagerの共有インスタンスを取得する
    /// </summary>
    static TextureManager &GetInstance();

    /// <summary>
    /// テクスチャ管理に必要なDirectXとSRV管理への参照を設定する
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
    /// 複数テクスチャをまとめて読み込み、ID配列を返す
    /// </summary>
    std::vector<uint32_t> LoadBatch(const std::vector<std::wstring> &filePaths);

    /// <summary>
    /// テクスチャのファイル読み込みとデコードをバックグラウンドで開始する
    /// </summary>
    uint32_t RequestAsyncLoad(const std::wstring &filePath);

    /// <summary>
    /// 複数テクスチャの非同期読み込みをまとめて開始する
    /// </summary>
    std::vector<uint32_t>
    RequestAsyncLoadBatch(const std::vector<std::wstring> &filePaths);

    /// <summary>
    /// 完了した非同期読み込みを現在のコマンドリストへ転送する
    /// </summary>
    void UpdateAsyncLoads();

    /// <summary>
    /// 非同期読み込みが完了したかを取得する
    /// </summary>
    bool IsAsyncLoadComplete(uint32_t requestId) const;

    /// <summary>
    /// 非同期読み込み結果のテクスチャIDを取得する
    /// </summary>
    std::optional<uint32_t> GetAsyncTextureId(uint32_t requestId) const;

    /// <summary>
    /// 非同期読み込みが失敗したかを取得する
    /// </summary>
    bool HasAsyncLoadFailed(uint32_t requestId) const;

    /// <summary>
    /// メモリからテクスチャをロードしてidを返す
    /// </summary>
    /// <param name="data">画像データの先頭アドレス</param>
    /// <param name="size">画像データのバイトサイズ</param>
    /// <returns>生成されたテクスチャのID</returns>
    uint32_t LoadFromMemory(const uint8_t *data, size_t size);

    /// <summary>
    /// RGBA8ピクセル配列から2Dテクスチャを作成する
    /// </summary>
    uint32_t CreateFromRgbaPixels(uint32_t width, uint32_t height,
                                  const uint8_t *pixels);

    /// <summary>
    /// 任意フォーマットのピクセル配列から2Dテクスチャを作成する
    /// </summary>
    uint32_t CreateTexture2D(uint32_t width, uint32_t height,
                             DXGI_FORMAT format, const uint8_t *pixels,
                             size_t rowPitch);

    /// <summary>
    /// 既存2Dテクスチャのピクセル内容を更新する
    /// </summary>
    void UpdateTexture2D(uint32_t textureId, const uint8_t *pixels,
                         size_t rowPitch);

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

    uint32_t GetWhiteTextureId() const { return whiteTextureId_; }
    uint32_t GetDefaultNormalTextureId() const { return defaultNormalTextureId_; }

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

  public:
    struct DecodedTexture {
        std::wstring pathKey;
        DirectX::ScratchImage scratch;
        DirectX::TexMetadata metadata{};
    };

    struct AsyncTextureRequest {
        uint32_t requestId = 0;
        std::future<DecodedTexture> future;
        uint32_t textureId = UINT32_MAX;
        bool completed = false;
        bool failed = false;
    };

  private:
    DirectXCommon *dxCommon_ = nullptr;
    SrvManager *srvManager_ = nullptr;

    std::vector<Entry> textures_;
    std::vector<Microsoft::WRL::ComPtr<ID3D12Resource>> uploadBuffers_;
    std::vector<std::vector<Microsoft::WRL::ComPtr<ID3D12Resource>>>
        frameUploadBuffers_;
    std::unordered_map<std::wstring, uint32_t> filePathToTextureId_;
    std::vector<AsyncTextureRequest> asyncRequests_;
    uint32_t nextAsyncRequestId_ = 1;
    uint32_t whiteTextureId_ = 0;
    uint32_t defaultNormalTextureId_ = 0;
};
