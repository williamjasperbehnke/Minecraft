#include "app/menus/FurnaceMenu.hpp"

#include "game/Camera.hpp"
#include "voxel/Block.hpp"
#include "voxel/Raycaster.hpp"
#include "world/World.hpp"

#include <algorithm>

namespace app::menus {

std::optional<glm::ivec3> FurnaceMenu::lookedAtCell(world::World &world,
                                                    const game::Camera &camera,
                                                    float raycastDistance) const {
    const std::optional<voxel::RayHit> hit =
        voxel::Raycaster::cast(world, camera.position(), camera.forward(), raycastDistance);
    if (!hit.has_value()) {
        return std::nullopt;
    }
    const voxel::BlockId id = world.getBlock(hit->block.x, hit->block.y, hit->block.z);
    if (!voxel::isFurnace(id)) {
        return std::nullopt;
    }
    return hit->block;
}

bool FurnaceMenu::isLookingAtFurnace(world::World &world, const game::Camera &camera,
                                     float raycastDistance) const {
    return lookedAtCell(world, camera, raycastDistance).has_value();
}

void FurnaceMenu::renderPanel(
    int width, int height, float uiScale, int hoveredSlotIndex, int craftingGridSize,
    const game::Inventory::Slot &smeltInput, const game::Inventory::Slot &smeltFuel,
    const game::Inventory::Slot &smeltOutput, float smeltProgress01, float smeltFuel01,
    const voxel::BlockRegistry &registry, const HudDrawContext &draw,
    std::vector<SlotLabel> &slotLabels,
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
    const float tableGridW = 3.0f * craftSlot + 2.0f * craftGap;
    const float tablePanelW = tableGridW + 44.0f * uiScale + craftSlot;
    const float furnaceCenterOffset = (tablePanelW - craftPanelW) * 0.35f;
    const float invX = cx - totalPanelW * 0.5f + furnaceCenterOffset;
    const float craftX = invX + invW + craftPanelGap;
    const float craftY = invY;
    const float furnaceInX = craftX + 4.0f * uiScale;
    const float furnaceInY = craftY + 2.0f * uiScale;
    const float furnaceFuelX = furnaceInX;
    const float furnaceFuelY = furnaceInY + craftSlot + 20.0f * uiScale;
    const float furnaceMidX = furnaceInX + craftSlot + 16.0f * uiScale;
    const float furnaceOutX = furnaceMidX + 42.0f * uiScale;
    const float craftOutY = furnaceInY;

    draw.drawRect(invX - 10.0f * uiScale, invY - 26.0f * uiScale, invW + 20.0f * uiScale,
                  invH + 36.0f * uiScale, 0.03f, 0.04f, 0.05f, 0.72f);
    draw.drawText(invX, invY - 18.0f * uiScale, "Inventory", 244, 246, 252, 255);
    const float craftBorderTopPad = 26.0f * uiScale;
    const float craftBorderBottomPad = 8.0f * uiScale;
    const float craftContentH = furnaceFuelY + craftSlot - craftY + 8.0f * uiScale;
    draw.drawRect(craftX - 10.0f * uiScale, craftY - craftBorderTopPad,
                  (furnaceOutX + craftSlot) - craftX + 20.0f * uiScale,
                  craftContentH + craftBorderTopPad + craftBorderBottomPad, 0.03f, 0.04f, 0.05f,
                  0.72f);
    draw.drawText(craftX, craftY - 20.0f * uiScale, "Furnace", 244, 246, 252, 255);

    draw.drawText(furnaceInX, furnaceInY - 11.0f * uiScale, "Input", 212, 222, 236, 255);
    draw.drawText(furnaceFuelX, furnaceFuelY - 11.0f * uiScale, "Fuel", 212, 222, 236, 255);
    draw.drawText(furnaceOutX, craftOutY - 11.0f * uiScale, "Output", 212, 222, 236, 255);

    const auto drawSlotContents = [&](float sx, float sy, const game::Inventory::Slot &slot) {
        draw.drawSlotFrame(sx, sy, craftSlot);
        if (slot.id == voxel::AIR || slot.count <= 0) {
            return;
        }
        const voxel::BlockDef &def = registry.get(slot.id);
        const float ix = sx + 8.0f * uiScale;
        const float iy = sy + 8.0f * uiScale;
        const float iw = craftSlot - 16.0f * uiScale;
        const float ih = craftSlot - 16.0f * uiScale;
        if (draw.isFlatItemId(slot.id)) {
            draw.appendItemIcon(ix, iy, ix + iw, iy + ih, slot.id, 1.0f);
        } else {
            const float cubeInset = 5.0f * uiScale;
            draw.appendCubeIcon(sx + cubeInset, sy + cubeInset, craftSlot - 2.0f * cubeInset,
                                craftSlot - 2.0f * cubeInset, def);
        }
        slotLabels.push_back(SlotLabel{sx + 5.0f, sy + craftSlot - 13.0f, std::to_string(slot.count)});
    };

    drawSlotContents(furnaceInX, furnaceInY, smeltInput);
    drawSlotContents(furnaceFuelX, furnaceFuelY, smeltFuel);
    drawSlotContents(furnaceOutX, craftOutY, smeltOutput);

    const int furnaceInputIndex = game::Inventory::kSlotCount;
    const int furnaceFuelIndex = game::Inventory::kSlotCount + 1;
    const int craftOutputIndex = game::Inventory::kSlotCount + game::CraftingSystem::kInputCount;
    const int trashIndex = craftOutputIndex + 1;
    if (hoveredSlotIndex == furnaceInputIndex) {
        draw.drawHoverOutline(furnaceInX, furnaceInY, craftSlot);
        setHoverTip(smeltInput.id, smeltInput.count);
    }
    if (hoveredSlotIndex == furnaceFuelIndex) {
        draw.drawHoverOutline(furnaceFuelX, furnaceFuelY, craftSlot);
        setHoverTip(smeltFuel.id, smeltFuel.count);
    }
    if (hoveredSlotIndex == craftOutputIndex) {
        draw.drawHoverOutline(furnaceOutX, craftOutY, craftSlot);
        setHoverTip(smeltOutput.id, smeltOutput.count);
    }

    const float inputCenterX = furnaceInX + craftSlot * 0.5f;
    const float outputCenterX = furnaceOutX + craftSlot * 0.5f;
    const float flowCenterX = (inputCenterX + outputCenterX) * 0.5f;
    const float progW = 38.0f * uiScale;
    const float progX = flowCenterX - progW * 0.5f;
    const float progY = furnaceInY + craftSlot * 0.44f;
    const float progH = 8.0f * uiScale;
    draw.drawText(progX + progW * 0.5f - draw.textWidthPx("Smelt") * 0.5f, progY - 10.0f * uiScale,
                  "Smelt", 186, 200, 220, 255);
    draw.drawRect(progX, progY, progW, progH, 0.10f, 0.11f, 0.13f, 0.95f);
    draw.drawRect(progX + 1.0f, progY + 1.0f, (progW - 2.0f) * std::clamp(smeltProgress01, 0.0f, 1.0f),
                  progH - 2.0f, 0.95f, 0.62f, 0.22f, 0.95f);

    const float burnX = furnaceMidX;
    const float burnY = furnaceFuelY + craftSlot * 0.50f;
    const float burnW = 30.0f * uiScale;
    const float burnH = 7.0f * uiScale;
    draw.drawText(burnX + burnW * 0.5f - draw.textWidthPx("Burn") * 0.5f, burnY - 10.0f * uiScale,
                  "Burn", 186, 200, 220, 255);
    draw.drawRect(burnX, burnY, burnW, burnH, 0.10f, 0.11f, 0.13f, 0.95f);
    draw.drawRect(burnX + 1.0f, burnY + 1.0f, (burnW - 2.0f) * std::clamp(smeltFuel01, 0.0f, 1.0f),
                  burnH - 2.0f, 0.98f, 0.46f, 0.20f, 0.95f);

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

} // namespace app::menus
