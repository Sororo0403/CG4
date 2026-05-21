#pragma once
#include <DirectXMath.h>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <vector>

class Camera;

/// <summary>
/// 半透明描画を奥から手前へ並べて実行するためのキュー。
/// </summary>
class TransparentRenderQueue {
  public:
    using DrawCallback = std::function<void()>;

    void Clear();

    /// <summary>
    /// 距離の二乗をキーにして描画を登録する。値が大きいものほど先に描画される。
    /// </summary>
    void Submit(float distanceSquared, DrawCallback draw);

    /// <summary>
    /// ワールド座標とカメラ位置から距離キーを計算して描画を登録する。
    /// </summary>
    void Submit(const DirectX::XMFLOAT3 &worldPosition, const Camera &camera,
                DrawCallback draw);

    /// <summary>
    /// 登録された半透明描画を奥から手前の順で実行する。
    /// </summary>
    void Flush();

    bool Empty() const { return items_.empty(); }
    size_t Size() const { return items_.size(); }

  private:
    struct Item {
        float distanceSquared = 0.0f;
        uint64_t sequence = 0;
        DrawCallback draw;
    };

    std::vector<Item> items_;
    uint64_t nextSequence_ = 0;
};
