#pragma once

#include "voxel/Block.hpp"
#include "voxel/Chunk.hpp"

#include <cstdint>
#include <optional>
#include <vector>

namespace voxel {

class LightingSolver {
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

    LightingSolver(const Chunk &chunk, const BlockRegistry &registry,
                   const NeighborChunks &neighbors, bool smoothLighting);

    float faceSkyLight(int adjX, int adjY, int adjZ, float bias) const;
    float faceBlockLight(int adjX, int adjY, int adjZ) const;

  private:
    struct LocalLight {
        std::vector<std::uint8_t> sky;
        std::vector<std::uint8_t> block;
    };

    static int lightIndex(int x, int y, int z);
    static float lightLevelToFloat(std::uint8_t level);

    bool isOpaque(BlockId id) const;
    bool canTransmitLight(BlockId id) const;
    LocalLight buildLocalLight(const Chunk &src) const;
    void applySkyBoundarySeeding();
    void buildExtendedSkyLight();
    void buildExtendedBlockLight();
    std::uint8_t sampledSky(int x, int y, int z) const;
    std::uint8_t sampledBlock(int x, int y, int z) const;
    float smoothedSkyLight(int x, int y, int z) const;
    float smoothedBlockLight(int x, int y, int z) const;

    const Chunk &chunk_;
    const BlockRegistry &registry_;
    NeighborChunks neighbors_;
    bool smoothLighting_ = false;

    LocalLight light_;
    std::optional<LocalLight> pxLight_;
    std::optional<LocalLight> nxLight_;
    std::optional<LocalLight> pzLight_;
    std::optional<LocalLight> nzLight_;
    std::optional<LocalLight> pxpzLight_;
    std::optional<LocalLight> pxnzLight_;
    std::optional<LocalLight> nxpzLight_;
    std::optional<LocalLight> nxnzLight_;

    std::vector<std::uint8_t> extSkyLight_;
    std::vector<std::uint8_t> extBlockLight_;
};

} // namespace voxel
