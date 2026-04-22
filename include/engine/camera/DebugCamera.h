#pragma once
#include "Camera.h"
#include "Input.h"
#include <DirectXMath.h>

/// <summary>
/// 入力操作で自由移動できるデバッグ用カメラ
/// </summary>
class DebugCamera : public Camera {
  public:
    /// <summary>
    /// 初期化処理
    /// </summary>
    /// <param name="aspect">アスペクト比</param>
    void Initialize(float aspect);

    /// <summary>
    /// 更新処理
    /// </summary>
    /// <param name="input">Inputインスタンス</param>
    /// <param name="deltaTime">前フレームからの経過時間(秒)</param>
    void Update(const Input &input, float deltaTime);

    /// <summary>
    /// 移動速度を設定する
    /// </summary>
    /// <param name="speed">設定する移動速度</param>
    void SetMoveSpeed(float speed) { moveSpeed_ = speed; }

  private:
    float moveSpeed_ = 5.0f;
    float mouseSensitivity_ = 0.002f;
    float zoomSpeed_ = 0.01f;
};
