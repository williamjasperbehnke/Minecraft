#include "voxel/ChunkMesher.hpp"

#include <glm/vec3.hpp>
#include <glm/vec4.hpp>

#include <vector>

namespace voxel {
namespace {

bool isOpaque(const BlockRegistry &registry, BlockId id) {
    if (id == AIR) {
        return false;
    }
    const auto &def = registry.get(id);
    return def.solid && !def.transparent;
}

bool isFaceExposed(const BlockRegistry &registry, BlockId currentId, BlockId neighborId) {
    // Never render interior faces between identical materials (e.g.,
    // water-water).
    if (neighborId == currentId && currentId != AIR) {
        return false;
    }
    // A face is visible only when the neighbor is not opaque.
    return !isOpaque(registry, neighborId);
}

bool isCrossPlant(BlockId id) {
    return id == TALL_GRASS || id == FLOWER;
}

glm::vec4 flipV(const glm::vec4 &uv) {
    return {uv.x, uv.w, uv.z, uv.y};
}

void appendQuad(gfx::CpuMesh &mesh, const glm::vec3 &p0, const glm::vec3 &p1, const glm::vec3 &p2,
                const glm::vec3 &p3, const glm::vec3 &normal, const glm::vec4 &uv) {
    const std::uint32_t base = static_cast<std::uint32_t>(mesh.vertices.size());
    mesh.vertices.push_back({p0.x, p0.y, p0.z, normal.x, normal.y, normal.z, uv.x, uv.y});
    mesh.vertices.push_back({p1.x, p1.y, p1.z, normal.x, normal.y, normal.z, uv.z, uv.y});
    mesh.vertices.push_back({p2.x, p2.y, p2.z, normal.x, normal.y, normal.z, uv.z, uv.w});
    mesh.vertices.push_back({p3.x, p3.y, p3.z, normal.x, normal.y, normal.z, uv.x, uv.w});
    mesh.indices.insert(mesh.indices.end(), {base, base + 1, base + 2, base, base + 2, base + 3});
}

void appendCrossPlant(gfx::CpuMesh &mesh, float wx, float wy, float wz, const glm::vec4 &uv) {
    const float inset = 0.10f;
    const glm::vec3 p0(wx + inset, wy, wz + inset);
    const glm::vec3 p1(wx + 1.0f - inset, wy, wz + 1.0f - inset);
    const glm::vec3 p2(wx + 1.0f - inset, wy + 1.0f, wz + 1.0f - inset);
    const glm::vec3 p3(wx + inset, wy + 1.0f, wz + inset);

    const glm::vec3 q0(wx + 1.0f - inset, wy, wz + inset);
    const glm::vec3 q1(wx + inset, wy, wz + 1.0f - inset);
    const glm::vec3 q2(wx + inset, wy + 1.0f, wz + 1.0f - inset);
    const glm::vec3 q3(wx + 1.0f - inset, wy + 1.0f, wz + inset);

    // Two crossed planes. We keep a single quad per plane to avoid coplanar
    // z-fighting.
    appendQuad(mesh, p0, p1, p2, p3, {0.0f, 1.0f, 0.0f}, uv);
    appendQuad(mesh, q0, q1, q2, q3, {0.0f, 1.0f, 0.0f}, uv);
}

} // namespace

gfx::CpuMesh ChunkMesher::buildFaceCulled(const Chunk &chunk, const gfx::TextureAtlas &atlas,
                                          const BlockRegistry &registry, glm::ivec2 chunkXZ,
                                          const NeighborChunks &neighbors) {
    gfx::CpuMesh out;

    auto neighborAt = [&](int x, int y, int z) -> BlockId {
        if (y < 0 || y >= Chunk::SY) {
            return AIR;
        }
        if (x >= 0 && x < Chunk::SX && z >= 0 && z < Chunk::SZ) {
            return chunk.getUnchecked(x, y, z);
        }
        if (x < 0 && neighbors.nx != nullptr) {
            return neighbors.nx->getUnchecked(Chunk::SX - 1, y, z);
        }
        if (x >= Chunk::SX && neighbors.px != nullptr) {
            return neighbors.px->getUnchecked(0, y, z);
        }
        if (z < 0 && neighbors.nz != nullptr) {
            return neighbors.nz->getUnchecked(x, y, Chunk::SZ - 1);
        }
        if (z >= Chunk::SZ && neighbors.pz != nullptr) {
            return neighbors.pz->getUnchecked(x, y, 0);
        }
        return AIR;
    };

    for (int x = 0; x < Chunk::SX; ++x) {
        for (int y = 0; y < Chunk::SY; ++y) {
            for (int z = 0; z < Chunk::SZ; ++z) {
                const BlockId id = chunk.getUnchecked(x, y, z);
                if (id == AIR || !registry.get(id).solid) {
                    continue;
                }

                const float wx = static_cast<float>(chunkXZ.x * Chunk::SX + x);
                const float wy = static_cast<float>(y);
                const float wz = static_cast<float>(chunkXZ.y * Chunk::SZ + z);
                const auto &d = registry.get(id);
                if (isCrossPlant(id)) {
                    appendCrossPlant(out, wx, wy, wz, flipV(atlas.uvRect(d.sideTile)));
                    continue;
                }

                if (isFaceExposed(registry, id, neighborAt(x + 1, y, z))) {
                    appendQuad(out, {wx + 1, wy, wz}, {wx + 1, wy, wz + 1},
                               {wx + 1, wy + 1, wz + 1}, {wx + 1, wy + 1, wz}, {1, 0, 0},
                               flipV(atlas.uvRect(d.sideTile)));
                }
                if (isFaceExposed(registry, id, neighborAt(x - 1, y, z))) {
                    appendQuad(out, {wx, wy, wz + 1}, {wx, wy, wz}, {wx, wy + 1, wz},
                               {wx, wy + 1, wz + 1}, {-1, 0, 0}, flipV(atlas.uvRect(d.sideTile)));
                }
                if (isFaceExposed(registry, id, neighborAt(x, y + 1, z))) {
                    appendQuad(out, {wx, wy + 1, wz}, {wx + 1, wy + 1, wz},
                               {wx + 1, wy + 1, wz + 1}, {wx, wy + 1, wz + 1}, {0, 1, 0},
                               atlas.uvRect(d.topTile));
                }
                if (isFaceExposed(registry, id, neighborAt(x, y - 1, z))) {
                    appendQuad(out, {wx, wy, wz + 1}, {wx + 1, wy, wz + 1}, {wx + 1, wy, wz},
                               {wx, wy, wz}, {0, -1, 0}, atlas.uvRect(d.bottomTile));
                }
                if (isFaceExposed(registry, id, neighborAt(x, y, z + 1))) {
                    appendQuad(out, {wx + 1, wy, wz + 1}, {wx, wy, wz + 1}, {wx, wy + 1, wz + 1},
                               {wx + 1, wy + 1, wz + 1}, {0, 0, 1},
                               flipV(atlas.uvRect(d.sideTile)));
                }
                if (isFaceExposed(registry, id, neighborAt(x, y, z - 1))) {
                    appendQuad(out, {wx, wy, wz}, {wx + 1, wy, wz}, {wx + 1, wy + 1, wz},
                               {wx, wy + 1, wz}, {0, 0, -1}, flipV(atlas.uvRect(d.sideTile)));
                }
            }
        }
    }
    return out;
}

} // namespace voxel
