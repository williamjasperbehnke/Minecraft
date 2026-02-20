#include "voxel/ChunkMesher.hpp"

#include "voxel/LightingSolver.hpp"

#include <algorithm>
#include <glm/vec2.hpp>
#include <glm/vec3.hpp>
#include <glm/vec4.hpp>

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
    if (neighborId == currentId && currentId != AIR) {
        return false;
    }
    return !isOpaque(registry, neighborId);
}

bool isCrossPlant(BlockId id) {
    return id == TALL_GRASS || id == FLOWER;
}

glm::vec4 flipV(const glm::vec4 &uv) {
    return {uv.x, uv.w, uv.z, uv.y};
}

void appendQuad(gfx::CpuMesh &mesh, const glm::vec3 &p0, const glm::vec3 &p1, const glm::vec3 &p2,
                const glm::vec3 &p3, const glm::vec3 &normal, const glm::vec4 &uv, float skyLight,
                float blockLight) {
    const std::uint32_t base = static_cast<std::uint32_t>(mesh.vertices.size());
    mesh.vertices.push_back(
        {p0.x, p0.y, p0.z, normal.x, normal.y, normal.z, uv.x, uv.y, skyLight, blockLight});
    mesh.vertices.push_back(
        {p1.x, p1.y, p1.z, normal.x, normal.y, normal.z, uv.z, uv.y, skyLight, blockLight});
    mesh.vertices.push_back(
        {p2.x, p2.y, p2.z, normal.x, normal.y, normal.z, uv.z, uv.w, skyLight, blockLight});
    mesh.vertices.push_back(
        {p3.x, p3.y, p3.z, normal.x, normal.y, normal.z, uv.x, uv.w, skyLight, blockLight});
    mesh.indices.insert(mesh.indices.end(), {base, base + 1, base + 2, base, base + 2, base + 3});
}

void appendCrossPlant(gfx::CpuMesh &mesh, float wx, float wy, float wz, const glm::vec4 &uv,
                      float skyLight, float blockLight) {
    const float inset = 0.10f;
    const glm::vec3 p0(wx + inset, wy, wz + inset);
    const glm::vec3 p1(wx + 1.0f - inset, wy, wz + 1.0f - inset);
    const glm::vec3 p2(wx + 1.0f - inset, wy + 1.0f, wz + 1.0f - inset);
    const glm::vec3 p3(wx + inset, wy + 1.0f, wz + inset);

    const glm::vec3 q0(wx + 1.0f - inset, wy, wz + inset);
    const glm::vec3 q1(wx + inset, wy, wz + 1.0f - inset);
    const glm::vec3 q2(wx + inset, wy + 1.0f, wz + 1.0f - inset);
    const glm::vec3 q3(wx + 1.0f - inset, wy + 1.0f, wz + inset);

    appendQuad(mesh, p0, p1, p2, p3, {0.0f, 1.0f, 0.0f}, uv, skyLight, blockLight);
    appendQuad(mesh, q0, q1, q2, q3, {0.0f, 1.0f, 0.0f}, uv, skyLight, blockLight);
}

glm::ivec2 wallTorchOutward(BlockId id) {
    switch (id) {
    case TORCH_WALL_POS_X:
        return {1, 0};
    case TORCH_WALL_NEG_X:
        return {-1, 0};
    case TORCH_WALL_POS_Z:
        return {0, 1};
    case TORCH_WALL_NEG_Z:
        return {0, -1};
    default:
        return {0, 0};
    }
}

void appendTorchCross(gfx::CpuMesh &mesh, float x0, float z0, float x1, float z1, float y0, float y1,
                      float halfWidth, const glm::vec4 &uv, float skyLight, float blockLight) {
    const glm::vec3 p0(x0 - halfWidth, y0, z0 - halfWidth);
    const glm::vec3 p1(x0 + halfWidth, y0, z0 + halfWidth);
    const glm::vec3 p2(x1 + halfWidth, y1, z1 + halfWidth);
    const glm::vec3 p3(x1 - halfWidth, y1, z1 - halfWidth);

    const glm::vec3 q0(x0 + halfWidth, y0, z0 - halfWidth);
    const glm::vec3 q1(x0 - halfWidth, y0, z0 + halfWidth);
    const glm::vec3 q2(x1 - halfWidth, y1, z1 + halfWidth);
    const glm::vec3 q3(x1 + halfWidth, y1, z1 - halfWidth);

    appendQuad(mesh, p0, p1, p2, p3, {0.0f, 1.0f, 0.0f}, uv, skyLight, blockLight);
    appendQuad(mesh, q0, q1, q2, q3, {0.0f, 1.0f, 0.0f}, uv, skyLight, blockLight);
}

} // namespace

