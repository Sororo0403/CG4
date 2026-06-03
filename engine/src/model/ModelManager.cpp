#include "model/ModelManager.h"
#include "core/AssetManager.h"
#include "graphics/DirectXCommon.h"
#include "graphics/GpuResourceLifetime.h"
#include "graphics/SrvManager.h"
#include "model/MaterialManager.h"
#include "model/Vertex.h"
#include "ModelPrimitiveFactory.h"
#include "texture/TextureManager.h"
#include <DirectXMath.h>
#include <algorithm>
#include <cwctype>
#include <filesystem>
#include <limits>
#include <system_error>
#include <utility>
#include <vector>

namespace {

std::filesystem::path ResolveModelPath(const std::filesystem::path &path) {
    return AssetManager::ResolvePathStrict(path);
}

std::filesystem::path SafeCurrentPath() {
    std::error_code ec;
    const std::filesystem::path path = std::filesystem::current_path(ec);
    return ec ? std::filesystem::path(L".") : path;
}

std::wstring NormalizeModelPathKey(const std::filesystem::path &path) {
    std::wstring key = path.lexically_normal().wstring();
#ifdef _WIN32
    std::transform(key.begin(), key.end(), key.begin(),
                   [](wchar_t c) { return static_cast<wchar_t>(towlower(c)); });
#endif
    return key;
}

std::string MakeAssimpModelPath(const std::filesystem::path &resolvedPath) {
    std::error_code ec;
    const std::filesystem::path relative =
        std::filesystem::relative(resolvedPath, SafeCurrentPath(), ec);
    if (!ec && !relative.empty()) {
        auto begin = relative.begin();
        if (begin != relative.end() && *begin != L"..") {
            return relative.generic_string();
        }
    }

    return resolvedPath.string();
}

void ResetModelPlayback(Model &model) {
    if (!model.animations.empty()) {
        model.currentAnimation = model.animations.begin()->first;
        model.animationTime = 0.0f;
        model.isLoop = true;
        model.isPlaying = true;
        model.animationFinished = false;
    }
}

bool CanAppendModel(const std::vector<Model> &models) {
    if (models.size() >=
        static_cast<size_t>((std::numeric_limits<uint32_t>::max)())) {
        return false;
    }
    return true;
}

uint32_t AppendModel(std::vector<Model> &models, Model &&model) {
    if (!CanAppendModel(models)) {
        return UINT32_MAX;
    }
    try {
        models.reserve(models.size() + 1u);
        models.push_back(std::move(model));
    } catch (...) {
        return UINT32_MAX;
    }
    return static_cast<uint32_t>(models.size() - 1);
}

bool ReserveSingleSubMesh(Model &model) {
    try {
        model.subMeshes.reserve(1u);
    } catch (...) {
        return false;
    }
    return true;
}

bool AppendSingleSubMesh(Model &model, const ModelSubMesh &subMesh) {
    try {
        model.subMeshes.push_back(subMesh);
    } catch (...) {
        return false;
    }
    return true;
}

void DestroyCreatedSubMesh(MeshManager &meshManager,
                           MaterialManager &materialManager,
                           ModelSubMesh &subMesh) {
    if (subMesh.meshId != UINT32_MAX) {
        meshManager.DestroyMesh(subMesh.meshId);
        subMesh.meshId = UINT32_MAX;
    }
    if (materialManager.IsValidMaterialId(subMesh.materialId)) {
        materialManager.DestroyMaterial(subMesh.materialId);
        subMesh.materialId = UINT32_MAX;
    }
}

void DestroyModelMeshes(MeshManager &meshManager, Model &model) {
    for (ModelSubMesh &subMesh : model.subMeshes) {
        if (subMesh.meshId != UINT32_MAX) {
            meshManager.DestroyMesh(subMesh.meshId);
            subMesh.meshId = UINT32_MAX;
        }
    }
    model.meshId = UINT32_MAX;
}

void DestroyModelMaterials(MaterialManager &materialManager, Model &model) {
    for (ModelSubMesh &subMesh : model.subMeshes) {
        if (materialManager.IsValidMaterialId(subMesh.materialId)) {
            materialManager.DestroyMaterial(subMesh.materialId);
        }
        subMesh.materialId = UINT32_MAX;
    }
    if (materialManager.IsValidMaterialId(model.materialId)) {
        materialManager.DestroyMaterial(model.materialId);
    }
    model.materialId = UINT32_MAX;
}

void DestroyModelSkinClusters(DirectXCommon *dxCommon, SrvManager *srvManager,
                              Model &model) {
    for (ModelSubMesh &subMesh : model.subMeshes) {
        SkinCluster &skinCluster = subMesh.skinCluster;

        if (dxCommon != nullptr) {
            dxCommon->UnregisterFrameRollbacks(&skinCluster);
        }

        if (srvManager != nullptr) {
            srvManager->FreeIfAllocated(skinCluster.inputVertexSrvIndex);
            srvManager->FreeIfAllocated(skinCluster.influenceSrvIndex);
            srvManager->FreeIfAllocated(skinCluster.skinnedVertexUavIndex);
        }

        if (skinCluster.influenceResource &&
            skinCluster.mappedInfluence != nullptr) {
            skinCluster.influenceResource->Unmap(0, nullptr);
            skinCluster.mappedInfluence = nullptr;
        }
        for (SkinPaletteFrame &frame : skinCluster.paletteFrames) {
            if (frame.resource && frame.mappedPalette != nullptr) {
                frame.resource->Unmap(0, nullptr);
                frame.mappedPalette = nullptr;
            }
        }

        skinCluster = {};
    }
}

void DestroyModelResources(MeshManager &meshManager,
                           MaterialManager &materialManager,
                           DirectXCommon *dxCommon, SrvManager *srvManager,
                           Model &model) {
    DestroyModelSkinClusters(dxCommon, srvManager, model);
    DestroyModelMeshes(meshManager, model);
    DestroyModelMaterials(materialManager, model);
}

uint32_t AppendModelOrDestroyResources(std::vector<Model> &models,
                                       MeshManager &meshManager,
                                       MaterialManager &materialManager,
                                       DirectXCommon *dxCommon,
                                       SrvManager *srvManager,
                                       Model &model) {
    const uint32_t modelId = AppendModel(models, std::move(model));
    if (modelId == UINT32_MAX) {
        DestroyModelResources(meshManager, materialManager, dxCommon,
                              srvManager, model);
    }
    return modelId;
}

uint32_t AppendPrimitiveModel(
    std::vector<Model> &models, MeshManager &meshManager,
    MaterialManager &materialManager, ModelRenderer &modelRenderer,
    DirectXCommon *dxCommon, SrvManager *srvManager, uint32_t textureId,
    ModelPrimitiveFactory::PrimitiveMeshData &&primitive) {
    Model model{};
    if (!ReserveSingleSubMesh(model)) {
        return UINT32_MAX;
    }

    ModelSubMesh subMesh{};
    subMesh.vertexCount = static_cast<uint32_t>(primitive.vertices.size());
    subMesh.meshId = meshManager.CreateMesh(
        primitive.vertices.data(), sizeof(Vertex),
        static_cast<uint32_t>(primitive.vertices.size()),
        primitive.indices.data(), static_cast<uint32_t>(primitive.indices.size()));
    if (subMesh.meshId == UINT32_MAX) {
        return UINT32_MAX;
    }
    subMesh.textureId = textureId;
    subMesh.materialId = materialManager.CreateMaterial(primitive.material);
    if (subMesh.materialId == UINT32_MAX) {
        meshManager.DestroyMesh(subMesh.meshId);
        return UINT32_MAX;
    }

    if (!AppendSingleSubMesh(model, subMesh)) {
        DestroyCreatedSubMesh(meshManager, materialManager, subMesh);
        return UINT32_MAX;
    }
    model.meshId = subMesh.meshId;
    model.textureId = textureId;
    model.materialId = subMesh.materialId;

    if (!modelRenderer.CreateSkinClusters(model)) {
        DestroyModelResources(meshManager, materialManager, dxCommon,
                              srvManager, model);
        return UINT32_MAX;
    }
    return AppendModelOrDestroyResources(models, meshManager, materialManager,
                                         dxCommon, srvManager, model);
}


} // namespace

