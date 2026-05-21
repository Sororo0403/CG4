#include "effect/EffectManager.h"

#include "core/AssetManager.h"
#include "debug/DebugLog.h"
#include "graphics/DirectXCommon.h"
#include "texture/TextureManager.h"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <nlohmann/json.hpp>
#include <stdexcept>

using json = nlohmann::json;

namespace {

constexpr float kTwoPi = 6.28318530718f;

std::string PathToString(const std::filesystem::path &path) {
    return path.generic_string();
}

std::wstring ToWidePath(const std::string &path) {
    return std::filesystem::path(path).wstring();
}

std::string ResolvePathRelativeToFile(const std::filesystem::path &baseFile,
                                      const std::string &path) {
    if (path.empty()) {
        return {};
    }

    const std::filesystem::path value(path);
    if (value.is_absolute()) {
        return PathToString(value.lexically_normal());
    }

    const std::filesystem::path candidate =
        (baseFile.parent_path() / value).lexically_normal();
    return PathToString(candidate);
}

float ReadFloat(const json &object, const char *key, float fallback) {
    return object.contains(key) && object.at(key).is_number()
               ? object.at(key).get<float>()
               : fallback;
}

uint32_t ReadUint(const json &object, const char *key, uint32_t fallback) {
    if (!object.contains(key) || !object.at(key).is_number_integer()) {
        return fallback;
    }

    const int value = object.at(key).get<int>();
    return value > 0 ? static_cast<uint32_t>(value) : fallback;
}

DirectX::XMFLOAT3 ReadFloat3(const json &object, const char *key,
                             const DirectX::XMFLOAT3 &fallback) {
    if (!object.contains(key) || !object.at(key).is_array() ||
        object.at(key).size() < 3) {
        return fallback;
    }

    const json &values = object.at(key);
    return {values.at(0).get<float>(), values.at(1).get<float>(),
            values.at(2).get<float>()};
}

DirectX::XMFLOAT4 ReadFloat4(const json &object, const char *key,
                             const DirectX::XMFLOAT4 &fallback) {
    if (!object.contains(key) || !object.at(key).is_array() ||
        object.at(key).size() < 4) {
        return fallback;
    }

    const json &values = object.at(key);
    return {values.at(0).get<float>(), values.at(1).get<float>(),
            values.at(2).get<float>(), values.at(3).get<float>()};
}

bool WriteFloat4Component(DirectX::XMFLOAT4 &value, char component,
                          float number) {
    switch (component) {
    case 'x':
    case 'r':
        value.x = number;
        return true;
    case 'y':
    case 'g':
        value.y = number;
        return true;
    case 'z':
    case 'b':
        value.z = number;
        return true;
    case 'w':
    case 'a':
        value.w = number;
        return true;
    default:
        return false;
    }
}

bool ApplyMaterialParamTarget(ParticleLayerDesc &desc,
                              const std::string &target, float value) {
    if (target.size() != 9 || target.at(7) != '.') {
        return false;
    }

    if (target.rfind("params0", 0) == 0) {
        return WriteFloat4Component(desc.materialParams0, target.at(8), value);
    }
    if (target.rfind("params1", 0) == 0) {
        return WriteFloat4Component(desc.materialParams1, target.at(8), value);
    }
    return false;
}

void ReadNamedMaterialParams(const json &material, ParticleLayerDesc &desc) {
    if (!material.contains("params") || !material.at("params").is_object()) {
        return;
    }

    const json &params = material.at("params");
    const json emptyBindings = json::object();
    const json &bindings =
        material.contains("paramBindings") && material.at("paramBindings").is_object()
            ? material.at("paramBindings")
            : emptyBindings;

    for (auto it = params.begin(); it != params.end(); ++it) {
        if (!it.value().is_number()) {
            continue;
        }

        const std::string name = it.key();
        std::string target = name;
        if (bindings.contains(name) && bindings.at(name).is_string()) {
            target = bindings.at(name).get<std::string>();
        }

        ApplyMaterialParamTarget(desc, target, it.value().get<float>());
    }
}

ParticleSpawnShape ParseSpawnShape(const std::string &value) {
    if (value == "point") {
        return ParticleSpawnShape::Point;
    }
    if (value == "box") {
        return ParticleSpawnShape::Box;
    }
    if (value == "ring") {
        return ParticleSpawnShape::Ring;
    }
    if (value == "disk") {
        return ParticleSpawnShape::Disk;
    }
    return ParticleSpawnShape::Sphere;
}

ParticleLayerDesc ParseParticleLayer(const json &layer,
                                     const std::filesystem::path &effectPath) {
    ParticleLayerDesc desc{};
    desc.name = layer.value("name", std::string{});
    desc.renderer = layer.value("renderer", desc.renderer);
    desc.texture =
        ResolvePathRelativeToFile(effectPath, layer.value("texture", desc.texture));
    desc.noiseTexture = ResolvePathRelativeToFile(
        effectPath, layer.value("noiseTexture", desc.noiseTexture));

    if (layer.contains("material")) {
        const json &material = layer.at("material");
        if (material.is_string()) {
            desc.material = material.get<std::string>();
        } else if (material.is_object()) {
            desc.material = material.value("name", desc.material);
            desc.materialPixelShader = ResolvePathRelativeToFile(
                effectPath,
                material.value("pixelShader", desc.materialPixelShader));
            desc.materialParams0 =
                ReadFloat4(material, "params0", desc.materialParams0);
            desc.materialParams1 =
                ReadFloat4(material, "params1", desc.materialParams1);
            ReadNamedMaterialParams(material, desc);
        }
    }

    desc.burstCount = ReadUint(layer, "burstCount", desc.burstCount);
    desc.maxParticles = ReadUint(
        layer, "maxParticles", (std::max)(64u, desc.burstCount * 3u));
    desc.spawnShape =
        ParseSpawnShape(layer.value("spawnShape", std::string{"sphere"}));
    desc.spawnOffsetScale =
        ReadFloat3(layer, "spawnOffsetScale", desc.spawnOffsetScale);
    desc.color = ReadFloat4(layer, "color", desc.color);
    desc.lifetime = ReadFloat(layer, "lifetime", desc.lifetime);
    desc.lifetimeRandom =
        ReadFloat(layer, "lifetimeRandom", desc.lifetimeRandom);
    desc.startScale = ReadFloat(layer, "startScale", desc.startScale);
    desc.endScale = ReadFloat(layer, "endScale", desc.endScale);
    desc.scaleRandom = ReadFloat(layer, "scaleRandom", desc.scaleRandom);
    desc.stretch = ReadFloat(layer, "stretch", desc.stretch);
    desc.damping = ReadFloat(layer, "damping", desc.damping);

    if (layer.contains("velocity") && layer.at("velocity").is_object()) {
        const json &velocity = layer.at("velocity");
        desc.velocity.radial =
            ReadFloat(velocity, "radial", desc.velocity.radial);
        desc.velocity.up = ReadFloat(velocity, "up", desc.velocity.up);
        desc.velocity.random =
            ReadFloat(velocity, "random", desc.velocity.random);
    }

    if (layer.contains("fade") && layer.at("fade").is_object()) {
        const json &fade = layer.at("fade");
        desc.fade.in = ReadFloat(fade, "in", desc.fade.in);
        desc.fade.out = ReadFloat(fade, "out", desc.fade.out);
        desc.fade.power = ReadFloat(fade, "power", desc.fade.power);
    }

    if (layer.contains("rotation") && layer.at("rotation").is_object()) {
        const json &rotation = layer.at("rotation");
        desc.rotation.randomStart =
            rotation.value("randomStart", desc.rotation.randomStart);
        desc.rotation.spin = ReadFloat(rotation, "spin", desc.rotation.spin);
    }

    desc.atlasColumns = ReadUint(layer, "atlasColumns", desc.atlasColumns);
    desc.atlasRows = ReadUint(layer, "atlasRows", desc.atlasRows);
    desc.atlasFrameStart =
        ReadUint(layer, "atlasFrameStart", desc.atlasFrameStart);
    desc.atlasFrameCount =
        ReadUint(layer, "atlasFrameCount", desc.atlasFrameCount);
    if (layer.contains("atlasFrames") && layer.at("atlasFrames").is_array()) {
        const auto frameCount =
            static_cast<uint32_t>(layer.at("atlasFrames").size());
        desc.atlasFrameCount =
            frameCount > 0 ? frameCount : desc.atlasFrameCount;
        if (!layer.contains("atlasColumns")) {
            desc.atlasColumns = (std::max)(1u, desc.atlasFrameCount);
        }
    }

    return desc;
}

CameraShakeDesc ParseCameraShake(const json &feedback) {
    CameraShakeDesc desc{};
    desc.duration = ReadFloat(feedback, "duration", desc.duration);
    desc.amplitude = ReadFloat(feedback, "amplitude", desc.amplitude);
    desc.frequency = ReadFloat(feedback, "frequency", desc.frequency);
    return desc;
}

} // namespace

