#pragma once

#include "app/menus/BaseMenu.hpp"
#include "app/menus/HudDrawContext.hpp"

#include "game/CraftingSystem.hpp"
#include "game/SmeltingSystem.hpp"
#include "voxel/Block.hpp"

#include <optional>
#include <string>
#include <vector>

namespace app::menus {

struct RecipeMenuLayout {
    float panelX = 0.0f;
    float panelY = 0.0f;
    float panelW = 0.0f;
    float panelH = 0.0f;
    float contentX = 0.0f;
    float contentY = 0.0f;
    float contentW = 0.0f;
    float contentH = 0.0f;
    int columns = 2;
    float cellW = 0.0f;
    float cellH = 0.0f;
    float cellGapX = 0.0f;
    float cellGapY = 0.0f;
    float gridInsetLeft = 0.0f;
    float gridInsetRight = 0.0f;
    float rowStride = 0.0f;
    float trackX = 0.0f;
    float trackY = 0.0f;
    float trackW = 0.0f;
    float trackH = 0.0f;
    float thumbH = 0.0f;
    float thumbY = 0.0f;
    float maxScroll = 0.0f;
    float searchX = 0.0f;
    float searchY = 0.0f;
    float searchW = 0.0f;
    float searchH = 0.0f;
    float craftableFilterX = 0.0f;
    float craftableFilterY = 0.0f;
    float craftableFilterSize = 0.0f;
    float craftableFilterW = 0.0f;
    float craftableFilterH = 0.0f;
    float ingredientTagX = 0.0f;
    float ingredientTagY = 0.0f;
    float ingredientTagW = 0.0f;
    float ingredientTagH = 0.0f;
    float ingredientTagCloseX = 0.0f;
    float ingredientTagCloseY = 0.0f;
    float ingredientTagCloseS = 0.0f;
};

class RecipeMenu : public BaseMenu {
  public:
    const char *menuId() const override {
        return "recipes";
    }

    RecipeMenuLayout computeLayout(int width, int height, float hudScale, int craftingGridSize,
                                   bool usingFurnace, std::size_t recipeCount) const;
    int rowAtCursor(double mx, double my, const RecipeMenuLayout &layout, float scroll,
                    std::size_t recipeCount) const;

    bool matchesSearch(const game::CraftingSystem::RecipeInfo &recipe,
                       const std::string &search) const;
    bool usesIngredient(const game::CraftingSystem::RecipeInfo &recipe,
                        voxel::BlockId targetId) const;
    bool smeltingMatchesSearch(const game::SmeltingSystem::Recipe &recipe,
                               const std::string &search) const;
    void renderOverlay(int width, int height, float uiScale, int craftingGridSize,
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
                       TooltipState &tooltip) const;
};

} // namespace app::menus
