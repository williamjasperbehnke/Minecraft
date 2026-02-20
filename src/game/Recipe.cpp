#include "game/Recipe.hpp"

#include <algorithm>

namespace game {
namespace {
voxel::BlockId normalizeWood(voxel::BlockId id) {
    if (id == voxel::WOOD || id == voxel::SPRUCE_WOOD || id == voxel::BIRCH_WOOD) {
        return voxel::WOOD;
    }
    return id;
}

voxel::BlockId normalizePlanks(voxel::BlockId id) {
    if (id == voxel::OAK_PLANKS || id == voxel::SPRUCE_PLANKS || id == voxel::BIRCH_PLANKS) {
        return voxel::OAK_PLANKS;
    }
    return id;
}
} // namespace

Recipe::Recipe(std::vector<Ingredient> ingredients, Inventory::Slot output, int minGridSize,
               int requiredOccupiedSlots)
    : ingredients_(std::move(ingredients)),
      minGridSize_(std::max(2, minGridSize)),
      requiredOccupiedSlots_(std::max(0, requiredOccupiedSlots)),
      output_(output) {}

Recipe::Recipe(std::vector<ShapedCell> shapedCells, Inventory::Slot output, int gridSize)
    : shapedCells_(std::move(shapedCells)),
      minGridSize_(std::max(2, gridSize)),
      requiredOccupiedSlots_(0),
      shaped_(true),
      output_(output) {}

bool Recipe::ingredientMatches(Ingredient ingredient, voxel::BlockId id) {
    if (ingredient.allowAnyWood) {
        return normalizeWood(id) == voxel::WOOD;
    }
    if (ingredient.allowAnyPlanks) {
        return normalizePlanks(id) == voxel::OAK_PLANKS;
    }
    return ingredient.id == id;
}

void Recipe::clearIfEmpty(Inventory::Slot &slot) {
    if (slot.count <= 0 || slot.id == voxel::AIR) {
        slot = {};
    }
}

bool Recipe::matches(const std::array<Inventory::Slot, kMaxInputCount> &input, int inputCount,
                     int gridSize) const {
    if (shaped_) {
        return matchesShaped(input, inputCount, gridSize);
    }
    if (gridSize < minGridSize_) {
        return false;
    }
    if (inputCount < 1 || inputCount > kMaxInputCount) {
        return false;
    }
    int occupiedSlots = 0;
    std::vector<int> available(ingredients_.size(), 0);
    for (int i = 0; i < inputCount; ++i) {
        const auto &slot = input[i];
        if (slot.count <= 0 || slot.id == voxel::AIR) {
            continue;
        }
        ++occupiedSlots;
        bool ingredientSupported = false;
        for (std::size_t r = 0; r < ingredients_.size(); ++r) {
            if (ingredientMatches(ingredients_[r], slot.id)) {
                available[r] += slot.count;
                ingredientSupported = true;
            }
        }
        if (!ingredientSupported) {
            return false;
        }
    }
    if (requiredOccupiedSlots_ > 0 && occupiedSlots != requiredOccupiedSlots_) {
        return false;
    }
    for (std::size_t r = 0; r < ingredients_.size(); ++r) {
        if (available[r] < ingredients_[r].count) {
            return false;
        }
    }
    return true;
}

bool Recipe::consume(std::array<Inventory::Slot, kMaxInputCount> &input, int inputCount,
                     int gridSize) const {
    if (shaped_) {
        return consumeShaped(input, inputCount, gridSize);
    }
    if (!matches(input, inputCount, gridSize)) {
        return false;
    }
    for (const Ingredient &ingredient : ingredients_) {
        int remaining = ingredient.count;
        if (requiredOccupiedSlots_ > 0) {
            for (int i = 0; i < inputCount && remaining > 0; ++i) {
                auto &slot = input[i];
                if (slot.count <= 0 || slot.id == voxel::AIR ||
                    !ingredientMatches(ingredient, slot.id)) {
                    continue;
                }
                slot.count -= 1;
                remaining -= 1;
                clearIfEmpty(slot);
            }
            continue;
        }
        for (int i = 0; i < inputCount && remaining > 0; ++i) {
            auto &slot = input[i];
            if (slot.count <= 0 || slot.id == voxel::AIR || !ingredientMatches(ingredient, slot.id)) {
                continue;
            }
            const int take = std::min(slot.count, remaining);
            slot.count -= take;
            remaining -= take;
            clearIfEmpty(slot);
        }
    }
    return true;
}

bool Recipe::matchesShaped(const std::array<Inventory::Slot, kMaxInputCount> &input, int inputCount,
                           int gridSize) const {
    return findShapedMatchSlots(input, inputCount, gridSize).has_value();
}

bool Recipe::consumeShaped(std::array<Inventory::Slot, kMaxInputCount> &input, int inputCount,
                           int gridSize) const {
    const auto matchedSlots = findShapedMatchSlots(input, inputCount, gridSize);
    if (!matchedSlots.has_value()) {
        return false;
    }
    const auto &slots = matchedSlots.value();
    for (std::size_t i = 0; i < shapedCells_.size(); ++i) {
        auto &slot = input[slots[i]];
        slot.count -= shapedCells_[i].ingredient.count;
        clearIfEmpty(slot);
    }
    return true;
}

std::optional<std::vector<int>>
Recipe::findShapedMatchSlots(const std::array<Inventory::Slot, kMaxInputCount> &input,
                             int inputCount, int gridSize) const {
    if (gridSize < minGridSize_ || inputCount < 1 || inputCount > kMaxInputCount ||
        shapedCells_.empty()) {
        return std::nullopt;
    }

    struct CellInfo {
        int relRow = 0;
        int relCol = 0;
        Ingredient ingredient{};
    };
    std::vector<CellInfo> cells;
    cells.reserve(shapedCells_.size());

    const int baseSize = minGridSize_;
    const int baseArea = baseSize * baseSize;
    int minRow = baseSize;
    int minCol = baseSize;
    int maxRow = -1;
    int maxCol = -1;
    for (const auto &cell : shapedCells_) {
        if (cell.slot < 0 || cell.slot >= baseArea) {
            return std::nullopt;
        }
        const int row = cell.slot / baseSize;
        const int col = cell.slot % baseSize;
        minRow = std::min(minRow, row);
        minCol = std::min(minCol, col);
        maxRow = std::max(maxRow, row);
        maxCol = std::max(maxCol, col);
        cells.push_back({row, col, cell.ingredient});
    }

    if (minRow > maxRow || minCol > maxCol) {
        return std::nullopt;
    }

    const int patternH = maxRow - minRow + 1;
    const int patternW = maxCol - minCol + 1;
    if (patternH > gridSize || patternW > gridSize) {
        return std::nullopt;
    }

    for (auto &cell : cells) {
        cell.relRow -= minRow;
        cell.relCol -= minCol;
    }

    for (int offY = 0; offY <= (gridSize - patternH); ++offY) {
        for (int offX = 0; offX <= (gridSize - patternW); ++offX) {
            std::vector<int> mappedSlots;
            mappedSlots.reserve(cells.size());
            std::vector<bool> required(inputCount, false);

            bool ok = true;
            for (const auto &cell : cells) {
                const int slotIdx = (offY + cell.relRow) * gridSize + (offX + cell.relCol);
                if (slotIdx < 0 || slotIdx >= inputCount) {
                    ok = false;
                    break;
                }
                required[slotIdx] = true;
                mappedSlots.push_back(slotIdx);

                const auto &slot = input[slotIdx];
                if (slot.id == voxel::AIR || slot.count < cell.ingredient.count ||
                    !ingredientMatches(cell.ingredient, slot.id)) {
                    ok = false;
                    break;
                }
            }
            if (!ok) {
                continue;
            }

            for (int i = 0; i < inputCount; ++i) {
                if (required[i]) {
                    continue;
                }
                const auto &slot = input[i];
                if (slot.id != voxel::AIR && slot.count > 0) {
                    ok = false;
                    break;
                }
            }
            if (ok) {
                return mappedSlots;
            }
        }
    }
    return std::nullopt;
}

} // namespace game