namespace {
ModelManager *gActiveModelManager = nullptr;
}

ModelManager &ModelManager::GetInstance() {
    static ModelManager instance;
    return gActiveModelManager != nullptr ? *gActiveModelManager : instance;
}

void ModelManager::SetActiveInstance(ModelManager *instance) {
    gActiveModelManager = instance;
}

ModelManager::~ModelManager() {
    Finalize(true);
}

void ModelManager::Initialize(DirectXCommon *dxCommon, SrvManager *srvManager,
                              TextureManager *textureManager) {
    if (!dxCommon || !srvManager || !textureManager) {
        Finalize();
        return;
    }
    if (!Finalize()) {
        return;
    }

    SetActiveInstance(this);
    dxCommon_ = dxCommon;
    srvManager_ = srvManager;
    textureManager_ = textureManager;

    meshManager_.Initialize(dxCommon_);
    materialManager_.Initialize(dxCommon_);

    assimpLoader_.Initialize(textureManager_, &meshManager_, &materialManager_);

    modelRenderer_.Initialize(dxCommon_, srvManager, &meshManager_,
                              textureManager_, &materialManager_);
}

bool ModelManager::Finalize() { return Finalize(false); }

bool ModelManager::Finalize(bool allowFrameAbort) {
    if (!CanReleaseGpuResources(dxCommon_, !models_.empty(),
                                allowFrameAbort)) {
        return false;
    }

    if (!modelRenderer_.Finalize(allowFrameAbort)) {
        return false;
    }

    for (Model &model : models_) {
        DestroyModelSkinClusters(dxCommon_, srvManager_, model);
    }

    modelPathToId_.clear();
    models_.clear();
    if (!materialManager_.Finalize(allowFrameAbort)) {
        return false;
    }
    if (!meshManager_.Finalize(allowFrameAbort)) {
        return false;
    }
    dxCommon_ = nullptr;
    srvManager_ = nullptr;
    textureManager_ = nullptr;
    if (gActiveModelManager == this) {
        SetActiveInstance(nullptr);
    }
    return true;
}

