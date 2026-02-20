#include "game/Camera.hpp"

#include <GLFW/glfw3.h>
#include <glm/ext/matrix_transform.hpp>
#include <glm/ext/scalar_constants.hpp>

namespace game {

Camera::Camera(glm::vec3 position) : position_(position) {}

glm::vec3 Camera::forward() const {
    const float yawR = glm::radians(yaw_);
    const float pitchR = glm::radians(pitch_);
    glm::vec3 dir;
    dir.x = cosf(yawR) * cosf(pitchR);
    dir.y = sinf(pitchR);
    dir.z = sinf(yawR) * cosf(pitchR);
    return glm::normalize(dir);
}

glm::mat4 Camera::view() const {
    return glm::lookAt(position_, position_ + forward(), glm::vec3(0.0f, 1.0f, 0.0f));
}

void Camera::handleKeyboard(GLFWwindow *window, float dt) {
    const glm::vec3 fwd = forward();
    const glm::vec3 right = glm::normalize(glm::cross(fwd, glm::vec3(0.0f, 1.0f, 0.0f)));

    const float velocity = speed_ * dt;
    if (glfwGetKey(window, GLFW_KEY_W) == GLFW_PRESS) {
        position_ += fwd * velocity;
    }
    if (glfwGetKey(window, GLFW_KEY_S) == GLFW_PRESS) {
        position_ -= fwd * velocity;
    }
    if (glfwGetKey(window, GLFW_KEY_A) == GLFW_PRESS) {
        position_ -= right * velocity;
    }
    if (glfwGetKey(window, GLFW_KEY_D) == GLFW_PRESS) {
        position_ += right * velocity;
    }
    if (glfwGetKey(window, GLFW_KEY_SPACE) == GLFW_PRESS) {
        position_.y += velocity;
    }
    if (glfwGetKey(window, GLFW_KEY_LEFT_SHIFT) == GLFW_PRESS) {
        position_.y -= velocity;
    }
}

void Camera::handleMouse(GLFWwindow *window) {
    double x = 0.0;
    double y = 0.0;
    glfwGetCursorPos(window, &x, &y);

    if (firstMouse_) {
        firstMouse_ = false;
        lastX_ = x;
        lastY_ = y;
    }

    const float dx = static_cast<float>(x - lastX_);
    const float dy = static_cast<float>(lastY_ - y);
    lastX_ = x;
    lastY_ = y;

    yaw_ += dx * sensitivity_;
    pitch_ += dy * sensitivity_;

    if (pitch_ > 89.0f) {
        pitch_ = 89.0f;
    }
    if (pitch_ < -89.0f) {
        pitch_ = -89.0f;
    }
}

void Camera::resetMouseLook(GLFWwindow *window) {
    double x = 0.0;
    double y = 0.0;
    glfwGetCursorPos(window, &x, &y);
    lastX_ = x;
    lastY_ = y;
    firstMouse_ = true;
}

} // namespace game
