#include "GameScene.h"
#include "DirectXCommon.h"
#include "Input.h"
#include "ModelManager.h"
#include "ModelRenderer.h"
#include "TextureManager.h"
#include "WinApp.h"
#include <DirectXMath.h>
#include <filesystem>
#include <stdexcept>

using namespace DirectX;

namespace {

XMMATRIX TransformToMatrix(const Transform &transform) {
    const XMVECTOR rotation =
        XMQuaternionNormalize(XMLoadFloat4(&transform.rotation));

    return XMMatrixScaling(transform.scale.x, transform.scale.y,
                           transform.scale.z) *
           XMMatrixRotationQuaternion(rotation) *
           XMMatrixTranslation(transform.position.x, transform.position.y,
                               transform.position.z);
}

Transform MatrixToTransform(const XMMATRIX &matrix) {
    XMVECTOR scale;
    XMVECTOR rotation;
    XMVECTOR translation;
    if (!XMMatrixDecompose(&scale, &rotation, &translation, matrix)) {
        throw std::runtime_error("Failed to decompose transform");
    }

    Transform transform{};
    XMStoreFloat3(&transform.scale, scale);
    XMStoreFloat4(&transform.rotation, XMQuaternionNormalize(rotation));
    XMStoreFloat3(&transform.position, translation);
    return transform;
}

} // namespace

void GameScene::Initialize(const SceneContext &ctx) {
    BaseScene::Initialize(ctx);

    const float aspect = static_cast<float>(ctx.winApp->GetWidth()) /
                         static_cast<float>(ctx.winApp->GetHeight());

    camera_.Initialize(aspect);
    camera_.SetMode(CameraMode::Free);
    camera_.SetPosition({0.0f, 2.5f, -10.0f});
    camera_.SetRotation({0.10f, 0.0f, 0.0f});
    camera_.UpdateMatrices();

    skyboxRenderer_.Initialize(ctx.dxCommon, ctx.srv, ctx.texture);

    ctx.dxCommon->BeginUpload();
    skyboxTextureId_ = LoadSkyboxTexture();
    LoadLevel(L"resources/levels/sample_level.json");
    ctx.dxCommon->EndUpload();
    ctx.texture->ReleaseUploadBuffers();
}

void GameScene::Update() {
    camera_.Update(*ctx_->input, ctx_->deltaTime);
    camera_.UpdateMatrices();
}

void GameScene::Draw() {
    skyboxRenderer_.Draw(skyboxTextureId_, camera_);

    ctx_->modelRenderer->PreDraw();
    for (const PlacedObject &object : placedObjects_) {
        const Model *model = ctx_->model->GetModel(object.modelId);
        if (!model) {
            continue;
        }

        ctx_->modelRenderer->Draw(*model, object.transform, camera_);
    }
    ctx_->modelRenderer->PostDraw();
}

uint32_t GameScene::LoadSkyboxTexture() {
    static constexpr const wchar_t *kCandidates[] = {
        L"resources/skybox/rostock_laage_airport_4k.dds",
        L"resources/textures/rostock_laage_airport_4k.dds",
        L"resources/rostock_laage_airport_4k.dds",
    };

    for (const wchar_t *path : kCandidates) {
        if (std::filesystem::exists(path)) {
            return ctx_->texture->Load(
                L"resources/rostock_laage_airport_4k.dds");
        }
    }

    return ctx_->texture->CreateDebugCubemap(8);
}

void GameScene::LoadLevel(const std::filesystem::path &path) {
    placedObjects_.clear();
    modelCache_.clear();

    const LevelData level = LevelLoader::Load(path);
    const std::filesystem::path baseDirectory = path.parent_path();

    for (const LevelObjectData &object : level.objects) {
        InstantiateLevelObject(object, baseDirectory, XMMatrixIdentity());
    }
}

void GameScene::InstantiateLevelObject(
    const LevelObjectData &object, const std::filesystem::path &baseDirectory,
    const XMMATRIX &parentWorld) {
    const XMMATRIX localWorld = TransformToMatrix(object.transform);
    const XMMATRIX world = localWorld * parentWorld;

    if (object.type == "MESH" && !object.fileName.empty()) {
        const std::filesystem::path modelPath = baseDirectory / object.fileName;
        const std::wstring fullPath = modelPath.lexically_normal().wstring();

        uint32_t modelId = 0;
        auto it = modelCache_.find(fullPath);
        if (it == modelCache_.end()) {
            modelId = ctx_->model->Load(fullPath);
            modelCache_.emplace(fullPath, modelId);
        } else {
            modelId = it->second;
        }

        placedObjects_.push_back(
            {object.name, fullPath, modelId, MatrixToTransform(world)});
    }

    for (const LevelObjectData &child : object.children) {
        InstantiateLevelObject(child, baseDirectory, world);
    }
}
