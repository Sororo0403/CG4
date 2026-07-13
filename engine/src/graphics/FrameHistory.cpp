#include "graphics/FrameHistory.h"

#include "camera/Camera.h"

#include <algorithm>
#include <cmath>
#include <exception>
#include <new>

void FrameHistory::Initialize(DirectXCommon* dxCommon, SrvManager* srvManager, uint32_t width,
                              uint32_t height) {
    dxCommon_ = dxCommon;
    srvManager_ = srvManager;
    width_ = width;
    height_ = height;
    initialized_ = dxCommon_ != nullptr && srvManager_ != nullptr;
    cameraHistory_.view = IdentityMatrix();
    cameraHistory_.projection = IdentityMatrix();
    cameraHistory_.viewProjection = IdentityMatrix();
    cameraHistory_.previousViewProjection = IdentityMatrix();
    UpdateStats();
}

void FrameHistory::Resize(uint32_t width, uint32_t height) {
    width_ = width;
    height_ = height;
    UpdateStats();
}

void FrameHistory::BeginFrame(const Camera& camera) {
    DirectX::XMFLOAT4X4 currentView{};
    DirectX::XMFLOAT4X4 currentProjection{};
    DirectX::XMFLOAT4X4 currentViewProjection{};
    DirectX::XMStoreFloat4x4(&currentView, camera.GetView());
    DirectX::XMStoreFloat4x4(&currentProjection, camera.GetProj());
    DirectX::XMStoreFloat4x4(&currentViewProjection, camera.GetViewProjection());

    const DirectX::XMFLOAT4X4 previousViewProjection =
        hasCameraHistory_ ? cameraHistory_.viewProjection : currentViewProjection;
    const DirectX::XMFLOAT2 previousJitter =
        hasCameraHistory_ ? cameraHistory_.jitter : DirectX::XMFLOAT2{0.0f, 0.0f};

    cameraHistory_.view = currentView;
    cameraHistory_.projection = currentProjection;
    cameraHistory_.viewProjection = currentViewProjection;
    cameraHistory_.previousViewProjection = previousViewProjection;
    cameraHistory_.previousJitter = previousJitter;
    cameraHistory_.jitter = jitterEnabled_
                                ? ComputeJitter(frameIndex_, width_, height_, jitterScale_)
                                : DirectX::XMFLOAT2{0.0f, 0.0f};
    hasCameraHistory_ = true;
    UpdateStats();
}

void FrameHistory::EndFrame() {
    try {
        previousWorld_.swap(currentWorld_);
        currentWorld_.clear();
    } catch (const std::exception&) {
        previousWorld_.clear();
        currentWorld_.clear();
    }
    ++frameIndex_;
    UpdateStats();
}

void FrameHistory::Clear() {
    try {
        previousWorld_.clear();
        currentWorld_.clear();
    } catch (const std::exception&) {
    }
    frameIndex_ = 0;
    hasCameraHistory_ = false;
    cameraHistory_ = {};
    cameraHistory_.view = IdentityMatrix();
    cameraHistory_.projection = IdentityMatrix();
    cameraHistory_.viewProjection = IdentityMatrix();
    cameraHistory_.previousViewProjection = IdentityMatrix();
    UpdateStats();
}

void FrameHistory::SetJitterEnabled(bool enabled) {
    jitterEnabled_ = enabled;
    if (!jitterEnabled_) {
        cameraHistory_.jitter = {0.0f, 0.0f};
        cameraHistory_.previousJitter = {0.0f, 0.0f};
    }
    UpdateStats();
}

void FrameHistory::SetJitterScale(float scale) {
    jitterScale_ = std::clamp(std::isfinite(scale) ? scale : 1.0f, 0.0f, 2.0f);
    UpdateStats();
}

DirectX::XMFLOAT4X4 FrameHistory::ResolvePreviousWorld(
    uint32_t objectId, const DirectX::XMFLOAT4X4& currentWorld) const {
    const auto it = previousWorld_.find(objectId);
    if (it == previousWorld_.end()) {
        return currentWorld;
    }
    return it->second;
}

FrameObjectHistory FrameHistory::ResolveObjectHistory(
    uint32_t objectId, const DirectX::XMFLOAT4X4& currentWorld) const {
    FrameObjectHistory history{};
    history.currentWorld = currentWorld;
    const auto it = previousWorld_.find(objectId);
    if (it == previousWorld_.end()) {
        history.previousWorld = currentWorld;
        history.hasPrevious = false;
        return history;
    }
    history.previousWorld = it->second;
    history.hasPrevious = true;
    return history;
}

void FrameHistory::StoreCurrentWorld(uint32_t objectId, const DirectX::XMFLOAT4X4& currentWorld) {
    if (objectId == 0xffffffffu) {
        return;
    }
    try {
        currentWorld_[objectId] = currentWorld;
    } catch (const std::exception&) {
        return;
    }
    UpdateStats();
}

DirectX::XMFLOAT4X4 FrameHistory::IdentityMatrix() {
    DirectX::XMFLOAT4X4 value{};
    DirectX::XMStoreFloat4x4(&value, DirectX::XMMatrixIdentity());
    return value;
}

float FrameHistory::Halton(uint64_t index, uint32_t base) {
    float result = 0.0f;
    float fraction = 1.0f / static_cast<float>(base);
    while (index > 0u) {
        result += fraction * static_cast<float>(index % base);
        index /= base;
        fraction /= static_cast<float>(base);
    }
    return result;
}

DirectX::XMFLOAT2 FrameHistory::ComputeJitter(uint64_t frameIndex, uint32_t width, uint32_t height,
                                              float scale) {
    if (width == 0u || height == 0u || scale <= 0.0f) {
        return {0.0f, 0.0f};
    }
    const uint64_t sampleIndex = frameIndex % 8u + 1u;
    const float x = (Halton(sampleIndex, 2u) - 0.5f) * scale;
    const float y = (Halton(sampleIndex, 3u) - 0.5f) * scale;
    return {x / static_cast<float>(width), y / static_cast<float>(height)};
}

void FrameHistory::UpdateStats() {
    stats_.frameIndex = frameIndex_;
    stats_.width = width_;
    stats_.height = height_;
    stats_.previousObjectCount = static_cast<uint32_t>(previousWorld_.size());
    stats_.currentObjectCount = static_cast<uint32_t>(currentWorld_.size());
    stats_.jitterEnabled = jitterEnabled_;
}
