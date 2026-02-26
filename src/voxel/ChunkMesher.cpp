#include "voxel/ChunkMesher.hpp"

#include "voxel/LightingSolver.hpp"

#include <algorithm>
#include <array>
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
    return isPlant(id);
}

bool isWaterRenderable(BlockId id) {
    return isWaterLike(id);
}

bool isLavaRenderable(BlockId id) {
    return isLavaLike(id);
}

glm::vec4 flipV(const glm::vec4 &uv) {
    return {uv.x, uv.w, uv.z, uv.y};
}

glm::ivec2 furnaceFrontNormal(BlockId id) {
    switch (id) {
    case FURNACE_POS_X:
    case LIT_FURNACE_POS_X:
        return {-1, 0};
    case FURNACE_NEG_X:
    case LIT_FURNACE_NEG_X:
        return {1, 0};
    case FURNACE_POS_Z:
    case LIT_FURNACE_POS_Z:
        return {0, -1};
    case FURNACE:
    case FURNACE_NEG_Z:
    case LIT_FURNACE_NEG_Z:
        return {0, 1};
    default:
        return {0, 0};
    }
}

void appendQuad(gfx::CpuMesh &mesh, const glm::vec3 &p0, const glm::vec3 &p1, const glm::vec3 &p2,
                const glm::vec3 &p3, const glm::vec3 &normal, const glm::vec4 &uv, float skyLight,
                float blockLight, float fluidLevel = -1.0f) {
    const std::uint32_t base = static_cast<std::uint32_t>(mesh.vertices.size());
    mesh.vertices.push_back(
        {p0.x, p0.y, p0.z, normal.x, normal.y, normal.z, uv.x, uv.y, skyLight, blockLight, fluidLevel});
    mesh.vertices.push_back(
        {p1.x, p1.y, p1.z, normal.x, normal.y, normal.z, uv.z, uv.y, skyLight, blockLight, fluidLevel});
    mesh.vertices.push_back(
        {p2.x, p2.y, p2.z, normal.x, normal.y, normal.z, uv.z, uv.w, skyLight, blockLight, fluidLevel});
    mesh.vertices.push_back(
        {p3.x, p3.y, p3.z, normal.x, normal.y, normal.z, uv.x, uv.w, skyLight, blockLight, fluidLevel});
    mesh.indices.insert(mesh.indices.end(), {base, base + 1, base + 2, base, base + 2, base + 3});
}

void appendQuadWithDiagonal(gfx::CpuMesh &mesh, const glm::vec3 &p0, const glm::vec3 &p1,
                            const glm::vec3 &p2, const glm::vec3 &p3, const glm::vec3 &normal,
                            const glm::vec4 &uv, float skyLight, float blockLight,
                            bool flipDiagonal, float fluidLevel = -1.0f) {
    const std::uint32_t base = static_cast<std::uint32_t>(mesh.vertices.size());
    mesh.vertices.push_back(
        {p0.x, p0.y, p0.z, normal.x, normal.y, normal.z, uv.x, uv.y, skyLight, blockLight, fluidLevel});
    mesh.vertices.push_back(
        {p1.x, p1.y, p1.z, normal.x, normal.y, normal.z, uv.z, uv.y, skyLight, blockLight, fluidLevel});
    mesh.vertices.push_back(
        {p2.x, p2.y, p2.z, normal.x, normal.y, normal.z, uv.z, uv.w, skyLight, blockLight, fluidLevel});
    mesh.vertices.push_back(
        {p3.x, p3.y, p3.z, normal.x, normal.y, normal.z, uv.x, uv.w, skyLight, blockLight, fluidLevel});
    if (flipDiagonal) {
        mesh.indices.insert(mesh.indices.end(),
                            {base, base + 1, base + 3, base + 1, base + 2, base + 3});
    } else {
        mesh.indices.insert(mesh.indices.end(),
                            {base, base + 1, base + 2, base, base + 2, base + 3});
    }
}

