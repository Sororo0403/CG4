#pragma once
#include "Sprite.h"
#include "SpriteRenderer.h"
#include <cstdint>
#include <string>
#include <vector>

class DirectXCommon;
class TextureManager;

class SpriteManager {
  public:
    /// <summary>
    /// 初期化処理
    /// </summary>
    /// <param name="dxCommon">DirectXCommonインスタンス</param>
    /// <param name="textureManager">TextureManagerインスタンス</param>
    /// <param name="srvManager">SrvManagerインスタンス</param>
    /// <param name="width">クライアント領域の幅</param>
    /// <param name="height">クライアント領域の高さ</param>
    void Initialize(DirectXCommon *dxCommon, TextureManager *textureManager,
                    SrvManager *srvManager, int width, int height);

    /// <summary>
    /// 描画処理
    /// </summary>
    /// <param name="id">描画するスプライトのid</param>
    void Draw(uint32_t id);

    /// <summary>
    /// スプライトを作成してidを返す
    /// </summary>
    /// <param name="filePath">作成するスプライトのファイルパス</param>
    /// <returns>スプライトid</returns>
    uint32_t Create(const std::wstring &filePath);

    /// <summary>
    /// 描画前処理
    /// </summary>
    void PreDraw();

    /// <summary>
    /// 描画後処理
    /// </summary>
    void PostDraw();

    // Getter
    Sprite &GetSprite(uint32_t id);
    const Sprite &GetSprite(uint32_t id) const;
    size_t GetCount() const { return sprites_.size(); }

  private:
    DirectXCommon *dxCommon_ = nullptr;
    TextureManager *textureManager_ = nullptr;

    SpriteRenderer spriteRenderer_;
    std::vector<Sprite> sprites_;
};