void EffectManager::LoadEffect(const std::string &name,
                               const std::string &path) {
    const std::filesystem::path resolvedPath =
        AssetManager::ResolvePath(std::filesystem::path(path));
    std::ifstream file(resolvedPath);
    if (!file) {
        const std::string resolved = PathToString(resolvedPath);
        Log("load_failed", "file_not_found", {{"name", name}, {"path", resolved}});
        throw std::runtime_error("Effect file not found: " + resolved);
    }

    json root{};
    try {
        file >> root;
    } catch (const std::exception &e) {
        const std::string resolved = PathToString(resolvedPath);
        Log("load_failed", "json_parse_error",
            {{"name", name}, {"path", resolved}, {"error", e.what()}});
        throw;
    }

    EffectAsset asset{};
    asset.name = root.value("name", name);
    asset.lifetime = ReadFloat(root, "lifetime", asset.lifetime);

    if (root.contains("particleLayers") &&
        root.at("particleLayers").is_array()) {
        for (const json &layer : root.at("particleLayers")) {
            asset.particleLayers.push_back(ParseParticleLayer(layer, resolvedPath));
        }
    }

    if (root.contains("screenFeedback") &&
        root.at("screenFeedback").is_array()) {
        for (const json &feedback : root.at("screenFeedback")) {
            if (feedback.value("type", std::string{}) == "cameraShake") {
                asset.cameraShakes.push_back(ParseCameraShake(feedback));
            }
        }
    }

    if (const std::optional<size_t> existing = FindAssetIndex(name)) {
        assets_.at(*existing) = std::move(asset);
    } else {
        assets_.push_back(std::move(asset));
    }

    activeEffects_.clear();
    cameraShakes_.clear();
    BuildRuntimesFromLoadedAssets();
    Log("loaded", "ok", {{"name", name}, {"path", PathToString(resolvedPath)}});
}

