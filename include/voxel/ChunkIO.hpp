#pragma once

#include "voxel/Chunk.hpp"
#include "world/ChunkCoord.hpp"

#include <cstdint>
#include <filesystem>

namespace voxel {

class ChunkIO {
  public:
    ChunkIO(std::filesystem::path root, std::uint32_t generatorVersion);

    bool save(const Chunk &chunk, world::ChunkCoord cc) const;
    bool load(Chunk &chunk, world::ChunkCoord cc) const;

  private:
    std::filesystem::path chunkPath(world::ChunkCoord cc) const;

    std::filesystem::path root_;
    std::uint32_t generatorVersion_;
};

} // namespace voxel
