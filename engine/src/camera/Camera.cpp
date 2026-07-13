#include "camera/Camera.h"

#include "core/Numeric.h"

#include <algorithm>
#include <cmath>

using namespace DirectX;

namespace {
using Numeric::FiniteOr;

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
constexpr float kTwoPi = XM_2PI;

float NormalizeAngle(float value, float fallback) {
    value = FiniteOr(value, fallback);
    const float normalized = std::remainder(value, kTwoPi);
    return FiniteOr(normalized, fallback);
}

bool IsFiniteVector(FXMVECTOR value) {
    return std::isfinite(XMVectorGetX(value)) && std::isfinite(XMVectorGetY(value)) &&
           std::isfinite(XMVectorGetZ(value)) && std::isfinite(XMVectorGetW(value));
}

bool IsFiniteMatrix(const XMMATRIX& matrix) {
    return IsFiniteVector(matrix.r[0]) && IsFiniteVector(matrix.r[1]) &&
           IsFiniteVector(matrix.r[2]) && IsFiniteVector(matrix.r[3]);
}

} // namespace

Camera::Camera() {
    UpdateMatrices();
}

void Camera::Initialize(float aspect) {
    aspect_ = aspect;
    UpdateMatrices();
}

void Camera::UpdateMatrices() {
    SanitizeProjection();
    position_ = {FiniteOr(position_.x, 0.0f), FiniteOr(position_.y, 0.0f),
                 FiniteOr(position_.z, -5.0f)};
    rotation_ = {NormalizeAngle(rotation_.x, 0.0f), NormalizeAngle(rotation_.y, 0.0f),
                 NormalizeAngle(rotation_.z, 0.0f)};

    const XMMATRIX world = XMMatrixRotationRollPitchYaw(rotation_.x, rotation_.y, rotation_.z) *
                           XMMatrixTranslation(position_.x, position_.y, position_.z);
    const XMVECTOR determinant = XMMatrixDeterminant(world);
    const float determinantValue = XMVectorGetX(determinant);
    if (IsFiniteMatrix(world) && std::isfinite(determinantValue) &&
        std::abs(determinantValue) > kMinDeterminant) {
        const XMMATRIX inverse = XMMatrixInverse(nullptr, world);
        view_ = IsFiniteMatrix(inverse) ? inverse : XMMatrixIdentity();
    } else {
        view_ = XMMatrixIdentity();
    }

    if (projectionMode_ == ProjectionMode::Orthographic) {
        proj_ = XMMatrixOrthographicLH(orthographicHeight_ * aspect_, orthographicHeight_, nearZ_,
                                       farZ_);
    } else {
        proj_ = XMMatrixPerspectiveFovLH(fovY_, aspect_, nearZ_, farZ_);
    }
    if (!IsFiniteMatrix(proj_)) {
        proj_ = XMMatrixIdentity();
    }
    viewProjection_ = view_ * proj_;
    if (!IsFiniteMatrix(viewProjection_)) {
        viewProjection_ = XMMatrixIdentity();
    }
}

void Camera::SetPosition(const XMFLOAT3& position) {
    position_ = {FiniteOr(position.x, position_.x), FiniteOr(position.y, position_.y),
                 FiniteOr(position.z, position_.z)};
    UpdateMatrices();
}

void Camera::SetRotation(const XMFLOAT3& rotation) {
    rotation_ = {NormalizeAngle(rotation.x, rotation_.x), NormalizeAngle(rotation.y, rotation_.y),
                 NormalizeAngle(rotation.z, rotation_.z)};
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
        (std::max)(FiniteOr(orthographicHeight_, kDefaultOrthoHeight), kMinOrthoHeight);
    nearZ_ = (std::max)(FiniteOr(nearZ_, kDefaultNearZ), kMinNearZ);
    farZ_ = (std::max)(FiniteOr(farZ_, kDefaultFarZ), nearZ_ + kMinDepthRange);
}