bool EffectManager::InitializeGpu(const SceneRenderServices &rendering) {
    if (gpuInitialized_) {
        return true;
    }
    if (!rendering.dxCommon || !rendering.srv || !rendering.texture ||
        !rendering.dxCommon->IsCommandListRecording()) {
        return false;
    }

    for (const std::unique_ptr<ParticleLayerRuntime> &layerPtr :
         particleLayers_) {
        ParticleLayerRuntime &layer = *layerPtr;
        uint32_t textureId = rendering.texture->GetWhiteTextureId();
        if (!layer.desc.texture.empty()) {
            try {
                textureId = rendering.texture->Load(ToWidePath(layer.desc.texture));
            } catch (const std::exception &e) {
                Log("texture_load_failed", "fallback_white",
                    {{"layer", layer.desc.name},
                     {"texture", layer.desc.texture},
                     {"error", e.what()}});
            }
        }

        uint32_t noiseTextureId = rendering.texture->GetWhiteTextureId();
        if (!layer.desc.noiseTexture.empty()) {
            try {
                noiseTextureId =
                    rendering.texture->Load(ToWidePath(layer.desc.noiseTexture));
            } catch (const std::exception &e) {
                Log("noise_texture_load_failed", "fallback_white",
                    {{"layer", layer.desc.name},
                     {"texture", layer.desc.noiseTexture},
                     {"error", e.what()}});
            }
        }

        GPUParticleMaterialSettings materialSettings{};
        materialSettings.pixelShaderPath =
            ToWidePath(layer.desc.materialPixelShader);
        materialSettings.params0 = layer.desc.materialParams0;
        materialSettings.params1 = layer.desc.materialParams1;
        materialSettings.noiseTextureId = noiseTextureId;
        layer.system.SetMaterialSettings(materialSettings);
        layer.system.Initialize(rendering.dxCommon, rendering.srv,
                                rendering.texture, textureId,
                                layer.desc.maxParticles);
        layer.textureId = textureId;
        layer.initialized = true;
    }

    gpuInitialized_ = true;
    return true;
}

void EffectManager::Play(const std::string &name,
                         const DirectX::XMFLOAT3 &worldPosition) {
    const std::optional<size_t> assetIndex = FindAssetIndex(name);
    if (!assetIndex) {
        Log("play_ignored", "effect_not_found", {{"name", name}});
        return;
    }
    if (!gpuInitialized_) {
        Log("play_ignored", "gpu_not_initialized", {{"name", name}});
        return;
    }

    const EffectAsset &asset = assets_.at(*assetIndex);
    const size_t runtimeOffset = GetRuntimeLayerOffset(*assetIndex);
    for (size_t layerIndex = 0; layerIndex < asset.particleLayers.size();
         ++layerIndex) {
        ParticleLayerRuntime &runtime =
            *particleLayers_.at(runtimeOffset + layerIndex);
        runtime.system.EmitOnce(
            BuildEmitterSettings(runtime.desc, worldPosition));
    }

    activeEffects_.push_back(
        {*assetIndex, worldPosition, 0.0f, (std::max)(0.0f, asset.lifetime)});

    for (const CameraShakeDesc &shake : asset.cameraShakes) {
        if (shake.duration > 0.0f && shake.amplitude > 0.0f) {
            cameraShakes_.push_back({shake, 0.0f});
        }
    }
}

