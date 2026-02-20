#include "world/WorldGen.hpp"

#include "voxel/Block.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <utility>

namespace world {
namespace {

enum class Biome : std::uint8_t {
    Plains = 0,
    Forest = 1,
    Desert = 2,
    Mountains = 3,
    Swamp = 4,
    Taiga = 5,
    Snowy = 6,
    Badlands = 7,
};

struct BiomeParams {
    float hillAmp;
    float mountainAmp;
    voxel::BlockId top;
    voxel::BlockId filler;
    int fillerDepthMin;
    int fillerDepthMax;
    float treeBase;
};

BiomeParams biomeParams(Biome biome) {
    switch (biome) {
    case Biome::Forest:
        return {12.0f, 16.0f, voxel::GRASS, voxel::DIRT, 3, 6, 0.030f};
    case Biome::Desert:
        return {8.0f, 10.0f, voxel::SAND, voxel::SAND, 4, 7, 0.0f};
    case Biome::Mountains:
        return {7.0f, 30.0f, voxel::STONE, voxel::STONE, 2, 4, 0.002f};
    case Biome::Swamp:
        return {4.0f, 5.0f, voxel::GRASS, voxel::DIRT, 2, 4, 0.010f};
    case Biome::Taiga:
        return {10.0f, 16.0f, voxel::SNOW_BLOCK, voxel::DIRT, 3, 5, 0.016f};
    case Biome::Snowy:
        return {8.0f, 12.0f, voxel::SNOW_BLOCK, voxel::STONE, 2, 4, 0.001f};
    case Biome::Badlands:
        return {9.0f, 18.0f, voxel::SAND, voxel::SANDSTONE, 3, 6, 0.0f};
    case Biome::Plains:
    default:
        return {9.0f, 12.0f, voxel::GRASS, voxel::DIRT, 3, 5, 0.012f};
    }
}

Biome pickBiome(float temp, float moisture, float rugged, float selector) {
    if (rugged > 0.72f) {
        return Biome::Mountains;
    }
    if (temp < -0.32f) {
        return (moisture > 0.0f) ? Biome::Taiga : Biome::Snowy;
    }
    if (temp > 0.38f && moisture < 0.02f) {
        return (selector > 0.62f) ? Biome::Badlands : Biome::Desert;
    }
    if (moisture > 0.34f && temp > -0.10f) {
        return Biome::Forest;
    }
    if (moisture > 0.28f && temp < 0.08f) {
        return Biome::Swamp;
    }
    return Biome::Plains;
}

void placeTree(voxel::Chunk &chunk, int lx, int baseY, int lz, int height, voxel::BlockId trunkId,
               voxel::BlockId leavesId, bool sparseCanopy) {
    if (baseY + height + 3 >= voxel::Chunk::SY) {
        return;
    }

    for (int y = baseY + 1; y <= baseY + height; ++y) {
        chunk.setRaw(lx, y, lz, trunkId);
    }
    if (leavesId == voxel::AIR) {
        return;
    }

    const int canopyBase = baseY + height - 2;
    for (int y = canopyBase; y <= baseY + height + 2; ++y) {
        const int dy = y - (baseY + height);
        const int radius = (dy >= 1) ? 1 : 2;
        for (int dx = -radius; dx <= radius; ++dx) {
            for (int dz = -radius; dz <= radius; ++dz) {
                const int x = lx + dx;
                const int z = lz + dz;
                if (!chunk.inBounds(x, y, z)) {
                    continue;
                }
                if (dx == 0 && dz == 0 && y <= baseY + height) {
                    continue;
                }
                if (std::abs(dx) + std::abs(dz) > radius + 1) {
                    continue;
                }
                if (sparseCanopy && ((x * 31 + y * 17 + z * 13) & 3) == 0) {
                    continue;
                }
                const voxel::BlockId existing = chunk.getUnchecked(x, y, z);
                if (existing == voxel::AIR || existing == voxel::WATER) {
                    chunk.setRaw(x, y, z, leavesId);
                }
            }
        }
    }
}

int floorDiv(int a, int b) {
    int q = a / b;
    int r = a % b;
    if (r != 0 && ((r > 0) != (b > 0))) {
        --q;
    }
    return q;
}

} // namespace

float WorldGen::hash01(std::int32_t x, std::int32_t y, std::int32_t z) const {
    std::uint32_t h = static_cast<std::uint32_t>(x) * 374761393u;
    h ^= static_cast<std::uint32_t>(y) * 668265263u;
    h ^= static_cast<std::uint32_t>(z) * 2246822519u;
    h ^= seed_ * 3266489917u;
    h = (h ^ (h >> 13u)) * 1274126177u;
    h ^= (h >> 16u);
    return static_cast<float>(h & 0x00FFFFFFu) / static_cast<float>(0x00FFFFFFu); // [0, 1]
}

float WorldGen::smooth(float t) {
    return t * t * t * (t * (t * 6.0f - 15.0f) + 10.0f);
}

float WorldGen::lerp(float a, float b, float t) {
    return a + (b - a) * t;
}

float WorldGen::valueNoise2D(float x, float z) const {
    const int x0 = static_cast<int>(std::floor(x));
    const int z0 = static_cast<int>(std::floor(z));
    const int x1 = x0 + 1;
    const int z1 = z0 + 1;

    const float tx = smooth(x - static_cast<float>(x0));
    const float tz = smooth(z - static_cast<float>(z0));

    const float v00 = hash01(x0, 0, z0) * 2.0f - 1.0f;
    const float v10 = hash01(x1, 0, z0) * 2.0f - 1.0f;
    const float v01 = hash01(x0, 0, z1) * 2.0f - 1.0f;
    const float v11 = hash01(x1, 0, z1) * 2.0f - 1.0f;

    const float ix0 = lerp(v00, v10, tx);
    const float ix1 = lerp(v01, v11, tx);
    return lerp(ix0, ix1, tz);
}

float WorldGen::valueNoise3D(float x, float y, float z) const {
    const int x0 = static_cast<int>(std::floor(x));
    const int y0 = static_cast<int>(std::floor(y));
    const int z0 = static_cast<int>(std::floor(z));
    const int x1 = x0 + 1;
    const int y1 = y0 + 1;
    const int z1 = z0 + 1;

    const float tx = smooth(x - static_cast<float>(x0));
    const float ty = smooth(y - static_cast<float>(y0));
    const float tz = smooth(z - static_cast<float>(z0));

    const float c000 = hash01(x0, y0, z0) * 2.0f - 1.0f;
    const float c100 = hash01(x1, y0, z0) * 2.0f - 1.0f;
    const float c010 = hash01(x0, y1, z0) * 2.0f - 1.0f;
    const float c110 = hash01(x1, y1, z0) * 2.0f - 1.0f;
    const float c001 = hash01(x0, y0, z1) * 2.0f - 1.0f;
    const float c101 = hash01(x1, y0, z1) * 2.0f - 1.0f;
    const float c011 = hash01(x0, y1, z1) * 2.0f - 1.0f;
    const float c111 = hash01(x1, y1, z1) * 2.0f - 1.0f;

    const float x00 = lerp(c000, c100, tx);
    const float x10 = lerp(c010, c110, tx);
    const float x01 = lerp(c001, c101, tx);
    const float x11 = lerp(c011, c111, tx);
    const float y0i = lerp(x00, x10, ty);
    const float y1i = lerp(x01, x11, ty);
    return lerp(y0i, y1i, tz);
}

float WorldGen::fbm2D(float x, float z, int octaves, float lacunarity, float gain) const {
    float amp = 1.0f;
    float freq = 1.0f;
    float sum = 0.0f;
    float norm = 0.0f;
    for (int i = 0; i < octaves; ++i) {
        sum += valueNoise2D(x * freq, z * freq) * amp;
        norm += amp;
        amp *= gain;
        freq *= lacunarity;
    }
    return (norm > 0.0f) ? (sum / norm) : 0.0f;
}

float WorldGen::fbm3D(float x, float y, float z, int octaves, float lacunarity, float gain) const {
    float amp = 1.0f;
    float freq = 1.0f;
    float sum = 0.0f;
    float norm = 0.0f;
    for (int i = 0; i < octaves; ++i) {
        sum += valueNoise3D(x * freq, y * freq, z * freq) * amp;
        norm += amp;
        amp *= gain;
        freq *= lacunarity;
    }
    return (norm > 0.0f) ? (sum / norm) : 0.0f;
}

void WorldGen::fillChunk(voxel::Chunk &chunk, ChunkCoord cc) const {
    constexpr int seaLevel = 62;
    auto getAt = [&](int x, int y, int z) -> voxel::BlockId { return chunk.getUnchecked(x, y, z); };
    auto setAt = [&](int x, int y, int z, voxel::BlockId id) { chunk.setRaw(x, y, z, id); };

    std::array<std::array<int, voxel::Chunk::SZ>, voxel::Chunk::SX> topHeights{};
    std::array<std::array<Biome, voxel::Chunk::SZ>, voxel::Chunk::SX> biomes{};
    std::array<std::array<float, voxel::Chunk::SZ>, voxel::Chunk::SX> moistures{};
    std::array<std::array<float, voxel::Chunk::SZ>, voxel::Chunk::SX> riverStrength{};

    // Pass 1: biome field + terrain + water.
    for (int lx = 0; lx < voxel::Chunk::SX; ++lx) {
        for (int lz = 0; lz < voxel::Chunk::SZ; ++lz) {
            const int wx = cc.x * voxel::Chunk::SX + lx;
            const int wz = cc.z * voxel::Chunk::SZ + lz;
            const float continental = fbm2D(wx * 0.0018f, wz * 0.0018f, 4, 2.0f, 0.5f);
            const float ridges = 1.0f - std::abs(fbm2D(wx * 0.0038f, wz * 0.0038f, 4, 2.0f, 0.55f));
            const float rugged = ridges * std::max(0.0f, continental + 0.2f);

            const float temp = fbm2D((wx + 18000) * 0.0012f, (wz - 9000) * 0.0012f, 3, 2.0f, 0.5f);
            const float moisture =
                fbm2D((wx - 25000) * 0.0012f, (wz + 17000) * 0.0012f, 3, 2.0f, 0.5f);
            const float selector =
                fbm2D((wx + 3200) * 0.0022f, (wz - 4100) * 0.0022f, 3, 2.0f, 0.5f);
            const Biome biome = pickBiome(temp, moisture, rugged, selector);
            const BiomeParams bp = biomeParams(biome);

            const float localHills = fbm2D(wx * 0.0100f, wz * 0.0100f, 5, 2.0f, 0.5f);
            const float mountains = rugged * rugged;

            const float riverNoise = std::abs(fbm2D(wx * 0.0035f, wz * 0.0035f, 3, 2.0f, 0.55f));
            const float river = std::clamp((0.11f - riverNoise) / 0.11f, 0.0f, 1.0f);
            const float riverCarve = river * river * (biome == Biome::Mountains ? 5.0f : 10.0f);

            float h = static_cast<float>(seaLevel);
            h += continental * 26.0f;
            h += localHills * bp.hillAmp;
            h += mountains * bp.mountainAmp;
            h -= riverCarve;
            if (biome == Biome::Swamp) {
                h -= 3.0f;
            }

            int height = static_cast<int>(std::floor(h));
            height = std::clamp(height, 6, voxel::Chunk::SY - 2);

            const float depthNoise = (fbm2D(wx * 0.03f, wz * 0.03f, 3, 2.0f, 0.5f) + 1.0f) * 0.5f;
            const int fillerDepth =
                bp.fillerDepthMin +
                static_cast<int>(std::floor(
                    depthNoise * static_cast<float>(bp.fillerDepthMax - bp.fillerDepthMin + 1)));
            const int fillerTop = std::max(1, height - fillerDepth);

            for (int y = 0; y < fillerTop; ++y) {
                setAt(lx, y, lz, voxel::STONE);
            }
            for (int y = fillerTop; y < height - 1; ++y) {
                setAt(lx, y, lz, bp.filler);
            }

            voxel::BlockId top = bp.top;
            const bool beachBand = height >= seaLevel - 2 && height <= seaLevel + 1;
            if (beachBand && biome != Biome::Snowy && biome != Biome::Taiga) {
                top = voxel::SAND;
            }
            if ((biome == Biome::Mountains || biome == Biome::Snowy) && height > seaLevel + 20) {
                top = voxel::STONE;
            }
            setAt(lx, height - 1, lz, top);

            // Minecraft-like ore pass.
            for (int y = 1; y < fillerTop; ++y) {
                if (getAt(lx, y, lz) != voxel::STONE) {
                    continue;
                }
                const float coalN = fbm3D(wx * 0.08f, y * 0.10f, wz * 0.08f, 2, 2.0f, 0.5f);
                const float copperN =
                    fbm3D((wx + 1900) * 0.09f, y * 0.11f, (wz - 900) * 0.09f, 2, 2.0f, 0.5f);
                const float ironN =
                    fbm3D((wx - 2100) * 0.11f, y * 0.13f, (wz + 700) * 0.11f, 2, 2.0f, 0.5f);
                const float goldN =
                    fbm3D((wx + 4100) * 0.11f, y * 0.15f, (wz + 3300) * 0.11f, 2, 2.0f, 0.5f);
                const float diamondN =
                    fbm3D((wx - 7000) * 0.12f, y * 0.18f, (wz + 5100) * 0.12f, 2, 2.0f, 0.5f);
                const float emeraldN =
                    fbm3D((wx + 9100) * 0.07f, y * 0.10f, (wz - 4300) * 0.07f, 2, 2.0f, 0.5f);

                if (y >= 8 && y <= 120 && coalN > 0.53f) {
                    setAt(lx, y, lz, voxel::COAL_ORE);
                    continue;
                }
                if (y >= 40 && y <= 96 && copperN > 0.60f) {
                    setAt(lx, y, lz, voxel::COPPER_ORE);
                    continue;
                }
                if (y >= 8 && y <= 72 && ironN > 0.66f) {
                    setAt(lx, y, lz, voxel::IRON_ORE);
                    continue;
                }
                float goldThresh = 0.79f;
                if (biome == Biome::Badlands) {
                    goldThresh -= 0.07f;
                }
                if (y >= 5 && y <= 36 && goldN > goldThresh) {
                    setAt(lx, y, lz, voxel::GOLD_ORE);
                    continue;
                }
                if (y >= 3 && y <= 18 && diamondN > 0.87f) {
                    setAt(lx, y, lz, voxel::DIAMOND_ORE);
                    continue;
                }
                if (biome == Biome::Mountains && y >= 40 && y <= 104 && emeraldN > 0.90f) {
                    setAt(lx, y, lz, voxel::EMERALD_ORE);
                    continue;
                }
            }

            const bool riverbed = river > 0.12f;
            const int waterFillThreshold = riverbed ? (seaLevel + 1) : seaLevel;
            if (height <= waterFillThreshold) {
                if (height >= 2) {
                    setAt(lx, height - 1, lz, voxel::SAND);
                    if (height >= 3) {
                        setAt(lx, height - 2, lz, voxel::SAND);
                    }
                }
                for (int y = height; y <= seaLevel && y < voxel::Chunk::SY; ++y) {
                    if (getAt(lx, y, lz) == voxel::AIR) {
                        if ((biome == Biome::Snowy || biome == Biome::Taiga) && y == seaLevel) {
                            setAt(lx, y, lz, voxel::ICE);
                        } else {
                            setAt(lx, y, lz, voxel::WATER);
                        }
                    }
                }
            }

            const int clearFrom = std::min(voxel::Chunk::SY, std::max(height + 1, seaLevel + 1));
            for (int y = clearFrom; y < voxel::Chunk::SY; ++y) {
                if (getAt(lx, y, lz) != voxel::WATER) {
                    setAt(lx, y, lz, voxel::AIR);
                }
            }

            topHeights[lx][lz] = height;
            biomes[lx][lz] = biome;
            moistures[lx][lz] = moisture;
            riverStrength[lx][lz] = river;
        }
    }

    // Pass 2: connected "ant tunnel" caves with cliff-biased mouths.
    auto carveSphereWorld = [&](float wx, float wy, float wz, float radius, bool wet) {
        const int minX = static_cast<int>(std::floor(wx - radius));
        const int maxX = static_cast<int>(std::floor(wx + radius));
        const int minY = static_cast<int>(std::floor(wy - radius));
        const int maxY = static_cast<int>(std::floor(wy + radius));
        const int minZ = static_cast<int>(std::floor(wz - radius));
        const int maxZ = static_cast<int>(std::floor(wz + radius));
        const int baseX = cc.x * voxel::Chunk::SX;
        const int baseZ = cc.z * voxel::Chunk::SZ;

        for (int x = minX; x <= maxX; ++x) {
            for (int y = minY; y <= maxY; ++y) {
                for (int z = minZ; z <= maxZ; ++z) {
                    const float dx = (static_cast<float>(x) + 0.5f) - wx;
                    const float dy = (static_cast<float>(y) + 0.5f) - wy;
                    const float dz = (static_cast<float>(z) + 0.5f) - wz;
                    if (dx * dx + dy * dy + dz * dz > radius * radius) {
                        continue;
                    }
                    const int lx = x - baseX;
                    const int lz = z - baseZ;
                    if (!chunk.inBounds(lx, y, lz)) {
                        continue;
                    }
                    if (wet && y <= seaLevel) {
                        setAt(lx, y, lz, voxel::WATER);
                    } else {
                        setAt(lx, y, lz, voxel::AIR);
                    }
                }
            }
        }
    };

    auto surfaceHeightAt = [&](int wx, int wz) -> int {
        const float continental = fbm2D(wx * 0.0018f, wz * 0.0018f, 4, 2.0f, 0.5f);
        const float ridges = 1.0f - std::abs(fbm2D(wx * 0.0038f, wz * 0.0038f, 4, 2.0f, 0.55f));
        const float rugged = ridges * std::max(0.0f, continental + 0.2f);
        const float temp = fbm2D((wx + 18000) * 0.0012f, (wz - 9000) * 0.0012f, 3, 2.0f, 0.5f);
        const float moisture = fbm2D((wx - 25000) * 0.0012f, (wz + 17000) * 0.0012f, 3, 2.0f, 0.5f);
        const float selector = fbm2D((wx + 3200) * 0.0022f, (wz - 4100) * 0.0022f, 3, 2.0f, 0.5f);
        const Biome biome = pickBiome(temp, moisture, rugged, selector);
        const BiomeParams bp = biomeParams(biome);
        const float localHills = fbm2D(wx * 0.0100f, wz * 0.0100f, 5, 2.0f, 0.5f);
        const float mountains = rugged * rugged;
        const float riverNoise = std::abs(fbm2D(wx * 0.0035f, wz * 0.0035f, 3, 2.0f, 0.55f));
        const float river = std::clamp((0.11f - riverNoise) / 0.11f, 0.0f, 1.0f);
        const float riverCarve = river * river * (biome == Biome::Mountains ? 5.0f : 10.0f);

        float h = static_cast<float>(seaLevel);
        h += continental * 26.0f;
        h += localHills * bp.hillAmp;
        h += mountains * bp.mountainAmp;
        h -= riverCarve;
        if (biome == Biome::Swamp) {
            h -= 3.0f;
        }
        return std::clamp(static_cast<int>(std::floor(h)), 6, voxel::Chunk::SY - 2);
    };

    struct Node {
        float x;
        float y;
        float z;
    };

    const int regionSizeBlocks = 48;
    const int chunkBaseX = cc.x * voxel::Chunk::SX;
    const int chunkBaseZ = cc.z * voxel::Chunk::SZ;
    const int regionX = (chunkBaseX >= 0)
                            ? (chunkBaseX / regionSizeBlocks)
                            : ((chunkBaseX - regionSizeBlocks + 1) / regionSizeBlocks);
    const int regionZ = (chunkBaseZ >= 0)
                            ? (chunkBaseZ / regionSizeBlocks)
                            : ((chunkBaseZ - regionSizeBlocks + 1) / regionSizeBlocks);

    auto makeNode = [&](int rx, int rz) -> Node {
        const float nx = static_cast<float>(rx * regionSizeBlocks) + 8.0f +
                         hash01(rx * 11, 1500, rz * 7) * static_cast<float>(regionSizeBlocks - 16);
        const float nz = static_cast<float>(rz * regionSizeBlocks) + 8.0f +
                         hash01(rx * 13, 1600, rz * 5) * static_cast<float>(regionSizeBlocks - 16);
        const int surface =
            surfaceHeightAt(static_cast<int>(std::floor(nx)), static_cast<int>(std::floor(nz)));
        const float depth = 14.0f + hash01(rx * 17, 1700, rz * 19) * 18.0f;
        const float ny = std::clamp(static_cast<float>(surface) - depth, 10.0f, 70.0f);
        return {nx, ny, nz};
    };

    auto carveTunnel = [&](const Node &a, const Node &b, float baseRadius, float wiggleFreq,
                           float wiggleAmp, bool wet) {
        const float vx = b.x - a.x;
        const float vy = b.y - a.y;
        const float vz = b.z - a.z;
        const float len = std::sqrt(vx * vx + vy * vy + vz * vz);
        const int steps = std::max(24, static_cast<int>(len * 1.6f));

        const float invLen = (len > 0.001f) ? (1.0f / len) : 1.0f;
        const float dirX = vx * invLen;
        const float dirY = vy * invLen;
        const float dirZ = vz * invLen;
        const float px = -dirZ;
        const float pz = dirX;

        for (int i = 0; i <= steps; ++i) {
            const float t = static_cast<float>(i) / static_cast<float>(steps);
            float wx = lerp(a.x, b.x, t);
            float wy = lerp(a.y, b.y, t);
            float wz = lerp(a.z, b.z, t);

            // Curvy, connected tunnels instead of random holes.
            const float n =
                fbm3D(wx * wiggleFreq, wy * wiggleFreq, wz * wiggleFreq, 3, 2.0f, 0.55f);
            wx += px * n * wiggleAmp;
            wz += pz * n * wiggleAmp;
            wy += std::sin(t * 6.2831853f + n * 2.0f) * 1.2f;

            const int surf =
                surfaceHeightAt(static_cast<int>(std::floor(wx)), static_cast<int>(std::floor(wz)));
            wy = std::min(wy, static_cast<float>(surf - 5));
            wy = std::clamp(wy, 8.0f, 78.0f);

            const float rNoise = fbm3D((wx + 600.0f) * 0.03f, (wy - 300.0f) * 0.03f,
                                       (wz + 1200.0f) * 0.03f, 2, 2.0f, 0.55f);
            const float radius = baseRadius + 0.55f * rNoise + 0.25f * std::sin(t * 9.0f);
            carveSphereWorld(wx, wy, wz, std::max(1.6f, radius), wet);

            // Rare wider pockets attached to the main tunnel.
            if ((i % 31) == 0 &&
                hash01(static_cast<int>(wx), i * 97, static_cast<int>(wz)) > 0.962f) {
                carveSphereWorld(wx, wy, wz, radius + 1.2f, false);
            }
        }
    };

    for (int rz = regionZ - 2; rz <= regionZ + 2; ++rz) {
        for (int rx = regionX - 2; rx <= regionX + 2; ++rx) {
            const Node center = makeNode(rx, rz);

            // Connect to neighbors in a deterministic sparse graph.
            const bool connectEast = hash01(rx * 31, 2100, rz * 13) > 0.20f;
            const bool connectSouth = hash01(rx * 29, 2200, rz * 17) > 0.20f;
            if (connectEast || (!connectEast && !connectSouth)) {
                const Node east = makeNode(rx + 1, rz);
                const float radius = 2.3f + hash01(rx * 23, 2300, rz * 11) * 0.8f;
                const bool wet = hash01(rx * 71, 2311, rz * 73) > 0.89f;
                carveTunnel(center, east, radius, 0.028f, 2.4f, wet);
            }
            if (connectSouth) {
                const Node south = makeNode(rx, rz + 1);
                const float radius = 2.2f + hash01(rx * 37, 2400, rz * 19) * 0.9f;
                const bool wet = hash01(rx * 79, 2413, rz * 83) > 0.89f;
                carveTunnel(center, south, radius, 0.029f, 2.3f, wet);
            }

            // Occasional extra link for branch loops.
            if (hash01(rx * 41, 2500, rz * 43) > 0.72f) {
                const int sx = (hash01(rx * 47, 2600, rz * 31) > 0.5f) ? 1 : -1;
                const int sz = (hash01(rx * 53, 2700, rz * 59) > 0.5f) ? 1 : -1;
                const Node diag = makeNode(rx + sx, rz + sz);
                const float radius = 2.0f + hash01(rx * 61, 2800, rz * 67) * 0.8f;
                const bool wet = hash01(rx * 89, 2819, rz * 97) > 0.91f;
                carveTunnel(center, diag, radius, 0.031f, 2.0f, wet);
            }
        }
    }

    // Freeform meandering worms add irregularity on top of the linked graph.
    for (int rz = regionZ - 1; rz <= regionZ + 1; ++rz) {
        for (int rx = regionX - 1; rx <= regionX + 1; ++rx) {
            const int wormCount = 1 + static_cast<int>(hash01(rx * 73, 3200, rz * 79) * 2.0f);
            for (int w = 0; w < wormCount; ++w) {
                float px =
                    static_cast<float>(rx * regionSizeBlocks) +
                    hash01(rx * 83, 3300 + w, rz * 89) * static_cast<float>(regionSizeBlocks);
                float pz =
                    static_cast<float>(rz * regionSizeBlocks) +
                    hash01(rx * 97, 3400 + w, rz * 101) * static_cast<float>(regionSizeBlocks);
                const int surf = surfaceHeightAt(static_cast<int>(std::floor(px)),
                                                 static_cast<int>(std::floor(pz)));
                float py = std::clamp(static_cast<float>(surf) -
                                          (10.0f + hash01(rx, 3500 + w, rz) * 22.0f),
                                      9.0f, 76.0f);

                float yaw = hash01(rx * 103, 3600 + w, rz * 107) * 6.2831853f;
                float pitch = (hash01(rx * 109, 3700 + w, rz * 113) - 0.5f) * 0.30f;
                const int steps =
                    70 + static_cast<int>(hash01(rx * 127, 3800 + w, rz * 131) * 90.0f);
                const float baseR = 1.7f + hash01(rx * 137, 3900 + w, rz * 139) * 1.2f;
                const bool wetWorm = hash01(rx * 149, 3911 + w, rz * 151) > 0.93f;

                for (int s = 0; s < steps; ++s) {
                    const float t = static_cast<float>(s) / static_cast<float>(steps);
                    const float nx = std::cos(yaw) * std::cos(pitch);
                    const float ny = std::sin(pitch);
                    const float nz = std::sin(yaw) * std::cos(pitch);
                    const float n = fbm3D(px * 0.027f, py * 0.027f, pz * 0.027f, 2, 2.0f, 0.55f);
                    const float r = std::max(1.6f, baseR + 0.7f * n + 0.35f * std::sin(t * 10.0f));
                    carveSphereWorld(px, py, pz, r, wetWorm);

                    yaw += fbm3D((px + 800.0f) * 0.02f, (py - 400.0f) * 0.02f,
                                 (pz + 250.0f) * 0.02f, 2, 2.0f, 0.55f) *
                           0.16f;
                    pitch = std::clamp(pitch + fbm3D((px - 200.0f) * 0.02f, (py + 200.0f) * 0.02f,
                                                     (pz - 600.0f) * 0.02f, 2, 2.0f, 0.55f) *
                                                   0.08f,
                                       -0.45f, 0.35f);
                    px += nx * 1.3f;
                    py += ny * 1.3f;
                    pz += nz * 1.3f;

                    const int surfLocal = surfaceHeightAt(static_cast<int>(std::floor(px)),
                                                          static_cast<int>(std::floor(pz)));
                    const float ceiling = std::max(8.0f, static_cast<float>(surfLocal - 3));
                    py = std::clamp(py, 8.0f, ceiling);
                }
            }
        }
    }

    // Ravines: long, wider cuts that can intersect tunnels and occasionally open
    // to surface.
    for (int rz = regionZ - 1; rz <= regionZ + 1; ++rz) {
        for (int rx = regionX - 1; rx <= regionX + 1; ++rx) {
            if (hash01(rx * 149, 4000, rz * 151) < 0.62f) {
                continue;
            }

            float px = static_cast<float>(rx * regionSizeBlocks) +
                       hash01(rx * 157, 4100, rz * 163) * static_cast<float>(regionSizeBlocks);
            float pz = static_cast<float>(rz * regionSizeBlocks) +
                       hash01(rx * 167, 4200, rz * 173) * static_cast<float>(regionSizeBlocks);
            const int surf =
                surfaceHeightAt(static_cast<int>(std::floor(px)), static_cast<int>(std::floor(pz)));
            float py = std::clamp(static_cast<float>(surf) -
                                      (8.0f + hash01(rx * 179, 4300, rz * 181) * 18.0f),
                                  14.0f, 72.0f);

            float yaw = hash01(rx * 191, 4400, rz * 193) * 6.2831853f;
            const int steps = 100 + static_cast<int>(hash01(rx * 197, 4500, rz * 199) * 80.0f);
            const float ravineHalfW = 2.6f + hash01(rx * 211, 4600, rz * 223) * 2.0f;
            const float ravineHalfH = 4.0f + hash01(rx * 227, 4700, rz * 229) * 2.5f;

            for (int s = 0; s < steps; ++s) {
                const float t = static_cast<float>(s) / static_cast<float>(steps);
                yaw += fbm3D((px + 1200.0f) * 0.01f, 0.0f, (pz - 900.0f) * 0.01f, 2, 2.0f, 0.55f) *
                       0.08f;
                const float dx = std::cos(yaw);
                const float dz = std::sin(yaw);
                px += dx * 1.35f;
                pz += dz * 1.35f;

                const int surfLocal = surfaceHeightAt(static_cast<int>(std::floor(px)),
                                                      static_cast<int>(std::floor(pz)));
                const float depthBias = 8.0f + std::sin(t * 6.2831853f) * 2.2f;
                py = std::clamp(static_cast<float>(surfLocal) - depthBias, 12.0f, 74.0f);

                const float rw =
                    std::max(2.2f, ravineHalfW + 0.7f * fbm3D(px * 0.02f, py * 0.02f, pz * 0.02f, 2,
                                                              2.0f, 0.55f));
                const float rh = std::max(
                    3.2f, ravineHalfH + 0.9f * fbm3D((px + 500.0f) * 0.02f, (py - 500.0f) * 0.02f,
                                                     (pz - 300.0f) * 0.02f, 2, 2.0f, 0.55f));

                const int minX = static_cast<int>(std::floor(px - rw));
                const int maxX = static_cast<int>(std::floor(px + rw));
                const int minY = static_cast<int>(std::floor(py - rh));
                const int maxY = static_cast<int>(std::floor(py + rh));
                const int minZ = static_cast<int>(std::floor(pz - rw));
                const int maxZ = static_cast<int>(std::floor(pz + rw));
                for (int x = minX; x <= maxX; ++x) {
                    for (int y = minY; y <= maxY; ++y) {
                        for (int z = minZ; z <= maxZ; ++z) {
                            const float ex = ((static_cast<float>(x) + 0.5f) - px) / rw;
                            const float ey = ((static_cast<float>(y) + 0.5f) - py) / rh;
                            const float ez = ((static_cast<float>(z) + 0.5f) - pz) / rw;
                            if (ex * ex + ey * ey + ez * ez > 1.0f) {
                                continue;
                            }
                            const int lx = x - chunkBaseX;
                            const int lz = z - chunkBaseZ;
                            if (!chunk.inBounds(lx, y, lz)) {
                                continue;
                            }
                            setAt(lx, y, lz, (y <= seaLevel) ? voxel::WATER : voxel::AIR);
                        }
                    }
                }
            }
        }
    }

    // Cliff-face cave mouths that feed into the connected tunnel network.
    for (int lx = 2; lx < voxel::Chunk::SX - 2; lx += 2) {
        for (int lz = 2; lz < voxel::Chunk::SZ - 2; lz += 2) {
            const int h = topHeights[lx][lz];
            if (h < seaLevel + 3 || biomes[lx][lz] == Biome::Swamp) {
                continue;
            }

            const int hpx = topHeights[lx + 1][lz];
            const int hnx = topHeights[lx - 1][lz];
            const int hpz = topHeights[lx][lz + 1];
            const int hnz = topHeights[lx][lz - 1];
            const int slope = std::max(
                {std::abs(h - hpx), std::abs(h - hnx), std::abs(h - hpz), std::abs(h - hnz)});
            if (slope < 4) {
                continue;
            }

            const int wx = chunkBaseX + lx;
            const int wz = chunkBaseZ + lz;
            const float chance = 0.02f + 0.012f * static_cast<float>(slope - 4);
            if (hash01(wx, 2900, wz) > chance) {
                continue;
            }

            int dirX = 0;
            int dirZ = 0;
            int bestDrop = -9999;
            const std::array<std::pair<int, int>, 4> dirs{{{1, 0}, {-1, 0}, {0, 1}, {0, -1}}};
            for (const auto &d : dirs) {
                const int nh = topHeights[lx + d.first][lz + d.second];
                const int drop = h - nh;
                if (drop > bestDrop) {
                    bestDrop = drop;
                    dirX = d.first;
                    dirZ = d.second;
                }
            }
            if (bestDrop <= 0) {
                continue;
            }

            float mx = static_cast<float>(wx) + 0.5f;
            float my = static_cast<float>(h - 2);
            float mz = static_cast<float>(wz) + 0.5f;
            const int length = 7 + static_cast<int>(hash01(wx, 3000, wz) * 8.0f);
            const float mouthRadius = 1.8f + hash01(wx, 3100, wz) * 0.7f;
            for (int s = 0; s < length; ++s) {
                carveSphereWorld(mx, my, mz, mouthRadius, false);
                mx += static_cast<float>(dirX) * 1.1f;
                mz += static_cast<float>(dirZ) * 1.1f;
                my -= 0.28f;
            }
        }
    }

    // Additional surface openings that taper down and hook into cave layers.
    for (int lx = 1; lx < voxel::Chunk::SX - 1; ++lx) {
        for (int lz = 1; lz < voxel::Chunk::SZ - 1; ++lz) {
            const int h = topHeights[lx][lz];
            if (h <= seaLevel + 3 || biomes[lx][lz] == Biome::Swamp) {
                continue;
            }
            const int hpx = topHeights[lx + 1][lz];
            const int hnx = topHeights[lx - 1][lz];
            const int hpz = topHeights[lx][lz + 1];
            const int hnz = topHeights[lx][lz - 1];
            const int slope = std::max(
                {std::abs(h - hpx), std::abs(h - hnx), std::abs(h - hpz), std::abs(h - hnz)});
            if (slope < 3) {
                continue;
            }
            const int wx = chunkBaseX + lx;
            const int wz = chunkBaseZ + lz;
            if (hash01(wx, 4800, wz) > 0.00045f) {
                continue;
            }

            const float radiusTop = 1.6f + hash01(wx, 4900, wz) * 1.0f;
            const int depth = 8 + static_cast<int>(hash01(wx, 5000, wz) * 10.0f);
            float oxAcc = 0.0f;
            float ozAcc = 0.0f;
            for (int d = 0; d < depth; ++d) {
                const float t = static_cast<float>(d) / static_cast<float>(depth);
                const float r = std::max(1.2f, radiusTop * (1.0f - 0.55f * t));
                oxAcc += (hash01(wx, 5100 + d, wz) - 0.5f) * 0.25f;
                ozAcc += (hash01(wx, 5200 + d, wz) - 0.5f) * 0.25f;
                oxAcc = std::clamp(oxAcc, -1.2f, 1.2f);
                ozAcc = std::clamp(ozAcc, -1.2f, 1.2f);
                carveSphereWorld(static_cast<float>(wx) + 0.5f + oxAcc, static_cast<float>(h - d),
                                 static_cast<float>(wz) + 0.5f + ozAcc, r, false);
            }
        }
    }

    // Shoreline stitch pass: fill small river/lake edge gaps up to sea level.
    for (int lx = 0; lx < voxel::Chunk::SX; ++lx) {
        for (int lz = 0; lz < voxel::Chunk::SZ; ++lz) {
            if (riverStrength[lx][lz] < 0.10f) {
                continue;
            }
            if (getAt(lx, seaLevel, lz) == voxel::WATER || getAt(lx, seaLevel, lz) == voxel::ICE) {
                continue;
            }
            bool touchesWater = false;
            const std::array<std::pair<int, int>, 4> dirs{{{1, 0}, {-1, 0}, {0, 1}, {0, -1}}};
            for (const auto &d : dirs) {
                const int nx = lx + d.first;
                const int nz = lz + d.second;
                if (nx < 0 || nz < 0 || nx >= voxel::Chunk::SX || nz >= voxel::Chunk::SZ) {
                    continue;
                }
                if (getAt(nx, seaLevel, nz) == voxel::WATER ||
                    getAt(nx, seaLevel, nz) == voxel::ICE) {
                    touchesWater = true;
                    break;
                }
            }
            if (!touchesWater) {
                continue;
            }

            for (int y = seaLevel; y >= 0; --y) {
                const voxel::BlockId id = getAt(lx, y, lz);
                if (id == voxel::AIR) {
                    const int wx = cc.x * voxel::Chunk::SX + lx;
                    const int wz = cc.z * voxel::Chunk::SZ + lz;
                    const Biome b = biomes[lx][lz];
                    if ((b == Biome::Snowy || b == Biome::Taiga) && y == seaLevel) {
                        setAt(lx, y, lz, voxel::ICE);
                    } else {
                        setAt(lx, y, lz, voxel::WATER);
                    }
                } else {
                    break;
                }
            }
        }
    }

    // Pass 3: biome-aware tree placement with anti-clump spacing.
    constexpr int treeCell = 7;
    for (int lx = 2; lx < voxel::Chunk::SX - 2; ++lx) {
        for (int lz = 2; lz < voxel::Chunk::SZ - 2; ++lz) {
            const int wx = cc.x * voxel::Chunk::SX + lx;
            const int wz = cc.z * voxel::Chunk::SZ + lz;
            const int y = topHeights[lx][lz] - 1;

            if (y <= seaLevel + 1 || y + 8 >= voxel::Chunk::SY) {
                continue;
            }
            const voxel::BlockId ground = getAt(lx, y, lz);
            const Biome biome = biomes[lx][lz];
            const bool validGround =
                (biome == Biome::Forest || biome == Biome::Plains || biome == Biome::Swamp)
                    ? (ground == voxel::GRASS)
                : (biome == Biome::Taiga || biome == Biome::Snowy)
                    ? (ground == voxel::SNOW_BLOCK || ground == voxel::GRASS)
                : (biome == Biome::Desert || biome == Biome::Badlands)
                    ? (ground == voxel::SAND || ground == voxel::SANDSTONE)
                : (biome == Biome::Mountains) ? (ground == voxel::STONE || ground == voxel::GRAVEL)
                                              : false;
            if (!validGround) {
                continue;
            }

            const int maxNeighbor = std::max({topHeights[lx - 1][lz], topHeights[lx + 1][lz],
                                              topHeights[lx][lz - 1], topHeights[lx][lz + 1]});
            const int minNeighbor = std::min({topHeights[lx - 1][lz], topHeights[lx + 1][lz],
                                              topHeights[lx][lz - 1], topHeights[lx][lz + 1]});
            if (maxNeighbor - minNeighbor > 2) {
                continue;
            }

            // Deterministic jittered tree grid: at most one tree anchor per cell.
            const int cellX = floorDiv(wx, treeCell);
            const int cellZ = floorDiv(wz, treeCell);
            const int cellBaseX = cellX * treeCell;
            const int cellBaseZ = cellZ * treeCell;
            const int inner = treeCell - 2;
            const int candidateX =
                cellBaseX + 1 +
                static_cast<int>(hash01(cellX, 7021, cellZ) * static_cast<float>(inner));
            const int candidateZ =
                cellBaseZ + 1 +
                static_cast<int>(hash01(cellX, 7022, cellZ) * static_cast<float>(inner));
            if (wx != candidateX || wz != candidateZ) {
                continue;
            }

            const BiomeParams bp = biomeParams(biome);
            float treeChance = bp.treeBase + std::max(0.0f, moistures[lx][lz]) * 0.02f;
            if (biome == Biome::Forest) {
                treeChance += 0.02f;
            } else if (biome == Biome::Taiga) {
                treeChance += 0.01f;
            } else if (biome == Biome::Desert || biome == Biome::Badlands) {
                treeChance *= 0.05f;
            } else if (biome == Biome::Mountains) {
                treeChance *= 0.4f;
            }

            const float r = hash01(wx, 12345, wz);
            if (r < treeChance) {
                int trunk = 4 + static_cast<int>(hash01(wx, 54321, wz) * 3.0f);
                voxel::BlockId trunkId = voxel::WOOD;
                voxel::BlockId leavesId = voxel::LEAVES;
                bool sparseCanopy = false;

                if (biome == Biome::Taiga) {
                    trunk += 1;
                    trunkId = voxel::SPRUCE_WOOD;
                    leavesId = voxel::SPRUCE_LEAVES;
                } else if (biome == Biome::Desert || biome == Biome::Badlands) {
                    // Desert vegetation is handled by cactus placement below.
                    continue;
                } else if (biome == Biome::Mountains) {
                    trunk = std::max(4, trunk);
                    trunkId = voxel::SPRUCE_WOOD;
                    leavesId = voxel::SPRUCE_LEAVES;
                    sparseCanopy = true;
                } else if (biome == Biome::Forest && hash01(wx, 65432, wz) > 0.55f) {
                    trunkId = voxel::BIRCH_WOOD;
                    leavesId = voxel::BIRCH_LEAVES;
                }

                placeTree(chunk, lx, y, lz, trunk, trunkId, leavesId, sparseCanopy);
            }
        }
    }

    // Desert/badlands cactus placement.
    for (int lx = 1; lx < voxel::Chunk::SX - 1; ++lx) {
        for (int lz = 1; lz < voxel::Chunk::SZ - 1; ++lz) {
            const Biome biome = biomes[lx][lz];
            if (biome != Biome::Desert && biome != Biome::Badlands) {
                continue;
            }
            const int wx = cc.x * voxel::Chunk::SX + lx;
            const int wz = cc.z * voxel::Chunk::SZ + lz;
            const int y = topHeights[lx][lz] - 1;
            if (y <= seaLevel + 1 || y + 5 >= voxel::Chunk::SY) {
                continue;
            }
            if (getAt(lx, y, lz) != voxel::SAND && getAt(lx, y, lz) != voxel::SANDSTONE) {
                continue;
            }
            if (hash01(wx, 9001, wz) > 0.015f) {
                continue;
            }
            if (getAt(lx, y + 1, lz) != voxel::AIR) {
                continue;
            }
            const int h = 2 + static_cast<int>(hash01(wx, 9002, wz) * 3.0f);
            for (int k = 1; k <= h; ++k) {
                if (getAt(lx, y + k, lz) != voxel::AIR) {
                    break;
                }
                setAt(lx, y + k, lz, voxel::CACTUS);
            }
        }
    }

    // Pass 4: small vegetation (tall grass + flowers) on grassy biomes.
    for (int lx = 1; lx < voxel::Chunk::SX - 1; ++lx) {
        for (int lz = 1; lz < voxel::Chunk::SZ - 1; ++lz) {
            const int wx = cc.x * voxel::Chunk::SX + lx;
            const int wz = cc.z * voxel::Chunk::SZ + lz;
            const int y = topHeights[lx][lz] - 1;
            if (y <= seaLevel + 1 || y + 1 >= voxel::Chunk::SY) {
                continue;
            }
            if (getAt(lx, y, lz) != voxel::GRASS) {
                continue;
            }
            if (getAt(lx, y + 1, lz) != voxel::AIR) {
                continue;
            }

            const Biome biome = biomes[lx][lz];
            if (biome != Biome::Plains && biome != Biome::Forest && biome != Biome::Swamp) {
                continue;
            }

            float grassChance = 0.12f;
            float flowerChance = 0.02f;
            if (biome == Biome::Plains) {
                grassChance = 0.22f;
                flowerChance = 0.05f;
            } else if (biome == Biome::Forest) {
                grassChance = 0.14f;
                flowerChance = 0.025f;
            } else if (biome == Biome::Swamp) {
                grassChance = 0.10f;
                flowerChance = 0.01f;
            }

            const float moist = std::clamp((moistures[lx][lz] + 1.0f) * 0.5f, 0.0f, 1.0f);
            grassChance += 0.10f * moist;
            flowerChance += 0.02f * moist;

            const float pick = hash01(wx, 9701, wz);
            if (pick < flowerChance) {
                setAt(lx, y + 1, lz, voxel::FLOWER);
            } else if (pick < flowerChance + grassChance) {
                setAt(lx, y + 1, lz, voxel::TALL_GRASS);
            }
        }
    }
    chunk.dirty = true;
}

} // namespace world
