#include "GameScene.h"
#include "DirectXCommon.h"
#include "Input.h"
#include "TextureManager.h"
#include "WinApp.h"
#include <filesystem>

void GameScene::Initialize(const SceneContext &ctx) {
    BaseScene::Initialize(ctx);

    const float aspect =
        static_cast<float>(ctx.winApp->GetWidth()) /
        static_cast<float>(ctx.winApp->GetHeight());

    camera_.Initialize(aspect);
    camera_.SetMode(CameraMode::Free);
    camera_.SetPosition({0.0f, 0.0f, 0.0f});
    camera_.SetRotation({0.0f, 0.0f, 0.0f});
    camera_.UpdateMatrices();

    skyboxRenderer_.Initialize(ctx.dxCommon, ctx.srv, ctx.texture);

    ctx.dxCommon->BeginUpload();
    skyboxTextureId_ = LoadSkyboxTexture();
    ctx.dxCommon->EndUpload();
    ctx.texture->ReleaseUploadBuffers();
}

void GameScene::Update() {
    camera_.Update(*ctx_->input, ctx_->deltaTime);
    camera_.UpdateMatrices();
}

void GameScene::Draw() { skyboxRenderer_.Draw(skyboxTextureId_, camera_); }

uint32_t GameScene::LoadSkyboxTexture() {
    static constexpr const wchar_t *kCandidates[] = {
        L"resources/skybox/rostock_laage_airport_4k.dds",
        L"resources/textures/rostock_laage_airport_4k.dds",
        L"resources/rostock_laage_airport_4k.dds",
    };

    for (const wchar_t *path : kCandidates) {
        if (std::filesystem::exists(path)) {
            return ctx_->texture->Load(path);
        }
    }

    return ctx_->texture->CreateDebugCubemap(8);
}
