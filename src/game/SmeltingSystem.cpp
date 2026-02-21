#include "game/SmeltingSystem.hpp"

namespace game {

SmeltingSystem::SmeltingSystem() {
    recipes_.push_back({voxel::SAND, voxel::GLASS, 1});
    recipes_.push_back({voxel::CLAY, voxel::BRICKS, 1});
    recipes_.push_back({voxel::IRON_ORE, voxel::IRON_INGOT, 1});
    recipes_.push_back({voxel::COPPER_ORE, voxel::COPPER_INGOT, 1});
    recipes_.push_back({voxel::GOLD_ORE, voxel::GOLD_INGOT, 1});
}

const SmeltingSystem::Recipe *SmeltingSystem::findRecipe(voxel::BlockId input) const {
    for (const Recipe &r : recipes_) {
        if (r.input == input) {
            return &r;
        }
    }
    return nullptr;
}

bool SmeltingSystem::canSmelt(voxel::BlockId input) const {
    return findRecipe(input) != nullptr;
}

float SmeltingSystem::fuelSeconds(voxel::BlockId id) const {
    switch (id) {
    case voxel::COAL_ORE:
        return 40.0f;
    case voxel::WOOD:
    case voxel::SPRUCE_WOOD:
    case voxel::BIRCH_WOOD:
        return 8.0f;
    case voxel::OAK_PLANKS:
    case voxel::SPRUCE_PLANKS:
    case voxel::BIRCH_PLANKS:
    case voxel::CRAFTING_TABLE:
        return 6.0f;
    case voxel::STICK:
        return 2.0f;
    case voxel::LEAVES:
    case voxel::SPRUCE_LEAVES:
    case voxel::BIRCH_LEAVES:
    case voxel::TALL_GRASS:
        return 1.5f;
    case voxel::CACTUS:
        return 5.0f;
    default:
        return 0.0f;
    }
}

bool SmeltingSystem::isFuel(voxel::BlockId id) const {
    return fuelSeconds(id) > 0.0f;
}

void SmeltingSystem::update(State &state, float dt) const {
    const bool hasInput = (state.input.id != voxel::AIR && state.input.count > 0);
    const Recipe *recipe = hasInput ? findRecipe(state.input.id) : nullptr;
    const bool outputCompatible =
        (recipe != nullptr) && (state.output.id == voxel::AIR || state.output.id == recipe->output);
    const bool outputHasSpace =
        (recipe != nullptr) && (state.output.count + recipe->outputCount <= Inventory::kMaxStack);
    const bool canSmeltNow = hasInput && recipe != nullptr && outputCompatible && outputHasSpace;

    if (!hasInput || recipe == nullptr) {
        state.progressSeconds = 0.0f;
    }

    // Only ignite new fuel when smelting can actually proceed.
    if (state.burnSecondsRemaining <= 0.0f && canSmeltNow && state.fuel.id != voxel::AIR &&
        state.fuel.count > 0) {
        const float burn = fuelSeconds(state.fuel.id);
        if (burn > 0.0f) {
            state.fuel.count -= 1;
            if (state.fuel.count <= 0) {
                state.fuel = {};
            }
            state.burnSecondsRemaining += burn;
            state.burnSecondsCapacity = burn;
        }
    }

    if (state.burnSecondsRemaining <= 0.0f) {
        return;
    }

    const float burnStep = std::min(dt, state.burnSecondsRemaining);
    state.burnSecondsRemaining = std::max(0.0f, state.burnSecondsRemaining - burnStep);

    if (!canSmeltNow || recipe == nullptr) {
        return;
    }

    state.progressSeconds += burnStep;

    while (state.progressSeconds >= kSmeltSeconds && state.input.count > 0) {
        if (state.input.count <= 0) {
            state.input = {};
            state.progressSeconds = 0.0f;
            break;
        }
        if (state.output.id != voxel::AIR && state.output.id != recipe->output) {
            break;
        }
        if (state.output.count + recipe->outputCount > Inventory::kMaxStack) {
            break;
        }

        state.input.count -= 1;
        if (state.input.count <= 0) {
            state.input = {};
        }

        if (state.output.id == voxel::AIR || state.output.count <= 0) {
            state.output.id = recipe->output;
            state.output.count = 0;
        }
        state.output.count += recipe->outputCount;
        state.progressSeconds -= kSmeltSeconds;

        if (state.input.id == voxel::AIR || state.input.count <= 0) {
            state.progressSeconds = 0.0f;
            break;
        }
    }
}

} // namespace game
