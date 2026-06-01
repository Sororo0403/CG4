#include "GameScene.h"

#include "Engine.h"

#ifdef _DEBUG
#include "imgui.h"
#endif

#include <algorithm>
#include <cmath>

namespace {

constexpr float kPlayerSize = 42.0f;
constexpr float kPlayerSpeed = 360.0f;
constexpr float kGoalSize = 56.0f;

DirectX::XMFLOAT2 Normalize(DirectX::XMFLOAT2 value) {
    const float length = std::sqrt(value.x * value.x + value.y * value.y);
    if (length <= 0.0001f) {
        return {};
    }
    return {value.x / length, value.y / length};
}

} // namespace

void GameScene::Initialize(const SceneContext &ctx) {
    BaseScene::Initialize(ctx);

    if (ctx_->systems.texture != nullptr) {
        whiteTextureId_ = ctx_->systems.texture->GetWhiteTextureId();
    }

    obstacles_ = {
        Rect{{310.0f, 128.0f}, {70.0f, 310.0f}, {0.18f, 0.22f, 0.28f, 1.0f}},
        Rect{{520.0f, 432.0f}, {330.0f, 56.0f}, {0.18f, 0.22f, 0.28f, 1.0f}},
        Rect{{720.0f, 164.0f}, {64.0f, 220.0f}, {0.18f, 0.22f, 0.28f, 1.0f}},
        Rect{{930.0f, 286.0f}, {72.0f, 250.0f}, {0.18f, 0.22f, 0.28f, 1.0f}},
    };

    Reset();
}

void GameScene::Update() {
    if (ctx_ && ctx_->systems.input && ctx_->systems.winApp &&
        ctx_->systems.input->IsKeyTrigger(DIK_ESCAPE)) {
        ctx_->systems.winApp->RequestClose();
    }

    elapsedTime_ += ctx_ != nullptr ? ctx_->frame.deltaTime : 0.0f;

    if (ctx_ && ctx_->systems.input &&
        ctx_->systems.input->IsKeyTrigger(DIK_R)) {
        Reset();
    }

    UpdatePlayer(ctx_ != nullptr ? ctx_->frame.deltaTime : 0.0f);
}

void GameScene::Draw() {}

void GameScene::DrawPostProcessOverlay() {
    if (ctx_ == nullptr || ctx_->rendering.sprite == nullptr) {
        return;
    }

    SpriteManager *sprite = ctx_->rendering.sprite;
    sprite->PreDraw(true);

    const int width = ctx_->systems.winApp != nullptr
                          ? ctx_->systems.winApp->GetWidth()
                          : 1280;
    const int height = ctx_->systems.winApp != nullptr
                           ? ctx_->systems.winApp->GetHeight()
                           : 720;

    DrawRect({{0.0f, 0.0f},
              {static_cast<float>(width), static_cast<float>(height)},
              {0.05f, 0.07f, 0.09f, 1.0f}},
             0.0f);
    DrawRect({{64.0f, 64.0f},
              {static_cast<float>(width) - 128.0f,
               static_cast<float>(height) - 128.0f},
              {0.10f, 0.13f, 0.16f, 1.0f}},
             0.1f);

    const float goalPulse = 0.5f + 0.5f * std::sin(elapsedTime_ * 5.0f);
    DrawRect({{static_cast<float>(width) - 150.0f,
               static_cast<float>(height) * 0.5f - kGoalSize * 0.5f},
              {kGoalSize, kGoalSize},
              {0.15f, 0.58f + goalPulse * 0.20f, 0.38f, 1.0f}},
             0.2f);

    for (const Rect &obstacle : obstacles_) {
        DrawRect(obstacle, 0.3f);
    }

    const DirectX::XMFLOAT4 playerColor =
        goalReached_ ? DirectX::XMFLOAT4{1.0f, 0.86f, 0.25f, 1.0f}
                     : DirectX::XMFLOAT4{0.22f, 0.50f, 0.95f, 1.0f};
    DrawRect({playerPosition_, {kPlayerSize, kPlayerSize}, playerColor}, 0.4f);

#ifdef _DEBUG
    ImGui::SetNextWindowPos(ImVec2(16.0f, 16.0f), ImGuiCond_Once);
    ImGui::SetNextWindowSize(ImVec2(260.0f, 96.0f), ImGuiCond_Once);
    if (ImGui::Begin("Game Scene")) {
        ImGui::Text("Move: WASD / Arrow keys");
        ImGui::Text("Reset: R");
        ImGui::Text("Goal: %s", goalReached_ ? "Reached" : "Not reached");
    }
    ImGui::End();
#endif

    sprite->PostDraw();
}

