#pragma once

#include "app/menus/BaseMenu.hpp"
#include "app/menus/UiMenuRenderer.hpp"

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

struct GLFWwindow;

namespace gfx {
class HudRenderer;
}

namespace app::menus {

struct WorldSelection {
    bool start = false;
    std::string name;
    std::filesystem::path path;
    std::uint32_t seed = 1337u;
};

class WorldSelectionMenu : public BaseMenu {
  public:
    const char *menuId() const override {
        return "world_selection";
    }

    WorldSelection run(GLFWwindow *window, gfx::HudRenderer &hud);
    void render(gfx::HudRenderer &hud, int width, int height,
                const std::vector<std::string> &worlds, int selectedWorld, bool createMode,
                bool editSeed, const std::string &createName, const std::string &createSeed,
                float cursorX, float cursorY) const;

  private:
    mutable UiMenuRenderer ui_;
};

} // namespace app::menus
