#include "camera/CameraManager.h"

CameraManager &CameraManager::GetInstance() {
    static CameraManager instance;
    return instance;
}

Camera &CameraManager::CreateCamera(const std::string &name, float aspect) {
    auto camera = std::make_unique<Camera>();
    camera->Initialize(aspect);
    Camera &result = *camera;
    RegisterCamera(name, std::move(camera));
    return result;
}

bool CameraManager::RegisterCamera(const std::string &name,
                                   std::unique_ptr<Camera> camera) {
    if (name.empty() || !camera) {
        return false;
    }

    const bool shouldActivate = cameras_.empty() || activeCameraName_ == name;
    cameras_[name] = std::move(camera);
    if (shouldActivate) {
        activeCameraName_ = name;
    }
    return true;
}

bool CameraManager::SetActiveCamera(const std::string &name) {
    if (cameras_.find(name) == cameras_.end()) {
        return false;
    }

    activeCameraName_ = name;
    return true;
}

Camera *CameraManager::GetActiveCamera() {
    return FindCamera(activeCameraName_);
}

const Camera *CameraManager::GetActiveCamera() const {
    return FindCamera(activeCameraName_);
}

Camera *CameraManager::FindCamera(const std::string &name) {
    auto it = cameras_.find(name);
    return it == cameras_.end() ? nullptr : it->second.get();
}

const Camera *CameraManager::FindCamera(const std::string &name) const {
    auto it = cameras_.find(name);
    return it == cameras_.end() ? nullptr : it->second.get();
}

void CameraManager::RemoveCamera(const std::string &name) {
    cameras_.erase(name);
    if (activeCameraName_ != name) {
        return;
    }

    activeCameraName_.clear();
    if (!cameras_.empty()) {
        activeCameraName_ = cameras_.begin()->first;
    }
}

void CameraManager::Clear() {
    cameras_.clear();
    activeCameraName_.clear();
}