void EffectManager::Update(float deltaTime) {
    if (gpuInitialized_) {
        for (const std::unique_ptr<ParticleLayerRuntime> &layer :
             particleLayers_) {
            layer->system.Update(deltaTime);
        }
    }

    for (ActiveEffectInstance &instance : activeEffects_) {
        instance.age += deltaTime;
    }
    activeEffects_.erase(
        std::remove_if(activeEffects_.begin(), activeEffects_.end(),
                       [](const ActiveEffectInstance &instance) {
                           return instance.age >= instance.duration;
                       }),
        activeEffects_.end());

    for (CameraShakeInstance &shake : cameraShakes_) {
        shake.elapsed += deltaTime;
    }
    cameraShakes_.erase(
        std::remove_if(cameraShakes_.begin(), cameraShakes_.end(),
                       [](const CameraShakeInstance &shake) {
                           return shake.elapsed >= shake.desc.duration;
                       }),
        cameraShakes_.end());
}

void EffectManager::Draw(const Camera &camera) {
    if (!gpuInitialized_) {
        return;
    }

    for (const std::unique_ptr<ParticleLayerRuntime> &layer :
         particleLayers_) {
        layer->system.Draw(camera);
    }
}

DirectX::XMFLOAT3 EffectManager::GetCameraShakeOffset() const {
    DirectX::XMFLOAT3 offset{0.0f, 0.0f, 0.0f};
    for (const CameraShakeInstance &shake : cameraShakes_) {
        if (shake.desc.duration <= 0.0f) {
            continue;
        }

        const float t =
            std::clamp(shake.elapsed / shake.desc.duration, 0.0f, 1.0f);
        const float falloff = (1.0f - t) * (1.0f - t);
        const float phase = shake.elapsed * shake.desc.frequency * kTwoPi;
        offset.x += std::sin(phase) * shake.desc.amplitude * falloff;
        offset.y += std::cos(phase * 1.37f) * shake.desc.amplitude * falloff;
    }
    return offset;
}

ParticleEmitterSettings EffectManager::BuildEmitterSettings(
    const ParticleLayerDesc &desc, const DirectX::XMFLOAT3 &position) {
    ParticleEmitterSettings settings{};
    settings.position = position;
    settings.emissionType = ParticleEmissionType::Burst;
    settings.spawnShape = desc.spawnShape;
    settings.burstCount = desc.burstCount;
    settings.spawnOffsetScale = desc.spawnOffsetScale;
    settings.tintColor = desc.color;
    settings.direction = {0.0f, 1.0f, 0.0f};
    settings.radialVelocity = desc.velocity.radial;
    settings.directionalVelocity = desc.velocity.up;
    settings.velocityBias = {0.0f, 0.0f, 0.0f};
    settings.baseLifeTime = desc.lifetime;
    settings.lifeTimeRandom = desc.lifetimeRandom;
    settings.startScale = desc.startScale;
    settings.endScale = desc.endScale;
    settings.scaleRandom = desc.scaleRandom;
    settings.stretch = desc.stretch;
    settings.rotationSpeed = desc.rotation.spin;
    settings.acceleration = {0.0f, 0.0f, 0.0f};
    settings.turbulence = desc.velocity.random;
    settings.damping = desc.damping;
    settings.fadeInTime = desc.fade.in;
    settings.fadeOutTime = desc.fade.out;
    settings.fadeOutPower = desc.fade.power;
    settings.atlasColumns = desc.atlasColumns;
    settings.atlasRows = desc.atlasRows;
    settings.atlasFrameStart = desc.atlasFrameStart;
    settings.atlasFrameCount = desc.atlasFrameCount;
    return settings;
}

std::optional<size_t> EffectManager::FindAssetIndex(
    const std::string &name) const {
    for (size_t index = 0; index < assets_.size(); ++index) {
        if (assets_[index].name == name) {
            return index;
        }
    }
    return std::nullopt;
}

size_t EffectManager::GetRuntimeLayerOffset(size_t assetIndex) const {
    size_t offset = 0;
    for (size_t index = 0; index < assetIndex && index < assets_.size();
         ++index) {
        offset += assets_[index].particleLayers.size();
    }
    return offset;
}

void EffectManager::BuildRuntimesFromLoadedAssets() {
    particleLayers_.clear();
    gpuInitialized_ = false;

    for (const EffectAsset &asset : assets_) {
        for (const ParticleLayerDesc &layer : asset.particleLayers) {
            auto runtime = std::make_unique<ParticleLayerRuntime>();
            runtime->desc = layer;
            particleLayers_.push_back(std::move(runtime));
        }
    }
}

void EffectManager::Log(
    std::string_view state, std::string_view value,
    std::initializer_list<DebugLogField> fields) const {
    DebugLog::Get().Write("EffectManager", "Effect", state, value, fields);
}
