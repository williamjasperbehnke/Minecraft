#pragma once

#include "game/Inventory.hpp"

#include <array>
#include <optional>
#include <vector>

namespace game {

class Recipe {
  public:
    static constexpr int kMaxInputCount = 9;

    struct Ingredient {
        voxel::BlockId id = voxel::AIR;
        bool allowAnyWood = false;
        int count = 1;
        bool allowAnyPlanks = false;
    };

    struct ShapedCell {
        int slot = 0;
        Ingredient ingredient{};
    };

    Recipe(std::vector<Ingredient> ingredients, Inventory::Slot output, int minGridSize = 2,
           int requiredOccupiedSlots = 0);
    Recipe(std::vector<ShapedCell> shapedCells, Inventory::Slot output, int gridSize);

    bool matches(const std::array<Inventory::Slot, kMaxInputCount> &input, int inputCount,
                 int gridSize) const;
    bool consume(std::array<Inventory::Slot, kMaxInputCount> &input, int inputCount,
                 int gridSize) const;
    const Inventory::Slot &output() const {
        return output_;
    }

  private:
    static bool ingredientMatches(Ingredient ingredient, voxel::BlockId id);
    static void clearIfEmpty(Inventory::Slot &slot);
    std::optional<std::vector<int>>
    findShapedMatchSlots(const std::array<Inventory::Slot, kMaxInputCount> &input, int inputCount,
                         int gridSize) const;
    bool matchesShaped(const std::array<Inventory::Slot, kMaxInputCount> &input, int inputCount,
                      int gridSize) const;
    bool consumeShaped(std::array<Inventory::Slot, kMaxInputCount> &input, int inputCount,
                      int gridSize) const;

    std::vector<Ingredient> ingredients_{};
    std::vector<ShapedCell> shapedCells_{};
    int minGridSize_ = 2;
    int requiredOccupiedSlots_ = 0;
    bool shaped_ = false;
    Inventory::Slot output_{};
};

} // namespace game
