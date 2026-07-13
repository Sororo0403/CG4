#include "model/ModelManager.h"

#include "core/AssetManager.h"
#include "core/ResourceHandle.h"
#include "geometry/ModelVertex.h"
#include "graphics/DirectXCommon.h"
#include "graphics/GpuResourceLifetime.h"
#include "graphics/SrvManager.h"
#include "internal/ModelPrimitiveFactory.h"
#include "internal/ModelSkinClusterResourceUtils.h"
#include "model/MaterialManager.h"
#include "model/Vertex.h"

#include <algorithm>
#include <cwctype>
#include <exception>
#include <filesystem>
#include <limits>
#include <system_error>
#include <utility>
#include <vector>

namespace {

std::filesystem::path ResolveModelPath(const std::filesystem::path& path) {
    return AssetManager::ResolvePathStrict(path);
}

std::filesystem::path SafeCurrentPath() {
    std::error_code ec;
    try {
        const std::filesystem::path path = std::filesystem::current_path(ec);
        return ec ? std::filesystem::path(L".") : path;
    } catch (const std::exception&) {
        return std::filesystem::path(L".");
    }
}

std::wstring NormalizeModelPathKey(const std::filesystem::path& path) {
    std::wstring key;
    try {
        key = path.lexically_normal().wstring();
    } catch (const std::exception&) {
        return {};
    }
#ifdef _WIN32
    std::transform(key.begin(), key.end(), key.begin(),
                   [](wchar_t c) { return static_cast<wchar_t>(towlower(c)); });
#endif
    return key;
}

std::string MakeAssimpModelPath(const std::filesystem::path& resolvedPath) {
    std::error_code ec;
    try {
        const std::filesystem::path relative =
            std::filesystem::relative(resolvedPath, SafeCurrentPath(), ec);
        if (!ec && !relative.empty()) {
            auto begin = relative.begin();
            if (begin != relative.end() && *begin != L"..") {
                return relative.generic_string();
            }
        }
    } catch (const std::exception&) {
    }

    try {
        return resolvedPath.string();
    } catch (const std::exception&) {
        return {};
    }
}

void ResetModelPlayback(Model& model) {
    if (!model.animations.empty()) {
        model.currentAnimation = model.animations.begin()->first;
        model.animationTime = 0.0f;
        model.isLoop = true;
        model.isPlaying = true;
        model.animationFinished = false;
    }
}

bool CanAppendModel(const std::vector<Model>& models) {
    if (models.size() >= static_cast<size_t>((std::numeric_limits<uint32_t>::max)())) {
        return false;
    }
    return true;
}

uint32_t AppendModel(std::vector<Model>& models, Model&& model) {
    if (!CanAppendModel(models)) {
        return kInvalidResourceId;
    }
    try {
        models.reserve(models.size() + 1u);
        models.push_back(std::move(model));
    } catch (const std::exception&) {
        return kInvalidResourceId;
    }
    return static_cast<uint32_t>(models.size() - 1);
}

bool ReserveSingleSubMesh(Model& model) {
    try {
        model.subMeshes.reserve(1u);
    } catch (const std::exception&) {
        return false;
    }
    return true;
}

bool AppendSingleSubMesh(Model& model, const ModelSubMesh& subMesh) {
    try {
        model.subMeshes.push_back(subMesh);
    } catch (const std::exception&) {
        return false;
    }
    return true;
}

void DestroyCreatedSubMesh(MeshManager& meshManager, MaterialManager& materialManager,
                           ModelSubMesh& subMesh) {
    if (IsValidResourceId(subMesh.meshId)) {
        meshManager.DestroyMesh(subMesh.meshId);
        subMesh.meshId = kInvalidResourceId;
    }
    if (materialManager.IsValidMaterialId(subMesh.materialId)) {
        materialManager.DestroyMaterial(subMesh.materialId);
        subMesh.materialId = kInvalidResourceId;
    }
}

void DestroyModelMeshes(MeshManager& meshManager, Model& model) {
    for (ModelSubMesh& subMesh : model.subMeshes) {
        if (IsValidResourceId(subMesh.meshId)) {
            meshManager.DestroyMesh(subMesh.meshId);
            subMesh.meshId = kInvalidResourceId;
        }
    }
    model.meshId = kInvalidResourceId;
}

void DestroyModelMaterials(MaterialManager& materialManager, Model& model) {
    for (ModelSubMesh& subMesh : model.subMeshes) {
        if (materialManager.IsValidMaterialId(subMesh.materialId)) {
            materialManager.DestroyMaterial(subMesh.materialId);
        }
        subMesh.materialId = kInvalidResourceId;
    }
    if (materialManager.IsValidMaterialId(model.materialId)) {
        materialManager.DestroyMaterial(model.materialId);
    }
    model.materialId = kInvalidResourceId;
}

void DestroyModelSkinClusters(DirectXCommon* dxCommon, SrvManager* srvManager, Model& model) {
    for (ModelSubMesh& subMesh : model.subMeshes) {
        SkinCluster& skinCluster = subMesh.skinCluster;

        if (dxCommon != nullptr) {
            dxCommon->UnregisterFrameRollbacks(&skinCluster);
        }

        if (srvManager != nullptr) {
            srvManager->FreeIfAllocated(skinCluster.inputVertexSrvIndex);
            srvManager->FreeIfAllocated(skinCluster.influenceSrvIndex);
            srvManager->FreeIfAllocated(skinCluster.skinnedVertexUavIndex);
        }

        ModelSkinClusterResourceUtils::UnmapSkinClusterMappings(skinCluster);
        skinCluster = {};
    }
}

void DestroyModelResources(MeshManager& meshManager, MaterialManager& materialManager,
                           DirectXCommon* dxCommon, SrvManager* srvManager, Model& model) {
    DestroyModelSkinClusters(dxCommon, srvManager, model);
    DestroyModelMeshes(meshManager, model);
    DestroyModelMaterials(materialManager, model);
}

uint32_t AppendModelOrDestroyResources(std::vector<Model>& models, MeshManager& meshManager,
                                       MaterialManager& materialManager, DirectXCommon* dxCommon,
                                       SrvManager* srvManager, Model& model) {
    const uint32_t modelId = AppendModel(models, std::move(model));
    if (!IsValidResourceId(modelId)) {
        DestroyModelResources(meshManager, materialManager, dxCommon, srvManager, model);
    }
    return modelId;
}

uint32_t AppendPrimitiveModel(std::vector<Model>& models, MeshManager& meshManager,
                              MaterialManager& materialManager, ModelRenderer& modelRenderer,
                              DirectXCommon* dxCommon, SrvManager* srvManager, uint32_t textureId,
                              ModelPrimitiveFactory::PrimitiveMeshData&& primitive) {
    Model model{};
    if (!ReserveSingleSubMesh(model)) {
        return kInvalidResourceId;
    }

    for (ModelVertex& vertex : primitive.vertices) {
        vertex.sourcePosition = vertex.position;
    }

    ModelSubMesh subMesh{};
    subMesh.vertexCount = static_cast<uint32_t>(primitive.vertices.size());
    subMesh.meshId = meshManager.CreateMesh(primitive.vertices.data(), sizeof(ModelVertex),
                                            static_cast<uint32_t>(primitive.vertices.size()),
                                            primitive.indices.data(),
                                            static_cast<uint32_t>(primitive.indices.size()));
    if (!IsValidResourceId(subMesh.meshId)) {
        return kInvalidResourceId;
    }
    subMesh.textureId = textureId;
    subMesh.materialId = materialManager.CreateMaterial(primitive.material);
    if (!IsValidResourceId(subMesh.materialId)) {
        meshManager.DestroyMesh(subMesh.meshId);
        return kInvalidResourceId;
    }

    if (!AppendSingleSubMesh(model, subMesh)) {
        DestroyCreatedSubMesh(meshManager, materialManager, subMesh);
        return kInvalidResourceId;
    }
    model.meshId = subMesh.meshId;
    model.textureId = textureId;
    model.materialId = subMesh.materialId;

    if (!modelRenderer.CreateSkinClusters(model)) {
        DestroyModelResources(meshManager, materialManager, dxCommon, srvManager, model);
        return kInvalidResourceId;
    }
    return AppendModelOrDestroyResources(models, meshManager, materialManager, dxCommon, srvManager,
                                         model);
}

} // namespace

