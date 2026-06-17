#include "model/ModelManager.h"

#include <vector>

ModelHandle ModelManager::LoadHandle(const std::wstring& path) {
    return ModelHandle(Load(path));
}

ModelHandle ModelManager::CreatePlaneHandle(uint32_t textureId, const Material& material) {
    return ModelHandle(CreatePlane(textureId, material));
}

ModelHandle ModelManager::CreatePlaneHandle(TextureHandle texture, const Material& material) {
    return CreatePlaneHandle(texture.Get(), material);
}

ModelHandle ModelManager::CreateBoxHandle(uint32_t textureId, const Material& material, float width,
                                          float height, float depth) {
    return ModelHandle(CreateBox(textureId, material, width, height, depth));
}

ModelHandle ModelManager::CreateBoxHandle(TextureHandle texture, const Material& material,
                                          float width, float height, float depth) {
    return CreateBoxHandle(texture.Get(), material, width, height, depth);
}

ModelHandle ModelManager::CreateSphereHandle(uint32_t textureId, const Material& material,
                                             uint32_t slice, uint32_t stack, float radius) {
    return ModelHandle(CreateSphere(textureId, material, slice, stack, radius));
}

ModelHandle ModelManager::CreateSphereHandle(TextureHandle texture, const Material& material,
                                             uint32_t slice, uint32_t stack, float radius) {
    return CreateSphereHandle(texture.Get(), material, slice, stack, radius);
}

ModelHandle ModelManager::CreateRingHandle(uint32_t textureId, const Material& material,
                                           uint32_t divide, float outerRadius, float innerRadius) {
    return ModelHandle(CreateRing(textureId, material, divide, outerRadius, innerRadius));
}

ModelHandle ModelManager::CreateRingHandle(TextureHandle texture, const Material& material,
                                           uint32_t divide, float outerRadius, float innerRadius) {
    return CreateRingHandle(texture.Get(), material, divide, outerRadius, innerRadius);
}

ModelHandle ModelManager::CreateCylinderHandle(uint32_t textureId, const Material& material,
                                               uint32_t divide, float topRadius, float bottomRadius,
                                               float height) {
    return ModelHandle(
        CreateCylinder(textureId, material, divide, topRadius, bottomRadius, height));
}

ModelHandle ModelManager::CreateCylinderHandle(TextureHandle texture, const Material& material,
                                               uint32_t divide, float topRadius, float bottomRadius,
                                               float height) {
    return CreateCylinderHandle(texture.Get(), material, divide, topRadius, bottomRadius, height);
}

MeshHandle ModelManager::CreateMeshHandle(const void* vertexData, uint32_t vertexStride,
                                          uint32_t vertexCount, const uint32_t* indexData,
                                          uint32_t indexCount,
                                          D3D12_PRIMITIVE_TOPOLOGY primitiveTopology) {
    return MeshHandle(CreateMesh(vertexData, vertexStride, vertexCount, indexData, indexCount,
                                 primitiveTopology));
}

const Mesh& ModelManager::GetMesh(MeshHandle meshId) const {
    return GetMesh(meshId.Get());
}

void ModelManager::UpdateAnimation(uint32_t modelId, float deltaTime) {
    if (modelId >= models_.size()) {
        return;
    }

    animator_.Update(models_[modelId], deltaTime);
    modelRenderer_.UpdateSkinClusters(models_[modelId]);
}

void ModelManager::UpdateAnimation(ModelHandle modelId, float deltaTime) {
    UpdateAnimation(modelId.Get(), deltaTime);
}

void ModelManager::PlayAnimation(uint32_t modelId, const std::string& animationName, bool loop) {
    if (modelId >= models_.size()) {
        return;
    }

    animator_.Play(models_[modelId], animationName, loop);
}

void ModelManager::PlayAnimation(ModelHandle modelId, const std::string& animationName, bool loop) {
    PlayAnimation(modelId.Get(), animationName, loop);
}

bool ModelManager::IsAnimationFinished(uint32_t modelId) const {
    if (modelId >= models_.size()) {
        return false;
    }

    return animator_.IsFinished(models_[modelId]);
}

bool ModelManager::IsAnimationFinished(ModelHandle modelId) const {
    return IsAnimationFinished(modelId.Get());
}

Model* ModelManager::GetModel(uint32_t modelId) {
    if (modelId >= models_.size()) {
        return nullptr;
    }

    return &models_[modelId];
}

Model* ModelManager::GetModel(ModelHandle modelId) {
    return GetModel(modelId.Get());
}

const Model* ModelManager::GetModel(uint32_t modelId) const {
    if (modelId >= models_.size()) {
        return nullptr;
    }

    return &models_[modelId];
}

const Model* ModelManager::GetModel(ModelHandle modelId) const {
    return GetModel(modelId.Get());
}

const Material& ModelManager::GetMaterial(uint32_t materialId) const {
    return materialManager_.GetMaterial(materialId);
}

const Material& ModelManager::GetMaterial(MaterialHandle materialId) const {
    return GetMaterial(materialId.Get());
}

void ModelManager::SetMaterial(uint32_t materialId, const Material& material) {
    materialManager_.SetMaterial(materialId, material);
}

void ModelManager::SetMaterial(MaterialHandle materialId, const Material& material) {
    SetMaterial(materialId.Get(), material);
}

void ModelManager::Draw(uint32_t modelId, const Transform& transform, const Camera& camera,
                        uint32_t environmentTextureId) {
    const Model* model = GetModel(modelId);
    if (!model) {
        return;
    }

    modelRenderer_.Draw(*model, transform, camera, environmentTextureId);
}

void ModelManager::Draw(ModelHandle modelId, const Transform& transform, const Camera& camera,
                        TextureHandle environmentTexture) {
    Draw(modelId.Get(), transform, camera, environmentTexture.Get());
}

