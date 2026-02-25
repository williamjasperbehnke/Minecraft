#pragma once

#include "app/menus/BaseMenu.hpp"
#include "app/menus/UiMenuRenderer.hpp"

#include "game/MapSystem.hpp"

namespace gfx {
class HudRenderer;
class TextureAtlas;
}

namespace voxel {
class BlockRegistry;
}

namespace app::menus {

struct MiniMapLayout {
    float panelX = 0.0f;
    float panelY = 0.0f;
    float panelW = 0.0f;
    float panelH = 0.0f;
    float compassX = 0.0f;
    float compassY = 0.0f;
    float compassW = 0.0f;
    float compassH = 0.0f;
    float followX = 0.0f;
    float followY = 0.0f;
    float followW = 0.0f;
    float followH = 0.0f;
    float waypointX = 0.0f;
    float waypointY = 0.0f;
    float waypointW = 0.0f;
    float waypointH = 0.0f;
    float minusX = 0.0f;
    float minusY = 0.0f;
    float minusW = 0.0f;
    float minusH = 0.0f;
    float plusX = 0.0f;
    float plusY = 0.0f;
    float plusW = 0.0f;
    float plusH = 0.0f;
};

class MiniMapMenu : public BaseMenu {
  public:
    const char *menuId() const override {
        return "mini_map";
    }

    MiniMapLayout computeLayout(int width) const;

    void render(gfx::HudRenderer &hud, int width, int height, const game::MapSystem &map,
                int playerWX, int playerWZ, float zoom, bool northLocked, bool showCompass,
                bool showWaypoints, float headingRad, const voxel::BlockRegistry &registry,
                const gfx::TextureAtlas &atlas) const;

  private:
    mutable UiMenuRenderer ui_;
};

} // namespace app::menus
