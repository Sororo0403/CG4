#pragma once
#include "Sprite.h"
#include "SpriteRenderer.h"
#include <cstdint>
#include <string>
#include <vector>

class DirectXCommon;
class SrvManager;
class TextureManager;

/// <summary>
/// スプライト生成と描画アクセスを管理する
/// </summary>
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
    /// スプライトを作成してidを返す
    /// </summary>
    /// <param name="filePath">作成するスプライトのファイルパス</param>
    /// <returns>スプライトid</returns>
    uint32_t Create(const std::wstring &filePath);

    /// <summary>
    /// スプライトを取得する
    /// </summary>
    /// <param name="id">スプライトID</param>
    /// <returns>スプライト参照</returns>
    Sprite &GetSprite(uint32_t id);
    /// <summary>
    /// スプライトを読み取り専用で取得する
    /// </summary>
    /// <param name="id">スプライトID</param>
    /// <returns>スプライト参照</returns>
    const Sprite &GetSprite(uint32_t id) const;
    /// <summary>
    /// 管理中スプライト数を取得する
    /// </summary>
    /// <returns>スプライト数</returns>
    size_t GetCount() const { return sprites_.size(); }
    /// <summary>
    /// 内部のSpriteRendererを取得する
    /// </summary>
    /// <returns>SpriteRendererへのポインタ</returns>
    SpriteRenderer *GetRenderer() { return &spriteRenderer_; }
    /// <summary>
    /// 内部のSpriteRendererを読み取り専用で取得する
    /// </summary>
    /// <returns>SpriteRendererへのポインタ</returns>
    const SpriteRenderer *GetRenderer() const { return &spriteRenderer_; }

  private:
    DirectXCommon *dxCommon_ = nullptr;
    TextureManager *textureManager_ = nullptr;

    SpriteRenderer spriteRenderer_;
    std::vector<Sprite> sprites_;
};
