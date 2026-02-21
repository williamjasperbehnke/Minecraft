#pragma once

#include "game/PlayerController.hpp"

struct GLFWwindow;

namespace game {

class Camera;
}

namespace world {
class World;
}

namespace game {

class Player {
  public:
    void setFromCamera(const glm::vec3 &cameraPos, const world::World &world,
                       bool resolveIntersections = true);
    void update(GLFWwindow *window, const world::World &world, const Camera &camera, float dt,
                bool inputEnabled);

    glm::vec3 cameraPosition() const;
    bool grounded() const;
    bool inWater() const;
    bool sprinting() const;
    bool crouching() const;
    float health01() const;
    float sprintStamina01() const;

  private:
    PlayerController controller_;
    float health_ = 20.0f;
    float sprintStamina_ = 4.0f;
    bool sprintExhausted_ = false;
};

} // namespace game

