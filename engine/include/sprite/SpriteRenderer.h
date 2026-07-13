#pragma once
#include "sprite/Sprite.h"

#include <cstddef>
#include <cstdint>
#include <memory>

class DirectXCommon;
class TextureManager;
class SrvManager;

class SpriteRenderer {
public:
    SpriteRenderer();
    ~SpriteRenderer();

    /// <summary>
    /// スプライト描画に必要なパイプラインとバッファを初期化する
    /// </summary>
    /// <param name="dxCommon">DirectXCommonインスタンス</param>
    /// <param name="textureManager">TextureManagerインスタンス</param>
    /// <param name="srvManager">SrvManagerインスタンス</param>
    /// <param name="width">クライアント領域の幅</param>
    /// <param name="height">クライアント領域の高さ</param>
    void Initialize(DirectXCommon* dxCommon, TextureManager* textureManager, SrvManager* srvManager,
                    int width, int height);
    bool Finalize();
    bool Finalize(bool allowFrameAbort);

    /// <summary>
    /// スプライト1枚分の頂点を一時バッファへ書き込んで描画する
    /// </summary>
    /// <param name="sprite">描画するスプライト</param>
    void Draw(const Sprite& sprite);

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
    bool IsReady() const;
    size_t GetUploadBytesPerFrame() const;
    size_t GetUploadTotalBytes() const;
    size_t GetUploadFrameOffset() const;

private:
    enum class PipelineKind : uint32_t {
        Alpha = 0,
        Modulate = 1,
        PremultipliedMask = 2,
        Count = 3,
    };

    enum class RenderTargetKind : uint32_t {
        SceneColor = 0,
        BackBuffer = 1,
        Count = 2,
    };

    static constexpr uint32_t kVerticesPerSprite = 6;
    static constexpr uint32_t kMaxSpriteDraws = 4096;
    static constexpr size_t kUploadBytesPerFrame = 1 * 1024 * 1024;

    struct SpriteVertex;
    struct QueuedDraw;
    struct State;

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
    void ResetResources();
    /// <summary>
    /// FlushQueuedDrawsを実行する
    /// </summary>
    void FlushQueuedDraws();

private:
    DirectXCommon* dxCommon_ = nullptr;
    TextureManager* textureManager_ = nullptr;
    SrvManager* srvManager_ = nullptr;
    std::unique_ptr<State> state_;
};
