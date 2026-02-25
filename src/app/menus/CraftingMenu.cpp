#include "app/menus/CraftingMenu.hpp"

#include "game/Camera.hpp"
#include "game/CraftingSystem.hpp"
#include "game/Inventory.hpp"
#include "voxel/Block.hpp"
#include "voxel/Raycaster.hpp"
#include "world/World.hpp"

#include <algorithm>
#include <limits>
#include <optional>

namespace app::menus {

int CraftingMenu::inventorySlotAtCursor(double mx, double my, int width, int height,
                                        bool showInventory, float hudScale,
                                        int craftingGridSize, bool usingFurnace) const {
    if (!showInventory) {
        return -1;
    }
    const float cx = width * 0.5f;
    const float uiScale = std::clamp(hudScale, 0.8f, 1.8f);
    const float slot = 48.0f * uiScale;
    const float gap = 10.0f * uiScale;
    const float totalW = static_cast<float>(game::Inventory::kHotbarSize) * slot +
                         static_cast<float>(game::Inventory::kHotbarSize - 1) * gap;
    const float x0 = cx - totalW * 0.5f;
    const float y0 = height - 90.0f * uiScale;

    int nearestSlot = -1;
    float nearestDist2 = std::numeric_limits<float>::max();
    auto considerSlot = [&](int idx, float sx, float sy, float size) {
        if (mx >= sx && mx <= sx + size && my >= sy && my <= sy + size) {
            nearestSlot = idx;
            nearestDist2 = 0.0f;
            return;
        }
        const float nx = static_cast<float>(
            std::clamp(mx, static_cast<double>(sx), static_cast<double>(sx + size)));
        const float ny = static_cast<float>(
            std::clamp(my, static_cast<double>(sy), static_cast<double>(sy + size)));
        const float dx = static_cast<float>(mx) - nx;
        const float dy = static_cast<float>(my) - ny;
        const float dist2 = dx * dx + dy * dy;
        if (dist2 < nearestDist2) {
            nearestDist2 = dist2;
            nearestSlot = idx;
        }
    };

    for (int i = 0; i < game::Inventory::kHotbarSize; ++i) {
        const float sx = x0 + i * (slot + gap);
        considerSlot(i, sx, y0, slot);
    }

    const int cols = game::Inventory::kColumns;
    const int rows = game::Inventory::kRows - 1;
    const float invSlot = 48.0f * uiScale;
    const float invGap = 10.0f * uiScale;
    const float invW = cols * invSlot + (cols - 1) * invGap;
    const float invH = rows * invSlot + (rows - 1) * invGap;
    const int craftGrid = std::clamp(craftingGridSize, game::CraftingSystem::kGridSizeInventory,
                                     game::CraftingSystem::kGridSizeTable);
    const float craftSlot = invSlot;
    const float craftGap = invGap;
    const float craftGridW =
        static_cast<float>(craftGrid) * craftSlot + static_cast<float>(craftGrid - 1) * craftGap;
    const float craftPanelGap = 26.0f * uiScale;
    const float craftPanelW = craftGridW + 44.0f * uiScale + craftSlot;
    const float totalPanelW = invW + craftPanelGap + craftPanelW;
    const float invX = cx - totalPanelW * 0.5f;
    const float invY = y0 - 44.0f * uiScale - invH;

    for (int row = 0; row < rows; ++row) {
        for (int col = 0; col < cols; ++col) {
            const float sx = invX + col * (invSlot + invGap);
            const float sy = invY + row * (invSlot + invGap);
            considerSlot(game::Inventory::kHotbarSize + row * cols + col, sx, sy, invSlot);
        }
    }

    const float craftX = invX + invW + craftPanelGap;
    const float trashGap = 10.0f * uiScale;
    const float craftRegionH = craftGridW + trashGap + craftSlot;
    const float craftY = usingFurnace
                             ? invY
                             : ((craftGrid == game::CraftingSystem::kGridSizeTable)
                                    ? invY
                                    : (invY + (invH - craftRegionH) * 0.5f));
    const float furnaceInXBase = craftX + 4.0f * uiScale;
    const float furnaceInYBase = craftY + 2.0f * uiScale;
    const float furnaceFuelYBase = furnaceInYBase + craftSlot + 20.0f * uiScale;
    const float furnaceMidX = furnaceInXBase + craftSlot + 16.0f * uiScale;
    const float furnaceOutXBase = furnaceMidX + 42.0f * uiScale;
    if (usingFurnace) {
        considerSlot(game::Inventory::kSlotCount, furnaceInXBase, furnaceInYBase, craftSlot);
        considerSlot(game::Inventory::kSlotCount + 1, furnaceInXBase, furnaceFuelYBase, craftSlot);
    } else {
        for (int r = 0; r < craftGrid; ++r) {
            for (int c = 0; c < craftGrid; ++c) {
                const int idx = game::Inventory::kSlotCount + r * craftGrid + c;
                const float sx = craftX + c * (craftSlot + craftGap);
                const float sy = craftY + r * (craftSlot + craftGap);
                considerSlot(idx, sx, sy, craftSlot);
            }
        }
    }
    const float outX = usingFurnace ? furnaceOutXBase : (craftX + craftGridW + 44.0f * uiScale);
    const float outY = usingFurnace ? furnaceInYBase : (craftY + (craftGridW - craftSlot) * 0.5f);
    considerSlot(game::CraftingSystem::kOutputSlotIndex, outX, outY, craftSlot);
    const float trashX = craftX - 10.0f * uiScale;
    const float invBorderBottomY = invY + invH + 10.0f * uiScale;
    const float trashY = invBorderBottomY - craftSlot - 2.0f;
    considerSlot(kTrashSlot, trashX, trashY, craftSlot);
    const float snapRadius = 9.0f * uiScale;
    if (nearestSlot >= 0 && nearestSlot < kUiSlotCountWithTrash &&
        nearestDist2 <= snapRadius * snapRadius) {
        return nearestSlot;
    }
    return -1;
}

void CraftingMenu::renderPanel(
    int width, int height, float uiScale, bool usingCraftingTable, int hoveredSlotIndex,
    int craftingGridSize,
    const std::array<game::Inventory::Slot, game::CraftingSystem::kInputCount> &craftInput,
    const game::Inventory::Slot &craftOutput, const voxel::BlockRegistry &registry,
    const HudDrawContext &draw, std::vector<SlotLabel> &slotLabels,
    const std::function<void(voxel::BlockId, int)> &setHoverTip) const {
    const float cx = width * 0.5f;
    const float y0 = height - 90.0f * uiScale;
    const int cols = game::Inventory::kColumns;
    const int rows = game::Inventory::kRows - 1;
    const float invSlot = 48.0f * uiScale;
    const float invGap = 10.0f * uiScale;
    const float invW = cols * invSlot + (cols - 1) * invGap;
    const float invH = rows * invSlot + (rows - 1) * invGap;
    const float invY = y0 - 44.0f * uiScale - invH;

    const int craftGrid = std::clamp(craftingGridSize, game::CraftingSystem::kGridSizeInventory,
                                     game::CraftingSystem::kGridSizeTable);
    const float craftSlot = invSlot;
    const float craftGap = invGap;
    const float craftGridW =
        static_cast<float>(craftGrid) * craftSlot + static_cast<float>(craftGrid - 1) * craftGap;
    const float craftPanelGap = 26.0f * uiScale;
    const float craftPanelW = craftGridW + 44.0f * uiScale + craftSlot;
    const float totalPanelW = invW + craftPanelGap + craftPanelW;
    const float invX = cx - totalPanelW * 0.5f;
    const float craftX = invX + invW + craftPanelGap;
    const float trashGap = 10.0f * uiScale;
    const float craftRegionH = craftGridW + trashGap + craftSlot;
    const float craftY = (craftGrid == game::CraftingSystem::kGridSizeTable)
                             ? invY
                             : (invY + (invH - craftRegionH) * 0.5f);
    const float craftOutX = craftX + craftGridW + 44.0f * uiScale;
    const float craftOutY = craftY + (craftGridW - craftSlot) * 0.5f;

    draw.drawRect(invX - 10.0f * uiScale, invY - 26.0f * uiScale, invW + 20.0f * uiScale,
                  invH + 36.0f * uiScale, 0.03f, 0.04f, 0.05f, 0.72f);
    draw.drawText(invX, invY - 18.0f * uiScale, "Inventory", 244, 246, 252, 255);
    const float craftBorderTopPad = (craftGrid == game::CraftingSystem::kGridSizeTable)
                                        ? 26.0f * uiScale
                                        : 28.0f * uiScale;
    const float craftBorderBottomPad = 8.0f * uiScale;
    const float craftContentH = craftGridW;
    draw.drawRect(craftX - 10.0f * uiScale, craftY - craftBorderTopPad,
                  (craftOutX + craftSlot) - craftX + 20.0f * uiScale,
                  craftContentH + craftBorderTopPad + craftBorderBottomPad, 0.03f, 0.04f, 0.05f,
                  0.72f);
    draw.drawText(craftX, craftY - 20.0f * uiScale,
                  usingCraftingTable ? "Crafting Table (3x3)" : "Crafting (2x2)", 244, 246, 252,
                  255);
    draw.drawText(craftX + craftGridW + 16.0f * uiScale, craftOutY + craftSlot * 0.45f, "->", 230,
                  236, 248, 255);

    for (int r = 0; r < craftGrid; ++r) {
        for (int c = 0; c < craftGrid; ++c) {
            const int craftIdx = r * craftGrid + c;
            const int uiIdx = game::Inventory::kSlotCount + craftIdx;
            const float sx = craftX + c * (craftSlot + craftGap);
            const float sy = craftY + r * (craftSlot + craftGap);
            draw.drawSlotFrame(sx, sy, craftSlot);
            const auto &slotData = craftInput[craftIdx];
            if (slotData.id != voxel::AIR && slotData.count > 0) {
                const voxel::BlockDef &def = registry.get(slotData.id);
                const float ix = sx + 8.0f * uiScale;
                const float iy = sy + 8.0f * uiScale;
                const float iw = craftSlot - 16.0f * uiScale;
                const float ih = craftSlot - 16.0f * uiScale;
                if (draw.isFlatItemId(slotData.id)) {
                    draw.appendItemIcon(ix, iy, ix + iw, iy + ih, slotData.id, 1.0f);
                } else {
                    const float cubeInset = 5.0f * uiScale;
                    draw.appendCubeIcon(sx + cubeInset, sy + cubeInset,
                                        craftSlot - 2.0f * cubeInset, craftSlot - 2.0f * cubeInset,
                                        def);
                }
                slotLabels.push_back(
                    SlotLabel{sx + 5.0f, sy + craftSlot - 13.0f, std::to_string(slotData.count)});
            }
            if (hoveredSlotIndex == uiIdx) {
                draw.drawHoverOutline(sx, sy, craftSlot);
                setHoverTip(slotData.id, slotData.count);
            }
        }
    }

    draw.drawSlotFrame(craftOutX, craftOutY, craftSlot);
    const int craftOutputIndex = game::Inventory::kSlotCount + static_cast<int>(craftInput.size());
    const int trashIndex = craftOutputIndex + 1;
    if (craftOutput.id != voxel::AIR && craftOutput.count > 0) {
        const voxel::BlockDef &def = registry.get(craftOutput.id);
        const float ix = craftOutX + 8.0f * uiScale;
        const float iy = craftOutY + 8.0f * uiScale;
        const float iw = craftSlot - 16.0f * uiScale;
        const float ih = craftSlot - 16.0f * uiScale;
        if (draw.isFlatItemId(craftOutput.id)) {
            draw.appendItemIcon(ix, iy, ix + iw, iy + ih, craftOutput.id, 1.0f);
        } else {
            const float cubeInset = 5.0f * uiScale;
            draw.appendCubeIcon(craftOutX + cubeInset, craftOutY + cubeInset,
                                craftSlot - 2.0f * cubeInset, craftSlot - 2.0f * cubeInset, def);
        }
        slotLabels.push_back(
            SlotLabel{craftOutX + 5.0f, craftOutY + craftSlot - 13.0f, std::to_string(craftOutput.count)});
    }
    if (hoveredSlotIndex == craftOutputIndex) {
        draw.drawHoverOutline(craftOutX, craftOutY, craftSlot);
        setHoverTip(craftOutput.id, craftOutput.count);
    }

    const float trashX = craftX - 10.0f * uiScale;
    const float invBorderBottomY = invY + invH + 10.0f * uiScale;
    const float trashY = invBorderBottomY - craftSlot - 2.0f;
    draw.drawRect(trashX - 2.0f, trashY - 2.0f, craftSlot + 4.0f, craftSlot + 4.0f, 0.60f, 0.22f,
                  0.22f, 0.78f);
    draw.drawSlotFrame(trashX, trashY, craftSlot);
    draw.drawRect(trashX + 8.0f * uiScale, trashY + 10.0f * uiScale, craftSlot - 16.0f * uiScale,
                  5.0f * uiScale, 0.55f, 0.18f, 0.18f, 0.95f);
    draw.drawRect(trashX + 14.0f * uiScale, trashY + 15.0f * uiScale, craftSlot - 28.0f * uiScale,
                  19.0f * uiScale, 0.78f, 0.24f, 0.24f, 0.95f);
    draw.drawRect(trashX + 18.0f * uiScale, trashY + 18.0f * uiScale, 2.0f * uiScale,
                  12.0f * uiScale, 0.92f, 0.82f, 0.82f, 0.85f);
    draw.drawRect(trashX + 23.0f * uiScale, trashY + 18.0f * uiScale, 2.0f * uiScale,
                  12.0f * uiScale, 0.92f, 0.82f, 0.82f, 0.85f);
    draw.drawRect(trashX + 28.0f * uiScale, trashY + 18.0f * uiScale, 2.0f * uiScale,
                  12.0f * uiScale, 0.92f, 0.82f, 0.82f, 0.85f);
    if (hoveredSlotIndex == trashIndex) {
        draw.drawHoverOutline(trashX, trashY, craftSlot);
    }
}

bool CraftingMenu::isLookingAtCraftingTable(world::World &world, const game::Camera &camera,
                                            float raycastDistance) const {
    const std::optional<voxel::RayHit> hit =
        voxel::Raycaster::cast(world, camera.position(), camera.forward(), raycastDistance);
    if (!hit.has_value()) {
        return false;
    }
    const voxel::BlockId id = world.getBlock(hit->block.x, hit->block.y, hit->block.z);
    return id == voxel::CRAFTING_TABLE;
}

} // namespace app::menus
