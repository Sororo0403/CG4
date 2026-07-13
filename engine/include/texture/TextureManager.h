#pragma once
#include "core/ResourceHandle.h"
#include "texture/Texture.h"

#include <cstddef>
#include <cstdint>
#include <d3d12.h>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <vector>
#include <wrl.h>

class DirectXCommon;
class SrvManager;
struct TextureManagerDecodedTexture;
struct TextureManagerAsyncRequest;
struct TextureManagerAsyncTerminalState;

namespace DirectX {
struct Image;
struct TexMetadata;
} // namespace DirectX

/// <summary>
/// テクスチャ読み込みとSRV管理を担当する
/// </summary>
class TextureManager {
private:
    struct Entry;
    struct State;

public:
    TextureManager();
    ~TextureManager();

    /// <summary>
    /// テクスチャ管理に必要なDirectXとSRV管理への参照を設定する
    /// </summary>
    /// <param name="dxCommon">DirectXCommonインスタンス</param>
    /// <param name="srvManager">SrvManagerインスタンス</param>
    void Initialize(DirectXCommon* dxCommon, SrvManager* srvManager);

    /// <summary>
    /// 管理中のテクスチャとSRV割り当てを解放する
    /// </summary>
    bool Finalize();
    bool Finalize(bool allowFrameAbort);

    /// <summary>
    /// ファイルからテクスチャをロードしてidを返す
    /// </summary>
    /// <param name="filePath">ロードするテクスチャのファイルパス</param>
    /// <returns>テクスチャid</returns>
    uint32_t Load(const std::wstring& filePath);
    uint32_t LoadSrgb(const std::wstring& filePath);
    uint32_t LoadLinear(const std::wstring& filePath);
    TextureHandle LoadHandle(const std::wstring& filePath) {
        return TextureHandle(Load(filePath));
    }

    /// <summary>
    /// 複数テクスチャをまとめて読み込み、ID配列を返す
    /// </summary>
    std::vector<uint32_t> LoadBatch(const std::vector<std::wstring>& filePaths);

    /// <summary>
    /// テクスチャのファイル読み込みとデコードをバックグラウンドで開始する
    /// </summary>
    uint32_t RequestAsyncLoad(const std::wstring& filePath);

    /// <summary>
    /// 複数テクスチャの非同期読み込みをまとめて開始する
    /// </summary>
    std::vector<uint32_t> RequestAsyncLoadBatch(const std::vector<std::wstring>& filePaths);

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
    std::optional<TextureHandle> GetAsyncTextureHandle(uint32_t requestId) const {
        const std::optional<uint32_t> textureId = GetAsyncTextureId(requestId);
        return textureId ? std::optional<TextureHandle>(TextureHandle(*textureId)) : std::nullopt;
    }

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
    /// <summary>
    /// FromMemoryを読み込む
    /// </summary>
    uint32_t LoadFromMemory(const uint8_t* data, size_t size);
    uint32_t LoadFromMemorySrgb(const uint8_t* data, size_t size);
    uint32_t LoadFromMemoryLinear(const uint8_t* data, size_t size);
    TextureHandle LoadFromMemoryHandle(const uint8_t* data, size_t size) {
        return TextureHandle(LoadFromMemory(data, size));
    }

    /// <summary>
    /// RGBA8ピクセル配列から2Dテクスチャを作成する
    /// </summary>
    uint32_t CreateFromRgbaPixels(uint32_t width, uint32_t height, const uint8_t* pixels);
    uint32_t CreateFromRgbaPixelsSrgb(uint32_t width, uint32_t height, const uint8_t* pixels);
    TextureHandle CreateFromRgbaPixelsHandle(uint32_t width, uint32_t height,
                                             const uint8_t* pixels) {
        return TextureHandle(CreateFromRgbaPixels(width, height, pixels));
    }

    /// <summary>
    /// 6面のRGBA8ピクセル配列からCubeテクスチャを作成する
    /// </summary>
    uint32_t CreateCubeFromRgbaPixels(uint32_t size, const uint8_t* const* facePixels);
    TextureHandle CreateCubeFromRgbaPixelsHandle(uint32_t size, const uint8_t* const* facePixels) {
        return TextureHandle(CreateCubeFromRgbaPixels(size, facePixels));
    }

