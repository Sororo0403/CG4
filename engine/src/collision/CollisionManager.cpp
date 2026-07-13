#include "collision/CollisionManager.h"

#include "core/Numeric.h"

#include <algorithm>
#include <array>
#include <cfloat>
#include <cmath>
#include <exception>
#include <iterator>
#include <limits>
#include <new>

using namespace DirectX;

namespace {
using Numeric::FiniteOr;

constexpr float kDefaultObbRotationW = 1.0f;

float FinitePairValue(float primary, float secondary) {
    if (std::isfinite(primary)) {
        return primary;
    }
    return std::isfinite(secondary) ? secondary : 0.0f;
}

float FiniteHalfExtent(float value) {
    return std::isfinite(value) ? std::fabs(value) * 0.5f : 0.0f;
}

AABB NormalizeAABB(const AABB& box) {
    const float minX = FinitePairValue(box.min.x, box.max.x);
    const float maxX = FinitePairValue(box.max.x, minX);
    const float minY = FinitePairValue(box.min.y, box.max.y);
    const float maxY = FinitePairValue(box.max.y, minY);
    const float minZ = FinitePairValue(box.min.z, box.max.z);
    const float maxZ = FinitePairValue(box.max.z, minZ);

    AABB normalized{};
    normalized.min.x = (std::min)(minX, maxX);
    normalized.min.y = (std::min)(minY, maxY);
    normalized.min.z = (std::min)(minZ, maxZ);
    normalized.max.x = (std::max)(minX, maxX);
    normalized.max.y = (std::max)(minY, maxY);
    normalized.max.z = (std::max)(minZ, maxZ);
    return normalized;
}

OBB AABBToOBB(const AABB& box) {
    const AABB normalized = NormalizeAABB(box);
    OBB result{};
    result.center = {
        (normalized.min.x + normalized.max.x) * 0.5f,
        (normalized.min.y + normalized.max.y) * 0.5f,
        (normalized.min.z + normalized.max.z) * 0.5f,
    };
    result.size = {
        normalized.max.x - normalized.min.x,
        normalized.max.y - normalized.min.y,
        normalized.max.z - normalized.min.z,
    };
    result.rotation = {0.0f, 0.0f, 0.0f, kDefaultObbRotationW};
    return result;
}

XMVECTOR NormalizeQuaternion(const XMFLOAT4& rotation) {
    if (!std::isfinite(rotation.x) || !std::isfinite(rotation.y) || !std::isfinite(rotation.z) ||
        !std::isfinite(rotation.w)) {
        return XMQuaternionIdentity();
    }
    XMVECTOR q = XMLoadFloat4(&rotation);
    const float lengthSq = XMVectorGetX(XMVector4LengthSq(q));
    if (!std::isfinite(lengthSq) || lengthSq <= 0.00001f) {
        return XMQuaternionIdentity();
    }
    return XMQuaternionNormalize(q);
}

OBB ShapeAabbToObb(const CollisionManager::Shape& shape) {
    return AABBToOBB(shape.aabb);
}

OBB ShapeObbToObb(const CollisionManager::Shape& shape) {
    return shape.obb;
}

AABB ShapeAabbBounds(const CollisionManager::Shape& shape) {
    return NormalizeAABB(shape.aabb);
}

AABB ShapeObbBounds(const CollisionManager::Shape& shape) {
    const OBB& box = shape.obb;
    const XMVECTOR center = XMVectorSet(FiniteOr(box.center.x, 0.0f), FiniteOr(box.center.y, 0.0f),
                                        FiniteOr(box.center.z, 0.0f), 0.0f);
    const XMVECTOR rotation = NormalizeQuaternion(box.rotation);
    const std::array<XMVECTOR, 3u> axes{
        XMVector3Rotate(XMVectorSet(FiniteHalfExtent(box.size.x), 0.0f, 0.0f, 0.0f), rotation),
        XMVector3Rotate(XMVectorSet(0.0f, FiniteHalfExtent(box.size.y), 0.0f, 0.0f), rotation),
        XMVector3Rotate(XMVectorSet(0.0f, 0.0f, FiniteHalfExtent(box.size.z), 0.0f), rotation),
    };

    AABB bounds{};
    bounds.min = {FLT_MAX, FLT_MAX, FLT_MAX};
    bounds.max = {-FLT_MAX, -FLT_MAX, -FLT_MAX};

    for (int x = -1; x <= 1; x += 2) {
        for (int y = -1; y <= 1; y += 2) {
            for (int z = -1; z <= 1; z += 2) {
                XMVECTOR corner = center + std::get<0>(axes) * static_cast<float>(x) +
                                  std::get<1>(axes) * static_cast<float>(y) +
                                  std::get<2>(axes) * static_cast<float>(z);
                XMFLOAT3 point{};
                XMStoreFloat3(&point, corner);
                if (!std::isfinite(point.x) || !std::isfinite(point.y) || !std::isfinite(point.z)) {
                    continue;
                }
                bounds.min.x = (std::min)(bounds.min.x, point.x);
                bounds.min.y = (std::min)(bounds.min.y, point.y);
                bounds.min.z = (std::min)(bounds.min.z, point.z);
                bounds.max.x = (std::max)(bounds.max.x, point.x);
                bounds.max.y = (std::max)(bounds.max.y, point.y);
                bounds.max.z = (std::max)(bounds.max.z, point.z);
            }
        }
    }

    if (bounds.min.x == FLT_MAX) {
        bounds.min = {0.0f, 0.0f, 0.0f};
        bounds.max = {0.0f, 0.0f, 0.0f};
    }

    return bounds;
}

struct CollisionShapeOperations {
    CollisionManager::ShapeType type;
    OBB (*toObb)(const CollisionManager::Shape&);
    AABB (*bounds)(const CollisionManager::Shape&);
};

constexpr std::array<CollisionShapeOperations, 2u> kCollisionShapeOperations{{
    {.type = CollisionManager::ShapeType::AABB, .toObb = ShapeAabbToObb, .bounds = ShapeAabbBounds},
    {.type = CollisionManager::ShapeType::OBB, .toObb = ShapeObbToObb, .bounds = ShapeObbBounds},
}};

const CollisionShapeOperations& ShapeOperationsFor(CollisionManager::ShapeType type) {
    const auto found = std::ranges::find_if(
        kCollisionShapeOperations,
        [type](const CollisionShapeOperations& operations) { return operations.type == type; });
    return found != kCollisionShapeOperations.end() ? *found : kCollisionShapeOperations.back();
}

OBB ToOBB(const CollisionManager::Shape& shape) {
    return ShapeOperationsFor(shape.type).toObb(shape);
}

} // namespace