void GameScene::DrawTransparent() {}

void GameScene::Reset() {
    playerPosition_ = {128.0f, 360.0f};
    velocity_ = {};
    elapsedTime_ = 0.0f;
    goalReached_ = false;
}

void GameScene::UpdatePlayer(float deltaTime) {
    if (ctx_ == nullptr || ctx_->systems.input == nullptr) {
        return;
    }

    Input *input = ctx_->systems.input;
    DirectX::XMFLOAT2 direction{};

    if (input->IsKeyPress(DIK_A) || input->IsKeyPress(DIK_LEFT)) {
        direction.x -= 1.0f;
    }
    if (input->IsKeyPress(DIK_D) || input->IsKeyPress(DIK_RIGHT)) {
        direction.x += 1.0f;
    }
    if (input->IsKeyPress(DIK_W) || input->IsKeyPress(DIK_UP)) {
        direction.y -= 1.0f;
    }
    if (input->IsKeyPress(DIK_S) || input->IsKeyPress(DIK_DOWN)) {
        direction.y += 1.0f;
    }

    direction = Normalize(direction);
    velocity_ = {direction.x * kPlayerSpeed, direction.y * kPlayerSpeed};

    const DirectX::XMFLOAT2 previous = playerPosition_;
    playerPosition_.x += velocity_.x * deltaTime;
    playerPosition_.y += velocity_.y * deltaTime;

    const int width = ctx_->systems.winApp != nullptr
                          ? ctx_->systems.winApp->GetWidth()
                          : 1280;
    const int height = ctx_->systems.winApp != nullptr
                           ? ctx_->systems.winApp->GetHeight()
                           : 720;

    playerPosition_.x =
        std::clamp(playerPosition_.x, 64.0f,
                   static_cast<float>(width) - 64.0f - kPlayerSize);
    playerPosition_.y =
        std::clamp(playerPosition_.y, 64.0f,
                   static_cast<float>(height) - 64.0f - kPlayerSize);

    const Rect player{playerPosition_, {kPlayerSize, kPlayerSize}, {}};
    for (const Rect &obstacle : obstacles_) {
        if (Intersects(player, obstacle)) {
            playerPosition_ = previous;
            break;
        }
    }

    const Rect goal{{static_cast<float>(width) - 150.0f,
                     static_cast<float>(height) * 0.5f - kGoalSize * 0.5f},
                    {kGoalSize, kGoalSize},
                    {}};
    goalReached_ = Intersects({playerPosition_, {kPlayerSize, kPlayerSize}, {}},
                              goal);
}

bool GameScene::Intersects(const Rect &a, const Rect &b) const {
    return a.position.x < b.position.x + b.size.x &&
           a.position.x + a.size.x > b.position.x &&
           a.position.y < b.position.y + b.size.y &&
           a.position.y + a.size.y > b.position.y;
}

void GameScene::DrawRect(const Rect &rect, float zOrder) {
    if (ctx_ == nullptr || ctx_->rendering.sprite == nullptr) {
        return;
    }

    Sprite sprite{};
    sprite.position = rect.position;
    sprite.size = rect.size;
    sprite.color = rect.color;
    sprite.textureId = whiteTextureId_;
    sprite.zOrder = zOrder;
    ctx_->rendering.sprite->DrawSprite(sprite);
}
