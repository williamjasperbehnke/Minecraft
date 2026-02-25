#pragma once

#include "app/menus/BaseMenu.hpp"
#include "app/menus/HudDrawContext.hpp"

#include "game/CraftingSystem.hpp"
#include "game/Inventory.hpp"

#include <array>
#include <functional>

#include <optional>
#include <vector>

#include <glm/vec3.hpp>

namespace game {
class Camera;
}

namespace world {
class World;
}

namespace app::menus {

class FurnaceMenu : public BaseMenu {
  public:
    const char *menuId() const override {
        return "furnace";
    }

    std::optional<glm::ivec3> lookedAtCell(world::World &world, const game::Camera &camera,
                                           float raycastDistance) const;
    void renderPanel(
        int width, int height, float uiScale, int hoveredSlotIndex, int craftingGridSize,
        const game::Inventory::Slot &smeltInput, const game::Inventory::Slot &smeltFuel,
        const game::Inventory::Slot &smeltOutput, float smeltProgress01, float smeltFuel01,
        const voxel::BlockRegistry &registry, const HudDrawContext &draw,
        std::vector<SlotLabel> &slotLabels,
        const std::function<void(voxel::BlockId, int)> &setHoverTip) const;
    bool isLookingAtFurnace(world::World &world, const game::Camera &camera,
                            float raycastDistance) const;
};

} // namespace app::menus