CollisionManager::Shape CollisionManager::Shape::FromOBB(const OBB& box) {
    Shape shape{};
    shape.type = ShapeType::OBB;
    shape.obb = box;
    return shape;
}

CollisionManager::Shape CollisionManager::Shape::FromAABB(const AABB& box) {
    Shape shape{};
    shape.type = ShapeType::AABB;
    shape.aabb = NormalizeAABB(box);
    return shape;
}

void CollisionManager::Clear() {
    bodies_.clear();
    nextBodyId_ = 1;
}

CollisionManager::BodyId CollisionManager::AddBody(const CollisionManager::BodyDesc& desc) {
    Body body = CreateBody(desc);
    body.id = AllocateBodyId();
    if (body.id == kInvalidBodyId) {
        return kInvalidBodyId;
    }
    try {
        bodies_.push_back(body);
    } catch (const std::exception&) {
        return kInvalidBodyId;
    }
    return body.id;
}

bool CollisionManager::RemoveBody(BodyId id) {
    const auto removed =
        std::ranges::remove_if(bodies_, [id](const Body& body) { return body.id == id; });
    const auto it = removed.begin();
    if (it == bodies_.end()) {
        return false;
    }

    bodies_.erase(it, bodies_.end());
    return true;
}

bool CollisionManager::UpdateBody(BodyId id, const BodyDesc& desc) {
    Body* body = FindBody(id);
    if (body == nullptr) {
        return false;
    }

    const BodyId preservedId = body->id;
    *body = CreateBody(desc);
    body->id = preservedId;
    return true;
}

bool CollisionManager::UpdateShape(BodyId id, const Shape& shape) {
    Body* body = FindBody(id);
    if (body == nullptr) {
        return false;
    }

    body->desc.shape = shape;
    body->bounds = ComputeBounds(shape);
    return true;
}

bool CollisionManager::UpdateFilter(BodyId id, const Filter& filter) {
    Body* body = FindBody(id);
    if (body == nullptr) {
        return false;
    }

    body->desc.filter = filter;
    return true;
}

