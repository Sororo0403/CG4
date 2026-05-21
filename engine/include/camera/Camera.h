#pragma once
#include <DirectXMath.h>

/// <summary>
/// ビュー行列と射影行列を管理する基本カメラ
/// </summary>
class Camera {
  public:
    Camera();

    /// <summary>
    /// 投影設定を初期化し、行列を再計算する
    /// </summary>
    void Initialize(float aspect);

    /// <summary>
    /// 現在の設定からビュー行列と射影行列を再計算する
    /// </summary>
    void UpdateMatrices();

    void SetPosition(const DirectX::XMFLOAT3 &position);
    void SetRotation(const DirectX::XMFLOAT3 &rotation);
    void SetAspect(float aspect);
    void SetPerspectiveFovDeg(float fovDeg);
    void SetPerspectiveFovRad(float fovRad);
    void SetClipRange(float nearZ, float farZ);

    const DirectX::XMMATRIX &GetView() const { return view_; }
    const DirectX::XMMATRIX &GetProj() const { return proj_; }
    const DirectX::XMMATRIX &GetViewProjection() const {
        return viewProjection_;
    }

    const DirectX::XMFLOAT3 &GetPosition() const { return position_; }
    const DirectX::XMFLOAT3 &GetRotation() const { return rotation_; }
    float GetAspect() const { return aspect_; }
    float GetFovY() const { return fovY_; }
    float GetNearZ() const { return nearZ_; }
    float GetFarZ() const { return farZ_; }

  private:
    void SanitizeProjection();

    DirectX::XMFLOAT3 position_{0.0f, 0.0f, -5.0f};
    DirectX::XMFLOAT3 rotation_{0.0f, 0.0f, 0.0f};

    float fovY_ = DirectX::XM_PIDIV4;
    float aspect_ = 16.0f / 9.0f;
    float nearZ_ = 0.1f;
    float farZ_ = 1000.0f;

    DirectX::XMMATRIX view_ = DirectX::XMMatrixIdentity();
    DirectX::XMMATRIX proj_ = DirectX::XMMatrixIdentity();
    DirectX::XMMATRIX viewProjection_ = DirectX::XMMatrixIdentity();
};
