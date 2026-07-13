#pragma once
#include <DirectXMath.h>
#include <cstdint>

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

    void SetPosition(const DirectX::XMFLOAT3& position);
    /// <summary>
    /// Rotationを設定する
    /// </summary>
    void SetRotation(const DirectX::XMFLOAT3& rotation);
    void SetAspect(float aspect);
    void SetPerspectiveFovDeg(float fovDeg);
    /// <summary>
    /// PerspectiveFovRadを設定する
    /// </summary>
    void SetPerspectiveFovRad(float fovRad);
    void SetOrthographicHeight(float height);
    void SetClipRange(float nearZ, float farZ);

    [[nodiscard]] const DirectX::XMMATRIX& GetView() const {
        return view_;
    }
    [[nodiscard]] const DirectX::XMMATRIX& GetProj() const {
        return proj_;
    }
    [[nodiscard]] const DirectX::XMMATRIX& GetViewProjection() const {
        return viewProjection_;
    }

    [[nodiscard]] const DirectX::XMFLOAT3& GetPosition() const {
        return position_;
    }
    [[nodiscard]] const DirectX::XMFLOAT3& GetRotation() const {
        return rotation_;
    }
    [[nodiscard]] float GetAspect() const {
        return aspect_;
    }
    [[nodiscard]] float GetFovY() const {
        return fovY_;
    }
    [[nodiscard]] float GetOrthographicHeight() const {
        return orthographicHeight_;
    }
    [[nodiscard]] float GetNearZ() const {
        return nearZ_;
    }
    [[nodiscard]] float GetFarZ() const {
        return farZ_;
    }

private:
    enum class ProjectionMode : uint8_t {
        Perspective,
        Orthographic,
    };

    /// <summary>
    /// SanitizeProjectionを実行する
    /// </summary>
    void SanitizeProjection();

    DirectX::XMFLOAT3 position_{0.0f, 0.0f, -5.0f};
    DirectX::XMFLOAT3 rotation_{0.0f, 0.0f, 0.0f};

    ProjectionMode projectionMode_ = ProjectionMode::Perspective;
    float fovY_ = DirectX::XM_PIDIV4;
    float orthographicHeight_ = 10.0f;
    float aspect_ = 16.0f / 9.0f;
    float nearZ_ = 0.1f;
    float farZ_ = 1000.0f;

    DirectX::XMMATRIX view_ = DirectX::XMMatrixIdentity();
    DirectX::XMMATRIX proj_ = DirectX::XMMatrixIdentity();
    DirectX::XMMATRIX viewProjection_ = DirectX::XMMatrixIdentity();
};