ModelManager::~ModelManager() {
    Finalize(true);
}

void ModelManager::Initialize(DirectXCommon* dxCommon, SrvManager* srvManager,
                              TextureManager* textureManager) {
    if (!dxCommon || !srvManager || !textureManager) {
        Finalize();
        return;
    }
    if (!Finalize()) {
        return;
    }

    dxCommon_ = dxCommon;
    srvManager_ = srvManager;
    textureManager_ = textureManager;

    meshManager_.Initialize(dxCommon_);
    materialManager_.Initialize(dxCommon_);

    assimpLoader_.Initialize(textureManager_, &meshManager_, &materialManager_);

    modelRenderer_.Initialize(dxCommon_, srvManager, &meshManager_, textureManager_,
                              &materialManager_);
}

bool ModelManager::Finalize() {
    return Finalize(false);
}

bool ModelManager::Finalize(bool allowFrameAbort) {
    if (!CanReleaseGpuResources(dxCommon_, !models_.empty(), allowFrameAbort)) {
        return false;
    }

    if (!modelRenderer_.Finalize(allowFrameAbort)) {
        return false;
    }

    for (Model& model : models_) {
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
    return true;
}

void ModelManager::ReleaseUploadBuffers() {
    meshManager_.ReleaseUploadBuffers();
    materialManager_.ReleaseDeferredResources();
}

void ModelManager::ReleaseCompletedFrameResources() {
    meshManager_.ReleaseCompletedFrameResources();
    materialManager_.ReleaseCompletedFrameResources();
}

uint32_t ModelManager::Load(const std::wstring& path) {
    std::filesystem::path p;
    try {
        p = ResolveModelPath(path);
    } catch (const std::exception&) {
        return kInvalidResourceId;
    }
    std::error_code ec;
    try {
        if (!std::filesystem::exists(p, ec)) {
            return kInvalidResourceId;
        }
    } catch (const std::exception&) {
        return kInvalidResourceId;
    }

    std::wstring pathKey;
    try {
        pathKey = NormalizeModelPathKey(p);
    } catch (const std::exception&) {
        return kInvalidResourceId;
    }
    if (pathKey.empty()) {
        return kInvalidResourceId;
    }
    auto it = modelPathToId_.find(pathKey);
    if (it != modelPathToId_.end()) {
        if (it->second >= models_.size()) {
            modelPathToId_.erase(it);
        } else {
            Model& cached = models_[it->second];
            ResetModelPlayback(cached);
            animator_.Update(cached, 0.0f);
            modelRenderer_.UpdateSkinClusters(cached);
            return it->second;
        }
    }

    std::string pathStr;
    try {
        pathStr = MakeAssimpModelPath(p);
    } catch (const std::exception&) {
        return kInvalidResourceId;
    }
    if (pathStr.empty()) {
        return kInvalidResourceId;
    }

    Model model = assimpLoader_.Load(pathStr);
    if (model.subMeshes.empty()) {
        return kInvalidResourceId;
    }
    if (!modelRenderer_.CreateSkinClusters(model)) {
        DestroyModelResources(meshManager_, materialManager_, dxCommon_, srvManager_, model);
        return kInvalidResourceId;
    }

    ResetModelPlayback(model);

    animator_.Update(model, 0.0f);
    modelRenderer_.UpdateSkinClusters(model);

    uint32_t modelId = AppendModelOrDestroyResources(models_, meshManager_, materialManager_,
                                                     dxCommon_, srvManager_, model);
    if (!IsValidResourceId(modelId)) {
        return modelId;
    }
    try {
        modelPathToId_[pathKey] = modelId;
    } catch (const std::exception&) {
    }
    return modelId;
}
uint32_t ModelManager::CreatePlane(uint32_t textureId, const Material& material) {
    auto primitive = ModelPrimitiveFactory::BuildPlane(textureId, material);
    if (!primitive) {
        return kInvalidResourceId;
    }
    return AppendPrimitiveModel(models_, meshManager_, materialManager_, modelRenderer_, dxCommon_,
                                srvManager_, textureId, std::move(*primitive));
}

uint32_t ModelManager::CreateBox(uint32_t textureId, const Material& material, float width,
                                 float height, float depth) {
    auto primitive = ModelPrimitiveFactory::BuildBox(textureId, material, width, height, depth);
    if (!primitive) {
        return kInvalidResourceId;
    }
    return AppendPrimitiveModel(models_, meshManager_, materialManager_, modelRenderer_, dxCommon_,
                                srvManager_, textureId, std::move(*primitive));
}

uint32_t ModelManager::CreateSphere(uint32_t textureId, const Material& material, uint32_t slice,
                                    uint32_t stack, float radius) {
    auto primitive = ModelPrimitiveFactory::BuildSphere(textureId, material, slice, stack, radius);
    if (!primitive) {
        return kInvalidResourceId;
    }
    return AppendPrimitiveModel(models_, meshManager_, materialManager_, modelRenderer_, dxCommon_,
                                srvManager_, textureId, std::move(*primitive));
}

uint32_t ModelManager::CreateRing(uint32_t textureId, const Material& material, uint32_t divide,
                                  float outerRadius, float innerRadius) {
    auto primitive =
        ModelPrimitiveFactory::BuildRing(textureId, material, divide, outerRadius, innerRadius);
    if (!primitive) {
        return kInvalidResourceId;
    }
    return AppendPrimitiveModel(models_, meshManager_, materialManager_, modelRenderer_, dxCommon_,
                                srvManager_, textureId, std::move(*primitive));
}

uint32_t ModelManager::CreateCylinder(uint32_t textureId, const Material& material, uint32_t divide,
                                      float topRadius, float bottomRadius, float height) {
    auto primitive = ModelPrimitiveFactory::BuildCylinder(textureId, material, divide, topRadius,
                                                          bottomRadius, height);
    if (!primitive) {
        return kInvalidResourceId;
    }
    return AppendPrimitiveModel(models_, meshManager_, materialManager_, modelRenderer_, dxCommon_,
                                srvManager_, textureId, std::move(*primitive));
}

uint32_t ModelManager::CreateMesh(const void* vertexData, uint32_t vertexStride,
                                  uint32_t vertexCount, const uint32_t* indexData,
                                  uint32_t indexCount, D3D12_PRIMITIVE_TOPOLOGY primitiveTopology) {
    return meshManager_.CreateMesh(vertexData, vertexStride, vertexCount, indexData, indexCount,
                                   primitiveTopology);
}

const Mesh& ModelManager::GetMesh(uint32_t meshId) const {
    return meshManager_.GetMesh(meshId);
}
