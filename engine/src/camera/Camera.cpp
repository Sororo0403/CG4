#include "Camera.h"

using namespace DirectX;

void Camera::Initialize(float aspect) { aspect_ = aspect; }

void Camera::UpdateMatrices() {
    if (mode_ == CameraMode::LookAt) {
        XMVECTOR eye = XMLoadFloat3(&position_);
        XMVECTOR targetV = XMLoadFloat3(&target_);
        XMVECTOR up = XMVectorSet(0, 1, 0, 0);

        view_ = XMMatrixLookAtLH(eye, targetV, up);
    } else {
        XMMATRIX world =
            XMMatrixRotationRollPitchYaw(rotation_.x, rotation_.y,
                                         rotation_.z) *
            XMMatrixTranslation(position_.x, position_.y, position_.z);

        view_ = XMMatrixInverse(nullptr, world);
    }

    proj_ = XMMatrixPerspectiveFovLH(fovY_, aspect_, nearZ_, farZ_);
}

void Camera::LookAt(const XMFLOAT3 &target) { target_ = target; }