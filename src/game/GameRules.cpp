#include "game/GameRules.hpp"

#include "world/World.hpp"

#include <cmath>

namespace game {

const char *blockName(voxel::BlockId id) {
    if (voxel::isFurnace(id)) {
        return "Furnace";
    }
    switch (id) {
    case voxel::GRASS:
        return "Grass Block";
    case voxel::DIRT:
        return "Dirt";
    case voxel::STONE:
        return "Stone";
    case voxel::SAND:
        return "Sand";
    case voxel::WOOD:
        return "Oak Log";
    case voxel::LEAVES:
        return "Leaves";
    case voxel::WATER:
        return "Water";
    case voxel::LAVA:
    case voxel::LAVA_SOURCE:
        return "Lava";
    case voxel::BEDROCK:
        return "Bedrock";
    case voxel::COAL_ORE:
        return "Coal Ore";
    case voxel::COPPER_ORE:
        return "Copper Ore";
    case voxel::IRON_ORE:
        return "Iron Ore";
    case voxel::GOLD_ORE:
        return "Gold Ore";
    case voxel::DIAMOND_ORE:
        return "Diamond Ore";
    case voxel::EMERALD_ORE:
        return "Emerald Ore";
    case voxel::GRAVEL:
        return "Gravel";
    case voxel::CLAY:
        return "Clay";
    case voxel::SNOW_BLOCK:
        return "Snow";
    case voxel::ICE:
        return "Ice";
    case voxel::SPRUCE_WOOD:
        return "Spruce Log";
    case voxel::SPRUCE_LEAVES:
        return "Spruce Leaves";
    case voxel::BIRCH_WOOD:
        return "Birch Log";
    case voxel::BIRCH_LEAVES:
        return "Birch Leaves";
    case voxel::CACTUS:
        return "Cactus";
    case voxel::SANDSTONE:
        return "Sandstone";
    case voxel::MUD:
        return "Mud";
    case voxel::MOSS:
        return "Moss Block";
    case voxel::BASALT:
        return "Basalt";
    case voxel::RED_SAND:
        return "Red Sand";
    case voxel::TALL_GRASS:
        return "Tall Grass";
    case voxel::FLOWER:
        return "Flower";
    case voxel::WILDFLOWER:
        return "Wildflower";
    case voxel::FERN:
        return "Fern";
    case voxel::DRY_GRASS:
        return "Dry Grass";
    case voxel::DEAD_BUSH:
        return "Dead Bush";
    case voxel::SEAGRASS:
        return "Seagrass";
    case voxel::KELP:
        return "Kelp";
    case voxel::CORAL:
        return "Coral";
    case voxel::TORCH:
    case voxel::TORCH_WALL_POS_X:
    case voxel::TORCH_WALL_NEG_X:
    case voxel::TORCH_WALL_POS_Z:
    case voxel::TORCH_WALL_NEG_Z:
        return "Torch";
    case voxel::CRAFTING_TABLE:
        return "Crafting Table";
    case voxel::OAK_PLANKS:
        return "Oak Planks";
    case voxel::SPRUCE_PLANKS:
        return "Spruce Planks";
    case voxel::BIRCH_PLANKS:
        return "Birch Planks";
    case voxel::STICK:
        return "Stick";
    case voxel::GLASS:
        return "Glass";
    case voxel::BRICKS:
        return "Bricks";
    case voxel::IRON_INGOT:
        return "Iron Ingot";
    case voxel::COPPER_INGOT:
        return "Copper Ingot";
    case voxel::GOLD_INGOT:
        return "Gold Ingot";
    default:
        return "Block";
    }
}

float breakSeconds(voxel::BlockId id) {
    if (voxel::isFurnace(id)) {
        return 0.58f;
    }
    switch (id) {
    case voxel::LEAVES:
    case voxel::SPRUCE_LEAVES:
    case voxel::BIRCH_LEAVES:
    case voxel::TALL_GRASS:
    case voxel::FLOWER:
    case voxel::WILDFLOWER:
    case voxel::FERN:
    case voxel::DRY_GRASS:
    case voxel::DEAD_BUSH:
    case voxel::SEAGRASS:
    case voxel::KELP:
    case voxel::CORAL:
    case voxel::LAVA:
    case voxel::LAVA_SOURCE:
    case voxel::TORCH:
    case voxel::TORCH_WALL_POS_X:
    case voxel::TORCH_WALL_NEG_X:
    case voxel::TORCH_WALL_POS_Z:
    case voxel::TORCH_WALL_NEG_Z:
        return 0.22f;
    case voxel::DIRT:
    case voxel::SAND:
    case voxel::GRAVEL:
    case voxel::CLAY:
    case voxel::SNOW_BLOCK:
    case voxel::MUD:
    case voxel::RED_SAND:
        return 0.34f;
    case voxel::WOOD:
    case voxel::SPRUCE_WOOD:
    case voxel::BIRCH_WOOD:
    case voxel::OAK_PLANKS:
    case voxel::SPRUCE_PLANKS:
    case voxel::BIRCH_PLANKS:
    case voxel::CACTUS:
    case voxel::CRAFTING_TABLE:
    case voxel::STICK:
        return 0.58f;
    case voxel::GLASS:
    case voxel::BRICKS:
        return 0.62f;
    case voxel::IRON_INGOT:
    case voxel::COPPER_INGOT:
    case voxel::GOLD_INGOT:
        return 0.30f;
    case voxel::COAL_ORE:
    case voxel::COPPER_ORE:
        return 0.70f;
    case voxel::IRON_ORE:
    case voxel::GOLD_ORE:
        return 0.82f;
    case voxel::DIAMOND_ORE:
    case voxel::EMERALD_ORE:
        return 0.94f;
    case voxel::STONE:
    case voxel::SANDSTONE:
    case voxel::BASALT:
    case voxel::BEDROCK:
    case voxel::ICE:
        return 0.68f;
    case voxel::MOSS:
        return 0.40f;
    default:
        return 0.45f;
    }
}

AudioSystem::SoundProfile soundProfileForBlock(voxel::BlockId id) {
    using Profile = AudioSystem::SoundProfile;
    if (voxel::isFurnace(id)) {
        return Profile::Stone;
    }
    switch (id) {
    case voxel::GRASS:
    case voxel::DIRT:
    case voxel::CLAY:
    case voxel::MUD:
    case voxel::MOSS:
        return Profile::Dirt;
    case voxel::STONE:
    case voxel::BEDROCK:
    case voxel::COAL_ORE:
    case voxel::COPPER_ORE:
    case voxel::IRON_ORE:
    case voxel::GOLD_ORE:
    case voxel::DIAMOND_ORE:
    case voxel::EMERALD_ORE:
    case voxel::SANDSTONE:
    case voxel::BRICKS:
        return Profile::Stone;
    case voxel::WOOD:
    case voxel::SPRUCE_WOOD:
    case voxel::BIRCH_WOOD:
    case voxel::OAK_PLANKS:
    case voxel::SPRUCE_PLANKS:
    case voxel::BIRCH_PLANKS:
    case voxel::STICK:
    case voxel::CACTUS:
    case voxel::CRAFTING_TABLE:
    case voxel::TORCH:
    case voxel::TORCH_WALL_POS_X:
    case voxel::TORCH_WALL_NEG_X:
    case voxel::TORCH_WALL_POS_Z:
    case voxel::TORCH_WALL_NEG_Z:
        return Profile::Wood;
    case voxel::LEAVES:
    case voxel::SPRUCE_LEAVES:
    case voxel::BIRCH_LEAVES:
    case voxel::TALL_GRASS:
    case voxel::FLOWER:
    case voxel::WILDFLOWER:
    case voxel::FERN:
    case voxel::DRY_GRASS:
    case voxel::DEAD_BUSH:
    case voxel::SEAGRASS:
    case voxel::KELP:
    case voxel::CORAL:
        return Profile::Foliage;
    case voxel::SAND:
    case voxel::RED_SAND:
    case voxel::GRAVEL:
        return Profile::Sand;
    case voxel::SNOW_BLOCK:
        return Profile::Snow;
    case voxel::ICE:
    case voxel::GLASS:
        return Profile::Ice;
    case voxel::LAVA:
    case voxel::LAVA_SOURCE:
        return Profile::Stone;
    case voxel::IRON_INGOT:
    case voxel::COPPER_INGOT:
    case voxel::GOLD_INGOT:
        return Profile::Stone;
    default:
        return Profile::Default;
    }
}

AudioSystem::SoundProfile footstepProfileForGround(voxel::BlockId id) {
    using Profile = AudioSystem::SoundProfile;
    if (id == voxel::GRASS) {
        return Profile::Grass;
    }
    return soundProfileForBlock(id);
}

voxel::BlockId blockUnderPlayer(const world::World &world, const glm::vec3 &cameraPos) {
    const glm::vec3 foot = cameraPos + glm::vec3(0.0f, -1.72f, 0.0f);
    const int wx = static_cast<int>(std::floor(foot.x));
    const int wz = static_cast<int>(std::floor(foot.z));
    int wy = static_cast<int>(std::floor(foot.y - 0.08f));
    voxel::BlockId id = world.getBlock(wx, wy, wz);
    if (id == voxel::AIR && wy > 0) {
        id = world.getBlock(wx, wy - 1, wz);
    }
    return id;
}

} // namespace game
