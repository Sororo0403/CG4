#include "physics/CharacterController.h"

#include <algorithm>

CharacterControllerState CharacterController::Move(
    const CharacterControllerState &state,
    const DirectX::XMFLOAT3 &desiredVelocity, bool wantsJump,
    float deltaTime) const {
    CharacterControllerState next = state;
    const float dt = (std::max)(deltaTime, 0.0f);

    next.velocity.x = desiredVelocity.x;
    next.velocity.z = desiredVelocity.z;
    if (state.grounded && wantsJump) {
        next.velocity.y = settings_.jumpSpeed;
        next.grounded = false;
    } else {
        next.velocity.y -= settings_.gravity * dt;
    }

    next.position.x += next.velocity.x * dt;
    next.position.y += next.velocity.y * dt;
    next.position.z += next.velocity.z * dt;

    if (groundHeightQuery_) {
        const float ground = groundHeightQuery_(next.position.x, next.position.z);
        const float bottom = next.position.y;
        const float snapLimit =
            state.grounded ? settings_.groundSnapDistance : settings_.stepHeight;
        if (bottom <= ground + snapLimit && next.velocity.y <= 0.0f) {
            next.position.y = ground;
            next.velocity.y = 0.0f;
            next.grounded = true;
        }
    }

    return next;
}
