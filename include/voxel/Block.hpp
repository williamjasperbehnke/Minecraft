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
constexpr BlockId CRAFTING_TABLE = 31;
constexpr BlockId OAK_PLANKS = 32;
constexpr BlockId SPRUCE_PLANKS = 33;
constexpr BlockId BIRCH_PLANKS = 34;
constexpr BlockId STICK = 35;
constexpr BlockId FURNACE = 36;
constexpr BlockId GLASS = 37;
constexpr BlockId BRICKS = 38;
constexpr BlockId IRON_INGOT = 39;
constexpr BlockId COPPER_INGOT = 40;
constexpr BlockId GOLD_INGOT = 41;
constexpr BlockId FURNACE_POS_X = 42;
constexpr BlockId FURNACE_NEG_X = 43;
constexpr BlockId FURNACE_POS_Z = 44;
constexpr BlockId FURNACE_NEG_Z = 45;
constexpr BlockId LIT_FURNACE_POS_X = 46;
constexpr BlockId LIT_FURNACE_NEG_X = 47;
constexpr BlockId LIT_FURNACE_POS_Z = 48;
constexpr BlockId LIT_FURNACE_NEG_Z = 49;

inline bool isTorch(BlockId id) {
    return id == TORCH || id == TORCH_WALL_POS_X || id == TORCH_WALL_NEG_X ||
           id == TORCH_WALL_POS_Z || id == TORCH_WALL_NEG_Z;
}

inline bool isWallTorch(BlockId id) {
    return id == TORCH_WALL_POS_X || id == TORCH_WALL_NEG_X || id == TORCH_WALL_POS_Z ||
           id == TORCH_WALL_NEG_Z;
}

inline bool isFurnace(BlockId id) {
    return id == FURNACE || id == FURNACE_POS_X || id == FURNACE_NEG_X || id == FURNACE_POS_Z ||
           id == FURNACE_NEG_Z || id == LIT_FURNACE_POS_X || id == LIT_FURNACE_NEG_X ||
           id == LIT_FURNACE_POS_Z || id == LIT_FURNACE_NEG_Z;
}

inline bool isLitFurnace(BlockId id) {
    return id == LIT_FURNACE_POS_X || id == LIT_FURNACE_NEG_X || id == LIT_FURNACE_POS_Z ||
           id == LIT_FURNACE_NEG_Z;
}

inline BlockId toUnlitFurnace(BlockId id) {
    switch (id) {
    case LIT_FURNACE_POS_X:
        return FURNACE_POS_X;
    case LIT_FURNACE_NEG_X:
        return FURNACE_NEG_X;
    case LIT_FURNACE_POS_Z:
        return FURNACE_POS_Z;
    case LIT_FURNACE_NEG_Z:
        return FURNACE_NEG_Z;
    case FURNACE:
        return FURNACE_NEG_Z;
    default:
        return id;
    }
}

inline BlockId toLitFurnace(BlockId id) {
    switch (id) {
    case FURNACE_POS_X:
        return LIT_FURNACE_POS_X;
    case FURNACE_NEG_X:
        return LIT_FURNACE_NEG_X;
    case FURNACE_POS_Z:
        return LIT_FURNACE_POS_Z;
    case FURNACE_NEG_Z:
    case FURNACE:
        return LIT_FURNACE_NEG_Z;
    default:
        return id;
    }
}

inline std::uint8_t emittedBlockLight(BlockId id) {
    if (isTorch(id)) {
        return 14;
    }
    if (isLitFurnace(id)) {
        return 13;
    }
    return 0;
}

struct BlockDef {
    bool solid = false;
    bool transparent = true;
    std::uint16_t sideTile = 0;
    std::uint16_t topTile = 0;
    std::uint16_t bottomTile = 0;
};

// Shared atlas tile ids for furnace rendering across world/UI/item meshes.
constexpr std::uint16_t TILE_FURNACE_FRONT = 40;
constexpr std::uint16_t TILE_FURNACE_TOP_BOTTOM = 41;
constexpr std::uint16_t TILE_FURNACE_SIDE = 42;
constexpr std::uint16_t TILE_FURNACE_FRONT_LIT = 43;

inline std::uint16_t furnaceFrontTile(bool lit) {
    return lit ? TILE_FURNACE_FRONT_LIT : TILE_FURNACE_FRONT;
}

inline bool isFurnaceDef(const BlockDef &def) {
    return def.sideTile == TILE_FURNACE_SIDE && def.topTile == TILE_FURNACE_TOP_BOTTOM &&
           def.bottomTile == TILE_FURNACE_TOP_BOTTOM;
}

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
        defs_[CRAFTING_TABLE] = {true, false, 32, 33, 34};
        defs_[OAK_PLANKS] = {true, false, 35, 35, 35};
        defs_[SPRUCE_PLANKS] = {true, false, 36, 36, 36};
        defs_[BIRCH_PLANKS] = {true, false, 37, 37, 37};
        defs_[STICK] = {false, true, 38, 38, 38};
        defs_[FURNACE] = {true, false, TILE_FURNACE_SIDE, TILE_FURNACE_TOP_BOTTOM,
                          TILE_FURNACE_TOP_BOTTOM};
        defs_[FURNACE_POS_X] = {true, false, TILE_FURNACE_SIDE, TILE_FURNACE_TOP_BOTTOM,
                                TILE_FURNACE_TOP_BOTTOM};
        defs_[FURNACE_NEG_X] = {true, false, TILE_FURNACE_SIDE, TILE_FURNACE_TOP_BOTTOM,
                                TILE_FURNACE_TOP_BOTTOM};
        defs_[FURNACE_POS_Z] = {true, false, TILE_FURNACE_SIDE, TILE_FURNACE_TOP_BOTTOM,
                                TILE_FURNACE_TOP_BOTTOM};
        defs_[FURNACE_NEG_Z] = {true, false, TILE_FURNACE_SIDE, TILE_FURNACE_TOP_BOTTOM,
                                TILE_FURNACE_TOP_BOTTOM};
        defs_[LIT_FURNACE_POS_X] = {true, false, TILE_FURNACE_SIDE, TILE_FURNACE_TOP_BOTTOM,
                                    TILE_FURNACE_TOP_BOTTOM};
        defs_[LIT_FURNACE_NEG_X] = {true, false, TILE_FURNACE_SIDE, TILE_FURNACE_TOP_BOTTOM,
                                    TILE_FURNACE_TOP_BOTTOM};
        defs_[LIT_FURNACE_POS_Z] = {true, false, TILE_FURNACE_SIDE, TILE_FURNACE_TOP_BOTTOM,
                                    TILE_FURNACE_TOP_BOTTOM};
        defs_[LIT_FURNACE_NEG_Z] = {true, false, TILE_FURNACE_SIDE, TILE_FURNACE_TOP_BOTTOM,
                                    TILE_FURNACE_TOP_BOTTOM};
        defs_[GLASS] = {true, true, 39, 39, 39};
        defs_[BRICKS] = {true, false, 44, 44, 44};
        defs_[IRON_INGOT] = {false, true, 45, 45, 45};
        defs_[COPPER_INGOT] = {false, true, 46, 46, 46};
        defs_[GOLD_INGOT] = {false, true, 47, 47, 47};
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
