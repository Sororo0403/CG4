#pragma once
#include <DirectXMath.h>
#include <array>
#include <cstdint>

class Camera;

class Frustum {
  public:
    void Build(const DirectX::XMMATRIX &viewProjection);
    void Build(const Camera &camera);

    bool IntersectsAABB(const DirectX::XMFLOAT3 &min,
                        const DirectX::XMFLOAT3 &max) const;

  private:
    std::array<DirectX::XMFLOAT4, 6> planes_{};
};

struct LODRange {
    float maxDistance = 0.0f;
    uint32_t level = 0;
};

class LODSelector {
  public:
    static uint32_t Select(float distance, const LODRange *ranges,
                           uint32_t rangeCount);
};
