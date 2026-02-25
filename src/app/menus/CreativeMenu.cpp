#include "app/menus/CreativeMenu.hpp"

#include "game/CraftingSystem.hpp"
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

const std::vector<voxel::BlockId> &CreativeMenu::catalog() const {
    static const std::vector<voxel::BlockId> kItems = {
        voxel::GRASS,
        voxel::DIRT,
        voxel::STONE,
        voxel::BEDROCK,
        voxel::SAND,
        voxel::WATER,
        voxel::LAVA,
        voxel::WOOD,
        voxel::LEAVES,
        voxel::SPRUCE_WOOD,
        voxel::SPRUCE_LEAVES,
        voxel::BIRCH_WOOD,
        voxel::BIRCH_LEAVES,
        voxel::CACTUS,
        voxel::SANDSTONE,
        voxel::RED_SAND,
        voxel::GRAVEL,
        voxel::CLAY,
        voxel::MUD,
        voxel::MOSS,
        voxel::BASALT,
        voxel::SNOW_BLOCK,
        voxel::ICE,
        voxel::COAL_ORE,
        voxel::COPPER_ORE,
        voxel::IRON_ORE,
        voxel::GOLD_ORE,
        voxel::DIAMOND_ORE,
        voxel::EMERALD_ORE,
        voxel::TALL_GRASS,
        voxel::FLOWER,
        voxel::WILDFLOWER,
        voxel::FERN,
        voxel::DRY_GRASS,
        voxel::DEAD_BUSH,
        voxel::SEAGRASS,
        voxel::KELP,
        voxel::CORAL,
        voxel::TORCH,
        voxel::CRAFTING_TABLE,
        voxel::OAK_PLANKS,
        voxel::SPRUCE_PLANKS,
        voxel::BIRCH_PLANKS,
        voxel::STICK,
        voxel::FURNACE,
        voxel::GLASS,
        voxel::BRICKS,
        voxel::IRON_INGOT,
        voxel::COPPER_INGOT,
        voxel::GOLD_INGOT,
    };
    return kItems;
}

bool CreativeMenu::itemMatchesSearch(voxel::BlockId id, const std::string &search) const {
    const std::string needle = toLowerAscii(search);
    if (needle.empty()) {
        return true;
    }
    return toLowerAscii(game::blockName(id)).find(needle) != std::string::npos;
}

CreativeMenuLayout CreativeMenu::computeLayout(int width, int height, float hudScale,
                                               int craftingGridSize, bool usingFurnace,
                                               std::size_t itemCount) const {
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
    const float tableGridW = 3.0f * craftSlot + 2.0f * craftGap;
    const float tablePanelW = tableGridW + 44.0f * uiScale + craftSlot;
    const float furnaceCenterOffset = usingFurnace ? (tablePanelW - craftPanelW) * 0.35f : 0.0f;
    const float invX = cx - totalPanelW * 0.5f + furnaceCenterOffset;
    const float invY = y0 - 44.0f * uiScale - invH;

    const float panelHeaderH = 34.0f * uiScale;
    const float panelBodyH = 220.0f * uiScale;
    const float panelH = panelHeaderH + panelBodyH;
    const float panelY = invY - panelH - 44.0f * uiScale;
    const float panelW = totalPanelW;

    const float searchX = invX + 88.0f * uiScale;
    const float searchY = panelY + 6.0f * uiScale;
    const float searchW = panelW - 126.0f * uiScale;
    const float searchH = 22.0f * uiScale;

    const float contentX = invX + 10.0f * uiScale;
    const float contentY = panelY + panelHeaderH;
    const float contentW = panelW - 28.0f * uiScale;
    const float contentH = panelBodyH - 10.0f * uiScale;

    const float cell = 40.0f * uiScale;
    const float cellGap = 8.0f * uiScale;
    const int columns =
        std::max(1, std::min(9, static_cast<int>((contentW + cellGap) / (cell + cellGap))));
    const float usedW =
        static_cast<float>(columns) * cell + static_cast<float>(columns - 1) * cellGap;
    const float gridInset = std::max(0.0f, (contentW - usedW) * 0.5f);
    const float rowStride = cell + cellGap;
    const int rowsCount = (static_cast<int>(itemCount) + columns - 1) / columns;
    const float totalContentH = static_cast<float>(rowsCount) * cell +
                                static_cast<float>(std::max(0, rowsCount - 1)) * cellGap;
    const float maxScroll = std::max(0.0f, totalContentH - contentH);

    const float trackX = invX + panelW - 12.0f * uiScale;
    const float trackY = contentY;
    const float trackW = 8.0f * uiScale;
    const float trackH = contentH;
    const float thumbH =
        (maxScroll > 0.0f) ? std::max(22.0f * uiScale, trackH * (contentH / totalContentH)) : trackH;

    return CreativeMenuLayout{invX, panelY, panelW, panelH, contentX, contentY, contentW,
                              contentH, searchX, searchY, searchW, searchH, columns, cell,
                              cellGap, gridInset, rowStride, maxScroll, trackX, trackY, trackW,
                              trackH, thumbH};
}

