#pragma once

#include <glm/vec3.hpp>

struct GLFWwindow;

namespace game {

class Camera;
}

namespace world {
class World;
}

namespace game {

class PlayerController {
  public:
    void setFromCamera(const glm::vec3 &cameraPos, const world::World &world,
                       bool resolveIntersections = true);
    void update(GLFWwindow *window, const world::World &world, const Camera &camera, float dt,
                bool inputEnabled, bool allowSprint = true);

    glm::vec3 cameraPosition() const;
    bool grounded() const {
        return grounded_;
    }
    bool inWater() const {
        return inWater_;
    }
    bool sprinting() const {
        return sprinting_;
    }
    bool crouching() const {
        return crouching_;
    }
    float consumeLandedImpactSpeed() {
        const float v = landedImpactSpeed_;
        landedImpactSpeed_ = 0.0f;
        return v;
    }

  private:
    bool intersectsSolid(const world::World &world, const glm::vec3 &feet) const;
    bool hasGroundSupport(const world::World &world, const glm::vec3 &feet) const;
    bool isWaterAt(const world::World &world, const glm::vec3 &pos) const;
    bool isOnIce(const world::World &world, const glm::vec3 &feet) const;
    void moveAxis(const world::World &world, int axis, float amount);

    glm::vec3 feetPos_{0.0f, 80.0f, 0.0f};
    glm::vec3 velocity_{0.0f};

    bool initialized_ = false;
    bool grounded_ = false;
    bool inWater_ = false;
    bool sprinting_ = false;
    bool crouching_ = false;
    float landedImpactSpeed_ = 0.0f;
};

} // namespace game
