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
    /// <summary>
    /// SpriteManagerの唯一のインスタンスを取得する
    /// </summary>
    static SpriteManager &GetInstance();

    SpriteManager(const SpriteManager &) = delete;
    SpriteManager &operator=(const SpriteManager &) = delete;

    /// <summary>
    /// スプライト管理と描画器を初期化する
    /// </summary>
    /// <param name="dxCommon">DirectXCommonインスタンス</param>
    /// <param name="textureManager">TextureManagerインスタンス</param>
    /// <param name="srvManager">SrvManagerインスタンス</param>
    /// <param name="width">クライアント領域の幅</param>
    /// <param name="height">クライアント領域の高さ</param>
    void Initialize(DirectXCommon *dxCommon, TextureManager *textureManager,
                    SrvManager *srvManager, int width, int height);

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
    void DrawSprite(const Sprite &sprite);

    /// <summary>
    /// スプライトを作成してidを返す
    /// </summary>
    /// <param name="filePath">作成するスプライトのファイルパス</param>
    /// <returns>スプライトid</returns>
    uint32_t Create(const std::wstring &filePath);

    /// <summary>
    /// フレーム開始時に一時描画領域を先頭へ戻す
    /// </summary>
    void BeginFrame();

    /// <summary>
    /// スプライト描画の開始状態を設定する
    /// </summary>
    void PreDraw();

    /// <summary>
    /// スプライト描画の終了状態を設定する
    /// </summary>
    void PostDraw();

    /// <summary>
    /// スプライトを取得する
    /// </summary>
    Sprite &GetSprite(uint32_t id);

    /// <summary>
    /// スプライトを読み取り専用で取得する
    /// </summary>
    const Sprite &GetSprite(uint32_t id) const;

    /// <summary>
    /// 管理中のスプライト数を取得する
    /// </summary>
    size_t GetCount() const { return sprites_.size(); }

    /// <summary>
    /// 描画領域サイズに合わせて投影行列を更新する
    /// </summary>
    void Resize(int width, int height);

  private:
    SpriteManager() = default;

    DirectXCommon *dxCommon_ = nullptr;
    TextureManager *textureManager_ = nullptr;

    SpriteRenderer spriteRenderer_;
    std::vector<Sprite> sprites_;
};
