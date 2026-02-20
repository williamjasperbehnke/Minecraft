#pragma once

#include <array>
#include <cstdint>

namespace voxel {

using BlockId = std::uint16_t;

constexpr BlockId AIR = 0;
constexpr BlockId GRASS = 1;
constexpr BlockId DIRT = 2;
constexpr BlockId STONE = 3;
constexpr BlockId SAND = 4;
constexpr BlockId WATER = 5;
constexpr BlockId WOOD = 6;
constexpr BlockId LEAVES = 7;
constexpr BlockId COAL_ORE = 8;
constexpr BlockId COPPER_ORE = 9;
constexpr BlockId IRON_ORE = 10;
constexpr BlockId GOLD_ORE = 11;
constexpr BlockId DIAMOND_ORE = 12;
constexpr BlockId EMERALD_ORE = 13;
constexpr BlockId GRAVEL = 14;
constexpr BlockId CLAY = 15;
constexpr BlockId SNOW_BLOCK = 16;
constexpr BlockId ICE = 17;
constexpr BlockId SPRUCE_WOOD = 18;
constexpr BlockId SPRUCE_LEAVES = 19;
constexpr BlockId BIRCH_WOOD = 20;
constexpr BlockId BIRCH_LEAVES = 21;
constexpr BlockId CACTUS = 22;
constexpr BlockId SANDSTONE = 23;
constexpr BlockId TALL_GRASS = 24;
constexpr BlockId FLOWER = 25;
constexpr BlockId TORCH = 26;
constexpr BlockId TORCH_WALL_POS_X = 27;
constexpr BlockId TORCH_WALL_NEG_X = 28;
constexpr BlockId TORCH_WALL_POS_Z = 29;
constexpr BlockId TORCH_WALL_NEG_Z = 30;

inline bool isTorch(BlockId id) {
    return id == TORCH || id == TORCH_WALL_POS_X || id == TORCH_WALL_NEG_X ||
           id == TORCH_WALL_POS_Z || id == TORCH_WALL_NEG_Z;
}

inline bool isWallTorch(BlockId id) {
    return id == TORCH_WALL_POS_X || id == TORCH_WALL_NEG_X || id == TORCH_WALL_POS_Z ||
           id == TORCH_WALL_NEG_Z;
}

struct BlockDef {
    bool solid = false;
    bool transparent = true;
    std::uint16_t sideTile = 0;
    std::uint16_t topTile = 0;
    std::uint16_t bottomTile = 0;
};

class BlockRegistry {
  public:
    BlockRegistry() {
        defs_[AIR] = {false, true, 0, 0, 0};
        defs_[GRASS] = {true, false, 1, 2, 3};
        defs_[DIRT] = {true, false, 3, 3, 3};
        defs_[STONE] = {true, false, 4, 4, 4};
        defs_[SAND] = {true, false, 5, 5, 5};
        defs_[WATER] = {true, true, 6, 6, 6};
        defs_[WOOD] = {true, false, 7, 8, 8};
        defs_[LEAVES] = {true, true, 9, 9, 9};
        defs_[COAL_ORE] = {true, false, 10, 10, 10};
        defs_[COPPER_ORE] = {true, false, 11, 11, 11};
        defs_[IRON_ORE] = {true, false, 12, 12, 12};
        defs_[GOLD_ORE] = {true, false, 13, 13, 13};
        defs_[DIAMOND_ORE] = {true, false, 14, 14, 14};
        defs_[EMERALD_ORE] = {true, false, 15, 15, 15};
        defs_[GRAVEL] = {true, false, 16, 16, 16};
        defs_[CLAY] = {true, false, 17, 17, 17};
        defs_[SNOW_BLOCK] = {true, false, 18, 18, 18};
        defs_[ICE] = {true, false, 19, 19, 19};
        defs_[SPRUCE_WOOD] = {true, false, 20, 21, 21};
        defs_[SPRUCE_LEAVES] = {true, true, 22, 22, 22};
        defs_[BIRCH_WOOD] = {true, false, 23, 24, 24};
        defs_[BIRCH_LEAVES] = {true, true, 25, 25, 25};
        defs_[CACTUS] = {true, false, 26, 27, 27};
        defs_[SANDSTONE] = {true, false, 28, 28, 28};
        defs_[TALL_GRASS] = {true, true, 29, 29, 29};
        defs_[FLOWER] = {true, true, 30, 30, 30};
        defs_[TORCH] = {true, true, 31, 31, 31};
        defs_[TORCH_WALL_POS_X] = {true, true, 31, 31, 31};
        defs_[TORCH_WALL_NEG_X] = {true, true, 31, 31, 31};
        defs_[TORCH_WALL_POS_Z] = {true, true, 31, 31, 31};
        defs_[TORCH_WALL_NEG_Z] = {true, true, 31, 31, 31};
    }

    void set(BlockId id, BlockDef def) {
        defs_[id] = def;
    }
    const BlockDef &get(BlockId id) const {
        return defs_[id];
    }

  private:
    std::array<BlockDef, 4096> defs_{};
};

} // namespace voxel
