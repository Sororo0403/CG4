#pragma once
#include "collision/CollisionUtil.h"
#include <cstdint>
#include <vector>

class CollisionManager {
  public:
    using BodyId = uint32_t;
    using LayerMask = uint32_t;

    static constexpr BodyId kInvalidBodyId = 0;
    static constexpr LayerMask kLayerNone = 0;
    static constexpr LayerMask kLayerDefault = 1u << 0;
    static constexpr LayerMask kLayerAll = 0xffffffffu;

    enum class ShapeType {
        AABB,
        OBB,
    };

    struct Shape {
        ShapeType type = ShapeType::OBB;
        OBB obb{};
        AABB aabb{};

        static Shape FromOBB(const OBB &box);
        static Shape FromAABB(const AABB &box);
    };

    struct Filter {
        LayerMask layer = kLayerDefault;
        LayerMask mask = kLayerAll;
    };

    struct BodyDesc {
        Shape shape{};
        Filter filter{};
        bool isActive = true;
        bool isTrigger = true;
        const void *userData = nullptr;
    };

    struct Body {
        BodyId id = kInvalidBodyId;
        BodyDesc desc{};
        AABB bounds{};
    };

    struct Hit {
        BodyId a = kInvalidBodyId;
        BodyId b = kInvalidBodyId;
        CollisionUtil::CollisionResult result{};
    };

    /// <summary>
    /// 登録済みBodyをすべて削除する
    /// </summary>
    void Clear();

    /// <summary>
    /// Bodyを登録してIDを返す
    /// </summary>
    BodyId AddBody(const BodyDesc &desc);

    /// <summary>
    /// 指定IDのBodyを削除する
    /// </summary>
    bool RemoveBody(BodyId id);

    /// <summary>
    /// 指定IDのBody情報を更新する
    /// </summary>
    bool UpdateBody(BodyId id, const BodyDesc &desc);

    /// <summary>
    /// 指定IDのBodyを取得する
    /// </summary>
    const Body *GetBody(BodyId id) const;

    /// <summary>
    /// 登録済みBody一覧を取得する
    /// </summary>
    const std::vector<Body> &GetBodies() const { return bodies_; }

    /// <summary>
    /// 指定IDのBody形状を更新する
    /// </summary>
    bool UpdateShape(BodyId id, const Shape &shape);

    /// <summary>
    /// 指定IDのBodyフィルタを更新する
    /// </summary>
    bool UpdateFilter(BodyId id, const Filter &filter);

    /// <summary>
    /// 指定IDのBodyの有効状態を更新する
    /// </summary>
    bool SetActive(BodyId id, bool isActive);

    /// <summary>
    /// 2つのBodyの衝突を判定する
    /// </summary>
    bool Test(BodyId a, BodyId b, Hit *outHit = nullptr) const;

    /// <summary>
    /// 指定Bodyと最初に衝突したBodyを取得する
    /// </summary>
    bool QueryFirst(BodyId body, Hit &outHit) const;

    /// <summary>
    /// 指定Bodyと衝突したBody一覧を取得する
    /// </summary>
    std::vector<Hit> Query(BodyId body) const;

    /// <summary>
    /// 衝突しているBodyペアをすべて取得する
    /// </summary>
    std::vector<Hit> FindPairs() const;

  private:
    /// <summary>
    /// 指定IDのBodyを検索する
    /// </summary>
    Body *FindBody(BodyId id);

    /// <summary>
    /// 指定IDのBodyを読み取り専用で検索する
    /// </summary>
    const Body *FindBody(BodyId id) const;

    /// <summary>
    /// レイヤーと有効状態から衝突判定対象かを判定する
    /// </summary>
    bool CanCollide(const Body &a, const Body &b) const;

    /// <summary>
    /// Bodyの境界AABBを計算する
    /// </summary>
    AABB ComputeBounds(const Shape &shape) const;

    /// <summary>
    /// 2つの形状の衝突情報を計算する
    /// </summary>
    CollisionUtil::CollisionResult TestShapes(const Shape &a,
                                              const Shape &b) const;

    /// <summary>
    /// BodyDescから内部Bodyを作成する
    /// </summary>
    Body CreateBody(const BodyDesc &desc);

  private:
    std::vector<Body> bodies_;
    BodyId nextBodyId_ = 1;
};
