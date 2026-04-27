#include "DebugCamera.h"
#include <algorithm>

using namespace DirectX;

void DebugCamera::Initialize(float aspect) { Camera::Initialize(aspect); }

void DebugCamera::Update(const Input &input, float deltaTime) {
    XMFLOAT3 pos = GetPosition();
    XMFLOAT3 rot = GetRotation();

    float move = moveSpeed_ * deltaTime;

    // 回転行列
    XMMATRIX rotMat = XMMatrixRotationRollPitchYaw(rot.x, rot.y, rot.z);

    // カメラ方向
    XMVECTOR forward =
        XMVector3TransformNormal(XMVectorSet(0, 0, 1, 0), rotMat);

    XMVECTOR right = XMVector3TransformNormal(XMVectorSet(1, 0, 0, 0), rotMat);

    XMVECTOR up = XMVectorSet(0, 1, 0, 0);

    XMVECTOR position = XMLoadFloat3(&pos);

    // 移動
    if (input.IsKeyPress(DIK_W))
        position += forward * move;

    if (input.IsKeyPress(DIK_S))
        position -= forward * move;

    if (input.IsKeyPress(DIK_A))
        position -= right * move;

    if (input.IsKeyPress(DIK_D))
        position += right * move;

    if (input.IsKeyPress(DIK_Q))
        position -= up * move;

    if (input.IsKeyPress(DIK_E))
        position += up * move;

    XMStoreFloat3(&pos, position);

    // マウス回転
    if (input.IsMousePress(0)) {
        rot.y += input.GetMouseDX() * mouseSensitivity_;
        rot.x += input.GetMouseDY() * mouseSensitivity_;

        rot.x = std::clamp(rot.x, -XM_PIDIV2 + 0.01f, XM_PIDIV2 - 0.01f);
    }

    // ホイールズーム
    long wheel = input.GetMouseWheel();
    if (wheel != 0) {
        position = XMLoadFloat3(&pos);
        position += forward * (wheel * zoomSpeed_);
        XMStoreFloat3(&pos, position);
    }

    SetPosition(pos);
    SetRotation(rot);
}