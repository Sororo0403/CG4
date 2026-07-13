#pragma once

#include <DirectXMath.h>
#include <cstdint>
#include <unordered_map>

class Camera;
class DirectXCommon;
class SrvManager;

struct FrameCameraHistory {
    DirectX::XMFLOAT4X4 view{};
    DirectX::XMFLOAT4X4 projection{};
    DirectX::XMFLOAT4X4 viewProjection{};
    DirectX::XMFLOAT4X4 previousViewProjection{};
    DirectX::XMFLOAT2 jitter{};
    DirectX::XMFLOAT2 previousJitter{};
};

struct FrameObjectHistory {
    DirectX::XMFLOAT4X4 currentWorld{};
    DirectX::XMFLOAT4X4 previousWorld{};
    bool hasPrevious = false;
};

struct FrameHistoryStats {
    uint64_t frameIndex = 0;
    uint32_t width = 0;
    uint32_t height = 0;
    uint32_t previousObjectCount = 0;
    uint32_t currentObjectCount = 0;
    bool jitterEnabled = false;
};

class FrameHistory {
public:
    void Initialize(DirectXCommon* dxCommon, SrvManager* srvManager, uint32_t width,
                    uint32_t height);
    void Resize(uint32_t width, uint32_t height);
    void BeginFrame(const Camera& camera);
    void EndFrame();
    void Clear();

    bool IsInitialized() const {
        return initialized_;
    }
    uint64_t GetFrameIndex() const {
        return frameIndex_;
    }
    uint32_t GetWidth() const {
        return width_;
    }
    uint32_t GetHeight() const {
        return height_;
    }
    bool IsJitterEnabled() const {
        return jitterEnabled_;
    }
    float GetJitterScale() const {
        return jitterScale_;
    }

    const FrameCameraHistory& CameraHistory() const {
        return cameraHistory_;
    }
    const FrameHistoryStats& GetStats() const {
        return stats_;
    }

    void SetJitterEnabled(bool enabled);
    void SetJitterScale(float scale);

    DirectX::XMFLOAT4X4 ResolvePreviousWorld(uint32_t objectId,
                                             const DirectX::XMFLOAT4X4& currentWorld) const;
    FrameObjectHistory ResolveObjectHistory(uint32_t objectId,
                                            const DirectX::XMFLOAT4X4& currentWorld) const;
    void StoreCurrentWorld(uint32_t objectId, const DirectX::XMFLOAT4X4& currentWorld);

private:
    static DirectX::XMFLOAT4X4 IdentityMatrix();
    static float Halton(uint64_t index, uint32_t base);
    static DirectX::XMFLOAT2 ComputeJitter(uint64_t frameIndex, uint32_t width, uint32_t height,
                                           float scale);
    void UpdateStats();

    DirectXCommon* dxCommon_ = nullptr;
    SrvManager* srvManager_ = nullptr;
    uint32_t width_ = 0;
    uint32_t height_ = 0;
    uint64_t frameIndex_ = 0;
    bool initialized_ = false;
    bool hasCameraHistory_ = false;
    bool jitterEnabled_ = false;
    float jitterScale_ = 1.0f;
    FrameCameraHistory cameraHistory_{};
    FrameHistoryStats stats_{};
    std::unordered_map<uint32_t, DirectX::XMFLOAT4X4> previousWorld_;
    std::unordered_map<uint32_t, DirectX::XMFLOAT4X4> currentWorld_;
};
