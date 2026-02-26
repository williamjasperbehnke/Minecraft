#pragma once

#include "gfx/ChunkMesh.hpp"
#include "gfx/TextureAtlas.hpp"
#include "voxel/Block.hpp"
#include "voxel/Chunk.hpp"

#include <glm/vec2.hpp>
#include <cstddef>
#include <functional>

namespace voxel {

class ChunkMesher {
  public:
    struct NeighborChunks {
        const Chunk *px = nullptr;
        const Chunk *nx = nullptr;
        const Chunk *pz = nullptr;
        const Chunk *nz = nullptr;
        const Chunk *pxpz = nullptr;
        const Chunk *pxnz = nullptr;
        const Chunk *nxpz = nullptr;
        const Chunk *nxnz = nullptr;
    };

    static void buildFaceCulledInto(gfx::CpuMesh &out, const Chunk &chunk,
                                    const gfx::TextureAtlas &atlas,
                                    const BlockRegistry &registry, glm::ivec2 chunkXZ,
                                    const NeighborChunks &neighbors, bool smoothLighting,
                                    const std::function<int(BlockId, int, int, int)>
                                        &fluidLevelLookup = {},
                                    std::size_t reserveVertices = 0,
                                    std::size_t reserveIndices = 0);

    static gfx::CpuMesh buildFaceCulled(const Chunk &chunk, const gfx::TextureAtlas &atlas,
                                        const BlockRegistry &registry, glm::ivec2 chunkXZ,
                                        const NeighborChunks &neighbors, bool smoothLighting,
                                        const std::function<int(BlockId, int, int, int)>
                                            &fluidLevelLookup = {},
                                        std::size_t reserveVertices = 0,
                                        std::size_t reserveIndices = 0);
};

} // namespace voxel
