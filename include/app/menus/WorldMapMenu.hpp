#pragma once

#include "app/menus/BaseMenu.hpp"
#include "app/menus/UiMenuRenderer.hpp"

#include "game/MapSystem.hpp"

#include <cstdint>
#include <string>

namespace gfx {
class HudRenderer;
class TextureAtlas;
}

namespace voxel {
class BlockRegistry;
}

namespace app::menus {

struct MapOverlayLayout {
    float panelX = 0.0f;
    float panelY = 0.0f;
    float panelW = 0.0f;
    float panelH = 0.0f;
    float gridX = 0.0f;
    float gridY = 0.0f;
    float gridW = 0.0f;
    float gridH = 0.0f;
    float cell = 4.0f;
    float chunkBtnX = 0.0f;
    float chunkBtnY = 0.0f;
    float chunkBtnW = 0.0f;
    float chunkBtnH = 0.0f;
};

struct WaypointEditorLayout {
    float panelX = 0.0f;
    float panelY = 0.0f;
    float panelW = 0.0f;
    float panelH = 0.0f;
    float nameX = 0.0f;
    float nameY = 0.0f;
    float nameW = 0.0f;
    float nameH = 0.0f;
    float colorX = 0.0f;
    float colorY = 0.0f;
    float colorS = 0.0f;
    float colorGap = 0.0f;
    float iconX = 0.0f;
    float iconY = 0.0f;
    float iconS = 0.0f;
    float iconGap = 0.0f;
    float closeX = 0.0f;
    float closeY = 0.0f;
    float closeS = 0.0f;
    float delX = 0.0f;
    float delY = 0.0f;
    float delW = 0.0f;
    float delH = 0.0f;
    float visibilityX = 0.0f;
    float visibilityY = 0.0f;
    float visibilityW = 0.0f;
    float visibilityH = 0.0f;
};

class WorldMapMenu : public BaseMenu {
  public:
    const char *menuId() const override {
        return "world_map";
    }

    MapOverlayLayout computeLayout(int width, int height, float zoom) const;
    WaypointEditorLayout computeWaypointEditorLayout(const MapOverlayLayout &mapLayout) const;

    void render(gfx::HudRenderer &hud, int width, int height, const game::MapSystem &map,
                int mapCenterWX, int mapCenterWZ, int playerWX, int playerWZ, float zoom,
                float cursorX, float cursorY, int selectedWaypoint,
                const std::string &waypointName, std::uint8_t waypointR,
                std::uint8_t waypointG, std::uint8_t waypointB, int waypointIcon,
                bool waypointVisible, float playerHeadingRad, bool waypointNameFocused,
                bool waypointEditorOpen, bool showChunkBorders,
                const voxel::BlockRegistry &registry,
                const gfx::TextureAtlas &atlas) const;

  private:
    mutable UiMenuRenderer ui_;
};

} // namespace app::menus
