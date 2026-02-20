#include "game/CraftingSystem.hpp"

#include <algorithm>

namespace game {

CraftingSystem::CraftingSystem() {
    auto addShapeless = [&](std::vector<Recipe::Ingredient> ingredients, voxel::BlockId outId,
                            int outCount, const std::string &label, int minGrid, int exactSlots,
                            std::vector<RecipeInfo::IngredientInfo> infoIn) {
        recipes_.emplace_back(std::move(ingredients), Inventory::Slot{outId, outCount}, minGrid,
                              exactSlots);
        RecipeInfo info{};
        info.ingredients = std::move(infoIn);
        info.outputId = outId;
        info.outputCount = outCount;
        info.label = label;
        info.minGridSize = minGrid;
        recipeInfos_.push_back(std::move(info));
    };

    auto addShaped = [&](std::vector<Recipe::ShapedCell> cells, voxel::BlockId outId, int outCount,
                         const std::string &label, int gridSize,
                         std::vector<RecipeInfo::IngredientInfo> infoIn,
                         std::vector<int> shapedSlots,
                         std::vector<RecipeInfo::ShapedCellInfo> shapedCells) {
        recipes_.emplace_back(cells, Inventory::Slot{outId, outCount}, gridSize);
        RecipeInfo info{};
        info.ingredients = std::move(infoIn);
        info.shapedSlots = std::move(shapedSlots);
        info.shapedCells = std::move(shapedCells);
        info.outputId = outId;
        info.outputCount = outCount;
        info.label = label;
        info.minGridSize = gridSize;
        recipeInfos_.push_back(std::move(info));
    };

    // Minecraft-like starter recipes.
    addShapeless({{voxel::WOOD, false, 1}}, voxel::OAK_PLANKS, 4, "Oak Planks", 2, 1,
                 {{voxel::WOOD, 1, false}});
    addShapeless({{voxel::SPRUCE_WOOD, false, 1}}, voxel::SPRUCE_PLANKS, 4, "Spruce Planks", 2, 1,
                 {{voxel::SPRUCE_WOOD, 1, false}});
    addShapeless({{voxel::BIRCH_WOOD, false, 1}}, voxel::BIRCH_PLANKS, 4, "Birch Planks", 2, 1,
                 {{voxel::BIRCH_WOOD, 1, false}});

    addShaped({{0, {voxel::AIR, false, 1, true}}, {2, {voxel::AIR, false, 1, true}}},
              voxel::STICK, 4, "Sticks", 2, {{voxel::OAK_PLANKS, 2, false, true}}, {0, 2},
              {{0, {voxel::OAK_PLANKS, 1, false, true}},
               {2, {voxel::OAK_PLANKS, 1, false, true}}});

    addShaped({{0, {voxel::AIR, false, 1, true}},
               {1, {voxel::AIR, false, 1, true}},
               {2, {voxel::AIR, false, 1, true}},
               {3, {voxel::AIR, false, 1, true}}},
              voxel::CRAFTING_TABLE, 1, "Crafting Table", 2,
              {{voxel::OAK_PLANKS, 4, false, true}}, {0, 1, 2, 3},
              {{0, {voxel::OAK_PLANKS, 1, false, true}},
               {1, {voxel::OAK_PLANKS, 1, false, true}},
               {2, {voxel::OAK_PLANKS, 1, false, true}},
               {3, {voxel::OAK_PLANKS, 1, false, true}}});

    addShapeless({{voxel::COAL_ORE, false, 1}, {voxel::STICK, false, 1}}, voxel::TORCH, 4, "Torch",
                 2, 2, {{voxel::COAL_ORE, 1, false}, {voxel::STICK, 1, false}});

    addShaped({{0, {voxel::SAND, false, 1}},
               {1, {voxel::SAND, false, 1}},
               {2, {voxel::SAND, false, 1}},
               {3, {voxel::SAND, false, 1}}},
              voxel::SANDSTONE, 1, "Sandstone", 2, {{voxel::SAND, 4, false}}, {0, 1, 2, 3},
              {{0, {voxel::SAND, 1, false}},
               {1, {voxel::SAND, 1, false}},
               {2, {voxel::SAND, 1, false}},
               {3, {voxel::SAND, 1, false}}});
}

int CraftingSystem::activeInputCount(int gridSize) {
    const int clamped = std::clamp(gridSize, kGridSizeInventory, kGridSizeTable);
    return clamped * clamped;
}

const Recipe *CraftingSystem::findMatch(const State &state, int gridSize) const {
    const int inputCount = activeInputCount(gridSize);
    for (const auto &recipe : recipes_) {
        if (recipe.matches(state.input, inputCount, gridSize)) {
            return &recipe;
        }
    }
    return nullptr;
}

void CraftingSystem::updateOutput(State &state, int gridSize) const {
    state.output = {};
    if (const Recipe *recipe = findMatch(state, gridSize)) {
        state.output = recipe->output();
    }
}

bool CraftingSystem::consumeInputs(State &state, int gridSize) const {
    const Recipe *recipe = findMatch(state, gridSize);
    if (recipe == nullptr) {
        return false;
    }
    const bool consumed = recipe->consume(state.input, activeInputCount(gridSize), gridSize);
    updateOutput(state, gridSize);
    return consumed;
}

} // namespace game
