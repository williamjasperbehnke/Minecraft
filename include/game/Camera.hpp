#pragma once

#include <glm/mat4x4.hpp>
#include <glm/vec3.hpp>

struct GLFWwindow;

namespace game {

class Camera {
  public:
    explicit Camera(glm::vec3 position);

    void handleKeyboard(GLFWwindow *window, float dt);
    void handleMouse(GLFWwindow *window);
    void resetMouseLook(GLFWwindow *window);

    glm::mat4 view() const;
    glm::vec3 position() const {
        return position_;
    }
    void setPosition(const glm::vec3 &position) {
        position_ = position;
    }
    glm::vec3 forward() const;

    float moveSpeed() const {
        return speed_;
    }
    void setMoveSpeed(float speed) {
        speed_ = speed;
    }

    float mouseSensitivity() const {
        return sensitivity_;
    }
    void setMouseSensitivity(float sensitivity) {
        sensitivity_ = sensitivity;
    }

  private:
    glm::vec3 position_;
    float yaw_ = -90.0f;
    float pitch_ = 0.0f;
    float speed_ = 15.0f;
    float sensitivity_ = 0.1f;

    bool firstMouse_ = true;
    double lastX_ = 0.0;
    double lastY_ = 0.0;
};

} // namespace game