bool CollisionManager::SetActive(BodyId id, bool isActive) {
    Body* body = FindBody(id);
    if (body == nullptr) {
        return false;
    }

    body->desc.isActive = isActive;
    return true;
}

const CollisionManager::Body* CollisionManager::GetBody(BodyId id) const {
    return FindBody(id);
}

bool CollisionManager::Test(BodyId a, BodyId b, Hit* outHit) const {
    const Body* bodyA = FindBody(a);
    const Body* bodyB = FindBody(b);
    if (bodyA == nullptr || bodyB == nullptr || !CanCollide(*bodyA, *bodyB)) {
        return false;
    }

    if (!CollisionUtil::CheckAABB(bodyA->bounds, bodyB->bounds)) {
        return false;
    }

    CollisionUtil::CollisionResult result = TestShapes(bodyA->desc.shape, bodyB->desc.shape);
    if (!result.hit) {
        return false;
    }

    if (outHit != nullptr) {
        outHit->a = bodyA->id;
        outHit->b = bodyB->id;
        outHit->result = result;
    }

    return true;
}

bool CollisionManager::QueryFirst(BodyId body, Hit& outHit) const {
    for (const Body& other : bodies_) {
        if (other.id == body) {
            continue;
        }
        if (Test(body, other.id, &outHit)) {
            return true;
        }
    }

    return false;
}

std::vector<CollisionManager::Hit> CollisionManager::Query(BodyId body) const {
    std::vector<Hit> hits;
    for (const Body& other : bodies_) {
        if (other.id == body) {
            continue;
        }

        Hit hit{};
        if (Test(body, other.id, &hit)) {
            try {
                hits.push_back(hit);
            } catch (const std::exception&) {
                return hits;
            }
        }
    }
    return hits;
}

std::vector<CollisionManager::Hit> CollisionManager::FindPairs() const {
    std::vector<Hit> hits;
    for (auto first = bodies_.cbegin(); first != bodies_.cend(); ++first) {
        for (auto second = std::next(first); second != bodies_.cend(); ++second) {
            Hit hit{};
            if (Test(first->id, second->id, &hit)) {
                try {
                    hits.push_back(hit);
                } catch (const std::exception&) {
                    return hits;
                }
            }
        }
    }
    return hits;
}

CollisionManager::Body* CollisionManager::FindBody(BodyId id) {
    auto it = std::ranges::find_if(bodies_, [id](const Body& body) { return body.id == id; });
    return it != bodies_.end() ? &*it : nullptr;
}

const CollisionManager::Body* CollisionManager::FindBody(BodyId id) const {
    auto it = std::ranges::find_if(bodies_, [id](const Body& body) { return body.id == id; });
    return it != bodies_.end() ? &*it : nullptr;
}

bool CollisionManager::CanCollide(const Body& a, const Body& b) {
    if (!a.desc.isActive || !b.desc.isActive || a.id == b.id) {
        return false;
    }

    const bool aAcceptsB = (a.desc.filter.mask & b.desc.filter.layer) != kLayerNone;
    const bool bAcceptsA = (b.desc.filter.mask & a.desc.filter.layer) != kLayerNone;
    return aAcceptsB && bAcceptsA;
}

AABB CollisionManager::ComputeBounds(const Shape& shape) {
    return ShapeOperationsFor(shape.type).bounds(shape);
}

CollisionUtil::CollisionResult CollisionManager::TestShapes(const Shape& a, const Shape& b) {
    return CollisionUtil::TestOBB(ToOBB(a), ToOBB(b));
}

CollisionManager::Body CollisionManager::CreateBody(const CollisionManager::BodyDesc& desc) {
    Body body{};
    body.desc = desc;
    body.bounds = ComputeBounds(desc.shape);
    return body;
}

CollisionManager::BodyId CollisionManager::AllocateBodyId() {
    if (bodies_.size() >= static_cast<size_t>((std::numeric_limits<BodyId>::max)()) - 1u) {
        return kInvalidBodyId;
    }

    for (;;) {
        if (nextBodyId_ == kInvalidBodyId) {
            nextBodyId_ = 1;
        }
        const BodyId candidate = nextBodyId_++;
        if (FindBody(candidate) == nullptr) {
            return candidate;
        }
    }
}