void ModelManager::ReleaseUploadBuffers() {
    meshManager_.ReleaseUploadBuffers();
    materialManager_.ReleaseDeferredResources();
}
uint32_t ModelManager::Load(const std::wstring &path) {
    std::filesystem::path p = ResolveModelPath(path);
    std::error_code ec;
    if (!std::filesystem::exists(p, ec)) {
        return UINT32_MAX;
    }

    const std::wstring pathKey = NormalizeModelPathKey(p);
    auto it = modelPathToId_.find(pathKey);
    if (it != modelPathToId_.end()) {
        if (it->second >= models_.size()) {
            modelPathToId_.erase(it);
        } else {
            Model &cached = models_[it->second];
            ResetModelPlayback(cached);
            animator_.Update(cached, 0.0f);
            modelRenderer_.UpdateSkinClusters(cached);
            return it->second;
        }
    }

    std::string pathStr = MakeAssimpModelPath(p);

    Model model = assimpLoader_.Load(pathStr);
    if (model.subMeshes.empty()) {
        return UINT32_MAX;
    }
    if (!modelRenderer_.CreateSkinClusters(model)) {
        DestroyModelResources(meshManager_, materialManager_, dxCommon_,
                              srvManager_, model);
        return UINT32_MAX;
    }

    ResetModelPlayback(model);

    animator_.Update(model, 0.0f);
    modelRenderer_.UpdateSkinClusters(model);

    uint32_t modelId =
        AppendModelOrDestroyResources(models_, meshManager_, materialManager_,
                                      dxCommon_, srvManager_, model);
    if (modelId == UINT32_MAX) {
        return modelId;
    }
    try {
        modelPathToId_[pathKey] = modelId;
    } catch (...) {
    }

    return modelId;
}
uint32_t ModelManager::CreatePlane(uint32_t textureId,
                                   const Material &material) {
    auto primitive = ModelPrimitiveFactory::BuildPlane(textureId, material);
    if (!primitive) {
        return UINT32_MAX;
    }
    return AppendPrimitiveModel(models_, meshManager_, materialManager_,
                                modelRenderer_, dxCommon_, srvManager_,
                                textureId, std::move(*primitive));
}

