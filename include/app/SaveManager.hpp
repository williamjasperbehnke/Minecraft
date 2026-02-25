#pragma once

#include <filesystem>
#include <optional>
#include <cstdint>
#include <vector>

#include "game/SmeltingSystem.hpp"

#include <glm/vec3.hpp>

namespace game {
class Inventory;
} // namespace game

namespace voxel {
class Chunk;
} // namespace voxel

namespace world {
struct ChunkCoord;
struct FurnaceRecordLocal;
struct FurnaceState;
class World;
} // namespace world

namespace app {

class SaveManager {
  public:
    static world::FurnaceState toWorldFurnaceState(const game::SmeltingSystem::State &src);
    static game::SmeltingSystem::State fromWorldFurnaceState(const world::FurnaceState &src);

    static bool loadPlayerData(const std::filesystem::path &worldDir, glm::vec3 &cameraPos,
                               int &selectedSlot, bool &ghostMode, game::Inventory &inventory,
                               game::SmeltingSystem::State &smelting);

    static void savePlayerData(const std::filesystem::path &worldDir, const glm::vec3 &cameraPos,
                               int selectedSlot, bool ghostMode,
                               const game::Inventory &inventory,
                               const game::SmeltingSystem::State &smelting);

    static void persistFurnaceState(world::World &world,
                                    const std::optional<glm::ivec3> &activeFurnaceCell,
                                    const game::SmeltingSystem::State &smelting);

    static void loadFurnaceState(world::World &world,
                                 const std::optional<glm::ivec3> &activeFurnaceCell,
                                 game::SmeltingSystem::State &smelting);

    static bool saveChunk(const std::filesystem::path &worldDir, std::uint32_t generatorVersion,
                          const voxel::Chunk &chunk, world::ChunkCoord cc,
                          const std::vector<world::FurnaceRecordLocal> *furnaces = nullptr);

    static bool loadChunk(const std::filesystem::path &worldDir, std::uint32_t generatorVersion,
                          voxel::Chunk &chunk, world::ChunkCoord cc,
                          std::vector<world::FurnaceRecordLocal> *furnacesOut = nullptr);
};

} // namespace app
