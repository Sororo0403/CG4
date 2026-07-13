#include "camera/CameraManager.h"

#include <exception>
#include <limits>
#include <new>

namespace {
std::string MakeGeneratedCameraName(
    const std::unordered_map<std::string, std::unique_ptr<Camera>>& cameras) {
    for (size_t suffix = cameras.size(); suffix < (std::numeric_limits<size_t>::max)(); ++suffix) {
        std::string candidate = "__camera" + std::to_string(suffix);
        if (!cameras.contains(candidate)) {
            return candidate;
        }
    }
    return "__camera";
}

Camera& FallbackCamera(float aspect) {
    static Camera fallback;
    fallback.Initialize(aspect);
    return fallback;
}
} // namespace

Camera& CameraManager::CreateCamera(const std::string& name, float aspect) {
    std::string cameraName;
    std::unique_ptr<Camera> camera;
    try {
        cameraName = name.empty() ? MakeGeneratedCameraName(cameras_) : name;
        camera = std::make_unique<Camera>();
    } catch (const std::exception&) {
        return FallbackCamera(aspect);
    }
    camera->Initialize(aspect);
    if (!RegisterCamera(cameraName, std::move(camera))) {
        return FallbackCamera(aspect);
    }

    Camera* registeredCamera = FindCamera(cameraName);
    return registeredCamera != nullptr ? *registeredCamera : FallbackCamera(aspect);
}

bool CameraManager::RegisterCamera(const std::string& name, std::unique_ptr<Camera> camera) {
    if (name.empty() || !camera) {
        return false;
    }

    const bool shouldActivate = cameras_.empty() || activeCameraName_ == name;
    try {
        cameras_[name] = std::move(camera);
        if (shouldActivate) {
            activeCameraName_ = name;
        }
    } catch (const std::exception&) {
        return false;
    }
    return true;
}

bool CameraManager::SetActiveCamera(const std::string& name) {
    if (!cameras_.contains(name)) {
        return false;
    }

    try {
        activeCameraName_ = name;
    } catch (const std::exception&) {
        return false;
    }
    return true;
}

Camera* CameraManager::GetActiveCamera() {
    return FindCamera(activeCameraName_);
}

const Camera* CameraManager::GetActiveCamera() const {
    return FindCamera(activeCameraName_);
}

Camera* CameraManager::FindCamera(const std::string& name) {
    auto it = cameras_.find(name);
    return it == cameras_.end() ? nullptr : it->second.get();
}

const Camera* CameraManager::FindCamera(const std::string& name) const {
    auto it = cameras_.find(name);
    return it == cameras_.end() ? nullptr : it->second.get();
}

void CameraManager::RemoveCamera(const std::string& name) {
    cameras_.erase(name);
    if (activeCameraName_ != name) {
        return;
    }

    activeCameraName_.clear();
    if (!cameras_.empty()) {
        try {
            activeCameraName_ = cameras_.begin()->first;
        } catch (const std::exception&) {
            activeCameraName_.clear();
        }
    }
}

void CameraManager::Clear() {
    cameras_.clear();
    activeCameraName_.clear();
}