gfx::CpuMesh ChunkMesher::buildFaceCulled(const Chunk &chunk, const gfx::TextureAtlas &atlas,
                                          const BlockRegistry &registry, glm::ivec2 chunkXZ,
                                          const NeighborChunks &neighbors, bool smoothLighting) {
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

    LightingSolver::NeighborChunks lightNeighbors{neighbors.px,   neighbors.nx,   neighbors.pz,
                                                  neighbors.nz,   neighbors.pxpz, neighbors.pxnz,
                                                  neighbors.nxpz, neighbors.nxnz};
    const LightingSolver lighting(chunk, registry, lightNeighbors, smoothLighting);

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
                    // Plants should not become fully dim just because a block is directly above:
                    // blend light from their own cell and the cell above.
                    const float sky = std::max(lighting.faceSkyLight(x, y, z, 0.92f),
                                               lighting.faceSkyLight(x, y + 1, z, 0.95f));
                    const float block =
                        std::max(lighting.faceBlockLight(x, y, z), lighting.faceBlockLight(x, y + 1, z));
                    appendCrossPlant(out, wx, wy, wz, flipV(atlas.uvRect(d.sideTile)), sky, block);
                    continue;
                }

                if (isTorch(id)) {
                    const float sky = lighting.faceSkyLight(x, y + 1, z, 0.95f);
                    const float block =
                        std::max(lighting.faceBlockLight(x, y, z), lighting.faceBlockLight(x, y + 1, z));
                    if (id == TORCH) {
                        appendCrossPlant(out, wx, wy, wz, flipV(atlas.uvRect(d.sideTile)), sky, block);
                    } else if (isWallTorch(id)) {
                        const glm::ivec2 outward = wallTorchOutward(id);
                        const float dx = static_cast<float>(outward.x);
                        const float dz = static_cast<float>(outward.y);
                        // Base is close to support face; top tilts outward.
                        const float baseX = wx + 0.5f - dx * 0.49f;
                        const float baseZ = wz + 0.5f - dz * 0.49f;
                        const float topX = baseX + dx * 0.42f;
                        const float topZ = baseZ + dz * 0.42f;
                        appendTorchCross(out, baseX, baseZ, topX, topZ, wy, wy + 1.0f, 0.49f,
                                         flipV(atlas.uvRect(d.sideTile)), sky, block);
                    }
                    continue;
                }

                if (isFaceExposed(registry, id, neighborAt(x + 1, y, z))) {
                    const float sky = lighting.faceSkyLight(x + 1, y, z, 0.86f);
                    const float block = lighting.faceBlockLight(x + 1, y, z);
                    appendQuad(out, {wx + 1, wy, wz}, {wx + 1, wy, wz + 1},
                               {wx + 1, wy + 1, wz + 1}, {wx + 1, wy + 1, wz}, {1, 0, 0},
                               flipV(atlas.uvRect(d.sideTile)), sky, block);
                }
                if (isFaceExposed(registry, id, neighborAt(x - 1, y, z))) {
                    const float sky = lighting.faceSkyLight(x - 1, y, z, 0.86f);
                    const float block = lighting.faceBlockLight(x - 1, y, z);
                    appendQuad(out, {wx, wy, wz + 1}, {wx, wy, wz}, {wx, wy + 1, wz},
                               {wx, wy + 1, wz + 1}, {-1, 0, 0}, flipV(atlas.uvRect(d.sideTile)),
                               sky, block);
                }
                if (isFaceExposed(registry, id, neighborAt(x, y + 1, z))) {
                    const float sky = lighting.faceSkyLight(x, y + 1, z, 1.00f);
                    const float block = lighting.faceBlockLight(x, y + 1, z);
                    appendQuad(out, {wx, wy + 1, wz}, {wx + 1, wy + 1, wz},
                               {wx + 1, wy + 1, wz + 1}, {wx, wy + 1, wz + 1}, {0, 1, 0},
                               atlas.uvRect(d.topTile), sky, block);
                }
                if (isFaceExposed(registry, id, neighborAt(x, y - 1, z))) {
                    const float sky = lighting.faceSkyLight(x, y - 1, z, 0.56f);
                    const float block = lighting.faceBlockLight(x, y - 1, z);
                    appendQuad(out, {wx, wy, wz + 1}, {wx + 1, wy, wz + 1}, {wx + 1, wy, wz},
                               {wx, wy, wz}, {0, -1, 0}, atlas.uvRect(d.bottomTile), sky, block);
                }
                if (isFaceExposed(registry, id, neighborAt(x, y, z + 1))) {
                    const float sky = lighting.faceSkyLight(x, y, z + 1, 0.90f);
                    const float block = lighting.faceBlockLight(x, y, z + 1);
                    appendQuad(out, {wx + 1, wy, wz + 1}, {wx, wy, wz + 1}, {wx, wy + 1, wz + 1},
                               {wx + 1, wy + 1, wz + 1}, {0, 0, 1}, flipV(atlas.uvRect(d.sideTile)),
                               sky, block);
                }
                if (isFaceExposed(registry, id, neighborAt(x, y, z - 1))) {
                    const float sky = lighting.faceSkyLight(x, y, z - 1, 0.90f);
                    const float block = lighting.faceBlockLight(x, y, z - 1);
                    appendQuad(out, {wx, wy, wz}, {wx + 1, wy, wz}, {wx + 1, wy + 1, wz},
                               {wx, wy + 1, wz}, {0, 0, -1}, flipV(atlas.uvRect(d.sideTile)), sky,
                               block);
                }
            }
        }
    }
    return out;
}

} // namespace voxel
