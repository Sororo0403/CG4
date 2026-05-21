#include "graphics/TransparentRenderQueue.h"

#include "camera/Camera.h"
#include <algorithm>
#include <cmath>
#include <utility>

using namespace DirectX;

void TransparentRenderQueue::Clear() {
    items_.clear();
    nextSequence_ = 0;
}

void TransparentRenderQueue::Submit(float distanceSquared, DrawCallback draw) {
    if (!draw) {
        return;
    }

    if (!std::isfinite(distanceSquared)) {
        distanceSquared = 0.0f;
    }

    items_.push_back({distanceSquared, nextSequence_++, std::move(draw)});
}

void TransparentRenderQueue::Submit(const XMFLOAT3 &worldPosition,
                                    const Camera &camera, DrawCallback draw) {
    const XMFLOAT3 cameraPosition = camera.GetPosition();
    const float dx = worldPosition.x - cameraPosition.x;
    const float dy = worldPosition.y - cameraPosition.y;
    const float dz = worldPosition.z - cameraPosition.z;
    Submit(dx * dx + dy * dy + dz * dz, std::move(draw));
}

void TransparentRenderQueue::Flush() {
    std::stable_sort(items_.begin(), items_.end(),
                     [](const Item &a, const Item &b) {
                         if (a.distanceSquared == b.distanceSquared) {
                             return a.sequence < b.sequence;
                         }
                         return a.distanceSquared > b.distanceSquared;
                     });

    for (const Item &item : items_) {
        item.draw();
    }

    Clear();
}
