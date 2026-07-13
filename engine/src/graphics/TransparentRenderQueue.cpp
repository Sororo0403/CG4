#include "graphics/TransparentRenderQueue.h"

#include "camera/Camera.h"

#include <algorithm>
#include <cmath>
#include <exception>
#include <limits>
#include <new>
#include <utility>

using namespace DirectX;

namespace {

bool IsFinitePosition(const XMFLOAT3& position) {
    return std::isfinite(position.x) && std::isfinite(position.y) && std::isfinite(position.z);
}

float SafeDistanceSquared(const XMFLOAT3& worldPosition, const XMFLOAT3& cameraPosition) {
    if (!IsFinitePosition(worldPosition) || !IsFinitePosition(cameraPosition)) {
        return 0.0f;
    }

    const double dx = static_cast<double>(worldPosition.x) - static_cast<double>(cameraPosition.x);
    const double dy = static_cast<double>(worldPosition.y) - static_cast<double>(cameraPosition.y);
    const double dz = static_cast<double>(worldPosition.z) - static_cast<double>(cameraPosition.z);
    const double distanceSquared = dx * dx + dy * dy + dz * dz;
    constexpr float maxDistanceSquared = (std::numeric_limits<float>::max)();
    if (!std::isfinite(distanceSquared) ||
        distanceSquared > static_cast<double>(maxDistanceSquared)) {
        return maxDistanceSquared;
    }
    return static_cast<float>(distanceSquared);
}

} // namespace

void TransparentRenderQueue::Clear() {
    items_.clear();
    nextSequence_ = 0;
    lastDistanceSquared_ = 0.0f;
    orderDirty_ = false;
}

void TransparentRenderQueue::Submit(float distanceSquared, DrawCallback draw) {
    if (!draw) {
        return;
    }

    if (!std::isfinite(distanceSquared) || distanceSquared < 0.0f) {
        distanceSquared = 0.0f;
    }

    try {
        items_.push_back({distanceSquared, nextSequence_, std::move(draw)});
    } catch (const std::exception&) {
        return;
    }

    if (items_.size() > 1u && distanceSquared > lastDistanceSquared_) {
        orderDirty_ = true;
    }
    lastDistanceSquared_ = distanceSquared;
    ++nextSequence_;
}

void TransparentRenderQueue::Submit(const XMFLOAT3& worldPosition, const Camera& camera,
                                    DrawCallback draw) {
    const XMFLOAT3 cameraPosition = camera.GetPosition();
    Submit(SafeDistanceSquared(worldPosition, cameraPosition), std::move(draw));
}

void TransparentRenderQueue::Flush() {
    if (orderDirty_ && items_.size() > 1u) {
        std::sort(items_.begin(), items_.end(), [](const Item& a, const Item& b) {
            if (a.distanceSquared == b.distanceSquared) {
                return a.sequence < b.sequence;
            }
            return a.distanceSquared > b.distanceSquared;
        });
    }

    for (const Item& item : items_) {
        try {
            item.draw();
        } catch (...) {
        }
    }

    Clear();
}
