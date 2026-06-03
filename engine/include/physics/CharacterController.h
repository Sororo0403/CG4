#pragma once

#include <DirectXMath.h>
#include <functional>

struct CharacterControllerSettings {
    float radius = 0.35f;
    float height = 1.8f;
    float stepHeight = 0.35f;
    float gravity = 18.0f;
    float jumpSpeed = 6.0f;
    float groundSnapDistance = 0.18f;
};

struct CharacterControllerState {
    DirectX::XMFLOAT3 position{0.0f, 0.0f, 0.0f};
    DirectX::XMFLOAT3 velocity{0.0f, 0.0f, 0.0f};
    bool grounded = false;
};

class CharacterController {
  public:
    using GroundHeightQuery = std::function<float(float x, float z)>;

    void SetSettings(const CharacterControllerSettings &settings) {
        settings_ = settings;
    }
    const CharacterControllerSettings &GetSettings() const { return settings_; }

    void SetGroundHeightQuery(GroundHeightQuery query) {
        groundHeightQuery_ = std::move(query);
    }

    CharacterControllerState Move(const CharacterControllerState &state,
                                  const DirectX::XMFLOAT3 &desiredVelocity,
                                  bool wantsJump, float deltaTime) const;

  private:
    CharacterControllerSettings settings_{};
    GroundHeightQuery groundHeightQuery_;
};
