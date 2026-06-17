#include "graphics/LightingScene.h"

#include <algorithm>

void LightingScene::BeginFrame() {
    legacyBase_ = SceneLighting{};
    pointLights_.clear();
    spotLights_.clear();
    reflectionProbes_.clear();
    stats_ = {};
}

void LightingScene::SetLegacyBase(const SceneLighting &lighting) {
    legacyBase_ = lighting;
    stats_.hasLegacyBase = true;
}

void LightingScene::SetSun(const DirectionalLightDesc &light) {
    sun_ = light;
    stats_.hasSun = true;
}

void LightingScene::AddPointLight(const LocalLightDesc &light) {
    pointLights_.push_back(light);
    stats_.pointLightCount = static_cast<uint32_t>(pointLights_.size());
}

void LightingScene::AddSpotLight(const SpotLightDesc &light) {
    spotLights_.push_back(light);
    stats_.spotLightCount = static_cast<uint32_t>(spotLights_.size());
}

void LightingScene::AddReflectionProbe(const ReflectionProbeDesc &probe) {
    reflectionProbes_.push_back(probe);
    stats_.reflectionProbeCount =
        static_cast<uint32_t>(reflectionProbes_.size());
}

SceneLighting LightingScene::BuildLegacySceneLighting() const {
    SceneLighting lighting = legacyBase_;
    if (stats_.hasSun) {
        lighting.keyLightDirection = sun_.direction;
        lighting.keyLightColor = {sun_.color.x * sun_.intensity,
                                  sun_.color.y * sun_.intensity,
                                  sun_.color.z * sun_.intensity,
                                  sun_.castsShadow ? 1.0f : 0.0f};
    }

    const uint32_t count = (std::min)(
        static_cast<uint32_t>(pointLights_.size()),
        static_cast<uint32_t>(lighting.pointLights.size()));
    for (uint32_t i = 0; i < count; ++i) {
        const LocalLightDesc &source = pointLights_[i];
        lighting.pointLights[i].positionRange = {
            source.position.x, source.position.y, source.position.z,
            source.range};
        lighting.pointLights[i].colorIntensity = {
            source.color.x, source.color.y, source.color.z,
            source.intensity};
    }

    if (!spotLights_.empty()) {
        const SpotLightDesc &source = spotLights_.front();
        lighting.spotLight.positionRange = {
            source.position.x, source.position.y, source.position.z,
            source.range};
        lighting.spotLight.direction = {
            source.direction.x, source.direction.y, source.direction.z, 0.0f};
        lighting.spotLight.colorIntensity = {
            source.color.x, source.color.y, source.color.z,
            source.intensity};
        lighting.spotLight.angleParams = {
            source.innerConeCos, source.outerConeCos, source.falloff,
            source.enabled ? 1.0f : 0.0f};
    }
    return lighting;
}

std::span<const LocalLightDesc> LightingScene::PointLights() const {
    return pointLights_;
}

std::span<const SpotLightDesc> LightingScene::SpotLights() const {
    return spotLights_;
}

std::span<const ReflectionProbeDesc> LightingScene::ReflectionProbes() const {
    return reflectionProbes_;
}
