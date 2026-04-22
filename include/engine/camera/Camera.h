#pragma once
#include <DirectXMath.h>

/// <summary>
/// カメラの挙動モード
/// </summary>
enum class CameraMode {
    Free,  // Debug用
    LookAt // 通常・三脚
};

/// <summary>
/// ビュー行列と射影行列を管理するカメラ
/// </summary>
class Camera {
  public:
    /// <summary>
    /// 初期化処理
    /// </summary>
    /// <param name="aspect">アスペクト比</param>
    void Initialize(float aspect);

    /// <summary>
    /// 行列を更新
    /// </summary>
    void UpdateMatrices();

    /// <summary>
    /// ターゲットを見るカメラ
    /// </summary>
    /// <param name="target">ターゲットの位置</param>
    void LookAt(const DirectX::XMFLOAT3 &target);

    /// <summary>
    /// カメラ位置を設定する
    /// </summary>
    /// <param name="pos">設定する位置</param>
    void SetPosition(const DirectX::XMFLOAT3 &pos) { position_ = pos; }
    /// <summary>
    /// カメラのオイラー回転を設定する
    /// </summary>
    /// <param name="rot">設定する回転</param>
    void SetRotation(const DirectX::XMFLOAT3 &rot) { rotation_ = rot; }
    /// <summary>
    /// 視野角を度数法で設定する
    /// </summary>
    /// <param name="fovDeg">垂直方向の視野角(度)</param>
    void SetPerspectiveFovDeg(float fovDeg) {
        fovY_ = DirectX::XMConvertToRadians(fovDeg);
    }
    /// <summary>
    /// カメラモードを設定する
    /// </summary>
    /// <param name="mode">設定するモード</param>
    void SetMode(CameraMode mode) { mode_ = mode; }

    /// <summary>
    /// ビュー行列を取得する
    /// </summary>
    /// <returns>現在のビュー行列</returns>
    const DirectX::XMMATRIX &GetView() const { return view_; }
    /// <summary>
    /// 射影行列を取得する
    /// </summary>
    /// <returns>現在の射影行列</returns>
    const DirectX::XMMATRIX &GetProj() const { return proj_; }
    /// <summary>
    /// カメラ位置を取得する
    /// </summary>
    /// <returns>現在の位置</returns>
    const DirectX::XMFLOAT3 &GetPosition() const { return position_; }
    /// <summary>
    /// カメラ回転を取得する
    /// </summary>
    /// <returns>現在の回転</returns>
    const DirectX::XMFLOAT3 &GetRotation() const { return rotation_; }
    /// <summary>
    /// 注視対象を取得する
    /// </summary>
    /// <returns>現在のターゲット位置</returns>
    const DirectX::XMFLOAT3 &GetTarget() const { return target_; }

  private:
    DirectX::XMFLOAT3 position_{0.0f, 0.0f, -5.0f};
    DirectX::XMFLOAT3 rotation_{0.0f, 0.0f, 0.0f};
    DirectX::XMFLOAT3 target_{0.0f, 0.0f, 0.0f};

    float fovY_ = DirectX::XM_PIDIV4;
    float aspect_ = 1.0f;
    float nearZ_ = 0.1f;
    float farZ_ = 100.0f;

    DirectX::XMMATRIX view_{};
    DirectX::XMMATRIX proj_{};

    CameraMode mode_ = CameraMode::LookAt;
};
