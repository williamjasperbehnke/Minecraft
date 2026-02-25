#include "game/CraftingSystem.hpp"

#include <algorithm>
#include <cmath>

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

    addShaped({{0, {voxel::STONE, false, 1}},
               {1, {voxel::STONE, false, 1}},
               {2, {voxel::STONE, false, 1}},
               {3, {voxel::STONE, false, 1}},
               {5, {voxel::STONE, false, 1}},
               {6, {voxel::STONE, false, 1}},
               {7, {voxel::STONE, false, 1}},
               {8, {voxel::STONE, false, 1}}},
              voxel::FURNACE, 1, "Furnace", 3, {{voxel::STONE, 8, false}},
              {0, 1, 2, 3, 5, 6, 7, 8},
              {{0, {voxel::STONE, 1, false}},
               {1, {voxel::STONE, 1, false}},
               {2, {voxel::STONE, 1, false}},
               {3, {voxel::STONE, 1, false}},
               {5, {voxel::STONE, 1, false}},
               {6, {voxel::STONE, 1, false}},
               {7, {voxel::STONE, 1, false}},
               {8, {voxel::STONE, 1, false}}});
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

bool CraftingSystem::recipeIngredientMatches(const RecipeInfo::IngredientInfo &ingredient,
                                             voxel::BlockId id) const {
    if (ingredient.allowAnyWood) {
        return id == voxel::WOOD || id == voxel::SPRUCE_WOOD || id == voxel::BIRCH_WOOD;
    }
    if (ingredient.allowAnyPlanks) {
        return id == voxel::OAK_PLANKS || id == voxel::SPRUCE_PLANKS || id == voxel::BIRCH_PLANKS;
    }
    return ingredient.id == id;
}

bool CraftingSystem::takeIngredientFromInventory(Inventory &inventory,
                                                 const RecipeInfo::IngredientInfo &ingredient,
                                                 voxel::BlockId &takenId) const {
    for (int i = 0; i < Inventory::kSlotCount; ++i) {
        auto &slot = inventory.slot(i);
        if (slot.id == voxel::AIR || slot.count <= 0 || !recipeIngredientMatches(ingredient, slot.id)) {
            continue;
        }
        takenId = slot.id;
        slot.count -= 1;
        if (slot.count <= 0) {
            slot = {};
        }
        return true;
    }
    return false;
}

