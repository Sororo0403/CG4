#pragma once
#include "graphics/UploadRingBuffer.h"
#include "sprite/Sprite.h"
#include <array>
#include <DirectXMath.h>
#include <cstddef>
#include <cstdint>
#include <vector>
#include <d3d12.h>
#include <wrl.h>

class DirectXCommon;
class TextureManager;
class SrvManager;

class SpriteRenderer {
  public:
    /// <summary>
    /// スプライト描画に必要なパイプラインとバッファを初期化する
    /// </summary>
    /// <param name="dxCommon">DirectXCommonインスタンス</param>
    /// <param name="textureManager">TextureManagerインスタンス</param>
    /// <param name="srvManager">SrvManagerインスタンス</param>
    /// <param name="width">クライアント領域の幅</param>
    /// <param name="height">クライアント領域の高さ</param>
    void Initialize(DirectXCommon *dxCommon, TextureManager *textureManager,
                    SrvManager *srvManager, int width, int height);

    /// <summary>
    /// スプライト1枚分の頂点を一時バッファへ書き込んで描画する
    /// </summary>
    /// <param name="sprite">描画するスプライト</param>
    void Draw(const Sprite &sprite);

    /// <summary>
    /// フレーム開始時に一時描画領域を先頭へ戻す
    /// </summary>
    void BeginFrame();

    /// <summary>
    /// スプライト用パイプラインを描画前に設定する
    /// </summary>
    void PreDraw(bool backBufferTarget = false);

    /// <summary>
    /// スプライト描画後の状態を整理する
    /// </summary>
    void PostDraw();

    /// <summary>
    /// 投影行列を更新する
    /// </summary>
    void UpdateProjection(int width, int height);

  private:
    enum class PipelineKind : uint32_t {
        Alpha = 0,
        Modulate = 1,
        PremultipliedMask = 2,
        Count,
    };

    enum class RenderTargetKind : uint32_t {
        SceneColor = 0,
        BackBuffer = 1,
        Count,
    };

    static constexpr uint32_t kVerticesPerSprite = 6;
    static constexpr uint32_t kMaxSpriteDraws = 4096;
    static constexpr size_t kUploadBytesPerFrame = 4 * 1024 * 1024;

    struct SpriteVertex {
        DirectX::XMFLOAT3 pos;
        DirectX::XMFLOAT2 uv;
        DirectX::XMFLOAT4 color;
    };

    struct QueuedDraw {
        PipelineKind pipelineKind = PipelineKind::Alpha;
        uint32_t textureId = 0;
        std::array<SpriteVertex, kVerticesPerSprite> vertices{};
    };

    /// <summary>
    /// ルートシグネチャを生成する
    /// </summary>
    void CreateRootSignature();

    /// <summary>
    /// パイプラインステートを生成する
    /// </summary>
    void CreatePipelineState();
    bool HasAllPipelineStates() const;

    void CreateUploadBuffer();
    /// <summary>
    /// FlushQueuedDrawsを実行する
    /// </summary>
    void FlushQueuedDraws();

  private:
    DirectXCommon *dxCommon_ = nullptr;
    TextureManager *textureManager_ = nullptr;
    SrvManager *srvManager_ = nullptr;

    Microsoft::WRL::ComPtr<ID3D12RootSignature> rootSignature_;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> pipelineStates_
        [static_cast<uint32_t>(RenderTargetKind::Count)]
        [static_cast<uint32_t>(PipelineKind::Count)];

    UploadRingBuffer uploadBuffer_;
    uint32_t drawCursor_ = 0;
    std::vector<QueuedDraw> queuedDraws_;
    std::vector<SpriteVertex> batchVertices_;

    DirectX::XMFLOAT4X4 matProjection_{};
    PipelineKind activePipelineKind_ = PipelineKind::Alpha;
    RenderTargetKind activeRenderTargetKind_ = RenderTargetKind::SceneColor;
};
