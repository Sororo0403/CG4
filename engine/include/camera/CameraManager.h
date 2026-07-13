#pragma once
#include "camera/Camera.h"

#include <memory>
#include <string>
#include <unordered_map>

/// <summary>
/// シーン内の複数カメラを名前付きで保持し、アクティブカメラを切り替える
/// </summary>
class CameraManager {
public:
    CameraManager() = default;
    CameraManager(const CameraManager&) = delete;
    CameraManager& operator=(const CameraManager&) = delete;

    Camera& CreateCamera(const std::string& name, float aspect);
    bool RegisterCamera(const std::string& name, std::unique_ptr<Camera> camera);
    /// <summary>
    /// ActiveCameraを設定する
    /// </summary>
    bool SetActiveCamera(const std::string& name);

    [[nodiscard]] Camera* GetActiveCamera();
    [[nodiscard]] const Camera* GetActiveCamera() const;
    [[nodiscard]] Camera* FindCamera(const std::string& name);
    [[nodiscard]] const Camera* FindCamera(const std::string& name) const;

    /// <summary>
    /// RemoveCameraを実行する
    /// </summary>
    void RemoveCamera(const std::string& name);
    void Clear();

private:
    std::unordered_map<std::string, std::unique_ptr<Camera>> cameras_;
    std::string activeCameraName_;
};
