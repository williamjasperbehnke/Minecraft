#pragma once

#include "app/menus/BaseMenu.hpp"
#include "app/menus/UiMenuRenderer.hpp"

namespace gfx {
class HudRenderer;
}

namespace app::menus {

class PauseMenu : public BaseMenu {
  public:
    const char *menuId() const override {
        return "pause";
    }

    int buttonAtCursor(double mx, double my, int width, int height) const;
    void render(gfx::HudRenderer &hud, int width, int height, float cursorX, float cursorY) const;

  private:
    mutable UiMenuRenderer ui_;
};

} // namespace app::menus
