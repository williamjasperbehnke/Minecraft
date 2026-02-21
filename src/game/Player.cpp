#include "game/Player.hpp"

#include "game/Camera.hpp"
#include "world/World.hpp"

#include <glm/common.hpp>

#include <algorithm>

namespace game {
namespace {

constexpr float kHealthMax = 20.0f;
constexpr float kSprintStaminaMax = 4.0f;
constexpr float kSprintDrainPerSec = 0.60f;
constexpr float kSprintRegenPerSec = 0.70f;
constexpr float kSprintResumeThreshold = 1.0f;
constexpr float kFallDamageMinImpact = 10.0f;
constexpr float kFallDamagePerSpeed = 1.8f;

} // namespace

void Player::setFromCamera(const glm::vec3 &cameraPos, const world::World &world,
                           bool resolveIntersections) {
    controller_.setFromCamera(cameraPos, world, resolveIntersections);
}

void Player::update(GLFWwindow *window, const world::World &world, const Camera &camera, float dt,
                    bool inputEnabled) {
    if (sprintExhausted_) {
        sprintStamina_ = std::min(kSprintStaminaMax, sprintStamina_ + kSprintRegenPerSec * dt);
        if (sprintStamina_ >= kSprintResumeThreshold) {
            sprintExhausted_ = false;
        }
    }

    controller_.update(window, world, camera, dt, inputEnabled, !sprintExhausted_);

    if (controller_.sprinting()) {
        sprintStamina_ = std::max(0.0f, sprintStamina_ - kSprintDrainPerSec * dt);
        if (sprintStamina_ <= 0.0f) {
            sprintExhausted_ = true;
        }
    } else {
        sprintStamina_ = std::min(kSprintStaminaMax, sprintStamina_ + kSprintRegenPerSec * dt);
    }

    const float impact = controller_.consumeLandedImpactSpeed();
    if (impact > kFallDamageMinImpact) {
        const float impactExcess = impact - kFallDamageMinImpact;
        health_ = std::max(0.0f, health_ - impactExcess * kFallDamagePerSpeed);
    }
}

glm::vec3 Player::cameraPosition() const {
    return controller_.cameraPosition();
}

bool Player::grounded() const {
    return controller_.grounded();
}

bool Player::inWater() const {
    return controller_.inWater();
}

bool Player::sprinting() const {
    return controller_.sprinting() && !sprintExhausted_;
}

bool Player::crouching() const {
    return controller_.crouching();
}

float Player::health01() const {
    return glm::clamp(health_ / kHealthMax, 0.0f, 1.0f);
}

float Player::sprintStamina01() const {
    return glm::clamp(sprintStamina_ / kSprintStaminaMax, 0.0f, 1.0f);
}

} // namespace game