uint32_t ModelManager::CreateBox(uint32_t textureId, const Material &material,
                                 float width, float height, float depth) {
    auto primitive = ModelPrimitiveFactory::BuildBox(textureId, material, width,
                                                     height, depth);
    if (!primitive) {
        return UINT32_MAX;
    }
    return AppendPrimitiveModel(models_, meshManager_, materialManager_,
                                modelRenderer_, dxCommon_, srvManager_,
                                textureId, std::move(*primitive));
}

uint32_t ModelManager::CreateSphere(uint32_t textureId,
                                    const Material &material, uint32_t slice,
                                    uint32_t stack, float radius) {
    auto primitive = ModelPrimitiveFactory::BuildSphere(textureId, material,
                                                        slice, stack, radius);
    if (!primitive) {
        return UINT32_MAX;
    }
    return AppendPrimitiveModel(models_, meshManager_, materialManager_,
                                modelRenderer_, dxCommon_, srvManager_,
                                textureId, std::move(*primitive));
}

uint32_t ModelManager::CreateRing(uint32_t textureId, const Material &material,
                                  uint32_t divide, float outerRadius,
                                  float innerRadius) {
    auto primitive = ModelPrimitiveFactory::BuildRing(textureId, material,
                                                      divide, outerRadius,
                                                      innerRadius);
    if (!primitive) {
        return UINT32_MAX;
    }
    return AppendPrimitiveModel(models_, meshManager_, materialManager_,
                                modelRenderer_, dxCommon_, srvManager_,
                                textureId, std::move(*primitive));
}

uint32_t ModelManager::CreateCylinder(uint32_t textureId,
                                      const Material &material, uint32_t divide,
                                      float topRadius, float bottomRadius,
                                      float height) {
    auto primitive = ModelPrimitiveFactory::BuildCylinder(
        textureId, material, divide, topRadius, bottomRadius, height);
    if (!primitive) {
        return UINT32_MAX;
    }
    return AppendPrimitiveModel(models_, meshManager_, materialManager_,
                                modelRenderer_, dxCommon_, srvManager_,
                                textureId, std::move(*primitive));
}

uint32_t ModelManager::CreateLowPolyTerrain(uint32_t textureId,
                                            const Material &material,
                                            uint32_t grid, float size,
                                            float maxHeight, float flatRadius,
                                            uint32_t seed) {
    auto primitive = ModelPrimitiveFactory::BuildLowPolyTerrain(
        textureId, material, grid, size, maxHeight, flatRadius, seed);
    if (!primitive) {
        return UINT32_MAX;
    }
    return AppendPrimitiveModel(models_, meshManager_, materialManager_,
                                modelRenderer_, dxCommon_, srvManager_,
                                textureId, std::move(*primitive));
}
uint32_t ModelManager::CreateMesh(
    const void *vertexData, uint32_t vertexStride, uint32_t vertexCount,
    const uint32_t *indexData, uint32_t indexCount,
    D3D12_PRIMITIVE_TOPOLOGY primitiveTopology) {
    return meshManager_.CreateMesh(vertexData, vertexStride, vertexCount,
                                   indexData, indexCount, primitiveTopology);
}

const Mesh &ModelManager::GetMesh(uint32_t meshId) const {
    return meshManager_.GetMesh(meshId);
}
void ModelManager::UpdateAnimation(uint32_t modelId, float deltaTime) {
    if (modelId >= models_.size()) {
        return;
    }

    animator_.Update(models_[modelId], deltaTime);
    modelRenderer_.UpdateSkinClusters(models_[modelId]);
}

