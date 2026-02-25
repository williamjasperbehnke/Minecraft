#include "gfx/HudRenderer.hpp"

#include "app/menus/CraftingMenu.hpp"
#include "app/menus/CreativeMenu.hpp"
#include "app/menus/FurnaceMenu.hpp"
#include "app/menus/HudDrawContext.hpp"
#include "app/menus/RecipeMenu.hpp"

#include <stb_easy_font.h>

#include <algorithm>
#include <cmath>

namespace gfx {
namespace {

float textWidthPxLocal(const std::string &text) {
    return static_cast<float>(stb_easy_font_width(const_cast<char *>(text.c_str())));
}

} // namespace

void HudRenderer::appendItemIconVerts(float x0, float y0, float x1, float y1, const glm::vec4 &uv,
                                      float light) {
    iconVerts_.push_back({x0, y0, uv.x, uv.y, light});
    iconVerts_.push_back({x1, y0, uv.z, uv.y, light});
    iconVerts_.push_back({x1, y1, uv.z, uv.w, light});
    iconVerts_.push_back({x0, y0, uv.x, uv.y, light});
    iconVerts_.push_back({x1, y1, uv.z, uv.w, light});
    iconVerts_.push_back({x0, y1, uv.x, uv.w, light});
}

void HudRenderer::appendSkewQuadVerts(const glm::vec2 &a, const glm::vec2 &b, const glm::vec2 &c,
                                      const glm::vec2 &d, const glm::vec4 &uv, float light) {
    iconVerts_.push_back({a.x, a.y, uv.x, uv.y, light});
    iconVerts_.push_back({b.x, b.y, uv.z, uv.y, light});
    iconVerts_.push_back({c.x, c.y, uv.z, uv.w, light});
    iconVerts_.push_back({a.x, a.y, uv.x, uv.y, light});
    iconVerts_.push_back({c.x, c.y, uv.z, uv.w, light});
    iconVerts_.push_back({d.x, d.y, uv.x, uv.w, light});
}

void HudRenderer::appendCubeIconVerts(float x, float y, float w, float h, const voxel::BlockDef &def,
                                      const TextureAtlas &atlas) {
    const glm::vec4 uvTop = atlas.uvRect(def.topTile);
    const glm::vec4 uvSide = atlas.uvRect(def.sideTile);
    const bool furnaceLike = voxel::isFurnaceDef(def);
    const glm::vec4 uvFront = furnaceLike ? atlas.uvRect(voxel::TILE_FURNACE_FRONT) : uvSide;
    const glm::vec2 t0{x + w * 0.50f, y + h * 0.10f};
    const glm::vec2 t1{x + w * 0.84f, y + h * 0.29f};
    const glm::vec2 t2{x + w * 0.50f, y + h * 0.49f};
    const glm::vec2 t3{x + w * 0.16f, y + h * 0.29f};
    const glm::vec2 bL{x + w * 0.16f, y + h * 0.71f};
    const glm::vec2 bR{x + w * 0.84f, y + h * 0.71f};
    const glm::vec2 bC{x + w * 0.50f, y + h * 0.90f};

    appendSkewQuadVerts(t3, t2, bC, bL, uvFront, 0.74f);
    appendSkewQuadVerts(t2, t1, bR, bC, uvSide, 0.90f);
    appendSkewQuadVerts(t0, t1, t2, t3, uvTop, 1.00f);
}

bool HudRenderer::isFlatItemId(voxel::BlockId id) const {
    return voxel::isPlant(id) || id == voxel::STICK ||
           id == voxel::IRON_INGOT || id == voxel::COPPER_INGOT || id == voxel::GOLD_INGOT ||
           voxel::isTorch(id);
}

void HudRenderer::drawSelectedOutline(float x, float y, float size) {
    drawRect(x - 3.0f, y - 3.0f, size + 6.0f, 4.0f, 1.0f, 0.90f, 0.30f, 1.0f);
    drawRect(x - 3.0f, y + size - 1.0f, size + 6.0f, 4.0f, 1.0f, 0.90f, 0.30f, 1.0f);
    drawRect(x - 3.0f, y + 1.0f, 4.0f, size - 2.0f, 1.0f, 0.90f, 0.30f, 1.0f);
    drawRect(x + size - 1.0f, y + 1.0f, 4.0f, size - 2.0f, 1.0f, 0.90f, 0.30f, 1.0f);
}

void HudRenderer::drawHoverOutline(float x, float y, float size) {
    drawRect(x - 2.0f, y - 2.0f, size + 4.0f, 3.0f, 0.36f, 0.78f, 1.0f, 0.95f);
    drawRect(x - 2.0f, y + size - 1.0f, size + 4.0f, 3.0f, 0.36f, 0.78f, 1.0f, 0.95f);
    drawRect(x - 2.0f, y + 1.0f, 3.0f, size - 2.0f, 0.36f, 0.78f, 1.0f, 0.95f);
    drawRect(x + size - 1.0f, y + 1.0f, 3.0f, size - 2.0f, 0.36f, 0.78f, 1.0f, 0.95f);
}

void HudRenderer::drawValidHintOutline(float x, float y, float size) {
    drawRect(x - 1.0f, y - 1.0f, size + 2.0f, 2.0f, 0.38f, 0.92f, 0.48f, 0.88f);
    drawRect(x - 1.0f, y + size - 1.0f, size + 2.0f, 2.0f, 0.38f, 0.92f, 0.48f, 0.88f);
    drawRect(x - 1.0f, y + 1.0f, 2.0f, size - 2.0f, 0.38f, 0.92f, 0.48f, 0.88f);
    drawRect(x + size - 1.0f, y + 1.0f, 2.0f, size - 2.0f, 0.38f, 0.92f, 0.48f, 0.88f);
}

void HudRenderer::renderCrosshair2D(float cx, float cy, float uiScale, bool showInventory) {
    if (showInventory) {
        return;
    }
    const float kArmGap = 3.0f * uiScale;
    const float kArmLen = 6.0f * uiScale;
    const float kArmThick = 2.0f * uiScale;
    drawRect(cx - 1.5f, cy - (kArmGap + kArmLen + 0.5f), 3.0f, kArmLen + 1.0f, 0.02f, 0.02f, 0.02f,
             0.72f);
    drawRect(cx - 1.5f, cy + kArmGap - 0.5f, 3.0f, kArmLen + 1.0f, 0.02f, 0.02f, 0.02f, 0.72f);
    drawRect(cx - (kArmGap + kArmLen + 0.5f), cy - 1.5f, kArmLen + 1.0f, 3.0f, 0.02f, 0.02f, 0.02f,
             0.72f);
    drawRect(cx + kArmGap - 0.5f, cy - 1.5f, kArmLen + 1.0f, 3.0f, 0.02f, 0.02f, 0.02f, 0.72f);
    drawRect(cx - 2.0f, cy - 2.0f, 4.0f, 4.0f, 0.02f, 0.02f, 0.02f, 0.72f);
    drawRect(cx - (kArmThick * 0.5f), cy - (kArmGap + kArmLen), kArmThick, kArmLen, 0.95f, 0.95f,
             0.95f, 0.96f);
    drawRect(cx - (kArmThick * 0.5f), cy + kArmGap, kArmThick, kArmLen, 0.95f, 0.95f, 0.95f, 0.96f);
    drawRect(cx - (kArmGap + kArmLen), cy - (kArmThick * 0.5f), kArmLen, kArmThick, 0.95f, 0.95f,
             0.95f, 0.96f);
    drawRect(cx + kArmGap, cy - (kArmThick * 0.5f), kArmLen, kArmThick, 0.95f, 0.95f, 0.95f, 0.96f);
    drawRect(cx - 1.0f, cy - 1.0f, 2.0f, 2.0f, 1.0f, 1.0f, 1.0f, 0.98f);
}

void HudRenderer::renderHotbar2D(
    int height, float cx, float uiScale, bool showInventory, int selectedIndex, int hoveredSlotIndex,
    bool hintFurnaceInput, bool hintFurnaceFuel, const game::SmeltingSystem &smeltRules,
    const std::array<voxel::BlockId, game::Inventory::kHotbarSize> &hotbar,
    const std::array<int, game::Inventory::kHotbarSize> &hotbarCounts,
    const voxel::BlockRegistry &registry, const TextureAtlas &atlas, float health01,
    float sprintStamina01, const std::function<void(voxel::BlockId, int)> &setHoverTip,
    std::vector<app::menus::SlotLabel> &slotLabels) {
    const float slot = 48.0f * uiScale;
    const float gap = 10.0f * uiScale;
    const float totalW = static_cast<float>(hotbar.size()) * slot +
                         static_cast<float>(hotbar.size() - 1) * gap;
    const float x0 = cx - totalW * 0.5f;
    const float y0 = height - 90.0f * uiScale;
    const float staminaFill = std::clamp(sprintStamina01, 0.0f, 1.0f);
    const float healthFill = std::clamp(health01, 0.0f, 1.0f);
    const float centerGap = 6.0f * uiScale;
    const float sideBarW = (totalW - centerGap) * 0.5f;
    const float staminaBarH = 8.0f * uiScale;
    const float healthBarX = x0;
    const float staminaBarX = x0 + sideBarW + centerGap;
    const float staminaBarY = y0 - (16.0f * uiScale);

    drawRect(healthBarX - 1.0f, staminaBarY - 1.0f, sideBarW + 2.0f, staminaBarH + 2.0f, 0.02f, 0.03f,
             0.04f, 0.95f);
    drawRect(healthBarX, staminaBarY, sideBarW, staminaBarH, 0.13f, 0.06f, 0.06f, 0.92f);
    if (healthFill > 0.0f) {
        const float fillW = sideBarW * healthFill;
        drawRect(healthBarX, staminaBarY, fillW, staminaBarH, 0.82f, 0.19f, 0.16f, 0.98f);
        drawRect(healthBarX, staminaBarY, fillW, staminaBarH * 0.45f, 0.96f, 0.44f, 0.38f, 0.74f);
    }
    const int healthPct = static_cast<int>(std::round(healthFill * 100.0f));
    const std::string healthText = std::to_string(healthPct) + "%";
    const float healthTextX = healthBarX + sideBarW * 0.5f - textWidthPxLocal(healthText) * 0.5f;
    const float healthTextY = staminaBarY - 11.0f;
    drawText(healthTextX + 1.0f, healthTextY + 1.0f, healthText, 12, 8, 8, 220);
    drawText(healthTextX, healthTextY, healthText, 248, 206, 202, 255);

    drawRect(staminaBarX - 1.0f, staminaBarY - 1.0f, sideBarW + 2.0f, staminaBarH + 2.0f, 0.02f,
             0.03f, 0.04f, 0.95f);
    drawRect(staminaBarX, staminaBarY, sideBarW, staminaBarH, 0.14f, 0.10f, 0.06f, 0.92f);
    if (staminaFill > 0.0f) {
        const float fillW = sideBarW * staminaFill;
        drawRect(staminaBarX, staminaBarY, fillW, staminaBarH, 0.88f, 0.54f, 0.12f, 0.98f);
        drawRect(staminaBarX, staminaBarY, fillW, staminaBarH * 0.45f, 1.00f, 0.78f, 0.28f, 0.78f);
    }
    const int staminaPct = static_cast<int>(std::round(staminaFill * 100.0f));
    const std::string staminaText = std::to_string(staminaPct) + "%";
    const float staminaTextX = staminaBarX + sideBarW * 0.5f - textWidthPxLocal(staminaText) * 0.5f;
    const float staminaTextY = staminaBarY - 11.0f;
    drawText(staminaTextX + 1.0f, staminaTextY + 1.0f, staminaText, 12, 10, 8, 220);
    drawText(staminaTextX, staminaTextY, staminaText, 245, 224, 178, 255);
    drawRect(x0 - 8.0f * uiScale, y0 - 6.0f * uiScale, totalW + 16.0f * uiScale,
             slot + 12.0f * uiScale, 0.03f, 0.04f, 0.05f, 0.58f);

    for (int i = 0; i < static_cast<int>(hotbar.size()); ++i) {
        const float x = x0 + i * (slot + gap);
        drawRect(x + 2.0f, y0 + 2.0f, slot, slot, 0.0f, 0.0f, 0.0f, 0.35f);
        drawRect(x, y0, slot, slot, 0.09f, 0.10f, 0.12f, 0.88f);
        drawRect(x + 2.0f, y0 + 2.0f, slot - 4.0f, slot - 4.0f, 0.16f, 0.17f, 0.20f, 0.76f);
        const voxel::BlockDef &def = registry.get(hotbar[i]);
        const float ix = x + 8.0f * uiScale;
        const float iy = y0 + 8.0f * uiScale;
        const float iw = slot - 16.0f * uiScale;
        const float ih = slot - 16.0f * uiScale;
        if (isFlatItemId(hotbar[i])) {
            const glm::vec4 uv = atlas.uvRect(def.sideTile);
            appendItemIconVerts(ix, iy, ix + iw, iy + ih, uv, 1.0f);
        } else {
            const float cubeInset = 5.0f * uiScale;
            appendCubeIconVerts(x + cubeInset, y0 + cubeInset, slot - 2.0f * cubeInset,
                                slot - 2.0f * cubeInset, def, atlas);
        }
        if (!showInventory && i == selectedIndex) {
            drawSelectedOutline(x, y0, slot);
        }
        if (showInventory && hoveredSlotIndex == i) {
            drawHoverOutline(x, y0, slot);
            setHoverTip(hotbar[i], hotbarCounts[i]);
        }
        if (showInventory && hotbarCounts[i] > 0 &&
            ((hintFurnaceInput && smeltRules.canSmelt(hotbar[i])) ||
             (hintFurnaceFuel && smeltRules.isFuel(hotbar[i])))) {
            drawValidHintOutline(x, y0, slot);
        }
        const int count = hotbarCounts[i];
        if (count > 0) {
            slotLabels.push_back(app::menus::SlotLabel{x + 6.0f, y0 + slot - 13.0f,
                                                       std::to_string(count)});
        }
        slotLabels.push_back(
            app::menus::SlotLabel{x + slot - 11.0f, y0 + 4.0f, std::to_string(i + 1)});
    }

    drawText(cx - 176.0f * uiScale, y0 + slot + 14.0f * uiScale,
             "1-9 Select  |  LMB Mine  |  RMB Place  |  E Inventory  |  F Creative  |  F2 HUD", 200,
             206, 222, 255);
}

void HudRenderer::renderInventoryBody2D(
    int width, int height, float cx, float uiScale, int hoveredSlotIndex, int craftingGridSize,
    bool usingCraftingTable, bool usingFurnace, const game::Inventory::Slot &smeltInput,
    const game::Inventory::Slot &smeltFuel, const game::Inventory::Slot &smeltOutput,
    float smeltProgress01, float smeltFuel01,
    const std::array<game::Inventory::Slot, game::CraftingSystem::kInputCount> &craftInput,
    const game::Inventory::Slot &craftOutput,
    const std::array<voxel::BlockId, game::Inventory::kSlotCount> &allIds,
    const std::array<int, game::Inventory::kSlotCount> &allCounts, voxel::BlockId carryingId,
    int carryingCount, float cursorX, float cursorY, const std::string &carryingName,
    bool showRecipeMenu, const std::vector<game::CraftingSystem::RecipeInfo> &recipes,
    const std::vector<game::SmeltingSystem::Recipe> &smeltingRecipes,
    const std::vector<bool> &recipeCraftable, float recipeScroll, float uiTimeSeconds,
    const std::string &recipeSearch, bool showCreativeMenu,
    const std::vector<voxel::BlockId> &creativeItems, float creativeScroll,
    const std::string &creativeSearch, bool recipeCraftableOnly,
    const std::optional<voxel::BlockId> &recipeIngredientFilter,
    const voxel::BlockRegistry &registry, const TextureAtlas &atlas,
    const game::SmeltingSystem &smeltRules, bool hintFurnaceInput, bool hintFurnaceFuel,
    const std::function<void(voxel::BlockId, int)> &setHoverTip,
    std::vector<app::menus::SlotLabel> &slotLabels,
    std::vector<app::menus::RecipeNameLabel> &recipeNameLabels, app::menus::TooltipState &tooltip,
    app::menus::DragIconState &dragIcon, std::vector<IconClipBatch> &iconClipBatches) {
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
    const float furnaceCenterOffset = usingFurnace ? (tablePanelW - craftPanelW) * 0.35f : 0.0f;
    const float invX = cx - totalPanelW * 0.5f + furnaceCenterOffset;

    auto drawSlotFrame = [&](float sx, float sy, float size) {
        drawRect(sx + 2.0f, sy + 2.0f, size, size, 0.0f, 0.0f, 0.0f, 0.32f);
        drawRect(sx, sy, size, size, 0.09f, 0.10f, 0.12f, 0.88f);
        drawRect(sx + 2.0f, sy + 2.0f, size - 4.0f, size - 4.0f, 0.16f, 0.17f, 0.20f, 0.76f);
    };

    const app::menus::HudDrawContext panelDrawCtx{
        [&](float x, float y, float w, float h, float r, float g, float b, float a) {
            drawRect(x, y, w, h, r, g, b, a);
        },
        [&](float x, float y, const std::string &text, unsigned char r, unsigned char g,
            unsigned char b, unsigned char a) { drawText(x, y, text, r, g, b, a); },
        [&](float x, float y, float size) { drawHoverOutline(x, y, size); },
        [&](float x, float y, float size) { drawValidHintOutline(x, y, size); },
        drawSlotFrame,
        {},
        {},
        [&](float x0, float y0, float x1, float y1, voxel::BlockId id, float light) {
            const glm::vec4 uv = atlas.uvRect(registry.get(id).sideTile);
            appendItemIconVerts(x0, y0, x1, y1, uv, light);
        },
        [&](float x, float y, float w, float h, const voxel::BlockDef &def) {
            appendCubeIconVerts(x, y, w, h, def, atlas);
        },
        {},
        {},
        textWidthPxLocal,
        [&](voxel::BlockId id) { return isFlatItemId(id); },
    };

    static app::menus::CraftingMenu craftingMenu;
    static app::menus::FurnaceMenu furnaceMenu;
    if (usingFurnace) {
        furnaceMenu.renderPanel(width, height, uiScale, hoveredSlotIndex, craftingGridSize, smeltInput,
                                smeltFuel, smeltOutput, smeltProgress01, smeltFuel01, registry,
                                panelDrawCtx, slotLabels, setHoverTip);
    } else {
        craftingMenu.renderPanel(width, height, uiScale, usingCraftingTable, hoveredSlotIndex,
                                 craftingGridSize, craftInput, craftOutput, registry, panelDrawCtx,
                                 slotLabels, setHoverTip);
    }

    for (int row = 0; row < rows; ++row) {
        for (int col = 0; col < cols; ++col) {
            const int slotIndex = game::Inventory::kHotbarSize + row * cols + col;
            const float sx = invX + col * (invSlot + invGap);
            const float sy = invY + row * (invSlot + invGap);
            drawSlotFrame(sx, sy, invSlot);
            const voxel::BlockDef &def = registry.get(allIds[slotIndex]);
            const float ix = sx + 8.0f * uiScale;
            const float iy = sy + 8.0f * uiScale;
            const float iw = invSlot - 16.0f * uiScale;
            const float ih = invSlot - 16.0f * uiScale;
            if (isFlatItemId(allIds[slotIndex])) {
                const glm::vec4 uv = atlas.uvRect(def.sideTile);
                appendItemIconVerts(ix, iy, ix + iw, iy + ih, uv, 1.0f);
            } else {
                const float cubeInset = 5.0f * uiScale;
                appendCubeIconVerts(sx + cubeInset, sy + cubeInset, invSlot - 2.0f * cubeInset,
                                    invSlot - 2.0f * cubeInset, def, atlas);
            }

            const int count = allCounts[slotIndex];
            if (count > 0) {
                slotLabels.push_back(
                    app::menus::SlotLabel{sx + 5.0f, sy + invSlot - 13.0f, std::to_string(count)});
            }
            if (hoveredSlotIndex == slotIndex) {
                drawHoverOutline(sx, sy, invSlot);
                setHoverTip(allIds[slotIndex], allCounts[slotIndex]);
            }
            if (allCounts[slotIndex] > 0 &&
                ((hintFurnaceInput && smeltRules.canSmelt(allIds[slotIndex])) ||
                 (hintFurnaceFuel && smeltRules.isFuel(allIds[slotIndex])))) {
                drawValidHintOutline(sx, sy, invSlot);
            }
        }
    }

    if (carryingCount > 0 && carryingId != voxel::AIR) {
        const float dragSize = 34.0f * uiScale;
        dragIcon.active = true;
        dragIcon.id = carryingId;
        dragIcon.count = carryingCount;
        dragIcon.x = cursorX + 10.0f * uiScale;
        dragIcon.y = cursorY + 8.0f * uiScale;
        dragIcon.size = dragSize;
        dragIcon.name = carryingName;
    }

    auto beginIconClipBatch = [&](float x, float y, float w, float h) {
        iconClipBatches.push_back({iconVerts_.size(), 0, x, y, w, h});
        return iconClipBatches.size() - 1;
    };
    auto endIconClipBatch = [&](std::size_t batchIndex) {
        if (batchIndex < iconClipBatches.size()) {
            iconClipBatches[batchIndex].count = iconVerts_.size() - iconClipBatches[batchIndex].start;
        }
    };

    auto drawRectClipped = [&](float x, float y, float w, float h, float cx0, float cy0, float cx1,
                               float cy1, float r, float g, float b, float a) {
        const float x0 = std::max(x, cx0);
        const float y0 = std::max(y, cy0);
        const float x1 = std::min(x + w, cx1);
        const float y1 = std::min(y + h, cy1);
        if (x1 <= x0 || y1 <= y0) {
            return;
        }
        drawRect(x0, y0, x1 - x0, y1 - y0, r, g, b, a);
    };
    auto drawTextClipped = [&](float x, float y, const std::string &text, float cx0, float cy0,
                               float cx1, float cy1, unsigned char r, unsigned char g,
                               unsigned char b, unsigned char a) {
        char buffer[99999];
        unsigned char color[4] = {r, g, b, a};
        const int quads =
            stb_easy_font_print(x, y, const_cast<char *>(text.c_str()), color, buffer, sizeof(buffer));
        struct StbVert {
            float x;
            float y;
            float z;
            unsigned char c[4];
        };
        const StbVert *q = reinterpret_cast<const StbVert *>(buffer);
        for (int i = 0; i < quads; ++i) {
            const StbVert &a0 = q[i * 4 + 0];
            const StbVert &a2 = q[i * 4 + 2];
            const float x0 = std::max(a0.x, cx0);
            const float y0 = std::max(a0.y, cy0);
            const float x1 = std::min(a2.x, cx1);
            const float y1 = std::min(a2.y, cy1);
            if (x1 <= x0 || y1 <= y0) {
                continue;
            }
            const float cr = r / 255.0f;
            const float cg = g / 255.0f;
            const float cb = b / 255.0f;
            const float ca = a / 255.0f;
            verts_.push_back(UiVertex{x0, y0, cr, cg, cb, ca});
            verts_.push_back(UiVertex{x1, y0, cr, cg, cb, ca});
            verts_.push_back(UiVertex{x1, y1, cr, cg, cb, ca});
            verts_.push_back(UiVertex{x0, y0, cr, cg, cb, ca});
            verts_.push_back(UiVertex{x1, y1, cr, cg, cb, ca});
            verts_.push_back(UiVertex{x0, y1, cr, cg, cb, ca});
        }
    };

    const app::menus::HudDrawContext drawCtx{
        [&](float x, float y, float w, float h, float r, float g, float b, float a) {
            drawRect(x, y, w, h, r, g, b, a);
        },
        [&](float x, float y, const std::string &text, unsigned char r, unsigned char g,
            unsigned char b, unsigned char a) { drawText(x, y, text, r, g, b, a); },
        [&](float x, float y, float size) { drawHoverOutline(x, y, size); },
        [&](float x, float y, float size) { drawValidHintOutline(x, y, size); },
        drawSlotFrame,
        drawRectClipped,
        drawTextClipped,
        [&](float x0, float y0, float x1, float y1, voxel::BlockId id, float light) {
            const glm::vec4 uv = atlas.uvRect(registry.get(id).sideTile);
            appendItemIconVerts(x0, y0, x1, y1, uv, light);
        },
        [&](float x, float y, float w, float h, const voxel::BlockDef &def) {
            appendCubeIconVerts(x, y, w, h, def, atlas);
        },
        beginIconClipBatch,
        endIconClipBatch,
        textWidthPxLocal,
        [&](voxel::BlockId id) { return isFlatItemId(id); },
    };

    static app::menus::CreativeMenu creativeMenu;
    static app::menus::RecipeMenu recipeMenu;
    if (showCreativeMenu) {
        creativeMenu.renderOverlay(width, height, uiScale, craftingGridSize, usingFurnace, cursorX,
                                   cursorY, creativeScroll, uiTimeSeconds, creativeSearch,
                                   creativeItems, registry, drawCtx, tooltip);
    }
    if (showRecipeMenu) {
        recipeMenu.renderOverlay(width, height, uiScale, craftingGridSize, usingFurnace, cursorX,
                                 cursorY, recipeScroll, uiTimeSeconds, recipeSearch,
                                 recipeCraftableOnly, recipeIngredientFilter, recipes,
                                 smeltingRecipes, recipeCraftable, registry, drawCtx,
                                 recipeNameLabels, tooltip);
    }
}

} // namespace gfx
