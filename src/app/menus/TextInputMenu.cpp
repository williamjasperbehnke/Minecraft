#include "app/menus/TextInputMenu.hpp"

#include <GLFW/glfw3.h>

#include <cctype>

namespace app::menus {
namespace {

std::string *gTextInput = nullptr;

} // namespace

void TextInputMenu::bind(GLFWwindow *window, std::string *text) {
    gTextInput = text;
    glfwSetCharCallback(window, onCharInput);
}

void TextInputMenu::unbind(GLFWwindow *window) {
    gTextInput = nullptr;
    glfwSetCharCallback(window, nullptr);
}

void TextInputMenu::onCharInput(GLFWwindow * /*window*/, unsigned int codepoint) {
    if (gTextInput == nullptr || codepoint > 127u) {
        return;
    }
    const char ch = static_cast<char>(codepoint);
    if (std::isprint(static_cast<unsigned char>(ch)) && gTextInput->size() < 32) {
        gTextInput->push_back(ch);
    }
}

} // namespace app::menus
