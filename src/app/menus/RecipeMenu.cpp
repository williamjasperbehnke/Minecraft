#include "app/menus/RecipeMenu.hpp"

#include "game/GameRules.hpp"
#include "game/Inventory.hpp"

#include <algorithm>
#include <cctype>

namespace app::menus {
namespace {

std::string toLowerAscii(std::string s) {
    for (char &c : s) {
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    return s;
}

} // namespace

RecipeMenuLayout RecipeMenu::computeLayout(int width, int height, float hudScale,
                                           int craftingGridSize, bool /*usingFurnace*/,
                                           std::size_t recipeCount) const {
    const float cx = width * 0.5f;
    const float uiScale = std::clamp(hudScale, 0.8f, 1.8f);
    const float y0 = height - 90.0f * uiScale;
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

    const float recipeHeaderH = 34.0f * uiScale;
    const float recipeBodyH = 220.0f * uiScale;
    const float recipeH = recipeHeaderH + recipeBodyH;
    const float recipeY = invY - recipeH - 44.0f * uiScale;
    const float recipeW = totalPanelW;
    const float searchX = invX + 88.0f * uiScale;
    const float searchY = recipeY + 6.0f * uiScale;
    const float searchW = recipeW - 372.0f * uiScale;
    const float searchH = 22.0f * uiScale;
    const float craftableFilterSize = 14.0f * uiScale;
    const float craftableFilterW = 86.0f * uiScale;
    const float craftableFilterH = 16.0f * uiScale;
    const float craftableFilterX = recipeW + invX - 124.0f * uiScale;
    const float craftableFilterY = recipeY + 10.0f * uiScale;
    const float ingredientTagX = craftableFilterX - 154.0f * uiScale;
    const float ingredientTagY = craftableFilterY - 1.0f * uiScale;
    const float ingredientTagW = 146.0f * uiScale;
    const float ingredientTagH = 16.0f * uiScale;
    const float ingredientTagCloseS = 12.0f * uiScale;
    const float ingredientTagCloseX = ingredientTagX + ingredientTagW - ingredientTagCloseS -
                                      2.0f * uiScale;
    const float ingredientTagCloseY = ingredientTagY + 2.0f * uiScale;
    const float contentX = invX + 10.0f * uiScale;
    const float contentY = recipeY + recipeHeaderH;
    const float contentW = recipeW - 28.0f * uiScale;
    const float contentH = recipeBodyH - 10.0f * uiScale;
    const int columns = 2;
    const float cellGapX = 10.0f * uiScale;
    const float cellGapY = 10.0f * uiScale;
    const float gridInsetRight = 30.0f * uiScale;
    const float gridInsetLeft = gridInsetRight;
    const float cellW = (contentW - gridInsetLeft - gridInsetRight - cellGapX) * 0.5f;
    const float cellH = 78.0f * uiScale;
    const float rowStride = cellH + cellGapY;
    const int rowsCount = (static_cast<int>(recipeCount) + columns - 1) / columns;
    const float totalContentH = static_cast<float>(rowsCount) * cellH +
                                static_cast<float>(std::max(0, rowsCount - 1)) * cellGapY;
    const float maxScroll = std::max(0.0f, totalContentH - contentH);
    const float trackW = 8.0f * uiScale;
    const float trackX = invX + recipeW - 12.0f * uiScale;
    const float trackY = contentY;
    const float trackH = contentH;
    const float thumbH =
        (maxScroll > 0.0f) ? std::max(22.0f * uiScale, trackH * (contentH / totalContentH)) : trackH;

    return RecipeMenuLayout{invX,
                            recipeY,
                            recipeW,
                            recipeH,
                            contentX,
                            contentY,
                            contentW,
                            contentH,
                            columns,
                            cellW,
                            cellH,
                            cellGapX,
                            cellGapY,
                            gridInsetLeft,
                            gridInsetRight,
                            rowStride,
                            trackX,
                            trackY,
                            trackW,
                            trackH,
                            thumbH,
                            trackY,
                            maxScroll,
                            searchX,
                            searchY,
                            searchW,
                            searchH,
                            craftableFilterX,
                            craftableFilterY,
                            craftableFilterSize,
                            craftableFilterW,
                            craftableFilterH,
                            ingredientTagX,
                            ingredientTagY,
                            ingredientTagW,
                            ingredientTagH,
                            ingredientTagCloseX,
                            ingredientTagCloseY,
                            ingredientTagCloseS};
}

int RecipeMenu::rowAtCursor(double mx, double my, const RecipeMenuLayout &layout, float scroll,
                            std::size_t recipeCount) const {
    if (mx < (layout.contentX + layout.gridInsetLeft) ||
        mx > (layout.contentX + layout.contentW - layout.gridInsetRight)) {
        return -1;
    }
    const int rowsCount = (static_cast<int>(recipeCount) + layout.columns - 1) / layout.columns;
    for (int row = 0; row < rowsCount; ++row) {
        const float ry = layout.contentY + static_cast<float>(row) * layout.rowStride - scroll;
        if (ry + layout.cellH < layout.contentY || ry > layout.contentY + layout.contentH) {
            continue;
        }
        for (int col = 0; col < layout.columns; ++col) {
            const int idx = row * layout.columns + col;
            if (idx >= static_cast<int>(recipeCount)) {
                break;
            }
            const float rx = layout.contentX + layout.gridInsetLeft +
                             static_cast<float>(col) * (layout.cellW + layout.cellGapX);
            if (mx >= rx && mx <= (rx + layout.cellW) && my >= ry && my <= (ry + layout.cellH)) {
                return idx;
            }
        }
    }
    return -1;
}

bool RecipeMenu::matchesSearch(const game::CraftingSystem::RecipeInfo &recipe,
                               const std::string &search) const {
    const std::string needle = toLowerAscii(search);
    if (needle.empty()) {
        return true;
    }
    if (toLowerAscii(recipe.label).find(needle) != std::string::npos) {
        return true;
    }
    if (toLowerAscii(game::blockName(recipe.outputId)).find(needle) != std::string::npos) {
        return true;
    }
    for (const auto &ingredient : recipe.ingredients) {
        if (ingredient.allowAnyWood) {
            if (std::string("wood").find(needle) != std::string::npos ||
                std::string("log").find(needle) != std::string::npos) {
                return true;
            }
        }
        if (ingredient.allowAnyPlanks) {
            if (std::string("plank").find(needle) != std::string::npos ||
                std::string("wood").find(needle) != std::string::npos) {
                return true;
            }
        }
        if (toLowerAscii(game::blockName(ingredient.id)).find(needle) != std::string::npos) {
            return true;
        }
    }
    return false;
}

bool RecipeMenu::usesIngredient(const game::CraftingSystem::RecipeInfo &recipe,
                                voxel::BlockId targetId) const {
    const bool isWoodTarget = (targetId == voxel::WOOD || targetId == voxel::SPRUCE_WOOD ||
                               targetId == voxel::BIRCH_WOOD);
    const bool isPlankTarget =
        (targetId == voxel::OAK_PLANKS || targetId == voxel::SPRUCE_PLANKS ||
         targetId == voxel::BIRCH_PLANKS);
    for (const auto &ingredient : recipe.ingredients) {
        if (ingredient.allowAnyWood && isWoodTarget) {
            return true;
        }
        if (ingredient.allowAnyPlanks && isPlankTarget) {
            return true;
        }
        if (ingredient.id == targetId) {
            return true;
        }
    }
    for (const auto &cell : recipe.shapedCells) {
        if (cell.ingredient.allowAnyWood && isWoodTarget) {
            return true;
        }
        if (cell.ingredient.allowAnyPlanks && isPlankTarget) {
            return true;
        }
        if (cell.ingredient.id == targetId) {
            return true;
        }
    }
    return false;
}

bool RecipeMenu::smeltingMatchesSearch(const game::SmeltingSystem::Recipe &recipe,
                                       const std::string &search) const {
    const std::string needle = toLowerAscii(search);
    if (needle.empty()) {
        return true;
    }
    if (toLowerAscii(game::blockName(recipe.input)).find(needle) != std::string::npos) {
        return true;
    }
    if (toLowerAscii(game::blockName(recipe.output)).find(needle) != std::string::npos) {
        return true;
    }
    return false;
}

void RecipeMenu::renderOverlay(int width, int height, float uiScale, int craftingGridSize,
                               bool usingFurnace, float cursorX, float cursorY, float recipeScroll,
                               float uiTimeSeconds, const std::string &recipeSearch,
                               bool recipeCraftableOnly,
                               const std::optional<voxel::BlockId> &recipeIngredientFilter,
                               const std::vector<game::CraftingSystem::RecipeInfo> &recipes,
                               const std::vector<game::SmeltingSystem::Recipe> &smeltingRecipes,
                               const std::vector<bool> &recipeCraftable,
                               const voxel::BlockRegistry &registry,
                               const HudDrawContext &draw,
                               std::vector<RecipeNameLabel> &recipeNameLabels,
                               TooltipState &tooltip) const {
    const std::size_t rowCount = usingFurnace ? smeltingRecipes.size() : recipes.size();
    const RecipeMenuLayout layout =
        computeLayout(width, height, uiScale, craftingGridSize, usingFurnace, rowCount);

    draw.drawRect(layout.panelX, layout.panelY, layout.panelW, layout.panelH, 0.03f, 0.04f, 0.05f,
                  0.78f);
    draw.drawText(layout.panelX + 10.0f * uiScale, layout.panelY + 10.0f * uiScale,
                  usingFurnace ? "Smelting" : "Recipes", 244, 246, 252, 255);
    draw.drawText(layout.panelX + layout.panelW - 36.0f * uiScale, layout.panelY + 9.0f * uiScale,
                  "R: Close", 182, 190, 206, 255);

    draw.drawRect(layout.searchX, layout.searchY, layout.searchW, layout.searchH, 0.06f, 0.08f, 0.10f,
                  0.95f);
    draw.drawRect(layout.searchX + 1.0f, layout.searchY + 1.0f, layout.searchW - 2.0f,
                  layout.searchH - 2.0f, 0.12f, 0.14f, 0.18f, 0.95f);
    const std::string searchText = recipeSearch.empty()
                                       ? (usingFurnace ? "Search smelting..." : "Search recipes...")
                                       : recipeSearch;
    const bool blinkOn = (static_cast<int>(std::floor(uiTimeSeconds * 2.0f)) % 2) == 0;
    draw.drawText(layout.searchX + 6.0f * uiScale, layout.searchY + 7.0f * uiScale,
                  searchText + (blinkOn ? "_" : ""), recipeSearch.empty() ? 168 : 232,
                  recipeSearch.empty() ? 176 : 238, recipeSearch.empty() ? 190 : 248, 255);
    draw.drawRect(layout.craftableFilterX, layout.craftableFilterY, layout.craftableFilterSize,
                  layout.craftableFilterSize, 0.09f, 0.10f, 0.12f, 0.95f);
    draw.drawRect(layout.craftableFilterX + 1.0f, layout.craftableFilterY + 1.0f,
                  layout.craftableFilterSize - 2.0f, layout.craftableFilterSize - 2.0f,
                  recipeCraftableOnly ? 0.28f : 0.15f, recipeCraftableOnly ? 0.74f : 0.18f,
                  recipeCraftableOnly ? 0.42f : 0.21f, 0.95f);
    if (recipeCraftableOnly) {
        const std::string checkText = "X";
        const float checkX =
            layout.craftableFilterX + (layout.craftableFilterSize - draw.textWidthPx(checkText)) * 0.5f;
        const float checkY = layout.craftableFilterY + (layout.craftableFilterSize - 8.0f) * 0.5f;
        draw.drawText(checkX, checkY, checkText, 242, 248, 255, 255);
    }
    draw.drawText(layout.craftableFilterX + layout.craftableFilterSize + 6.0f * uiScale,
                  layout.craftableFilterY + 4.0f * uiScale, usingFurnace ? "Smeltable" : "Craftable",
                  198, 214, 236, 255);
    if (recipeIngredientFilter.has_value()) {
        const std::string ingredientTag =
            std::string("Ingredient: ") + game::blockName(recipeIngredientFilter.value());
        draw.drawRect(layout.ingredientTagX, layout.ingredientTagY, layout.ingredientTagW,
                      layout.ingredientTagH, 0.10f, 0.22f, 0.16f, 0.92f);
        draw.drawRect(layout.ingredientTagX + 1.0f, layout.ingredientTagY + 1.0f,
                      layout.ingredientTagW - 2.0f, layout.ingredientTagH - 2.0f, 0.14f, 0.30f, 0.22f,
                      0.95f);
        draw.drawRect(layout.ingredientTagX + 1.0f, layout.ingredientTagY + 1.0f,
                      layout.ingredientTagW - 2.0f, 2.0f, 0.22f, 0.44f, 0.32f, 0.88f);
        draw.drawRect(layout.ingredientTagCloseX, layout.ingredientTagCloseY, layout.ingredientTagCloseS,
                      layout.ingredientTagCloseS, 0.20f, 0.36f, 0.27f, 0.95f);
        draw.drawRect(layout.ingredientTagCloseX + 1.0f, layout.ingredientTagCloseY + 1.0f,
                      layout.ingredientTagCloseS - 2.0f, layout.ingredientTagCloseS - 2.0f, 0.16f, 0.26f,
                      0.20f, 0.95f);
        const std::string clearText = "x";
        const float clearX =
            layout.ingredientTagCloseX + (layout.ingredientTagCloseS - draw.textWidthPx(clearText)) * 0.5f;
        const float clearY = layout.ingredientTagCloseY + (layout.ingredientTagCloseS - 8.0f) * 0.5f;
        draw.drawText(clearX, clearY, clearText, 228, 244, 234, 255);
        draw.drawTextClipped(layout.ingredientTagX + 6.0f * uiScale, layout.ingredientTagY + 4.0f * uiScale,
                             ingredientTag, layout.ingredientTagX, layout.ingredientTagY,
                             layout.ingredientTagCloseX - 2.0f * uiScale,
                             layout.ingredientTagY + layout.ingredientTagH, 208, 236, 218, 255);
    }

    draw.drawRect(layout.contentX, layout.contentY, layout.contentW, layout.contentH, 0.09f, 0.10f, 0.12f,
                  0.70f);
    const std::size_t recipeIconClipBatch =
        draw.beginIconClipBatch(layout.contentX, layout.contentY, layout.contentW, layout.contentH);
    std::string hoveredIngredientName;

    if (usingFurnace) {
        const float cellH = 74.0f * uiScale;
        const float rowStride = cellH + layout.cellGapY;
        const int rowsCount =
            (static_cast<int>(smeltingRecipes.size()) + layout.columns - 1) / layout.columns;
        const float totalContentH = static_cast<float>(rowsCount) * cellH +
                                    static_cast<float>(std::max(0, rowsCount - 1)) * layout.cellGapY;
        const float maxScroll = std::max(0.0f, totalContentH - layout.contentH);
        const float scroll = std::clamp(recipeScroll, 0.0f, maxScroll);
        for (std::size_t i = 0; i < smeltingRecipes.size(); ++i) {
            const auto &recipe = smeltingRecipes[i];
            const int row = static_cast<int>(i) / layout.columns;
            const int col = static_cast<int>(i) % layout.columns;
            const float rx = layout.contentX + layout.gridInsetLeft +
                             static_cast<float>(col) * (layout.cellW + layout.cellGapX);
            const float ry = layout.contentY + static_cast<float>(row) * rowStride - scroll;
            if (ry + cellH < layout.contentY || ry > (layout.contentY + layout.contentH)) {
                continue;
            }
            const bool hovered =
                (cursorX >= rx && cursorX <= (rx + layout.cellW) && cursorY >= ry && cursorY <= (ry + cellH));
            const bool craftable = i < recipeCraftable.size() && recipeCraftable[i];
            const float baseR = craftable ? 0.12f : 0.10f;
            const float baseG = craftable ? 0.20f : 0.12f;
            const float baseB = craftable ? 0.13f : 0.15f;
            const float glow = hovered ? 0.08f : 0.0f;
            draw.drawRectClipped(rx, ry, layout.cellW, cellH, layout.contentX, layout.contentY,
                                 layout.contentX + layout.contentW, layout.contentY + layout.contentH,
                                 baseR + glow, baseG + glow, baseB + glow, craftable ? 0.92f : 0.86f);
            draw.drawRectClipped(rx + 2.0f * uiScale, ry + 2.0f * uiScale, layout.cellW - 4.0f * uiScale,
                                 cellH - 4.0f * uiScale, layout.contentX, layout.contentY,
                                 layout.contentX + layout.contentW, layout.contentY + layout.contentH,
                                 craftable ? 0.18f : 0.14f, craftable ? 0.22f : 0.16f,
                                 craftable ? 0.18f : 0.20f, 0.82f);
            const float iconS = 36.0f * uiScale;
            const float iconY = ry + 8.0f * uiScale;
            float iconX = rx + 10.0f * uiScale;
            draw.drawRectClipped(iconX, iconY, iconS, iconS, layout.contentX, layout.contentY,
                                 layout.contentX + layout.contentW, layout.contentY + layout.contentH, 0.08f,
                                 0.10f, 0.12f, 0.95f);
            if (draw.isFlatItemId(recipe.input)) {
                draw.appendItemIcon(iconX + 4.0f * uiScale, iconY + 4.0f * uiScale,
                                    iconX + iconS - 4.0f * uiScale, iconY + iconS - 4.0f * uiScale,
                                    recipe.input, 1.0f);
            } else {
                const float inset = 2.0f * uiScale;
                draw.appendCubeIcon(iconX + inset, iconY + inset, iconS - 2.0f * inset,
                                    iconS - 2.0f * inset, registry.get(recipe.input));
            }
            if (cursorX >= iconX && cursorX <= (iconX + iconS) && cursorY >= iconY &&
                cursorY <= (iconY + iconS)) {
                hoveredIngredientName = game::blockName(recipe.input);
            }
            iconX += 46.0f * uiScale;
            draw.drawTextClipped(iconX, ry + 21.0f * uiScale, "->", layout.contentX, layout.contentY,
                                 layout.contentX + layout.contentW, layout.contentY + layout.contentH, 230,
                                 236, 248, 255);
            iconX += 18.0f * uiScale;
            draw.drawRectClipped(iconX, iconY, iconS, iconS, layout.contentX, layout.contentY,
                                 layout.contentX + layout.contentW, layout.contentY + layout.contentH, 0.08f,
                                 0.10f, 0.12f, 0.95f);
            if (draw.isFlatItemId(recipe.output)) {
                draw.appendItemIcon(iconX + 4.0f * uiScale, iconY + 4.0f * uiScale,
                                    iconX + iconS - 4.0f * uiScale, iconY + iconS - 4.0f * uiScale,
                                    recipe.output, 1.0f);
            } else {
                const float inset = 2.0f * uiScale;
                draw.appendCubeIcon(iconX + inset, iconY + inset, iconS - 2.0f * inset,
                                    iconS - 2.0f * inset, registry.get(recipe.output));
            }
            if (cursorX >= iconX && cursorX <= (iconX + iconS) && cursorY >= iconY &&
                cursorY <= (iconY + iconS)) {
                hoveredIngredientName = game::blockName(recipe.output);
            }
            draw.drawTextClipped(iconX + 2.0f * uiScale, iconY + iconS - 10.0f * uiScale,
                                 std::to_string(std::max(1, recipe.outputCount)), layout.contentX,
                                 layout.contentY, layout.contentX + layout.contentW,
                                 layout.contentY + layout.contentH, 232, 236, 246, 255);
            const std::string label = game::blockName(recipe.output);
            const float labelW = std::clamp(draw.textWidthPx(label) + 14.0f * uiScale, 46.0f * uiScale,
                                            layout.cellW - 14.0f * uiScale);
            const float labelX = rx + (layout.cellW - labelW) * 0.5f;
            const float labelY = ry + cellH - 18.0f * uiScale;
            const bool labelFullyVisible = labelY >= layout.contentY &&
                                           (labelY + 14.0f * uiScale) <= (layout.contentY + layout.contentH);
            if (labelFullyVisible) {
                recipeNameLabels.push_back({labelX, labelY, labelW, 14.0f * uiScale, label});
            }
        }

        draw.drawRect(layout.trackX, layout.trackY, 4.0f * uiScale, layout.trackH, 0.14f, 0.16f, 0.18f,
                      0.95f);
        if (maxScroll > 0.0f) {
            const float thumbH = std::max(22.0f * uiScale, layout.trackH * (layout.contentH / totalContentH));
            const float thumbTravel = std::max(0.0f, layout.trackH - thumbH);
            const float thumbY = layout.trackY + (scroll / maxScroll) * thumbTravel;
            draw.drawRect(layout.trackX - 2.0f * uiScale, thumbY, 8.0f * uiScale, thumbH, 0.30f, 0.72f,
                          0.95f, 0.95f);
        } else {
            draw.drawRect(layout.trackX - 2.0f * uiScale, layout.trackY, 8.0f * uiScale, layout.trackH,
                          0.24f, 0.28f, 0.33f, 0.85f);
        }
    } else {
        const float scroll = std::clamp(recipeScroll, 0.0f, layout.maxScroll);
        const std::array<voxel::BlockId, 3> woodCycle = {voxel::WOOD, voxel::SPRUCE_WOOD, voxel::BIRCH_WOOD};
        const std::array<voxel::BlockId, 3> plankCycle = {voxel::OAK_PLANKS, voxel::SPRUCE_PLANKS,
                                                          voxel::BIRCH_PLANKS};
        const int cycleIndex =
            static_cast<int>(std::floor(std::max(0.0f, uiTimeSeconds) * 0.6f)) % static_cast<int>(woodCycle.size());
        for (std::size_t i = 0; i < recipes.size(); ++i) {
            const auto &recipe = recipes[i];
            const int row = static_cast<int>(i) / layout.columns;
            const int col = static_cast<int>(i) % layout.columns;
            const float rx = layout.contentX + layout.gridInsetLeft +
                             static_cast<float>(col) * (layout.cellW + layout.cellGapX);
            const float ry = layout.contentY + static_cast<float>(row) * layout.rowStride - scroll;
            if (ry + layout.cellH < layout.contentY || ry > (layout.contentY + layout.contentH)) {
                continue;
            }
            const bool hovered = (cursorX >= rx && cursorX <= (rx + layout.cellW) && cursorY >= ry &&
                                  cursorY <= (ry + layout.cellH));
            const bool craftable = i < recipeCraftable.size() && recipeCraftable[i];
            const float baseR = craftable ? 0.12f : 0.10f;
            const float baseG = craftable ? 0.20f : 0.12f;
            const float baseB = craftable ? 0.13f : 0.15f;
            const float glow = hovered ? 0.08f : 0.0f;
            draw.drawRectClipped(rx, ry, layout.cellW, layout.cellH, layout.contentX, layout.contentY,
                                 layout.contentX + layout.contentW, layout.contentY + layout.contentH,
                                 baseR + glow, baseG + glow, baseB + glow, craftable ? 0.92f : 0.86f);
            draw.drawRectClipped(rx + 2.0f * uiScale, ry + 2.0f * uiScale, layout.cellW - 4.0f * uiScale,
                                 layout.cellH - 4.0f * uiScale, layout.contentX, layout.contentY,
                                 layout.contentX + layout.contentW, layout.contentY + layout.contentH,
                                 craftable ? 0.18f : 0.14f, craftable ? 0.22f : 0.16f,
                                 craftable ? 0.18f : 0.20f, 0.82f);
            if (hovered) {
                draw.drawRectClipped(rx - 1.0f, ry - 1.0f, layout.cellW + 2.0f, 2.0f, layout.contentX,
                                     layout.contentY, layout.contentX + layout.contentW,
                                     layout.contentY + layout.contentH, 0.38f, 0.80f, 1.0f, 0.95f);
                draw.drawRectClipped(rx - 1.0f, ry + layout.cellH - 1.0f, layout.cellW + 2.0f, 2.0f,
                                     layout.contentX, layout.contentY, layout.contentX + layout.contentW,
                                     layout.contentY + layout.contentH, 0.38f, 0.80f, 1.0f, 0.95f);
                draw.drawRectClipped(rx - 1.0f, ry, 2.0f, layout.cellH, layout.contentX, layout.contentY,
                                     layout.contentX + layout.contentW, layout.contentY + layout.contentH,
                                     0.38f, 0.80f, 1.0f, 0.95f);
                draw.drawRectClipped(rx + layout.cellW - 1.0f, ry, 2.0f, layout.cellH, layout.contentX,
                                     layout.contentY, layout.contentX + layout.contentW,
                                     layout.contentY + layout.contentH, 0.38f, 0.80f, 1.0f, 0.95f);
            }
            float iconX = rx + 8.0f * uiScale;
            const float iconY = ry + 7.0f * uiScale;
            const float iconS = 36.0f * uiScale;
            for (const auto &in : recipe.ingredients) {
                voxel::BlockId renderId = in.id;
                if (in.allowAnyWood) {
                    renderId = woodCycle[cycleIndex];
                } else if (in.allowAnyPlanks) {
                    renderId = plankCycle[cycleIndex];
                }
                draw.drawRectClipped(iconX, iconY, iconS, iconS, layout.contentX, layout.contentY,
                                     layout.contentX + layout.contentW, layout.contentY + layout.contentH,
                                     0.08f, 0.10f, 0.12f, 0.95f);
                const float drawX = iconX + 4.0f * uiScale;
                const float drawY = iconY + 4.0f * uiScale;
                const float drawS = iconS - 8.0f * uiScale;
                if (draw.isFlatItemId(renderId)) {
                    draw.appendItemIcon(drawX, drawY, drawX + drawS, drawY + drawS, renderId, 1.0f);
                } else {
                    const float cubeInset = 2.0f * uiScale;
                    draw.appendCubeIcon(iconX + cubeInset, iconY + cubeInset, iconS - 2.0f * cubeInset,
                                        iconS - 2.0f * cubeInset, registry.get(renderId));
                }
                draw.drawTextClipped(iconX + 2.0f * uiScale, iconY + iconS - 10.0f * uiScale,
                                     std::to_string(in.count), layout.contentX, layout.contentY,
                                     layout.contentX + layout.contentW, layout.contentY + layout.contentH,
                                     232, 236, 246, 255);
                if (cursorX >= iconX && cursorX <= (iconX + iconS) && cursorY >= iconY &&
                    cursorY <= (iconY + iconS)) {
                    hoveredIngredientName = (in.allowAnyWood || in.allowAnyPlanks) ? game::blockName(renderId)
                                                                                    : game::blockName(in.id);
                }
                iconX += 48.0f * uiScale;
            }
            draw.drawTextClipped(iconX, ry + 19.0f * uiScale, "->", layout.contentX, layout.contentY,
                                 layout.contentX + layout.contentW, layout.contentY + layout.contentH, 230,
                                 236, 248, 255);
            iconX += 20.0f * uiScale;
            draw.drawRectClipped(iconX, iconY, iconS, iconS, layout.contentX, layout.contentY,
                                 layout.contentX + layout.contentW, layout.contentY + layout.contentH, 0.08f,
                                 0.10f, 0.12f, 0.95f);
            if (draw.isFlatItemId(recipe.outputId)) {
                draw.appendItemIcon(iconX + 4.0f * uiScale, iconY + 4.0f * uiScale,
                                    iconX + iconS - 4.0f * uiScale, iconY + iconS - 4.0f * uiScale,
                                    recipe.outputId, 1.0f);
            } else {
                const float outCubeInset = 2.0f * uiScale;
                draw.appendCubeIcon(iconX + outCubeInset, iconY + outCubeInset,
                                    iconS - 2.0f * outCubeInset, iconS - 2.0f * outCubeInset,
                                    registry.get(recipe.outputId));
            }
            if (cursorX >= iconX && cursorX <= (iconX + iconS) && cursorY >= iconY &&
                cursorY <= (iconY + iconS)) {
                hoveredIngredientName = game::blockName(recipe.outputId);
            }
            draw.drawTextClipped(iconX + 2.0f * uiScale, iconY + iconS - 10.0f * uiScale,
                                 std::to_string(recipe.outputCount), layout.contentX, layout.contentY,
                                 layout.contentX + layout.contentW, layout.contentY + layout.contentH, 232,
                                 236, 246, 255);
            const float labelPad = 7.0f * uiScale;
            const float labelH = 14.0f * uiScale;
            const float labelTextW = draw.textWidthPx(recipe.label);
            const float labelW = std::clamp(labelTextW + labelPad * 2.0f, 42.0f * uiScale,
                                            layout.cellW - 18.0f * uiScale);
            const float labelX = rx + (layout.cellW - labelW) * 0.5f;
            const float labelY = ry + layout.cellH - labelH - 4.0f * uiScale;
            const bool labelFullyVisible =
                labelY >= layout.contentY && (labelY + labelH) <= (layout.contentY + layout.contentH);
            if (labelFullyVisible) {
                recipeNameLabels.push_back({labelX, labelY, labelW, labelH, recipe.label});
            }
        }

        draw.drawRect(layout.trackX, layout.trackY, 4.0f * uiScale, layout.trackH, 0.14f, 0.16f, 0.18f,
                      0.95f);
        if (layout.maxScroll > 0.0f) {
            const float thumbTravel = std::max(0.0f, layout.trackH - layout.thumbH);
            const float thumbY = layout.trackY + (scroll / layout.maxScroll) * thumbTravel;
            draw.drawRect(layout.trackX - 2.0f * uiScale, thumbY, 8.0f * uiScale, layout.thumbH, 0.30f,
                          0.72f, 0.95f, 0.95f);
        } else {
            draw.drawRect(layout.trackX - 2.0f * uiScale, layout.trackY, 8.0f * uiScale, layout.trackH,
                          0.24f, 0.28f, 0.33f, 0.85f);
        }
    }

    draw.endIconClipBatch(recipeIconClipBatch);
    if (!hoveredIngredientName.empty()) {
        tooltip.text = hoveredIngredientName;
        tooltip.x = cursorX + 12.0f * uiScale;
        tooltip.y = cursorY - 8.0f * uiScale;
    }
}

} // namespace app::menus
