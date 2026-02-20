#include "game/GameRules.hpp"

#include "world/World.hpp"

#include <cmath>

namespace game {

const char *blockName(voxel::BlockId id) {
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
    case voxel::TALL_GRASS:
        return "Tall Grass";
    case voxel::FLOWER:
        return "Flower";
    default:
        return "Block";
    }
}

std::string biomeHintFromSurface(const world::World &world, const glm::vec3 &pos) {
    const int wx = static_cast<int>(std::floor(pos.x));
    const int wz = static_cast<int>(std::floor(pos.z));
    int topY = 0;
    voxel::BlockId top = voxel::AIR;
    for (int y = 127; y >= 0; --y) {
        const voxel::BlockId id = world.getBlock(wx, y, wz);
        if (id != voxel::AIR && id != voxel::WATER && id != voxel::LEAVES &&
            id != voxel::SPRUCE_LEAVES && id != voxel::BIRCH_LEAVES && id != voxel::TALL_GRASS &&
            id != voxel::FLOWER) {
            topY = y;
            top = id;
            break;
        }
    }
    if (top == voxel::SNOW_BLOCK || top == voxel::ICE) {
        return "Biome: Snowy/Taiga";
    }
    if (top == voxel::SAND || top == voxel::SANDSTONE || top == voxel::CACTUS) {
        return "Biome: Desert/Badlands";
    }
    if (top == voxel::STONE && topY > 88) {
        return "Biome: Mountains";
    }
    if (top == voxel::GRASS) {
        return "Biome: Plains/Forest/Swamp";
    }
    return "Biome: Overworld";
}

float breakSeconds(voxel::BlockId id) {
    switch (id) {
    case voxel::LEAVES:
    case voxel::SPRUCE_LEAVES:
    case voxel::BIRCH_LEAVES:
    case voxel::TALL_GRASS:
    case voxel::FLOWER:
        return 0.22f;
    case voxel::DIRT:
    case voxel::SAND:
    case voxel::GRAVEL:
    case voxel::CLAY:
    case voxel::SNOW_BLOCK:
        return 0.34f;
    case voxel::WOOD:
    case voxel::SPRUCE_WOOD:
    case voxel::BIRCH_WOOD:
    case voxel::CACTUS:
        return 0.58f;
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
    case voxel::ICE:
        return 0.68f;
    default:
        return 0.45f;
    }
}

AudioSystem::SoundProfile soundProfileForBlock(voxel::BlockId id) {
    using Profile = AudioSystem::SoundProfile;
    switch (id) {
    case voxel::GRASS:
        return Profile::Dirt;
    case voxel::DIRT:
    case voxel::CLAY:
        return Profile::Dirt;
    case voxel::STONE:
    case voxel::COAL_ORE:
    case voxel::COPPER_ORE:
    case voxel::IRON_ORE:
    case voxel::GOLD_ORE:
    case voxel::DIAMOND_ORE:
    case voxel::EMERALD_ORE:
    case voxel::SANDSTONE:
        return Profile::Stone;
    case voxel::WOOD:
    case voxel::SPRUCE_WOOD:
    case voxel::BIRCH_WOOD:
    case voxel::CACTUS:
        return Profile::Wood;
    case voxel::LEAVES:
    case voxel::SPRUCE_LEAVES:
    case voxel::BIRCH_LEAVES:
    case voxel::TALL_GRASS:
    case voxel::FLOWER:
        return Profile::Foliage;
    case voxel::SAND:
    case voxel::GRAVEL:
        return Profile::Sand;
    case voxel::SNOW_BLOCK:
        return Profile::Snow;
    case voxel::ICE:
        return Profile::Ice;
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