void appendCrossPlant(gfx::CpuMesh &mesh, float wx, float wy, float wz, float topY,
                      const glm::vec4 &uv, float skyLight, float blockLight) {
    const float inset = 0.10f;
    const glm::vec3 p0(wx + inset, wy, wz + inset);
    const glm::vec3 p1(wx + 1.0f - inset, wy, wz + 1.0f - inset);
    const glm::vec3 p2(wx + 1.0f - inset, topY, wz + 1.0f - inset);
    const glm::vec3 p3(wx + inset, topY, wz + inset);

    const glm::vec3 q0(wx + 1.0f - inset, wy, wz + inset);
    const glm::vec3 q1(wx + inset, wy, wz + 1.0f - inset);
    const glm::vec3 q2(wx + inset, topY, wz + 1.0f - inset);
    const glm::vec3 q3(wx + 1.0f - inset, topY, wz + inset);

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

void ChunkMesher::buildFaceCulledInto(gfx::CpuMesh &out, const Chunk &chunk,
                                      const gfx::TextureAtlas &atlas,
                                      const BlockRegistry &registry, glm::ivec2 chunkXZ,
                                      const NeighborChunks &neighbors, bool smoothLighting,
                                      const std::function<int(BlockId, int, int, int)>
                                          &fluidLevelLookup,
                                      std::size_t reserveVertices,
                                      std::size_t reserveIndices) {
    out.vertices.clear();
    out.indices.clear();
    if (reserveVertices > out.vertices.capacity()) {
        out.vertices.reserve(reserveVertices);
    }
    if (reserveIndices > out.indices.capacity()) {
        out.indices.reserve(reserveIndices);
    }

    auto neighborAt = [&](int x, int y, int z) -> BlockId {
        if (y < 0 || y >= Chunk::SY) {
            return AIR;
        }
        if (x >= 0 && x < Chunk::SX && z >= 0 && z < Chunk::SZ) {
            return chunk.getUnchecked(x, y, z);
        }
        // Handle diagonals first so out-of-range x/z pairs don't index neighbors
        // with invalid local coordinates.
        if (x < 0 && z < 0) {
            return (neighbors.nxnz != nullptr) ? neighbors.nxnz->getUnchecked(Chunk::SX - 1, y, Chunk::SZ - 1)
                                               : AIR;
        }
        if (x < 0 && z >= Chunk::SZ) {
            return (neighbors.nxpz != nullptr) ? neighbors.nxpz->getUnchecked(Chunk::SX - 1, y, 0)
                                               : AIR;
        }
        if (x >= Chunk::SX && z < 0) {
            return (neighbors.pxnz != nullptr) ? neighbors.pxnz->getUnchecked(0, y, Chunk::SZ - 1)
                                               : AIR;
        }
        if (x >= Chunk::SX && z >= Chunk::SZ) {
            return (neighbors.pxpz != nullptr) ? neighbors.pxpz->getUnchecked(0, y, 0) : AIR;
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
    auto hasNeighborChunkFor = [&](int x, int z) -> bool {
        if (x < 0) {
            return neighbors.nx != nullptr;
        }
        if (x >= Chunk::SX) {
            return neighbors.px != nullptr;
        }
        if (z < 0) {
            return neighbors.nz != nullptr;
        }
        if (z >= Chunk::SZ) {
            return neighbors.pz != nullptr;
        }
        return true;
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
                const bool furnace = isFurnace(id);
                const glm::ivec2 front = furnace ? furnaceFrontNormal(id) : glm::ivec2{0, 0};
                const glm::vec4 furnaceFrontUv =
                    flipV(atlas.uvRect(furnaceFrontTile(isLitFurnace(id))));
                const glm::vec4 sideUv = flipV(atlas.uvRect(d.sideTile));
                if (isWaterRenderable(id)) {
                    const auto &waterDef = registry.get(WATER);
                    const glm::vec4 waterSideUv = flipV(atlas.uvRect(waterDef.sideTile));
                    const glm::vec4 waterTopUv = atlas.uvRect(waterDef.topTile);
                    const glm::vec4 waterBottomUv = atlas.uvRect(waterDef.bottomTile);
                    const auto isWaterNeighbor = [&](int nx, int ny, int nz) {
                        return isWaterLike(neighborAt(nx, ny, nz));
                    };
                    const bool topCovered = isWaterLike(neighborAt(x, y + 1, z));
                    const bool bottomCovered = (y > 0) && isWaterLike(neighborAt(x, y - 1, z));
                    const auto inferWaterLevel = [&](BlockId bid, int sx, int sy, int sz) -> int {
                        if (!isWaterLike(bid)) {
                            return -1;
                        }
                        if (fluidLevelLookup) {
                            const int swx = chunkXZ.x * Chunk::SX + sx;
                            const int swz = chunkXZ.y * Chunk::SZ + sz;
                            const int lvl = fluidLevelLookup(WATER, swx, sy, swz);
                            if (lvl >= 0) {
                                return std::clamp(lvl, 0, 7);
                            }
                        }
                        if (isWaterloggedPlant(bid)) {
                            return 0;
                        }
                        if (bid == WATER_SOURCE) {
                            return 0;
                        }
                        // Fallback for water blocks without tracked state.
                        return 7;
                    };
                    const auto cellTopHeight = [&](BlockId bid, int sx, int sy, int sz) -> float {
                        if (!isWaterLike(bid)) {
                            return 0.0f;
                        }
                        if (isWaterLike(neighborAt(sx, sy + 1, sz))) {
                            return wy + 1.0f;
                        }
                        const int lvl = inferWaterLevel(bid, sx, sy, sz);
                        if (lvl < 0) {
                            return wy + 0.86f;
                        }
                        const float h = 0.86f - (static_cast<float>(lvl) / 7.0f) * 0.56f;
                        return wy + std::clamp(h, 0.30f, 0.86f);
                    };
                    int waterLevelInt = -1;
                    float waterDebugLevel = -1.0f;
                    waterLevelInt = inferWaterLevel(id, x, y, z);
                    if (waterLevelInt >= 0) {
                        waterDebugLevel = static_cast<float>(waterLevelInt) / 7.0f;
                    }
                    const auto shouldRenderWaterSide = [&](int nx, int ny, int nz) -> bool {
                        const BlockId nid = neighborAt(nx, ny, nz);
                        if (!isWaterLike(nid)) {
                            return !isOpaque(registry, nid);
                        }
                        // Internal stitch wall only for significant drops to avoid visible holes,
                        // while suppressing tiny step walls that look like surface gridlines.
                        constexpr float kInternalStitchMinDrop = 0.10f;
                        const float currH = cellTopHeight(id, x, y, z);
                        const float neighH = cellTopHeight(nid, nx, ny, nz);
                        return (currH - neighH) > kInternalStitchMinDrop;
                    };
                    const auto waterSideBaseY = [&](int nx, int ny, int nz) -> float {
                        const BlockId nid = neighborAt(nx, ny, nz);
                        if (!isWaterLike(nid)) {
                            return wy;
                        }
                        return cellTopHeight(nid, nx, ny, nz);
                    };
                    const auto sampleWaterHeight = [&](int sx, int sy, int sz) -> float {
                        const BlockId sid = neighborAt(sx, sy, sz);
                        if (!isWaterLike(sid)) {
                            return 0.0f;
                        }
                        return cellTopHeight(sid, sx, sy, sz);
                    };
                    const auto blendCorner = [&](int dx, int dz) -> float {
                        float sum = 0.0f;
                        int count = 0;
                        const std::array<glm::ivec2, 4> offs = {
                            glm::ivec2{0, 0}, glm::ivec2{dx, 0}, glm::ivec2{0, dz},
                            glm::ivec2{dx, dz}};
                        for (const auto &o : offs) {
                            const BlockId sid = neighborAt(x + o.x, y, z + o.y);
                            if (!isWaterLike(sid)) {
                                continue;
                            }
                            sum += sampleWaterHeight(x + o.x, y, z + o.y);
                            ++count;
                        }
                        return (count > 0) ? (sum / static_cast<float>(count)) : (wy + 0.86f);
                    };

                    float hNW = topCovered ? (wy + 1.0f) : blendCorner(-1, -1);
                    float hNE = topCovered ? (wy + 1.0f) : blendCorner(1, -1);
                    float hSW = topCovered ? (wy + 1.0f) : blendCorner(-1, 1);
                    float hSE = topCovered ? (wy + 1.0f) : blendCorner(1, 1);
                    const float xMinTop = wx;
                    const float xMaxTop = wx + 1.0f;
                    const float zMinTop = wz;
                    const float zMaxTop = wz + 1.0f;
                    const float xMinSide = wx;
                    const float xMaxSide = wx + 1.0f;
                    const float zMinSide = wz;
                    const float zMaxSide = wz + 1.0f;

                    if (isWaterloggedPlant(id)) {
                        const float sky = std::max(lighting.faceSkyLight(x, y, z, 0.92f),
                                                   lighting.faceSkyLight(x, y + 1, z, 0.95f));
                        const float block = std::max(lighting.faceBlockLight(x, y, z),
                                                     lighting.faceBlockLight(x, y + 1, z));
                        const float surfaceY = cellTopHeight(id, x, y, z);
                        const float plantTopY = std::max(wy + 0.25f, surfaceY - 0.02f);
                        appendCrossPlant(out, wx, wy, wz, plantTopY, flipV(atlas.uvRect(d.sideTile)),
                                         sky, block);
                    }

                    const BlockId nPosX = neighborAt(x + 1, y, z);
                    if (hasNeighborChunkFor(x + 1, z) && shouldRenderWaterSide(x + 1, y, z)) {
                        const float sky = lighting.faceSkyLight(x + 1, y, z, 0.86f);
                        const float block = lighting.faceBlockLight(x + 1, y, z);
                        const float faceX = xMaxSide;
                        const float baseY = waterSideBaseY(x + 1, y, z);
                        appendQuad(out, {faceX, baseY, zMinSide}, {faceX, baseY, zMaxSide},
                                   {faceX, hSE, zMaxSide}, {faceX, hNE, zMinSide}, {1, 0, 0},
                                   waterSideUv, sky, block, waterDebugLevel);
                    }
                    const BlockId nNegX = neighborAt(x - 1, y, z);
                    if (hasNeighborChunkFor(x - 1, z) && shouldRenderWaterSide(x - 1, y, z)) {
                        const float sky = lighting.faceSkyLight(x - 1, y, z, 0.86f);
                        const float block = lighting.faceBlockLight(x - 1, y, z);
                        const float faceX = xMinSide;
                        const float baseY = waterSideBaseY(x - 1, y, z);
                        appendQuad(out, {faceX, baseY, zMaxSide}, {faceX, baseY, zMinSide},
                                   {faceX, hNW, zMinSide}, {faceX, hSW, zMaxSide}, {-1, 0, 0},
                                   waterSideUv, sky, block, waterDebugLevel);
                    }
                    if (!topCovered) {
                        const float sky = lighting.faceSkyLight(x, y + 1, z, 1.00f);
                        const float block = lighting.faceBlockLight(x, y + 1, z);
                        const bool flipDiag = ((x + z) & 1) != 0;
                        appendQuadWithDiagonal(out, {xMinTop, hNW, zMinTop},
                                               {xMaxTop, hNE, zMinTop}, {xMaxTop, hSE, zMaxTop},
                                               {xMinTop, hSW, zMaxTop}, {0, 1, 0}, waterTopUv, sky,
                                               block, flipDiag, waterDebugLevel);
                    }
                    const BlockId nPosZ = neighborAt(x, y, z + 1);
                    if (hasNeighborChunkFor(x, z + 1) && shouldRenderWaterSide(x, y, z + 1)) {
                        const float sky = lighting.faceSkyLight(x, y, z + 1, 0.90f);
                        const float block = lighting.faceBlockLight(x, y, z + 1);
                        const float faceZ = zMaxSide;
                        const float baseY = waterSideBaseY(x, y, z + 1);
                        appendQuad(out, {xMaxSide, baseY, faceZ}, {xMinSide, baseY, faceZ},
                                   {xMinSide, hSW, faceZ}, {xMaxSide, hSE, faceZ}, {0, 0, 1},
                                   waterSideUv, sky, block, waterDebugLevel);
                    }
                    const BlockId nNegZ = neighborAt(x, y, z - 1);
                    if (hasNeighborChunkFor(x, z - 1) && shouldRenderWaterSide(x, y, z - 1)) {
                        const float sky = lighting.faceSkyLight(x, y, z - 1, 0.90f);
                        const float block = lighting.faceBlockLight(x, y, z - 1);
                        const float faceZ = zMinSide;
                        const float baseY = waterSideBaseY(x, y, z - 1);
                        appendQuad(out, {xMinSide, baseY, faceZ}, {xMaxSide, baseY, faceZ},
                                   {xMaxSide, hNE, faceZ}, {xMinSide, hNW, faceZ}, {0, 0, -1},
                                   waterSideUv, sky, block, waterDebugLevel);
                    }
                    const BlockId nDown = neighborAt(x, y - 1, z);
                    if (y > 0 && !bottomCovered && nDown == AIR) {
                        const float sky = lighting.faceSkyLight(x, y - 1, z, 0.56f);
                        const float block = lighting.faceBlockLight(x, y - 1, z);
                        appendQuad(out, {xMinTop, wy, zMaxTop}, {xMaxTop, wy, zMaxTop},
                                   {xMaxTop, wy, zMinTop}, {xMinTop, wy, zMinTop}, {0, -1, 0},
                                   waterBottomUv, sky, block, waterDebugLevel);
                    }
                    continue;
                }
                if (isLavaRenderable(id)) {
                    const auto &lavaDef = registry.get(LAVA);
                    const glm::vec4 lavaSideUv = flipV(atlas.uvRect(lavaDef.sideTile));
                    const glm::vec4 lavaTopUv = atlas.uvRect(lavaDef.topTile);
                    // Lava should continue to appear emissive even when enclosed.
                    constexpr float kMinLavaBlockLight = 12.0f / 15.0f;
                    const auto isLavaNeighbor = [&](int nx, int ny, int nz) {
                        return isLavaLike(neighborAt(nx, ny, nz));
                    };
                    const auto inferLavaLevel = [&](BlockId bid, int sx, int sy, int sz) -> int {
                        if (!isLavaLike(bid)) {
                            return -1;
                        }
                        if (fluidLevelLookup) {
                            const int swx = chunkXZ.x * Chunk::SX + sx;
                            const int swz = chunkXZ.y * Chunk::SZ + sz;
                            const int lvl = fluidLevelLookup(LAVA, swx, sy, swz);
                            if (lvl >= 0) {
                                return std::clamp(lvl, 0, 4);
                            }
                        }
                        if (bid == LAVA_SOURCE) {
                            return 0;
                        }
                        return 4;
                    };
                    const bool topCovered = isLavaNeighbor(x, y + 1, z);
                    const auto sampleLavaHeight = [&](int sx, int sy, int sz) -> float {
                        const BlockId sid = neighborAt(sx, sy, sz);
                        if (!isLavaLike(sid)) {
                            return 0.0f;
                        }
                        if (isLavaLike(neighborAt(sx, sy + 1, sz))) {
                            return wy + 1.0f;
                        }
                        const int lvl = inferLavaLevel(sid, sx, sy, sz);
                        if (lvl < 0) {
                            return wy + 0.86f;
                        }
                        const float h = 1.0f - (static_cast<float>(std::clamp(lvl, 0, 4)) / 6.0f);
                        return wy + std::clamp(h, 0.26f, 1.0f);
                    };
                    const auto blendLavaCorner = [&](int dx, int dz) -> float {
                        float sum = 0.0f;
                        int count = 0;
                        const std::array<glm::ivec2, 4> offs = {
                            glm::ivec2{0, 0}, glm::ivec2{dx, 0}, glm::ivec2{0, dz},
                            glm::ivec2{dx, dz}};
                        for (const auto &o : offs) {
                            if (!isLavaNeighbor(x + o.x, y, z + o.y)) {
                                continue;
                            }
                            sum += sampleLavaHeight(x + o.x, y, z + o.y);
                            ++count;
                        }
                        return (count > 0) ? (sum / static_cast<float>(count)) : (wy + 0.86f);
                    };
                    const auto shouldRenderLavaSide = [&](int nx, int ny, int nz) -> bool {
                        const BlockId nid = neighborAt(nx, ny, nz);
                        if (!isLavaLike(nid)) {
                            return !isOpaque(registry, nid);
                        }
                        const float currH = sampleLavaHeight(x, y, z);
                        const float neighH = sampleLavaHeight(nx, ny, nz);
                        return (currH - neighH) > 0.02f;
                    };

                    float hNW = topCovered ? (wy + 1.0f) : blendLavaCorner(-1, -1);
                    float hNE = topCovered ? (wy + 1.0f) : blendLavaCorner(1, -1);
                    float hSW = topCovered ? (wy + 1.0f) : blendLavaCorner(-1, 1);
                    float hSE = topCovered ? (wy + 1.0f) : blendLavaCorner(1, 1);

                    const BlockId nPosX = neighborAt(x + 1, y, z);
                    if (hasNeighborChunkFor(x + 1, z) && shouldRenderLavaSide(x + 1, y, z)) {
                        const float sky = lighting.faceSkyLight(x + 1, y, z, 0.86f);
                        const float block =
                            std::max(kMinLavaBlockLight, lighting.faceBlockLight(x + 1, y, z));
                        appendQuad(out, {wx + 1, wy, wz}, {wx + 1, wy, wz + 1},
                                   {wx + 1, hSE, wz + 1}, {wx + 1, hNE, wz}, {1, 0, 0}, lavaSideUv,
                                   sky, block);
                    }
                    const BlockId nNegX = neighborAt(x - 1, y, z);
                    if (hasNeighborChunkFor(x - 1, z) && shouldRenderLavaSide(x - 1, y, z)) {
                        const float sky = lighting.faceSkyLight(x - 1, y, z, 0.86f);
                        const float block =
                            std::max(kMinLavaBlockLight, lighting.faceBlockLight(x - 1, y, z));
                        appendQuad(out, {wx, wy, wz + 1}, {wx, wy, wz}, {wx, hNW, wz},
                                   {wx, hSW, wz + 1}, {-1, 0, 0}, lavaSideUv, sky, block);
                    }
                    if (!topCovered) {
                        const float sky = lighting.faceSkyLight(x, y + 1, z, 1.00f);
                        const float block =
                            std::max(kMinLavaBlockLight, lighting.faceBlockLight(x, y + 1, z));
                        const bool flipDiag = ((x + z) & 1) != 0;
                        appendQuadWithDiagonal(out, {wx, hNW, wz}, {wx + 1, hNE, wz},
                                               {wx + 1, hSE, wz + 1}, {wx, hSW, wz + 1}, {0, 1, 0},
                                               lavaTopUv, sky, block, flipDiag);
                    }
                    const BlockId nPosZ = neighborAt(x, y, z + 1);
                    if (hasNeighborChunkFor(x, z + 1) && shouldRenderLavaSide(x, y, z + 1)) {
                        const float sky = lighting.faceSkyLight(x, y, z + 1, 0.90f);
                        const float block =
                            std::max(kMinLavaBlockLight, lighting.faceBlockLight(x, y, z + 1));
                        appendQuad(out, {wx + 1, wy, wz + 1}, {wx, wy, wz + 1},
                                   {wx, hSW, wz + 1}, {wx + 1, hSE, wz + 1}, {0, 0, 1}, lavaSideUv,
                                   sky, block);
                    }
                    const BlockId nNegZ = neighborAt(x, y, z - 1);
                    if (hasNeighborChunkFor(x, z - 1) && shouldRenderLavaSide(x, y, z - 1)) {
                        const float sky = lighting.faceSkyLight(x, y, z - 1, 0.90f);
                        const float block =
                            std::max(kMinLavaBlockLight, lighting.faceBlockLight(x, y, z - 1));
                        appendQuad(out, {wx, wy, wz}, {wx + 1, wy, wz}, {wx + 1, hNE, wz},
                                   {wx, hNW, wz}, {0, 0, -1}, lavaSideUv, sky, block);
                    }
                    continue;
                }
                if (isCrossPlant(id)) {
                    // Plants should not become fully dim just because a block is directly above:
                    // blend light from their own cell and the cell above.
                    const float sky = std::max(lighting.faceSkyLight(x, y, z, 0.92f),
                                               lighting.faceSkyLight(x, y + 1, z, 0.95f));
                    const float block =
                        std::max(lighting.faceBlockLight(x, y, z), lighting.faceBlockLight(x, y + 1, z));
                    appendCrossPlant(out, wx, wy, wz, wy + 1.0f, flipV(atlas.uvRect(d.sideTile)), sky,
                                     block);
                    continue;
                }

                if (isTorch(id)) {
                    const float sky = lighting.faceSkyLight(x, y + 1, z, 0.95f);
                    const float block =
                        std::max(lighting.faceBlockLight(x, y, z), lighting.faceBlockLight(x, y + 1, z));
                    if (id == TORCH) {
                        appendCrossPlant(out, wx, wy, wz, wy + 1.0f, flipV(atlas.uvRect(d.sideTile)),
                                         sky, block);
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

                if (hasNeighborChunkFor(x + 1, z) &&
                    isFaceExposed(registry, id, neighborAt(x + 1, y, z))) {
                    const float sky = lighting.faceSkyLight(x + 1, y, z, 0.86f);
                    const float block = lighting.faceBlockLight(x + 1, y, z);
                    appendQuad(out, {wx + 1, wy, wz}, {wx + 1, wy, wz + 1},
                               {wx + 1, wy + 1, wz + 1}, {wx + 1, wy + 1, wz}, {1, 0, 0},
                               (furnace && front.x == 1) ? furnaceFrontUv : sideUv, sky, block);
                }
                if (hasNeighborChunkFor(x - 1, z) &&
                    isFaceExposed(registry, id, neighborAt(x - 1, y, z))) {
                    const float sky = lighting.faceSkyLight(x - 1, y, z, 0.86f);
                    const float block = lighting.faceBlockLight(x - 1, y, z);
                    appendQuad(out, {wx, wy, wz + 1}, {wx, wy, wz}, {wx, wy + 1, wz},
                               {wx, wy + 1, wz + 1}, {-1, 0, 0},
                               (furnace && front.x == -1) ? furnaceFrontUv : sideUv,
                               sky, block);
                }
                if (isFaceExposed(registry, id, neighborAt(x, y + 1, z))) {
                    const float sky = lighting.faceSkyLight(x, y + 1, z, 1.00f);
                    const float block = lighting.faceBlockLight(x, y + 1, z);
                    appendQuad(out, {wx, wy + 1, wz}, {wx + 1, wy + 1, wz},
                               {wx + 1, wy + 1, wz + 1}, {wx, wy + 1, wz + 1}, {0, 1, 0},
                               atlas.uvRect(d.topTile), sky, block);
                }
                if (y > 0 && isFaceExposed(registry, id, neighborAt(x, y - 1, z))) {
                    const float sky = lighting.faceSkyLight(x, y - 1, z, 0.56f);
                    const float block = lighting.faceBlockLight(x, y - 1, z);
                    appendQuad(out, {wx, wy, wz + 1}, {wx + 1, wy, wz + 1}, {wx + 1, wy, wz},
                               {wx, wy, wz}, {0, -1, 0}, atlas.uvRect(d.bottomTile), sky, block);
                }
                if (hasNeighborChunkFor(x, z + 1) &&
                    isFaceExposed(registry, id, neighborAt(x, y, z + 1))) {
                    const float sky = lighting.faceSkyLight(x, y, z + 1, 0.90f);
                    const float block = lighting.faceBlockLight(x, y, z + 1);
                    appendQuad(out, {wx + 1, wy, wz + 1}, {wx, wy, wz + 1}, {wx, wy + 1, wz + 1},
                               {wx + 1, wy + 1, wz + 1}, {0, 0, 1},
                               (furnace && front.y == 1) ? furnaceFrontUv : sideUv,
                               sky, block);
                }
                if (hasNeighborChunkFor(x, z - 1) &&
                    isFaceExposed(registry, id, neighborAt(x, y, z - 1))) {
                    const float sky = lighting.faceSkyLight(x, y, z - 1, 0.90f);
                    const float block = lighting.faceBlockLight(x, y, z - 1);
                    appendQuad(out, {wx, wy, wz}, {wx + 1, wy, wz}, {wx + 1, wy + 1, wz},
                               {wx, wy + 1, wz}, {0, 0, -1},
                               (furnace && front.y == -1) ? furnaceFrontUv : sideUv, sky,
                               block);
                }
            }
        }
    }
}

gfx::CpuMesh ChunkMesher::buildFaceCulled(const Chunk &chunk, const gfx::TextureAtlas &atlas,
                                          const BlockRegistry &registry, glm::ivec2 chunkXZ,
                                          const NeighborChunks &neighbors, bool smoothLighting,
                                          const std::function<int(BlockId, int, int, int)>
                                              &fluidLevelLookup,
                                          std::size_t reserveVertices,
                                          std::size_t reserveIndices) {
    gfx::CpuMesh out;
    buildFaceCulledInto(out, chunk, atlas, registry, chunkXZ, neighbors, smoothLighting,
                        fluidLevelLookup, reserveVertices, reserveIndices);
    return out;
}

} // namespace voxel
