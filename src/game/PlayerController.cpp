#include "game/PlayerController.hpp"

#include "game/Camera.hpp"
#include "world/World.hpp"

#include <GLFW/glfw3.h>
#include <glm/common.hpp>
#include <glm/geometric.hpp>

#include <algorithm>
#include <cmath>

namespace game {
namespace {

constexpr float kHalfWidth = 0.30f;
constexpr float kHeight = 1.80f;
constexpr float kEyeOffset = 1.62f;
constexpr float kStep = 0.05f;
constexpr float kGroundSpeed = 7.0f;
constexpr float kAirSpeed = 6.0f;
constexpr float kSwimSpeed = 2.8f;
constexpr float kLavaSpeed = 2.0f;
constexpr float kGroundAccel = 40.0f;
constexpr float kAirAccel = 14.0f;
constexpr float kSwimAccel = 10.0f;
constexpr float kLavaAccel = 9.0f;
constexpr float kIceAccel = 8.0f;
constexpr float kGravity = 26.0f;
constexpr float kJumpSpeed = 8.4f;
constexpr float kSwimUpAccel = 18.0f;
constexpr float kSwimDownAccel = 22.0f;
constexpr float kSwimDrag = 4.4f;
constexpr float kLavaUpAccel = 14.0f;
constexpr float kLavaDownAccel = 18.0f;
constexpr float kLavaDrag = 5.5f;
constexpr float kCrouchViewDrop = 0.22f;
constexpr float kViewOffsetLerp = 12.0f;
constexpr float kSprintMultiplier = 1.60f;
constexpr float kCrouchMultiplier = 0.45f;
constexpr float kGroundFrictionMove = 3.1f;
constexpr float kGroundFrictionIdle = 9.5f;
constexpr float kIceFriction = 0.55f;
constexpr float kAirDrag = 0.2f;
constexpr float kEps = 0.001f;

float approach(float current, float target, float maxDelta) {
    if (current < target) {
        return std::min(current + maxDelta, target);
    }
    return std::max(current - maxDelta, target);
}

} // namespace

void PlayerController::setFromCamera(const glm::vec3 &cameraPos, const world::World &world,
                                     bool resolveIntersections) {
    feetPos_ = cameraPos - glm::vec3(0.0f, kEyeOffset, 0.0f);
    velocity_ = glm::vec3(0.0f);
    grounded_ = false;
    inWater_ = false;
    inLava_ = false;
    sprinting_ = false;
    crouching_ = false;
    landedImpactSpeed_ = 0.0f;
    crouchViewOffset_ = 0.0f;
    initialized_ = true;

    if (resolveIntersections) {
        for (int i = 0; i < 8 && intersectsSolid(world, feetPos_); ++i) {
            feetPos_.y += 1.0f;
        }
    }
}

glm::vec3 PlayerController::cameraPosition() const {
    return feetPos_ + glm::vec3(0.0f, kEyeOffset + crouchViewOffset_, 0.0f);
}

bool PlayerController::isWaterAt(const world::World &world, const glm::vec3 &pos) const {
    const int wx = static_cast<int>(std::floor(pos.x));
    const int wy = static_cast<int>(std::floor(pos.y));
    const int wz = static_cast<int>(std::floor(pos.z));
    return voxel::isWaterLike(world.getBlock(wx, wy, wz));
}

bool PlayerController::isLavaAt(const world::World &world, const glm::vec3 &pos) const {
    const int wx = static_cast<int>(std::floor(pos.x));
    const int wy = static_cast<int>(std::floor(pos.y));
    const int wz = static_cast<int>(std::floor(pos.z));
    return voxel::isLavaLike(world.getBlock(wx, wy, wz));
}

bool PlayerController::isOnIce(const world::World &world, const glm::vec3 &feet) const {
    const float y = feet.y - 0.08f;
    const glm::vec3 samples[5] = {
        glm::vec3(feet.x, y, feet.z),
        glm::vec3(feet.x - kHalfWidth * 0.7f, y, feet.z - kHalfWidth * 0.7f),
        glm::vec3(feet.x + kHalfWidth * 0.7f, y, feet.z - kHalfWidth * 0.7f),
        glm::vec3(feet.x - kHalfWidth * 0.7f, y, feet.z + kHalfWidth * 0.7f),
        glm::vec3(feet.x + kHalfWidth * 0.7f, y, feet.z + kHalfWidth * 0.7f),
    };
    for (const glm::vec3 &p : samples) {
        const int wx = static_cast<int>(std::floor(p.x));
        const int wy = static_cast<int>(std::floor(p.y));
        const int wz = static_cast<int>(std::floor(p.z));
        if (world.getBlock(wx, wy, wz) == voxel::ICE) {
            return true;
        }
    }
    return false;
}

bool PlayerController::intersectsSolid(const world::World &world, const glm::vec3 &feet) const {
    const glm::vec3 aabbMin = feet + glm::vec3(-kHalfWidth, 0.0f, -kHalfWidth);
    const glm::vec3 aabbMax = feet + glm::vec3(kHalfWidth, kHeight, kHalfWidth);

    const int minX = static_cast<int>(std::floor(aabbMin.x));
    const int minY = static_cast<int>(std::floor(aabbMin.y));
    const int minZ = static_cast<int>(std::floor(aabbMin.z));
    const int maxX = static_cast<int>(std::floor(aabbMax.x - kEps));
    const int maxY = static_cast<int>(std::floor(aabbMax.y - kEps));
    const int maxZ = static_cast<int>(std::floor(aabbMax.z - kEps));

    for (int y = minY; y <= maxY; ++y) {
        for (int z = minZ; z <= maxZ; ++z) {
            for (int x = minX; x <= maxX; ++x) {
                if (world.isSolidBlock(x, y, z)) {
                    return true;
                }
            }
        }
    }
    return false;
}

bool PlayerController::hasGroundSupport(const world::World &world, const glm::vec3 &feet) const {
    const float probeY = feet.y - 0.08f;
    const float s = kHalfWidth - 0.02f;
    const glm::vec3 probes[5] = {
        glm::vec3(feet.x, probeY, feet.z),
        glm::vec3(feet.x - s, probeY, feet.z - s),
        glm::vec3(feet.x + s, probeY, feet.z - s),
        glm::vec3(feet.x - s, probeY, feet.z + s),
        glm::vec3(feet.x + s, probeY, feet.z + s),
    };
    for (const glm::vec3 &p : probes) {
        const int wx = static_cast<int>(std::floor(p.x));
        const int wy = static_cast<int>(std::floor(p.y));
        const int wz = static_cast<int>(std::floor(p.z));
        if (world.isSolidBlock(wx, wy, wz)) {
            return true;
        }
    }
    return false;
}

void PlayerController::moveAxis(const world::World &world, int axis, float amount) {
    if (amount == 0.0f) {
        return;
    }
    const int steps = std::max(1, static_cast<int>(std::ceil(std::abs(amount) / kStep)));
    const float step = amount / static_cast<float>(steps);

    for (int i = 0; i < steps; ++i) {
        feetPos_[axis] += step;
        if (intersectsSolid(world, feetPos_)) {
            feetPos_[axis] -= step;
            if (axis == 1 && step < 0.0f) {
                grounded_ = true;
            }
            velocity_[axis] = 0.0f;
            break;
        }
        if (axis != 1 && crouching_ && grounded_ && !inWater_ && !inLava_ &&
            !hasGroundSupport(world, feetPos_)) {
            feetPos_[axis] -= step;
            velocity_[axis] = 0.0f;
            break;
        }
    }
}

void PlayerController::update(GLFWwindow *window, const world::World &world, const Camera &camera,
                              float dt, bool inputEnabled, bool allowSprint) {
    if (!initialized_) {
        setFromCamera(camera.position(), world);
    }
    grounded_ = intersectsSolid(world, feetPos_ + glm::vec3(0.0f, -0.05f, 0.0f));
    if (grounded_ && velocity_.y < 0.0f) {
        velocity_.y = 0.0f;
    }

    glm::vec3 forward = camera.forward();
    forward.y = 0.0f;
    if (glm::dot(forward, forward) < 1e-6f) {
        forward = glm::vec3(0.0f, 0.0f, -1.0f);
    } else {
        forward = glm::normalize(forward);
    }
    const glm::vec3 right = glm::normalize(glm::cross(forward, glm::vec3(0.0f, 1.0f, 0.0f)));

    glm::vec3 wish(0.0f);
    bool jumpDown = false;
    bool sprint = false;
    bool crouch = false;
    bool shiftDown = false;
    bool descend = false;
    if (inputEnabled) {
        if (glfwGetKey(window, GLFW_KEY_W) == GLFW_PRESS)
            wish += forward;
        if (glfwGetKey(window, GLFW_KEY_S) == GLFW_PRESS)
            wish -= forward;
        if (glfwGetKey(window, GLFW_KEY_D) == GLFW_PRESS)
            wish += right;
        if (glfwGetKey(window, GLFW_KEY_A) == GLFW_PRESS)
            wish -= right;
        jumpDown = glfwGetKey(window, GLFW_KEY_SPACE) == GLFW_PRESS;
        shiftDown = (glfwGetKey(window, GLFW_KEY_LEFT_SHIFT) == GLFW_PRESS) ||
                    (glfwGetKey(window, GLFW_KEY_RIGHT_SHIFT) == GLFW_PRESS);
        sprint = shiftDown;
        crouch = (glfwGetKey(window, GLFW_KEY_LEFT_CONTROL) == GLFW_PRESS) ||
                 (glfwGetKey(window, GLFW_KEY_RIGHT_CONTROL) == GLFW_PRESS);
    }

    if (glm::dot(wish, wish) > 1e-6f) {
        wish = glm::normalize(wish);
    }
    const bool hasWish = glm::dot(wish, wish) > 1e-6f;

    inWater_ = isWaterAt(world, feetPos_ + glm::vec3(0.0f, 0.10f, 0.0f)) ||
               isWaterAt(world, feetPos_ + glm::vec3(0.0f, 0.90f, 0.0f)) ||
               isWaterAt(world, feetPos_ + glm::vec3(0.0f, 1.45f, 0.0f));
    inLava_ = isLavaAt(world, feetPos_ + glm::vec3(0.0f, 0.10f, 0.0f)) ||
              isLavaAt(world, feetPos_ + glm::vec3(0.0f, 0.90f, 0.0f)) ||
              isLavaAt(world, feetPos_ + glm::vec3(0.0f, 1.45f, 0.0f));
    const bool inFluid = inWater_ || inLava_;
    // Shift should sink in water; crouch key still descends in any fluid.
    descend = crouch || (shiftDown && inWater_);
    const glm::vec3 fluidProbeA = feetPos_ + glm::vec3(0.0f, 0.20f, 0.0f);
    const glm::vec3 fluidProbeB = feetPos_ + glm::vec3(0.0f, 0.95f, 0.0f);
    const glm::vec3 fluidProbeC = feetPos_ + glm::vec3(0.0f, 1.55f, 0.0f);
    const glm::vec3 fluidFlow =
        (world.fluidCurrentAt(fluidProbeA) + world.fluidCurrentAt(fluidProbeB) +
         world.fluidCurrentAt(fluidProbeC)) /
        3.0f;
    const bool onIce = grounded_ && !inFluid && isOnIce(world, feetPos_);

    if (crouch) {
        sprint = false;
    }
    crouching_ = crouch;
    sprinting_ = allowSprint && sprint && !inFluid && hasWish;
    const float moveModifier = (!inFluid && sprinting_) ? kSprintMultiplier
                              : (!inFluid && crouch) ? kCrouchMultiplier
                                                      : 1.0f;
    const float maxSpeed =
        (inLava_ ? kLavaSpeed : (inWater_ ? kSwimSpeed : (grounded_ ? kGroundSpeed : kAirSpeed))) *
        moveModifier;
    const float crouchTargetOffset = (crouching_ && grounded_ && !inFluid) ? -kCrouchViewDrop : 0.0f;
    crouchViewOffset_ =
        approach(crouchViewOffset_, crouchTargetOffset, kViewOffsetLerp * dt);
    const float accel = inLava_ ? kLavaAccel
                       : (inWater_ ? kSwimAccel
                                   : (grounded_ ? (onIce ? kIceAccel : kGroundAccel) : kAirAccel));
    const float targetX = wish.x * maxSpeed;
    const float targetZ = wish.z * maxSpeed;
    velocity_.x = approach(velocity_.x, targetX, accel * dt);
    velocity_.z = approach(velocity_.z, targetZ, accel * dt);

    if (!inFluid) {
        const float friction =
            grounded_
                ? (onIce ? kIceFriction : (hasWish ? kGroundFrictionMove : kGroundFrictionIdle))
                : kAirDrag;
        const float damp = std::max(0.0f, 1.0f - friction * dt);
        velocity_.x *= damp;
        velocity_.z *= damp;
    }

    // Flowing water/lava can push hitbox entities.
    constexpr float kPlayerFluidPush = 1.6f;
    velocity_.x += fluidFlow.x * dt * kPlayerFluidPush;
    velocity_.z += fluidFlow.z * dt * kPlayerFluidPush;
    velocity_.y += fluidFlow.y * dt * 0.55f * kPlayerFluidPush;

    if (inWater_) {
        // Water movement: weaker gravity, buoyancy-like control, and drag.
        velocity_.y -= 4.0f * dt;
        if (jumpDown) {
            velocity_.y += kSwimUpAccel * dt;
        }
        if (descend) {
            velocity_.y -= kSwimDownAccel * dt;
        }
        const float drag = std::max(0.0f, 1.0f - kSwimDrag * dt);
        velocity_ *= drag;
        velocity_.y = glm::clamp(velocity_.y, -3.8f, 4.6f);
    } else if (inLava_) {
        // Lava movement: very heavy drag and weak vertical control.
        velocity_.y -= 5.0f * dt;
        if (jumpDown) {
            velocity_.y += kLavaUpAccel * dt;
        }
        if (descend) {
            velocity_.y -= kLavaDownAccel * dt;
        }
        const float drag = std::max(0.0f, 1.0f - kLavaDrag * dt);
        velocity_ *= drag;
        velocity_.y = glm::clamp(velocity_.y, -3.0f, 3.2f);
    } else {
        // Hold-space auto-jump: if space is still held when we touch ground, jump
        // again.
        if (jumpDown && grounded_) {
            velocity_.y = kJumpSpeed;
            grounded_ = false;
        }
        velocity_.y -= kGravity * dt;
        velocity_.y = std::max(velocity_.y, -70.0f);
    }
    const bool wasGrounded = grounded_;
    const float preVerticalVelocity = velocity_.y;
    moveAxis(world, 0, velocity_.x * dt);
    grounded_ = false; // Recomputed by vertical collision resolution below.
    moveAxis(world, 1, velocity_.y * dt);
    moveAxis(world, 2, velocity_.z * dt);

    landedImpactSpeed_ = 0.0f;
    if (!inFluid && !wasGrounded && grounded_ && preVerticalVelocity < 0.0f) {
        landedImpactSpeed_ = -preVerticalVelocity;
    }
}

} // namespace game
