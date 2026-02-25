#pragma once

#include "app/menus/BaseMenu.hpp"
#include "app/menus/HudDrawContext.hpp"
#include "game/CraftingSystem.hpp"
#include "game/Inventory.hpp"

#include <array>
#include <functional>
#include <vector>

namespace game {
class Camera;
}

namespace world {
class World;
}

namespace app::menus {

class CraftingMenu : public BaseMenu {
  public:
    static constexpr int kCraftInputCount = game::CraftingSystem::kInputCount;
    static constexpr int kCraftOutputSlot = game::CraftingSystem::kOutputSlotIndex;
    static constexpr int kUiSlotCount = game::CraftingSystem::kUiSlotCount;
    static constexpr int kTrashSlot = kUiSlotCount;
    static constexpr int kUiSlotCountWithTrash = kTrashSlot + 1;

    const char *menuId() const override {
        return "crafting";
    }

    int inventorySlotAtCursor(double mx, double my, int width, int height, bool showInventory,
                              float hudScale, int craftingGridSize, bool usingFurnace) const;
    void renderPanel(
        int width, int height, float uiScale, bool usingCraftingTable, int hoveredSlotIndex,
        int craftingGridSize,
        const std::array<game::Inventory::Slot, game::CraftingSystem::kInputCount> &craftInput,
        const game::Inventory::Slot &craftOutput, const voxel::BlockRegistry &registry,
        const HudDrawContext &draw, std::vector<SlotLabel> &slotLabels,
        const std::function<void(voxel::BlockId, int)> &setHoverTip) const;
    bool isLookingAtCraftingTable(world::World &world, const game::Camera &camera,
                                  float raycastDistance) const;
};

} // namespace app::menus
