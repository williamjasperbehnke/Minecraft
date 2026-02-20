#pragma once

#include "gfx/ChunkMesh.hpp"
#include "gfx/TextureAtlas.hpp"
#include "voxel/Block.hpp"
#include "voxel/Chunk.hpp"

#include <glm/vec2.hpp>

namespace voxel {

class ChunkMesher {
  public:
    struct NeighborChunks {
        const Chunk *px = nullptr;
        const Chunk *nx = nullptr;
        const Chunk *pz = nullptr;
        const Chunk *nz = nullptr;
    };

    static gfx::CpuMesh buildFaceCulled(const Chunk &chunk, const gfx::TextureAtlas &atlas,
                                        const BlockRegistry &registry, glm::ivec2 chunkXZ,
                                        const NeighborChunks &neighbors);
};

} // namespace voxel