int CreativeMenu::itemAtCursor(double mx, double my, const CreativeMenuLayout &layout, float scroll,
                               std::size_t itemCount) const {
    if (mx < (layout.contentX + layout.gridInset) ||
        mx > (layout.contentX + layout.contentW - layout.gridInset) || my < layout.contentY ||
        my > (layout.contentY + layout.contentH)) {
        return -1;
    }
    const int rowsCount = (static_cast<int>(itemCount) + layout.columns - 1) / layout.columns;
    for (int row = 0; row < rowsCount; ++row) {
        const float sy = layout.contentY + static_cast<float>(row) * layout.rowStride - scroll;
        if (sy + layout.cell < layout.contentY || sy > (layout.contentY + layout.contentH)) {
            continue;
        }
        for (int col = 0; col < layout.columns; ++col) {
            const int idx = row * layout.columns + col;
            if (idx >= static_cast<int>(itemCount)) {
                break;
            }
            const float sx =
                layout.contentX + layout.gridInset + static_cast<float>(col) * (layout.cell + layout.cellGap);
            if (mx >= sx && mx <= (sx + layout.cell) && my >= sy && my <= (sy + layout.cell)) {
                return idx;
            }
        }
    }
    return -1;
}

void CreativeMenu::renderOverlay(int width, int height, float uiScale, int craftingGridSize,
                                 bool usingFurnace, float cursorX, float cursorY,
                                 float creativeScroll, float uiTimeSeconds,
                                 const std::string &creativeSearch,
                                 const std::vector<voxel::BlockId> &creativeItems,
                                 const voxel::BlockRegistry &registry,
                                 const HudDrawContext &draw, TooltipState &tooltip) const {
    const CreativeMenuLayout layout =
        computeLayout(width, height, uiScale, craftingGridSize, usingFurnace, creativeItems.size());
    draw.drawRect(layout.panelX, layout.panelY, layout.panelW, layout.panelH, 0.03f, 0.04f, 0.05f,
                  0.78f);
    draw.drawText(layout.panelX + 10.0f * uiScale, layout.panelY + 10.0f * uiScale, "Creative", 244,
                  246, 252, 255);
    draw.drawText(layout.panelX + layout.panelW - 36.0f * uiScale, layout.panelY + 9.0f * uiScale,
                  "F: Close", 182, 190, 206, 255);
    draw.drawRect(layout.searchX, layout.searchY, layout.searchW, layout.searchH, 0.06f, 0.08f, 0.10f,
                  0.95f);
    draw.drawRect(layout.searchX + 1.0f, layout.searchY + 1.0f, layout.searchW - 2.0f,
                  layout.searchH - 2.0f, 0.12f, 0.14f, 0.18f, 0.95f);
    const bool blinkOn = (static_cast<int>(std::floor(uiTimeSeconds * 2.0f)) % 2) == 0;
    const std::string searchText = creativeSearch.empty() ? "Search items..." : creativeSearch;
    draw.drawText(layout.searchX + 6.0f * uiScale, layout.searchY + 7.0f * uiScale,
                  searchText + (blinkOn ? "_" : ""), creativeSearch.empty() ? 168 : 232,
                  creativeSearch.empty() ? 176 : 238, creativeSearch.empty() ? 190 : 248, 255);

    const std::size_t iconClipBatch = draw.beginIconClipBatch(
        layout.contentX, layout.contentY, layout.contentW, layout.contentH);
    draw.drawRect(layout.contentX, layout.contentY, layout.contentW, layout.contentH, 0.09f, 0.10f,
                  0.12f, 0.70f);
    const float scroll = std::clamp(creativeScroll, 0.0f, layout.maxScroll);
    for (std::size_t i = 0; i < creativeItems.size(); ++i) {
        const int row = static_cast<int>(i) / layout.columns;
        const int col = static_cast<int>(i) % layout.columns;
        const float sx =
            layout.contentX + layout.gridInset + static_cast<float>(col) * (layout.cell + layout.cellGap);
        const float sy = layout.contentY + static_cast<float>(row) * layout.rowStride - scroll;
        if (sy + layout.cell < layout.contentY || sy > (layout.contentY + layout.contentH)) {
            continue;
        }

        draw.drawRectClipped(sx + 2.0f, sy + 2.0f, layout.cell, layout.cell, layout.contentX,
                             layout.contentY, layout.contentX + layout.contentW,
                             layout.contentY + layout.contentH, 0.0f, 0.0f, 0.0f, 0.32f);
        draw.drawRectClipped(sx, sy, layout.cell, layout.cell, layout.contentX, layout.contentY,
                             layout.contentX + layout.contentW, layout.contentY + layout.contentH, 0.09f,
                             0.10f, 0.12f, 0.88f);
        draw.drawRectClipped(sx + 2.0f, sy + 2.0f, layout.cell - 4.0f, layout.cell - 4.0f,
                             layout.contentX, layout.contentY, layout.contentX + layout.contentW,
                             layout.contentY + layout.contentH, 0.16f, 0.17f, 0.20f, 0.76f);

        const voxel::BlockId id = creativeItems[i];
        const voxel::BlockDef &def = registry.get(id);
        if (draw.isFlatItemId(id)) {
            const float ix = sx + 7.0f * uiScale;
            const float iy = sy + 7.0f * uiScale;
            const float is = layout.cell - 14.0f * uiScale;
            draw.appendItemIcon(ix, iy, ix + is, iy + is, id, 1.0f);
        } else {
            const float cubeInset = 4.0f * uiScale;
            draw.appendCubeIcon(sx + cubeInset, sy + cubeInset, layout.cell - 2.0f * cubeInset,
                                layout.cell - 2.0f * cubeInset, def);
        }
        if (cursorX >= sx && cursorX <= (sx + layout.cell) && cursorY >= sy &&
            cursorY <= (sy + layout.cell)) {
            tooltip.text = game::blockName(id);
            tooltip.x = cursorX + 12.0f * uiScale;
            tooltip.y = cursorY - 8.0f * uiScale;
        }
    }
    draw.endIconClipBatch(iconClipBatch);

    draw.drawRect(layout.trackX, layout.trackY, 4.0f * uiScale, layout.trackH, 0.14f, 0.16f, 0.18f,
                  0.95f);
    if (layout.maxScroll > 0.0f) {
        const float thumbTravel = std::max(0.0f, layout.trackH - layout.thumbH);
        const float t = scroll / layout.maxScroll;
        const float thumbY = layout.trackY + t * thumbTravel;
        draw.drawRect(layout.trackX - 2.0f * uiScale, thumbY, 8.0f * uiScale, layout.thumbH, 0.30f,
                      0.72f, 0.95f, 0.95f);
    } else {
        draw.drawRect(layout.trackX - 2.0f * uiScale, layout.trackY, 8.0f * uiScale, layout.trackH,
                      0.24f, 0.28f, 0.33f, 0.85f);
    }
}

} // namespace app::menus
