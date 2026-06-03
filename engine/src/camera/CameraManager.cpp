#include "camera/CameraManager.h"
#include <limits>

namespace {
CameraManager *gActiveCameraManager = nullptr;

std::string MakeGeneratedCameraName(
    const std::unordered_map<std::string, std::unique_ptr<Camera>> &cameras) {
    for (size_t suffix = cameras.size();
         suffix < (std::numeric_limits<size_t>::max)(); ++suffix) {
        std::string candidate = "__camera" + std::to_string(suffix);
        if (cameras.find(candidate) == cameras.end()) {
            return candidate;
        }
    }
    return "__camera";
}
}

CameraManager &CameraManager::GetInstance() {
    static CameraManager instance;
    return gActiveCameraManager != nullptr ? *gActiveCameraManager : instance;
}

void CameraManager::SetActiveInstance(CameraManager *instance) {
    gActiveCameraManager = instance;
}

Camera &CameraManager::CreateCamera(const std::string &name, float aspect) {
    const std::string cameraName =
        name.empty() ? MakeGeneratedCameraName(cameras_) : name;
    auto camera = std::make_unique<Camera>();
    camera->Initialize(aspect);
    Camera &result = *camera;
    RegisterCamera(cameraName, std::move(camera));
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