void ModelManager::PlayAnimation(uint32_t modelId,
                                 const std::string &animationName, bool loop) {
    if (modelId >= models_.size()) {
        return;
    }

    animator_.Play(models_[modelId], animationName, loop);
}

bool ModelManager::IsAnimationFinished(uint32_t modelId) const {
    if (modelId >= models_.size()) {
        return false;
    }

    return animator_.IsFinished(models_[modelId]);
}

Model *ModelManager::GetModel(uint32_t modelId) {
    if (modelId >= models_.size()) {
        return nullptr;
    }

    return &models_[modelId];
}

const Model *ModelManager::GetModel(uint32_t modelId) const {
    if (modelId >= models_.size()) {
        return nullptr;
    }

    return &models_[modelId];
}

const Material &ModelManager::GetMaterial(uint32_t materialId) const {
    return materialManager_.GetMaterial(materialId);
}

void ModelManager::SetMaterial(uint32_t materialId, const Material &material) {
    materialManager_.SetMaterial(materialId, material);
}
void ModelManager::Draw(uint32_t modelId, const Transform &transform,
                        const Camera &camera, uint32_t environmentTextureId) {
    const Model *model = GetModel(modelId);
    if (!model) {
        return;
    }

    modelRenderer_.Draw(*model, transform, camera, environmentTextureId);
}

void ModelManager::DrawInstanced(uint32_t modelId, const Transform *transforms,
                                 uint32_t instanceCount,
                                 const Camera &camera,
                                 uint32_t environmentTextureId) {
    const Model *model = GetModel(modelId);
    if (!model) {
        return;
    }

    modelRenderer_.DrawInstanced(*model, transforms, instanceCount, camera,
                                 environmentTextureId);
}

void ModelManager::DrawInstanced(uint32_t modelId,
                                 const InstanceData *instances,
                                 uint32_t instanceCount,
                                 const Camera &camera,
                                 uint32_t environmentTextureId) {
    const Model *model = GetModel(modelId);
    if (!model) {
        return;
    }

    modelRenderer_.DrawInstanced(*model, instances, instanceCount, camera,
                                 environmentTextureId);
}

void ModelManager::DrawShadow(
    uint32_t modelId, const Transform &transform,
    const DirectX::XMFLOAT4X4 &lightViewProjection) {
    const Model *model = GetModel(modelId);
    if (!model) {
        return;
    }

    modelRenderer_.DrawShadow(*model, transform, lightViewProjection);
}

void ModelManager::DrawInstancedShadow(
    uint32_t modelId, const Transform *transforms, uint32_t instanceCount,
    const DirectX::XMFLOAT4X4 &lightViewProjection) {
    const Model *model = GetModel(modelId);
    if (!model) {
        return;
    }

    modelRenderer_.DrawInstancedShadow(*model, transforms, instanceCount,
                                       lightViewProjection);
}

void ModelManager::DrawInstancedShadow(
    uint32_t modelId, const InstanceData *instances, uint32_t instanceCount,
    const DirectX::XMFLOAT4X4 &lightViewProjection) {
    const Model *model = GetModel(modelId);
    if (!model) {
        return;
    }

    modelRenderer_.DrawInstancedShadow(*model, instances, instanceCount,
                                       lightViewProjection);
}

void ModelManager::PrepareSkinning(uint32_t modelId) {
    const Model *model = GetModel(modelId);
    if (!model) {
        return;
    }

    modelRenderer_.PrepareSkinning(*model);
}

void ModelManager::PrepareSkinning(std::initializer_list<uint32_t> modelIds) {
    std::vector<const Model *> models;
    try {
        models.reserve(modelIds.size());
    } catch (...) {
        return;
    }
    for (uint32_t modelId : modelIds) {
        const Model *model = GetModel(modelId);
        if (model) {
            try {
                models.push_back(model);
            } catch (...) {
                break;
            }
        }
    }

    modelRenderer_.PrepareSkinning(models);
}