    /// <summary>
    /// 既存Cubeテクスチャの6面RGBA8ピクセル内容を更新する
    /// </summary>
    void UpdateCubeFromRgbaPixels(uint32_t textureId, uint32_t size,
                                  const uint8_t* const* facePixels);

    /// <summary>
    /// 任意フォーマットのピクセル配列から2Dテクスチャを作成する
    /// </summary>
    uint32_t CreateTexture2D(uint32_t width, uint32_t height, DXGI_FORMAT format,
                             const uint8_t* pixels, size_t rowPitch);
    TextureHandle CreateTexture2DHandle(uint32_t width, uint32_t height, DXGI_FORMAT format,
                                        const uint8_t* pixels, size_t rowPitch) {
        return TextureHandle(CreateTexture2D(width, height, format, pixels, rowPitch));
    }

    /// <summary>
    /// 既存2Dテクスチャのピクセル内容を更新する
    /// </summary>
    void UpdateTexture2D(uint32_t textureId, const uint8_t* pixels, size_t rowPitch);
    void UpdateTexture2D(TextureHandle texture, const uint8_t* pixels, size_t rowPitch) {
        UpdateTexture2D(texture.Get(), pixels, rowPitch);
    }

    /// <summary>
    /// 指定テクスチャとSRV割り当てを解放する
    /// </summary>
    bool ReleaseTexture(uint32_t textureId, bool allowFrameAbort = false);
    bool ReleaseTexture(TextureHandle texture, bool allowFrameAbort = false) {
        return ReleaseTexture(texture.Get(), allowFrameAbort);
    }

    /// <summary>
    /// ロード時に使った一時UploadBufferを解放
    /// </summary>
    void ReleaseUploadBuffers();
    void ReleaseCompletedFrameResources();

    /// <summary>
    /// テクスチャのGPUハンドルを取得する
    /// </summary>
    /// <param name="textureId">テクスチャID</param>
    /// <returns>GPUディスクリプタハンドル</returns>
    D3D12_GPU_DESCRIPTOR_HANDLE GetGpuHandle(uint32_t textureId) const;
    D3D12_GPU_DESCRIPTOR_HANDLE GetGpuHandle(TextureHandle texture) const {
        return GetGpuHandle(texture.Get());
    }

    uint32_t GetWhiteTextureId() const;
    uint32_t GetWhiteCubeTextureId() const;
    uint32_t GetBlackCubeTextureId() const;
    uint32_t GetDefaultNormalTextureId() const;
    TextureHandle GetWhiteTextureHandle() const {
        return TextureHandle(GetWhiteTextureId());
    }
    TextureHandle GetWhiteCubeTextureHandle() const {
        return TextureHandle(GetWhiteCubeTextureId());
    }
    TextureHandle GetBlackCubeTextureHandle() const {
        return TextureHandle(GetBlackCubeTextureId());
    }
    TextureHandle GetDefaultNormalTextureHandle() const {
        return TextureHandle(GetDefaultNormalTextureId());
    }

    /// <summary>
    /// 指定IDが有効なテクスチャを指しているかを取得する
    /// </summary>
    bool IsValidTextureId(uint32_t textureId) const;
    bool IsCubeTextureId(uint32_t textureId) const;
    bool IsValidTexture(TextureHandle texture) const {
        return IsValidTextureId(texture.Get());
    }
    bool IsCubeTexture(TextureHandle texture) const {
        return IsCubeTextureId(texture.Get());
    }

    /// <summary>
    /// テクスチャリソースを取得する
    /// </summary>
    /// <param name="textureId">テクスチャID</param>
    /// <returns>ID3D12Resourceへのポインタ</returns>
    ID3D12Resource* GetResource(uint32_t textureId) const;
    ID3D12Resource* GetResource(TextureHandle texture) const {
        return GetResource(texture.Get());
    }

    /// <summary>
    /// テクスチャ幅を取得する
    /// </summary>
    /// <param name="id">テクスチャID</param>
    /// <returns>テクスチャ幅</returns>
    uint32_t GetWidth(uint32_t id) const;
    uint32_t GetWidth(TextureHandle texture) const {
        return GetWidth(texture.Get());
    }

