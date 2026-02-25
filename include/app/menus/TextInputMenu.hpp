#pragma once

#include "app/menus/BaseMenu.hpp"

#include <string>

struct GLFWwindow;

namespace app::menus {

class TextInputMenu : public BaseMenu {
  public:
    const char *menuId() const override {
        return "text_input";
    }

    static void bind(GLFWwindow *window, std::string *text);
    static void unbind(GLFWwindow *window);

  private:
    static void onCharInput(GLFWwindow *window, unsigned int codepoint);
};

} // namespace app::menus
