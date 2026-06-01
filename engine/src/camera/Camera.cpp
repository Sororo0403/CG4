#include "camera/Camera.h"
#include <algorithm>
#include <cmath>

using namespace DirectX;

namespace {

constexpr float kMinAspect = 0.0001f;
constexpr float kMinFovY = XMConvertToRadians(1.0f);
constexpr float kMaxFovY = XMConvertToRadians(179.0f);
constexpr float kMinOrthoHeight = 0.001f;
constexpr float kMinNearZ = 0.001f;
constexpr float kMinDepthRange = 0.001f;
constexpr float kDefaultAspect = 16.0f / 9.0f;
constexpr float kDefaultFovY = XM_PIDIV4;
constexpr float kDefaultOrthoHeight = 10.0f;
constexpr float kDefaultNearZ = 0.1f;
constexpr float kDefaultFarZ = 1000.0f;
constexpr float kMinDeterminant = 0.000001f;

float FiniteOr(float value, float fallback) {
    return std::isfinite(value) ? value : fallback;
}

} // namespace

Camera::Camera() { UpdateMatrices(); }

void Camera::Initialize(float aspect) {
    aspect_ = aspect;
    UpdateMatrices();
}

void Camera::UpdateMatrices() {
    SanitizeProjection();

    const XMMATRIX world =
        XMMatrixRotationRollPitchYaw(rotation_.x, rotation_.y, rotation_.z) *
        XMMatrixTranslation(position_.x, position_.y, position_.z);
    const XMVECTOR determinant = XMMatrixDeterminant(world);
    const float determinantValue = XMVectorGetX(determinant);
    view_ = std::isfinite(determinantValue) &&
                    std::abs(determinantValue) > kMinDeterminant
                ? XMMatrixInverse(nullptr, world)
                : XMMatrixIdentity();

    if (projectionMode_ == ProjectionMode::Orthographic) {
        proj_ = XMMatrixOrthographicLH(orthographicHeight_ * aspect_,
                                       orthographicHeight_, nearZ_, farZ_);
    } else {
        proj_ = XMMatrixPerspectiveFovLH(fovY_, aspect_, nearZ_, farZ_);
    }
    viewProjection_ = view_ * proj_;
}

void Camera::SetPosition(const XMFLOAT3 &position) {
    position_ = {FiniteOr(position.x, position_.x),
                 FiniteOr(position.y, position_.y),
                 FiniteOr(position.z, position_.z)};
    UpdateMatrices();
}

void Camera::SetRotation(const XMFLOAT3 &rotation) {
    rotation_ = {FiniteOr(rotation.x, rotation_.x),
                 FiniteOr(rotation.y, rotation_.y),
                 FiniteOr(rotation.z, rotation_.z)};
    UpdateMatrices();
}

void Camera::SetAspect(float aspect) {
    aspect_ = aspect;
    UpdateMatrices();
}

void Camera::SetPerspectiveFovDeg(float fovDeg) {
    SetPerspectiveFovRad(XMConvertToRadians(fovDeg));
}

void Camera::SetPerspectiveFovRad(float fovRad) {
    projectionMode_ = ProjectionMode::Perspective;
    fovY_ = fovRad;
    UpdateMatrices();
}

void Camera::SetOrthographicHeight(float height) {
    projectionMode_ = ProjectionMode::Orthographic;
    orthographicHeight_ = height;
    UpdateMatrices();
}

void Camera::SetClipRange(float nearZ, float farZ) {
    nearZ_ = nearZ;
    farZ_ = farZ;
    UpdateMatrices();
}

void Camera::SanitizeProjection() {
    aspect_ = (std::max)(FiniteOr(aspect_, kDefaultAspect), kMinAspect);
    fovY_ = FiniteOr(fovY_, kDefaultFovY);
    fovY_ = std::clamp(fovY_, kMinFovY, kMaxFovY);
    orthographicHeight_ =
        (std::max)(FiniteOr(orthographicHeight_, kDefaultOrthoHeight),
                   kMinOrthoHeight);
    nearZ_ = (std::max)(FiniteOr(nearZ_, kDefaultNearZ), kMinNearZ);
    farZ_ = (std::max)(FiniteOr(farZ_, kDefaultFarZ),
                       nearZ_ + kMinDepthRange);
}
