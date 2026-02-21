#pragma once

#include "voxel/Chunk.hpp"
#include "world/ChunkCoord.hpp"
#include "world/FurnaceState.hpp"

#include <cstdint>
#include <filesystem>
#include <vector>

namespace voxel {

class ChunkIO {
  public:
    ChunkIO(std::filesystem::path root, std::uint32_t generatorVersion);

    bool save(const Chunk &chunk, world::ChunkCoord cc,
              const std::vector<world::FurnaceRecordLocal> *furnaces = nullptr) const;
    bool load(Chunk &chunk, world::ChunkCoord cc,
              std::vector<world::FurnaceRecordLocal> *furnacesOut = nullptr) const;

  private:
    std::filesystem::path chunkPath(world::ChunkCoord cc) const;

    std::filesystem::path root_;
    std::uint32_t generatorVersion_;
};

} // namespace voxel
