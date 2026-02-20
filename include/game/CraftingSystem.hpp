#pragma once

#include "game/Inventory.hpp"
#include "game/Recipe.hpp"

#include <array>
#include <string>
#include <vector>

namespace game {

class CraftingSystem {
  public:
    static constexpr int kGridSizeInventory = 2;
    static constexpr int kGridSizeTable = 3;
    static constexpr int kInputCount = Recipe::kMaxInputCount;
    static constexpr int kOutputSlotIndex = game::Inventory::kSlotCount + kInputCount;
    static constexpr int kUiSlotCount = game::Inventory::kSlotCount + kInputCount + 1;

    struct State {
        std::array<Inventory::Slot, kInputCount> input{};
        Inventory::Slot output{};
    };

    struct RecipeInfo {
        struct IngredientInfo {
            voxel::BlockId id = voxel::AIR;
            int count = 0;
            bool allowAnyWood = false;
            bool allowAnyPlanks = false;
        };
        struct ShapedCellInfo {
            int slot = 0;
            IngredientInfo ingredient{};
        };
        std::vector<IngredientInfo> ingredients;
        std::vector<int> shapedSlots;
        std::vector<ShapedCellInfo> shapedCells;
        voxel::BlockId outputId = voxel::AIR;
        int outputCount = 0;
        std::string label;
        int minGridSize = kGridSizeInventory;
    };

    CraftingSystem();

    static int activeInputCount(int gridSize);

    void updateOutput(State &state, int gridSize) const;
    bool consumeInputs(State &state, int gridSize) const;
    const std::vector<RecipeInfo> &recipeInfos() const {
        return recipeInfos_;
    }

  private:
    const Recipe *findMatch(const State &state, int gridSize) const;

    std::vector<Recipe> recipes_{};
    std::vector<RecipeInfo> recipeInfos_{};
};

} // namespace game
