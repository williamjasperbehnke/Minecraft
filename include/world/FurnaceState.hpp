#pragma once

#include "voxel/Block.hpp"

#include <cstdint>

namespace world {

struct FurnaceSlotState {
    voxel::BlockId id = voxel::AIR;
    int count = 0;
};

struct FurnaceState {
    FurnaceSlotState input{};
    FurnaceSlotState fuel{};
    FurnaceSlotState output{};
    float progressSeconds = 0.0f;
    float burnSecondsRemaining = 0.0f;
    float burnSecondsCapacity = 0.0f;
};

struct FurnaceRecordLocal {
    std::uint8_t x = 0;
    std::uint8_t y = 0;
    std::uint8_t z = 0;
    FurnaceState state{};
};

} // namespace world
