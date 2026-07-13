#pragma once
#include "sprite/Sprite.h"
#include "sprite/SpriteRenderer.h"

#include <cstdint>
#include <string>
#include <vector>

class DirectXCommon;
class TextureManager;

class SpriteManager {
public:
    SpriteManager() = default;

    SpriteManager(const SpriteManager&) = delete;
    SpriteManager& operator=(const SpriteManager&) = delete;

    /// <summary>
    /// スプライト管理と描画器を初期化する
    /// </summary>
    /// <param name="dxCommon">DirectXCommonインスタンス</param>
    /// <param name="textureManager">TextureManagerインスタンス</param>
    /// <param name="srvManager">SrvManagerインスタンス</param>
    /// <param name="width">クライアント領域の幅</param>
    /// <param name="height">クライアント領域の高さ</param>
    void Initialize(DirectXCommon* dxCommon, TextureManager* textureManager, SrvManager* srvManager,
                    int width, int height);
    void Finalize();

    /// <summary>
    /// 指定IDのスプライトを描画する
    /// </summary>
    /// <param name="id">描画するスプライトのid</param>
    void Draw(uint32_t id);

    /// <summary>
    /// 管理中のスプライトをzOrder順に描画する
    /// </summary>
    void DrawAllSorted(bool backToFront = false);

    /// <summary>
    /// 指定スプライトを一時描画領域へ直接描画する
    /// </summary>
    void DrawSprite(const Sprite& sprite);

    /// <summary>
    /// スプライトを作成してidを返す
    /// </summary>
    /// <param name="filePath">作成するスプライトのファイルパス</param>
    /// <returns>スプライトid</returns>
    uint32_t Create(const std::wstring& filePath);

    /// <summary>
    /// フレーム開始時に一時描画領域を先頭へ戻す
    /// </summary>
    void BeginFrame();

    /// <summary>
    /// スプライト描画の開始状態を設定する
    /// </summary>
    void PreDraw(bool backBufferTarget = false);

    /// <summary>
    /// スプライト描画の終了状態を設定する
    /// </summary>
    void PostDraw();

    /// <summary>
    /// 低レベルのスプライト描画器を取得する
    /// </summary>
    SpriteRenderer* GetRenderer() {
        return &spriteRenderer_;
    }
    const SpriteRenderer* GetRenderer() const {
        return &spriteRenderer_;
    }
    bool IsReady() const {
        return spriteRenderer_.IsReady();
    }
    size_t GetUploadBytesPerFrame() const {
        return spriteRenderer_.GetUploadBytesPerFrame();
    }
    size_t GetUploadTotalBytes() const {
        return spriteRenderer_.GetUploadTotalBytes();
    }
    size_t GetUploadFrameOffset() const {
        return spriteRenderer_.GetUploadFrameOffset();
    }

    /// <summary>
    /// スプライトを取得する
    /// </summary>
    Sprite& GetSprite(uint32_t id);

    /// <summary>
    /// スプライトを読み取り専用で取得する
    /// </summary>
    const Sprite& GetSprite(uint32_t id) const;

    /// <summary>
    /// 管理中のスプライト数を取得する
    /// </summary>
    size_t GetCount() const {
        return sprites_.size();
    }

    /// <summary>
    /// 指定IDが有効なスプライトを指しているかを取得する
    /// </summary>
    bool IsValidSpriteId(uint32_t id) const;

    /// <summary>
    /// 描画領域サイズに合わせて投影行列を更新する
    /// </summary>
    void Resize(int width, int height);

private:
    DirectXCommon* dxCommon_ = nullptr;
    TextureManager* textureManager_ = nullptr;

    SpriteRenderer spriteRenderer_;
    std::vector<Sprite> sprites_;
    std::vector<size_t> sortedIndices_;
    std::vector<float> sortedZOrders_;
    bool sortedBackToFront_ = false;
    bool sortedCacheValid_ = false;
};