void ModelManager::DrawInstanced(uint32_t modelId, const Transform* transforms,
                                 uint32_t instanceCount, const Camera& camera,
                                 uint32_t environmentTextureId) {
    const Model* model = GetModel(modelId);
    if (!model) {
        return;
    }

    modelRenderer_.DrawInstanced(*model, transforms, instanceCount, camera, environmentTextureId);
}

void ModelManager::DrawInstanced(ModelHandle modelId, const Transform* transforms,
                                 uint32_t instanceCount, const Camera& camera,
                                 TextureHandle environmentTexture) {
    DrawInstanced(modelId.Get(), transforms, instanceCount, camera, environmentTexture.Get());
}

void ModelManager::DrawInstanced(uint32_t modelId, const InstanceData* instances,
                                 uint32_t instanceCount, const Camera& camera,
                                 uint32_t environmentTextureId) {
    const Model* model = GetModel(modelId);
    if (!model) {
        return;
    }

    modelRenderer_.DrawInstanced(*model, instances, instanceCount, camera, environmentTextureId);
}

void ModelManager::DrawInstanced(ModelHandle modelId, const InstanceData* instances,
                                 uint32_t instanceCount, const Camera& camera,
                                 TextureHandle environmentTexture) {
    DrawInstanced(modelId.Get(), instances, instanceCount, camera, environmentTexture.Get());
}

void ModelManager::DrawShadow(uint32_t modelId, const Transform& transform,
                              const DirectX::XMFLOAT4X4& lightViewProjection) {
    const Model* model = GetModel(modelId);
    if (!model) {
        return;
    }

    modelRenderer_.DrawShadow(*model, transform, lightViewProjection);
}

void ModelManager::DrawShadow(ModelHandle modelId, const Transform& transform,
                              const DirectX::XMFLOAT4X4& lightViewProjection) {
    DrawShadow(modelId.Get(), transform, lightViewProjection);
}

void ModelManager::DrawInstancedShadow(uint32_t modelId, const Transform* transforms,
                                       uint32_t instanceCount,
                                       const DirectX::XMFLOAT4X4& lightViewProjection) {
    const Model* model = GetModel(modelId);
    if (!model) {
        return;
    }

    modelRenderer_.DrawInstancedShadow(*model, transforms, instanceCount, lightViewProjection);
}

void ModelManager::DrawInstancedShadow(ModelHandle modelId, const Transform* transforms,
                                       uint32_t instanceCount,
                                       const DirectX::XMFLOAT4X4& lightViewProjection) {
    DrawInstancedShadow(modelId.Get(), transforms, instanceCount, lightViewProjection);
}

void ModelManager::DrawInstancedShadow(uint32_t modelId, const InstanceData* instances,
                                       uint32_t instanceCount,
                                       const DirectX::XMFLOAT4X4& lightViewProjection) {
    const Model* model = GetModel(modelId);
    if (!model) {
        return;
    }

    modelRenderer_.DrawInstancedShadow(*model, instances, instanceCount, lightViewProjection);
}

void ModelManager::DrawInstancedShadow(ModelHandle modelId, const InstanceData* instances,
                                       uint32_t instanceCount,
                                       const DirectX::XMFLOAT4X4& lightViewProjection) {
    DrawInstancedShadow(modelId.Get(), instances, instanceCount, lightViewProjection);
}

void ModelManager::PrepareSkinning(uint32_t modelId) {
    const Model* model = GetModel(modelId);
    if (!model) {
        return;
    }

    modelRenderer_.PrepareSkinning(*model);
}

void ModelManager::PrepareSkinning(ModelHandle modelId) {
    PrepareSkinning(modelId.Get());
}

void ModelManager::PrepareSkinning(std::initializer_list<uint32_t> modelIds) {
    std::vector<const Model*> models;
    models.reserve(modelIds.size());
    for (uint32_t modelId : modelIds) {
        const Model* model = GetModel(modelId);
        if (model) {
            models.push_back(model);
        }
    }

    modelRenderer_.PrepareSkinning(models);
}

void ModelManager::PrepareSkinningHandles(std::initializer_list<ModelHandle> modelIds) {
    for (const ModelHandle modelId : modelIds) {
        PrepareSkinning(modelId);
    }
}

void ModelManager::BeginFrame() {
    modelRenderer_.BeginFrame();
}

void ModelManager::PreDraw() {
    modelRenderer_.PreDraw();
}

void ModelManager::PreDrawShadow() {
    modelRenderer_.PreDrawShadow();
}

void ModelManager::PostDraw() {
    modelRenderer_.PostDraw();
}

void ModelManager::SetSceneLighting(const SceneLighting& lighting) {
    modelRenderer_.SetSceneLighting(lighting);
}

void ModelManager::SetDrawEffect(const ModelDrawEffect& effect) {
    modelRenderer_.SetDrawEffect(effect);
}

void ModelManager::ClearDrawEffect() {
    modelRenderer_.ClearDrawEffect();
}

void ModelManager::SetSceneFog(const SceneFog& fog) {
    modelRenderer_.SetSceneFog(fog);
}

ModelRenderer* ModelManager::GetRenderer() {
    return &modelRenderer_;
}

const ModelRenderer* ModelManager::GetRenderer() const {
    return &modelRenderer_;
}

MeshManager* ModelManager::GetMeshManager() {
    return &meshManager_;
}

const MeshManager* ModelManager::GetMeshManager() const {
    return &meshManager_;
}

bool ModelManager::IsReady() const {
    return dxCommon_ != nullptr && srvManager_ != nullptr && textureManager_ != nullptr &&
           modelRenderer_.IsReady();
}