    /// <summary>
    /// テクスチャ高さを取得する
    /// </summary>
    /// <param name="id">テクスチャID</param>
    /// <returns>テクスチャ高さ</returns>
    uint32_t GetHeight(uint32_t id) const;
    uint32_t GetHeight(TextureHandle texture) const {
        return GetHeight(texture.Get());
    }
    size_t GetTextureCount() const;
    uint64_t GetTextureGpuBytes() const;
    uint64_t GetUploadBytes() const;

private:
    bool ResetStateForInitialize();
    bool CreateDefaultTextures();
    bool HasExpectedDefaultTexture(uint32_t textureId, UINT16 arraySize, bool isCube) const;
    bool HasValidDefaultTextures() const;
    uint32_t GetWhiteFallbackTextureId() const;

    /// <summary>
    /// Image配列からGPUテクスチャを生成する
    /// </summary>
    /// <param name="images">画像配列</param>
    /// <param name="imageCount">画像枚数</param>
    /// <param name="metadata">テクスチャメタデータ</param>
    /// <returns>生成されたテクスチャID</returns>
    uint32_t CreateTexture(const DirectX::Image* images, size_t imageCount,
                           const DirectX::TexMetadata& metadata);
    bool ReserveTextureStorage();
    bool StoreTextureUploadBuffer(const Microsoft::WRL::ComPtr<ID3D12Resource>& uploadBuffer,
                                  bool ownsUploadPass);
    bool StoreTextureEntry(Texture&& texture, uint32_t srvIndex, uint32_t& textureId);
    void RollBackTextureEntry(uint32_t textureId, uint32_t srvIndex);
    bool RegisterTextureFrameRollback(uint32_t textureId, uint32_t srvIndex);
    struct TextureCreationWork;
    bool InitializeTextureCreationWork(const DirectX::Image* images, size_t imageCount,
                                       const DirectX::TexMetadata& metadata,
                                       uint32_t fallbackTextureId, TextureCreationWork& work);
    bool BeginTextureCreationUpload(const TextureCreationWork& work);
    bool CreateTextureGpuResources(TextureCreationWork& work);
    bool AllocateAndStoreTexture(TextureCreationWork& work);
    bool CopyAndCreateTextureSrv(TextureCreationWork& work);
    uint32_t LoadWithColorSpace(const std::wstring& filePath, int colorSpacePolicy);
    uint32_t LoadFromMemoryWithColorSpace(const uint8_t* data, size_t size, int colorSpacePolicy);

private:
    uint32_t AllocateAsyncRequestId();
    uint32_t EnqueueCompletedAsyncRequest(uint32_t textureId);
    bool CanRequestAsyncLoad() const;
    static bool TryResolveAsyncLoadPath(const std::wstring& filePath,
                                        std::filesystem::path& resolvedPath, std::wstring& pathKey);
    std::optional<uint32_t> FindReusableAsyncRequestOrTexture(const std::wstring& pathKey);
    bool InitializeAsyncLoadRequest(const std::filesystem::path& resolvedPath,
                                    const std::wstring& pathKey,
                                    TextureManagerAsyncRequest& request);
    uint32_t StoreAsyncLoadRequest(TextureManagerAsyncRequest&& request);
    bool TryCompleteAsyncTextureUpload(TextureManagerAsyncRequest& request,
                                       TextureManagerDecodedTexture& decoded);
    void RestoreAsyncTextureCache(const std::wstring& pathKey, bool hadPreviousCache,
                                  uint32_t previousTextureId);
    void RecordAsyncTerminalState(uint32_t requestId, std::optional<uint32_t> textureId,
                                  bool failed);
    std::optional<TextureManagerAsyncTerminalState> FindAsyncTerminalState(
        uint32_t requestId) const;
    void PruneCompletedAsyncRequests();
    void EnsureAsyncWorkers();
    void StartQueuedAsyncLoads();
    void StopAsyncWorkers();
    void StopAsyncLoads();

    DirectXCommon* dxCommon_ = nullptr;
    SrvManager* srvManager_ = nullptr;
    std::unique_ptr<State> state_;
};
