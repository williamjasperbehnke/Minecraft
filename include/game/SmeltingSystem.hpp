#pragma once

#include "game/Inventory.hpp"

#include <vector>

namespace game {

class SmeltingSystem {
  public:
    struct Recipe {
        voxel::BlockId input = voxel::AIR;
        voxel::BlockId output = voxel::AIR;
        int outputCount = 1;
    };

    struct State {
        Inventory::Slot input{};
        Inventory::Slot fuel{};
        Inventory::Slot output{};
        float progressSeconds = 0.0f;
        float burnSecondsRemaining = 0.0f;
        float burnSecondsCapacity = 0.0f;
    };

    static constexpr float kSmeltSeconds = 2.6f;

    SmeltingSystem();

    void update(State &state, float dt) const;
    const std::vector<Recipe> &recipes() const {
        return recipes_;
    }
    bool canSmelt(voxel::BlockId input) const;
    bool isFuel(voxel::BlockId id) const;
    float fuelSeconds(voxel::BlockId id) const;

  private:
    const Recipe *findRecipe(voxel::BlockId input) const;

    std::vector<Recipe> recipes_{};
};

} // namespace game