bool CraftingSystem::tryAddRecipeSet(const RecipeInfo &recipe, Inventory &inventory,
                                     State &crafting, int activeInputs) const {
    Inventory invBackup = inventory;
    State craftBackup = crafting;

    auto compatible = [&](const Inventory::Slot &slot, const RecipeInfo::IngredientInfo &ingredient) {
        if (slot.id == voxel::AIR || slot.count <= 0) {
            return false;
        }
        return recipeIngredientMatches(ingredient, slot.id);
    };

    if (!recipe.shapedCells.empty()) {
        const int gridSize = static_cast<int>(std::round(std::sqrt(static_cast<float>(activeInputs))));
        if (gridSize * gridSize != activeInputs) {
            inventory = invBackup;
            crafting = craftBackup;
            return false;
        }
        const int baseSize = std::max(2, recipe.minGridSize);
        int minRow = baseSize;
        int minCol = baseSize;
        int maxRow = -1;
        int maxCol = -1;
        for (const auto &cell : recipe.shapedCells) {
            if (cell.slot < 0 || cell.slot >= baseSize * baseSize) {
                inventory = invBackup;
                crafting = craftBackup;
                return false;
            }
            const int row = cell.slot / baseSize;
            const int col = cell.slot % baseSize;
            minRow = std::min(minRow, row);
            minCol = std::min(minCol, col);
            maxRow = std::max(maxRow, row);
            maxCol = std::max(maxCol, col);
        }
        const int patternH = maxRow - minRow + 1;
        const int patternW = maxCol - minCol + 1;
        if (patternH <= 0 || patternW <= 0 || patternH > gridSize || patternW > gridSize) {
            inventory = invBackup;
            crafting = craftBackup;
            return false;
        }

        std::vector<int> mappedSlots(recipe.shapedCells.size(), -1);
        int bestScore = -1;
        bool foundPlacement = false;
        for (int offY = 0; offY <= (gridSize - patternH); ++offY) {
            for (int offX = 0; offX <= (gridSize - patternW); ++offX) {
                bool ok = true;
                int score = 0;
                std::vector<int> trialSlots(recipe.shapedCells.size(), -1);
                for (std::size_t i = 0; i < recipe.shapedCells.size(); ++i) {
                    const auto &cell = recipe.shapedCells[i];
                    const int row = cell.slot / baseSize;
                    const int col = cell.slot % baseSize;
                    const int relRow = row - minRow;
                    const int relCol = col - minCol;
                    const int mapped = (offY + relRow) * gridSize + (offX + relCol);
                    if (mapped < 0 || mapped >= activeInputs) {
                        ok = false;
                        break;
                    }
                    trialSlots[i] = mapped;
                    const auto &dst = crafting.input[mapped];
                    if (dst.id != voxel::AIR && dst.count > 0) {
                        if (!compatible(dst, cell.ingredient)) {
                            ok = false;
                            break;
                        }
                        score += 1;
                    }
                    if (dst.count + cell.ingredient.count > Inventory::kMaxStack) {
                        ok = false;
                        break;
                    }
                }
                if (ok && score > bestScore) {
                    mappedSlots = std::move(trialSlots);
                    bestScore = score;
                    foundPlacement = true;
                }
            }
        }
        if (!foundPlacement) {
            inventory = invBackup;
            crafting = craftBackup;
            return false;
        }

        for (std::size_t i = 0; i < recipe.shapedCells.size(); ++i) {
            const auto &cell = recipe.shapedCells[i];
            auto &dst = crafting.input[mappedSlots[i]];
            for (int n = 0; n < cell.ingredient.count; ++n) {
                voxel::BlockId taken = voxel::AIR;
                if (!takeIngredientFromInventory(inventory, cell.ingredient, taken)) {
                    inventory = invBackup;
                    crafting = craftBackup;
                    return false;
                }
                if (dst.id == voxel::AIR || dst.count <= 0) {
                    dst.id = taken;
                    dst.count = 0;
                }
                dst.count += 1;
            }
        }
        return true;
    }

    for (const auto &ingredient : recipe.ingredients) {
        for (int n = 0; n < ingredient.count; ++n) {
            voxel::BlockId taken = voxel::AIR;
            if (!takeIngredientFromInventory(inventory, ingredient, taken)) {
                inventory = invBackup;
                crafting = craftBackup;
                return false;
            }

            int placeIdx = -1;
            int firstEmpty = -1;
            int matchingOccupied = 0;
            int leastMatchingIdx = -1;
            int leastMatchingCount = Inventory::kMaxStack + 1;
            for (int i = 0; i < activeInputs; ++i) {
                const auto &dst = crafting.input[i];
                if (dst.id == voxel::AIR || dst.count <= 0) {
                    if (firstEmpty < 0) {
                        firstEmpty = i;
                    }
                    continue;
                }
                if (!recipeIngredientMatches(ingredient, dst.id)) {
                    continue;
                }
                ++matchingOccupied;
                if (dst.count < Inventory::kMaxStack && dst.count < leastMatchingCount) {
                    leastMatchingCount = dst.count;
                    leastMatchingIdx = i;
                }
            }

            const int desiredSlotsForIngredient = std::max(1, ingredient.count);
            if (matchingOccupied < desiredSlotsForIngredient && firstEmpty >= 0) {
                placeIdx = firstEmpty;
            } else if (leastMatchingIdx >= 0) {
                placeIdx = leastMatchingIdx;
            } else if (firstEmpty >= 0) {
                placeIdx = firstEmpty;
            }
            if (placeIdx < 0) {
                inventory = invBackup;
                crafting = craftBackup;
                return false;
            }

            auto &dst = crafting.input[placeIdx];
            if (dst.id == voxel::AIR || dst.count <= 0) {
                dst.id = taken;
                dst.count = 0;
            }
            dst.count += 1;
        }
    }

    return true;
}

} // namespace game
