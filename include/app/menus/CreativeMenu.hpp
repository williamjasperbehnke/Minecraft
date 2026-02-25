#pragma once

#include "app/menus/BaseMenu.hpp"
#include "app/menus/HudDrawContext.hpp"

#include "game/CraftingSystem.hpp"

#include "voxel/Block.hpp"

#include <string>
#include <vector>

namespace app::menus {

struct CreativeMenuLayout {
    float panelX = 0.0f;
    float panelY = 0.0f;
    float panelW = 0.0f;
    float panelH = 0.0f;
    float contentX = 0.0f;
    float contentY = 0.0f;
    float contentW = 0.0f;
    float contentH = 0.0f;
    float searchX = 0.0f;
    float searchY = 0.0f;
    float searchW = 0.0f;
    float searchH = 0.0f;
    int columns = 1;
    float cell = 0.0f;
    float cellGap = 0.0f;
    float gridInset = 0.0f;
    float rowStride = 0.0f;
    float maxScroll = 0.0f;
    float trackX = 0.0f;
    float trackY = 0.0f;
    float trackW = 0.0f;
    float trackH = 0.0f;
    float thumbH = 0.0f;
};

class CreativeMenu : public BaseMenu {
  public:
    const char *menuId() const override {
        return "creative";
    }

    const std::vector<voxel::BlockId> &catalog() const;
    bool itemMatchesSearch(voxel::BlockId id, const std::string &search) const;

    CreativeMenuLayout computeLayout(int width, int height, float hudScale, int craftingGridSize,
                                     bool usingFurnace, std::size_t itemCount) const;
    int itemAtCursor(double mx, double my, const CreativeMenuLayout &layout, float scroll,
                     std::size_t itemCount) const;
    void renderOverlay(int width, int height, float uiScale, int craftingGridSize,
                       bool usingFurnace, float cursorX, float cursorY, float creativeScroll,
                       float uiTimeSeconds, const std::string &creativeSearch,
                       const std::vector<voxel::BlockId> &creativeItems,
                       const voxel::BlockRegistry &registry,
                       const HudDrawContext &draw, TooltipState &tooltip) const;
};

} // namespace app::menus
