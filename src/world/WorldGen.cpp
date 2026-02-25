#include "world/WorldGen.hpp"

#include "voxel/Block.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <string>
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
    Meadow = 8,
    Rainforest = 9,
    Savanna = 10,
    Volcanic = 11,
    Highlands = 12,
    Alpine = 13,
    JaggedPeaks = 14,
    DenseForest = 15,
    GiantGrove = 16,
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
        return {12.0f, 16.0f, voxel::GRASS, voxel::DIRT, 3, 6, 0.042f};
    case Biome::Desert:
        return {8.0f, 10.0f, voxel::SAND, voxel::SAND, 4, 7, 0.0f};
    case Biome::Mountains:
        return {8.0f, 36.0f, voxel::STONE, voxel::STONE, 2, 4, 0.002f};
    case Biome::Swamp:
        return {4.0f, 5.0f, voxel::GRASS, voxel::DIRT, 2, 4, 0.010f};
    case Biome::Taiga:
        return {10.0f, 16.0f, voxel::SNOW_BLOCK, voxel::DIRT, 3, 5, 0.016f};
    case Biome::Snowy:
        return {8.0f, 12.0f, voxel::SNOW_BLOCK, voxel::STONE, 2, 4, 0.001f};
    case Biome::Badlands:
        return {9.0f, 18.0f, voxel::SAND, voxel::SANDSTONE, 3, 6, 0.0f};
    case Biome::Meadow:
        return {6.0f, 8.0f, voxel::MOSS, voxel::DIRT, 3, 5, 0.010f};
    case Biome::Rainforest:
        return {9.0f, 14.0f, voxel::MOSS, voxel::MUD, 4, 7, 0.050f};
    case Biome::Savanna:
        return {7.0f, 10.0f, voxel::RED_SAND, voxel::RED_SAND, 3, 6, 0.006f};
    case Biome::Volcanic:
        return {8.0f, 24.0f, voxel::BASALT, voxel::BASALT, 3, 6, 0.0f};
    case Biome::Highlands:
        return {13.0f, 24.0f, voxel::GRASS, voxel::STONE, 3, 6, 0.006f};
    case Biome::Alpine:
        return {9.0f, 34.0f, voxel::SNOW_BLOCK, voxel::STONE, 2, 4, 0.003f};
    case Biome::JaggedPeaks:
        return {7.0f, 46.0f, voxel::STONE, voxel::STONE, 2, 4, 0.001f};
    case Biome::DenseForest:
        return {12.0f, 16.0f, voxel::MOSS, voxel::DIRT, 3, 6, 0.058f};
    case Biome::GiantGrove:
        return {11.0f, 18.0f, voxel::MOSS, voxel::DIRT, 4, 7, 0.074f};
    case Biome::Plains:
    default:
        return {9.0f, 12.0f, voxel::GRASS, voxel::DIRT, 3, 5, 0.012f};
    }
}

Biome pickBiome(float temp, float moisture, float rugged, float selector, float continental,
                float regionBias) {
    const float alpine = rugged + std::max(0.0f, continental) * 0.18f;
    const float extremePeak = alpine + selector * 0.12f;
    if (extremePeak > 0.95f) {
        return (temp < -0.08f) ? Biome::Alpine : Biome::JaggedPeaks;
    }
    if (alpine > 0.84f) {
        if (temp > 0.22f && moisture < -0.08f && selector > 0.15f && continental > 0.18f) {
            return Biome::Volcanic;
        }
        return (temp < -0.18f) ? Biome::Alpine : Biome::Mountains;
    }
    if (alpine > 0.72f && continental > 0.16f && selector > -0.20f) {
        return (temp < -0.12f) ? Biome::Alpine : Biome::Highlands;
    }

    // Region bias nudges nearby areas toward coherent climate families.
    const float aridBias = std::max(0.0f, regionBias);
    const float humidBias = std::max(0.0f, -regionBias);

    if (temp < -0.48f) {
        return (moisture > -0.08f + humidBias * 0.12f) ? Biome::Taiga : Biome::Snowy;
    }

    if (temp > 0.32f + humidBias * 0.08f && moisture < -0.10f + aridBias * 0.10f) {
        return (selector > 0.52f + aridBias * 0.10f) ? Biome::Badlands : Biome::Desert;
    }

    if (temp > 0.16f && temp < 0.46f && moisture > -0.04f && moisture < 0.22f && selector > 0.20f) {
        return Biome::Savanna;
    }

    if (temp > 0.20f && moisture > 0.46f && selector > -0.30f) {
        if (selector > 0.56f) {
            return Biome::GiantGrove;
        }
        if (selector > 0.22f) {
            return Biome::DenseForest;
        }
        return Biome::Rainforest;
    }

    if (moisture > 0.34f - humidBias * 0.05f && temp > -0.20f) {
        if (selector > 0.48f && moisture > 0.42f) {
            return Biome::DenseForest;
        }
        if (temp < 0.06f && selector < -0.05f) {
            return Biome::Swamp;
        }
        return Biome::Forest;
    }

    if (temp > -0.06f && temp < 0.24f && moisture > 0.16f && selector < 0.48f) {
        return Biome::Meadow;
    }

    if (temp < -0.12f) {
        return (moisture > -0.04f) ? Biome::Taiga : Biome::Snowy;
    }

    if (moisture < -0.18f && temp > 0.16f) {
        return (selector > 0.45f) ? Biome::Badlands : Biome::Desert;
    }

    if (moisture > 0.22f && temp < 0.12f && selector < -0.22f) {
        return Biome::Swamp;
    }

    if (selector > 0.55f && moisture > 0.12f) {
        return Biome::Forest;
    }
    return Biome::Plains;
}

const char *biomeName(Biome biome) {
    switch (biome) {
    case Biome::Plains:
        return "Plains";
    case Biome::Forest:
        return "Forest";
    case Biome::Desert:
        return "Desert";
    case Biome::Mountains:
        return "Mountains";
    case Biome::Swamp:
        return "Swamp";
    case Biome::Taiga:
        return "Taiga";
    case Biome::Snowy:
        return "Snowy";
    case Biome::Badlands:
        return "Badlands";
    case Biome::Meadow:
        return "Meadow";
    case Biome::Rainforest:
        return "Rainforest";
    case Biome::Savanna:
        return "Savanna";
    case Biome::Volcanic:
        return "Volcanic";
    case Biome::Highlands:
        return "Highlands";
    case Biome::Alpine:
        return "Alpine";
    case Biome::JaggedPeaks:
        return "Jagged Peaks";
    case Biome::DenseForest:
        return "Dense Forest";
    case Biome::GiantGrove:
        return "Giant Grove";
    default:
        return "Unknown";
    }
}

void placeTree(voxel::Chunk &chunk, int lx, int baseY, int lz, int height, voxel::BlockId trunkId,
               voxel::BlockId leavesId, bool sparseCanopy) {
    if (baseY + height + 3 >= voxel::Chunk::SY) {
        return;
    }

    for (int y = baseY + 1; y <= baseY + height; ++y) {
        if (!chunk.inBounds(lx, y, lz)) {
            continue;
        }
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

void placeFlatCanopyTree(voxel::Chunk &chunk, int lx, int baseY, int lz, int height,
                         voxel::BlockId trunkId, voxel::BlockId leavesId) {
    if (baseY + height + 4 >= voxel::Chunk::SY) {
        return;
    }

    for (int y = baseY + 1; y <= baseY + height; ++y) {
        if (!chunk.inBounds(lx, y, lz)) {
            continue;
        }
        chunk.setRaw(lx, y, lz, trunkId);
    }
    const int canopyY = baseY + height;
    for (int dy = 0; dy <= 1; ++dy) {
        const int y = canopyY + dy;
        const int radius = (dy == 0) ? 2 : 1;
        for (int dx = -radius; dx <= radius; ++dx) {
            for (int dz = -radius; dz <= radius; ++dz) {
                const int x = lx + dx;
                const int z = lz + dz;
                if (!chunk.inBounds(x, y, z)) {
                    continue;
                }
                if (dx == 0 && dz == 0 && dy == 0) {
                    continue;
                }
                if (std::abs(dx) + std::abs(dz) > radius + 1) {
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

void placeDeadTree(voxel::Chunk &chunk, int lx, int baseY, int lz, int height, voxel::BlockId trunkId,
                   std::uint32_t branchSeed) {
    if (baseY + height + 2 >= voxel::Chunk::SY) {
        return;
    }
    for (int y = baseY + 1; y <= baseY + height; ++y) {
        if (!chunk.inBounds(lx, y, lz)) {
            continue;
        }
        chunk.setRaw(lx, y, lz, trunkId);
    }

    const int branchY = baseY + height - 1;
    const int branches = 1 + static_cast<int>(branchSeed & 1u);
    for (int i = 0; i < branches; ++i) {
        const int dir = static_cast<int>((branchSeed >> (i * 2)) & 3u);
        const int dx = (dir == 0) ? 1 : (dir == 1) ? -1 : 0;
        const int dz = (dir == 2) ? 1 : (dir == 3) ? -1 : 0;
        const int len = 1 + static_cast<int>((branchSeed >> (6 + i)) & 1u);
        for (int s = 1; s <= len; ++s) {
            const int x = lx + dx * s;
            const int y = branchY + ((s == len) ? 1 : 0);
            const int z = lz + dz * s;
            if (!chunk.inBounds(x, y, z)) {
                continue;
            }
            const voxel::BlockId existing = chunk.getUnchecked(x, y, z);
            if (existing == voxel::AIR || existing == voxel::WATER) {
                chunk.setRaw(x, y, z, trunkId);
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

std::string WorldGen::biomeLabelAt(int wx, int wz) const {
    const float warpX =
        fbm2D((wx + 7100) * 0.00085f, (wz - 2900) * 0.00085f, 3, 2.0f, 0.5f) * 28.0f;
    const float warpZ =
        fbm2D((wx - 6400) * 0.00085f, (wz + 3900) * 0.00085f, 3, 2.0f, 0.5f) * 28.0f;
    const float sx = static_cast<float>(wx) + warpX;
    const float sz = static_cast<float>(wz) + warpZ;

    const float continentalRaw = fbm2D(sx * 0.0018f, sz * 0.0018f, 4, 2.0f, 0.5f);
    const float continental = std::clamp(continentalRaw * 0.84f + 0.16f, -1.0f, 1.0f);
    const float ridges = 1.0f - std::abs(fbm2D(sx * 0.0038f, sz * 0.0038f, 4, 2.0f, 0.55f));
    const float rugged = ridges * std::max(0.0f, continental + 0.2f);

    const float latitude = std::cos((sz - 8000.0f) * 0.00013f);
    const float tempMacro =
        fbm2D((sx + 18000.0f) * 0.00035f, (sz - 9000.0f) * 0.00035f, 4, 2.0f, 0.5f);
    const float tempLocal =
        fbm2D((sx + 18000.0f) * 0.0012f, (sz - 9000.0f) * 0.0012f, 3, 2.0f, 0.5f);
    const float temp =
        std::clamp(tempLocal * 0.52f + tempMacro * 0.26f + latitude * 0.36f, -1.0f, 1.0f);

    const float moistureMacro =
        fbm2D((sx - 25000.0f) * 0.00040f, (sz + 17000.0f) * 0.00040f, 4, 2.0f, 0.5f);
    const float moistureLocal =
        fbm2D((sx - 25000.0f) * 0.0012f, (sz + 17000.0f) * 0.0012f, 3, 2.0f, 0.5f);
    const float rainShadow = std::max(0.0f, rugged - 0.34f) * 0.44f;
    const float coastMoisture = std::max(0.0f, 0.26f - std::abs(continental)) * 0.30f;
    const float moisture =
        std::clamp(moistureLocal * 0.58f + moistureMacro * 0.34f + coastMoisture - rainShadow,
                   -1.0f, 1.0f);

    const float regionBias =
        fbm2D((sx + 5200.0f) * 0.00030f, (sz - 11400.0f) * 0.00030f, 3, 2.0f, 0.5f);
    const float selector = fbm2D((sx + 3200.0f) * 0.0022f, (sz - 4100.0f) * 0.0022f, 3, 2.0f, 0.5f);
    const Biome biome = pickBiome(temp, moisture, rugged, selector, continental, regionBias);
    return biomeName(biome);
}

void WorldGen::fillChunk(voxel::Chunk &chunk, ChunkCoord cc) const {
    constexpr int seaLevel = 62;
    auto getAt = [&](int x, int y, int z) -> voxel::BlockId { return chunk.getUnchecked(x, y, z); };
    auto setAt = [&](int x, int y, int z, voxel::BlockId id) { chunk.setRaw(x, y, z, id); };

    std::array<std::array<int, voxel::Chunk::SZ>, voxel::Chunk::SX> topHeights{};
    std::array<std::array<Biome, voxel::Chunk::SZ>, voxel::Chunk::SX> biomes{};
    std::array<std::array<float, voxel::Chunk::SZ>, voxel::Chunk::SX> moistures{};
    std::array<std::array<float, voxel::Chunk::SZ>, voxel::Chunk::SX> riverStrength{};

    struct TerrainSample {
        int height = seaLevel;
        float moisture = 0.0f;
        float river = 0.0f;
        Biome biome = Biome::Plains;
        BiomeParams biomeParams{};
    };

    auto sampleTerrainAt = [&](int wx, int wz) -> TerrainSample {
        const float warpX = fbm2D((wx + 7100) * 0.00085f, (wz - 2900) * 0.00085f, 3, 2.0f, 0.5f) *
                            28.0f;
        const float warpZ = fbm2D((wx - 6400) * 0.00085f, (wz + 3900) * 0.00085f, 3, 2.0f, 0.5f) *
                            28.0f;
        const float sx = static_cast<float>(wx) + warpX;
        const float sz = static_cast<float>(wz) + warpZ;

        const float continentalRaw = fbm2D(sx * 0.0018f, sz * 0.0018f, 4, 2.0f, 0.5f);
        // Lift continentalness so more terrain stays emergent and oceans are less dominant.
        const float continental = std::clamp(continentalRaw * 0.84f + 0.16f, -1.0f, 1.0f);
        const float ridges = 1.0f - std::abs(fbm2D(sx * 0.0038f, sz * 0.0038f, 4, 2.0f, 0.55f));
        const float rugged = ridges * std::max(0.0f, continental + 0.2f);
        const float mountains = rugged * rugged;

        const float latitude = std::cos((sz - 8000.0f) * 0.00013f);
        const float tempMacro =
            fbm2D((sx + 18000.0f) * 0.00035f, (sz - 9000.0f) * 0.00035f, 4, 2.0f, 0.5f);
        const float tempLocal =
            fbm2D((sx + 18000.0f) * 0.0012f, (sz - 9000.0f) * 0.0012f, 3, 2.0f, 0.5f);
        const float temp =
            std::clamp(tempLocal * 0.52f + tempMacro * 0.26f + latitude * 0.36f, -1.0f, 1.0f);

        const float moistureMacro =
            fbm2D((sx - 25000.0f) * 0.00040f, (sz + 17000.0f) * 0.00040f, 4, 2.0f, 0.5f);
        const float moistureLocal =
            fbm2D((sx - 25000.0f) * 0.0012f, (sz + 17000.0f) * 0.0012f, 3, 2.0f, 0.5f);
        const float rainShadow = std::max(0.0f, rugged - 0.34f) * 0.44f;
        const float coastMoisture = std::max(0.0f, 0.26f - std::abs(continental)) * 0.30f;
        const float moisture =
            std::clamp(moistureLocal * 0.58f + moistureMacro * 0.34f + coastMoisture - rainShadow,
                       -1.0f, 1.0f);

        const float regionBias =
            fbm2D((sx + 5200.0f) * 0.00030f, (sz - 11400.0f) * 0.00030f, 3, 2.0f, 0.5f);
        const float selector =
            fbm2D((sx + 3200.0f) * 0.0022f, (sz - 4100.0f) * 0.0022f, 3, 2.0f, 0.5f);
        const Biome biome = pickBiome(temp, moisture, rugged, selector, continental, regionBias);
        const BiomeParams bp = biomeParams(biome);
        const float blendWarp =
            fbm2D((sx - 5100.0f) * 0.0017f, (sz + 2400.0f) * 0.0017f, 2, 2.0f, 0.5f);
        const Biome biomeAlt =
            pickBiome(temp, moisture, rugged, selector + blendWarp * 0.26f, continental, regionBias);
        const BiomeParams bpAlt = biomeParams(biomeAlt);
        const float blendNoise =
            std::abs(fbm2D((sx + 9100.0f) * 0.0015f, (sz - 7600.0f) * 0.0015f, 2, 2.0f, 0.5f));
        const float biomeBlend = std::clamp(0.18f + (1.0f - blendNoise) * 0.18f, 0.0f, 0.38f);
        const float hillAmp = lerp(bp.hillAmp, bpAlt.hillAmp, biomeBlend);
        const float mountainAmp = lerp(bp.mountainAmp, bpAlt.mountainAmp, biomeBlend);

        const float localHills = fbm2D(sx * 0.0100f, sz * 0.0100f, 5, 2.0f, 0.5f);
        const float highlandDetail =
            fbm2D((sx - 4100.0f) * 0.017f, (sz + 8300.0f) * 0.017f, 3, 2.0f, 0.5f);
        const float highlandRidges =
            1.0f - std::abs(fbm2D((sx + 2700.0f) * 0.014f, (sz - 5100.0f) * 0.014f, 3, 2.0f, 0.55f));
        const float macroShape = fbm2D(sx * 0.00055f, sz * 0.00055f, 3, 2.0f, 0.5f);
        const float macroRelief = fbm2D((sx - 22000.0f) * 0.00028f, (sz + 11000.0f) * 0.00028f, 4,
                                        2.0f, 0.5f);
        const float upliftMask = fbm2D((sx + 6600.0f) * 0.0011f, (sz - 7200.0f) * 0.0011f, 3, 2.0f,
                                       0.5f);
        const float oceanSpanNoise =
            fbm2D((sx + 13400.0f) * 0.00020f, (sz - 21300.0f) * 0.00020f, 4, 2.0f, 0.5f);
        const float oceanDetailNoise =
            fbm2D((sx - 9600.0f) * 0.00085f, (sz + 4800.0f) * 0.00085f, 3, 2.0f, 0.5f);
        const float oceanShelfNoise =
            fbm2D((sx + 6200.0f) * 0.00115f, (sz - 9800.0f) * 0.00115f, 3, 2.0f, 0.5f);
        const float oceanTrenchNoise =
            fbm2D((sx - 15100.0f) * 0.00165f, (sz + 12900.0f) * 0.00165f, 4, 2.0f, 0.5f);
        const float seamountNoise =
            fbm2D((sx + 21800.0f) * 0.0019f, (sz - 7700.0f) * 0.0019f, 3, 2.0f, 0.5f);
        const float erosion = fbm2D((sx - 9400.0f) * 0.0046f, (sz + 4300.0f) * 0.0046f, 3, 2.0f, 0.5f);

        const float riverA = std::abs(fbm2D(sx * 0.0034f, sz * 0.0034f, 3, 2.0f, 0.55f));
        const float riverB = std::abs(fbm2D((sx + 13000.0f) * 0.0045f, (sz - 6000.0f) * 0.0045f, 2,
                                            2.0f, 0.55f));
        const float riverNoise = std::min(riverA, riverB * 1.08f);
        const float river = std::clamp((0.11f - riverNoise) / 0.11f, 0.0f, 1.0f);
        const float riverCarve = river * river * (biome == Biome::Mountains ? 4.8f : 8.6f);

        float h = static_cast<float>(seaLevel);
        h += continental * 34.0f;
        h += localHills * hillAmp;
        h += mountains * mountainAmp;
        h += macroShape * 16.0f;
        h += macroRelief * 22.0f;
        h += std::max(0.0f, upliftMask + 0.12f) * 10.0f;
        h -= std::max(0.0f, -macroRelief - 0.20f) * 8.0f;
        // Break up giant contiguous oceans into smaller seas/island chains.
        if (continental < -0.10f) {
            h += std::max(0.0f, oceanSpanNoise - 0.04f) * 24.0f;
            h += std::max(0.0f, oceanDetailNoise - 0.22f) * 7.0f;
        }
        const float oceanMask = std::clamp((-continental + 0.14f) / 0.78f, 0.0f, 1.0f);
        if (oceanMask > 0.0f) {
            // Continent shelves, trenches and seamount chains make oceans less flat.
            const float shelfNearCoast = std::pow(1.0f - oceanMask, 1.5f);
            h += shelfNearCoast * std::max(0.0f, oceanShelfNoise + 0.05f) * 12.0f;

            const float deepOcean = std::pow(oceanMask, 1.55f);
            h -= deepOcean * std::max(0.0f, oceanTrenchNoise - 0.18f) * 18.0f;

            const float seamount = std::max(0.0f, seamountNoise - 0.30f);
            h += deepOcean * std::pow(seamount, 2.0f) * 26.0f;
        }
        h += std::pow(std::max(0.0f, mountains), 1.5f) * 14.0f;
        h -= std::max(0.0f, erosion - 0.2f) * 8.0f;
        h -= riverCarve;
        if (h < static_cast<float>(seaLevel) - 10.0f) {
            h = lerp(h, static_cast<float>(seaLevel) - 6.0f, 0.40f);
        }
        auto applyBiomeHeightShape = [&](Biome b, float inHeight) {
            float out = inHeight;
            if (b == Biome::Swamp) {
                out = lerp(out, static_cast<float>(seaLevel) + 1.5f, 0.45f);
            }
            if (b == Biome::Rainforest) {
                out = lerp(out, static_cast<float>(seaLevel) + 6.0f, 0.16f);
            }
            if (b == Biome::Badlands) {
                const float aboveSea = out - static_cast<float>(seaLevel);
                out = static_cast<float>(seaLevel) + std::floor(aboveSea / 2.6f) * 2.6f;
            }
            if (b == Biome::Volcanic) {
                const float jagged =
                    std::abs(fbm2D((sx + 7800.0f) * 0.011f, (sz - 5200.0f) * 0.011f, 3, 2.0f, 0.52f));
                out += 6.0f + std::pow(jagged, 1.8f) * 9.0f;
            }
            if (b == Biome::Highlands) {
                out += 6.0f + std::max(0.0f, mountains - 0.22f) * 14.0f;
                out += highlandDetail * 3.8f;
                out += std::pow(std::max(0.0f, highlandRidges - 0.35f), 1.7f) * 5.4f;
            }
            if (b == Biome::Mountains) {
                out += 10.0f + std::max(0.0f, mountains - 0.16f) * 18.0f;
            }
            if (b == Biome::Alpine) {
                out += 13.0f + mountains * 20.0f;
            }
            if (b == Biome::JaggedPeaks) {
                out += 20.0f + std::pow(std::max(0.0f, ridges), 2.4f) * 30.0f;
            }
            return out;
        };
        h = lerp(applyBiomeHeightShape(biome, h), applyBiomeHeightShape(biomeAlt, h), biomeBlend);

        TerrainSample sample{};
        sample.height = std::clamp(static_cast<int>(std::floor(h)), 6, voxel::Chunk::SY - 2);
        sample.moisture = moisture;
        sample.river = river;
        sample.biome = biome;
        sample.biomeParams = bp;
        return sample;
    };

    // Pass 1: biome field + terrain + water.
    for (int lx = 0; lx < voxel::Chunk::SX; ++lx) {
        for (int lz = 0; lz < voxel::Chunk::SZ; ++lz) {
            const int wx = cc.x * voxel::Chunk::SX + lx;
            const int wz = cc.z * voxel::Chunk::SZ + lz;
            const TerrainSample terrain = sampleTerrainAt(wx, wz);
            const Biome biome = terrain.biome;
            const BiomeParams bp = terrain.biomeParams;
            const int height = terrain.height;
            const float river = terrain.river;

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
            const bool beachBand = height >= seaLevel - 3 && height <= seaLevel + 2;
            if (beachBand && biome != Biome::Snowy && biome != Biome::Taiga &&
                biome != Biome::Savanna && biome != Biome::Volcanic && biome != Biome::Alpine &&
                biome != Biome::JaggedPeaks) {
                top = voxel::SAND;
            }
            if ((biome == Biome::Mountains || biome == Biome::Snowy || biome == Biome::Alpine ||
                 biome == Biome::JaggedPeaks) &&
                height > seaLevel + 20) {
                top = voxel::STONE;
            }
            if (biome == Biome::Volcanic && height > seaLevel + 8) {
                top = voxel::BASALT;
            }
            if (biome == Biome::JaggedPeaks && height > seaLevel + 14) {
                top = voxel::BASALT;
            }
            // Add snowcaps only on highlands/mountain-family biomes at very high elevation.
            if (height > seaLevel + 44 &&
                (biome == Biome::Highlands || biome == Biome::Mountains ||
                 biome == Biome::Alpine || biome == Biome::JaggedPeaks)) {
                top = voxel::SNOW_BLOCK;
            }
            setAt(lx, height - 1, lz, top);

            // Bedrock floor with jagged upper edge near world bottom.
            const int bedrockTop = 1 + static_cast<int>(hash01(wx, 5481, wz) * 3.0f);
            for (int y = 0; y <= bedrockTop && y < voxel::Chunk::SY; ++y) {
                setAt(lx, y, lz, voxel::BEDROCK);
            }

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
                if ((biome == Biome::Mountains || biome == Biome::Highlands ||
                     biome == Biome::Alpine || biome == Biome::JaggedPeaks) &&
                    y >= 40 && y <= 116 && emeraldN > 0.90f) {
                    setAt(lx, y, lz, voxel::EMERALD_ORE);
                    continue;
                }
            }

            const bool riverbed = river > 0.12f;
            const int waterFillThreshold = riverbed ? (seaLevel + 1) : seaLevel;
            if (height <= waterFillThreshold) {
                const int depthBelowSea = std::max(0, seaLevel - height);
                const float seabedNoise =
                    fbm2D((wx + 4100.0f) * 0.045f, (wz - 2100.0f) * 0.045f, 2, 2.0f, 0.55f);
                voxel::BlockId seabedTop = voxel::SAND;
                voxel::BlockId seabedSub = voxel::SAND;
                if (depthBelowSea >= 18 && seabedNoise < -0.30f) {
                    seabedTop = voxel::CLAY;
                    seabedSub = voxel::CLAY;
                } else if (depthBelowSea >= 9 && seabedNoise > 0.34f) {
                    seabedTop = voxel::GRAVEL;
                    seabedSub = voxel::GRAVEL;
                } else if (biome == Biome::Volcanic && depthBelowSea >= 6) {
                    seabedTop = voxel::BASALT;
                    seabedSub = voxel::BASALT;
                } else if (riverbed && depthBelowSea <= 4) {
                    seabedTop = voxel::GRAVEL;
                    seabedSub = voxel::SAND;
                }
                if (height >= 2) {
                    setAt(lx, height - 1, lz, seabedTop);
                    if (height >= 3) {
                        setAt(lx, height - 2, lz, seabedSub);
                    }
                }
                for (int y = height; y <= seaLevel && y < voxel::Chunk::SY; ++y) {
                    if (getAt(lx, y, lz) == voxel::AIR) {
                        if ((biome == Biome::Snowy || biome == Biome::Taiga ||
                             biome == Biome::Alpine) &&
                            y == seaLevel) {
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
            moistures[lx][lz] = terrain.moisture;
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
        return sampleTerrainAt(wx, wz).height;
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

    // Deep caverns close to bedrock for late-game exploration.
    for (int rz = regionZ - 1; rz <= regionZ + 1; ++rz) {
        for (int rx = regionX - 1; rx <= regionX + 1; ++rx) {
            if (hash01(rx * 233, 5300, rz * 239) < 0.48f) {
                continue;
            }
            float px = static_cast<float>(rx * regionSizeBlocks) +
                       hash01(rx * 241, 5311, rz * 251) * static_cast<float>(regionSizeBlocks);
            float pz = static_cast<float>(rz * regionSizeBlocks) +
                       hash01(rx * 257, 5323, rz * 263) * static_cast<float>(regionSizeBlocks);
            float py = 6.0f + hash01(rx * 269, 5333, rz * 271) * 10.0f;
            float yaw = hash01(rx * 277, 5347, rz * 281) * 6.2831853f;
            const int steps = 70 + static_cast<int>(hash01(rx * 283, 5351, rz * 293) * 80.0f);
            const float baseR = 1.9f + hash01(rx * 307, 5369, rz * 311) * 1.2f;

            for (int s = 0; s < steps; ++s) {
                const float n = fbm3D((px + 800.0f) * 0.030f, (py - 1600.0f) * 0.030f,
                                      (pz - 900.0f) * 0.030f, 2, 2.0f, 0.55f);
                const float r = std::max(1.5f, baseR + n * 0.45f);
                carveSphereWorld(px, py, pz, r, false);

                yaw += fbm3D((px - 500.0f) * 0.016f, 0.0f, (pz + 400.0f) * 0.016f, 2, 2.0f, 0.55f) *
                       0.10f;
                px += std::cos(yaw) * 1.15f;
                pz += std::sin(yaw) * 1.15f;
                py += fbm3D((px + 1700.0f) * 0.020f, (py - 600.0f) * 0.020f,
                            (pz - 1200.0f) * 0.020f, 2, 2.0f, 0.55f) *
                      0.35f;
                py = std::clamp(py, 4.0f, 18.0f);
            }
        }
    }

    // Additional dedicated lava chambers.
    for (int rz = regionZ - 1; rz <= regionZ + 1; ++rz) {
        for (int rx = regionX - 1; rx <= regionX + 1; ++rx) {
            if (hash01(rx * 313, 5421, rz * 317) < 0.56f) {
                continue;
            }
            const int pockets = 1 + static_cast<int>(hash01(rx * 331, 5431, rz * 337) * 2.0f);
            for (int p = 0; p < pockets; ++p) {
                const float cx = static_cast<float>(rx * regionSizeBlocks) +
                                 6.0f +
                                 hash01(rx * 347 + p * 3, 5441, rz * 349 + p * 5) *
                                     static_cast<float>(regionSizeBlocks - 12);
                const float cz = static_cast<float>(rz * regionSizeBlocks) +
                                 6.0f +
                                 hash01(rx * 353 + p * 7, 5453, rz * 359 + p * 11) *
                                     static_cast<float>(regionSizeBlocks - 12);
                const float cy = 5.0f + hash01(rx * 367 + p, 5461, rz * 373 + p) * 7.0f;
                const float radius = 2.6f + hash01(rx * 379 + p, 5471, rz * 383 + p) * 1.8f;
                carveSphereWorld(cx, cy, cz, radius, false);

                const int minX = static_cast<int>(std::floor(cx - radius));
                const int maxX = static_cast<int>(std::floor(cx + radius));
                const int minY = static_cast<int>(std::floor(cy - radius));
                const int maxY = static_cast<int>(std::floor(cy + radius));
                const int minZ = static_cast<int>(std::floor(cz - radius));
                const int maxZ = static_cast<int>(std::floor(cz + radius));
                for (int wxi = minX; wxi <= maxX; ++wxi) {
                    for (int wyi = minY; wyi <= maxY; ++wyi) {
                        for (int wzi = minZ; wzi <= maxZ; ++wzi) {
                            const float dx = (static_cast<float>(wxi) + 0.5f) - cx;
                            const float dy = (static_cast<float>(wyi) + 0.5f) - cy;
                            const float dz = (static_cast<float>(wzi) + 0.5f) - cz;
                            const float rr = radius + 0.2f;
                            if (dx * dx + dy * dy + dz * dz > rr * rr) {
                                continue;
                            }
                            if (wyi > static_cast<int>(std::floor(cy)) + 1) {
                                continue;
                            }
                            const int lx = wxi - chunkBaseX;
                            const int lz = wzi - chunkBaseZ;
                            if (!chunk.inBounds(lx, wyi, lz)) {
                                continue;
                            }
                            if (wyi <= 1) {
                                continue;
                            }
                            if (getAt(lx, wyi, lz) == voxel::AIR || getAt(lx, wyi, lz) == voxel::WATER) {
                                setAt(lx, wyi, lz, voxel::LAVA);
                            }
                        }
                    }
                }
            }
        }
    }

    // Lava pools in very deep cave pockets.
    auto isLavaFloor = [&](voxel::BlockId id) {
        return id == voxel::STONE || id == voxel::BASALT || id == voxel::GRAVEL ||
               id == voxel::BEDROCK ||
               id == voxel::SANDSTONE;
    };
    for (int lx = 1; lx < voxel::Chunk::SX - 1; ++lx) {
        for (int lz = 1; lz < voxel::Chunk::SZ - 1; ++lz) {
            const int wx = chunkBaseX + lx;
            const int wz = chunkBaseZ + lz;
            for (int y = 3; y <= 13; ++y) {
                if (getAt(lx, y, lz) != voxel::AIR) {
                    continue;
                }
                if (!isLavaFloor(getAt(lx, y - 1, lz))) {
                    continue;
                }
                if (getAt(lx, y + 1, lz) != voxel::AIR && getAt(lx, y + 1, lz) != voxel::WATER) {
                    continue;
                }
                int airNeighbors = 0;
                airNeighbors += (getAt(lx + 1, y, lz) == voxel::AIR) ? 1 : 0;
                airNeighbors += (getAt(lx - 1, y, lz) == voxel::AIR) ? 1 : 0;
                airNeighbors += (getAt(lx, y, lz + 1) == voxel::AIR) ? 1 : 0;
                airNeighbors += (getAt(lx, y, lz - 1) == voxel::AIR) ? 1 : 0;
                if (airNeighbors < 2) {
                    continue;
                }
                const float heat =
                    fbm3D((wx + 9100.0f) * 0.075f, y * 0.14f, (wz - 6700.0f) * 0.075f, 2, 2.0f, 0.5f);
                const float noise =
                    fbm3D((wx - 5200.0f) * 0.085f, y * 0.18f, (wz + 7800.0f) * 0.085f, 2, 2.0f, 0.5f);
                const float chance = (y <= 6) ? 0.45f : (y <= 9) ? 0.30f : 0.18f;
                if (heat > -0.08f && noise > 0.40f && hash01(wx, 5401 + y, wz) < chance) {
                    setAt(lx, y, lz, voxel::LAVA);
                }
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
                    if ((b == Biome::Snowy || b == Biome::Taiga || b == Biome::Alpine) &&
                        y == seaLevel) {
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
    // Evaluate anchors in a padded world area so trees crossing chunk seams are complete.
    constexpr int treeCell = 6;
    constexpr int treePad = 4;
    for (int wz = chunkBaseZ - treePad; wz < chunkBaseZ + voxel::Chunk::SZ + treePad; ++wz) {
        for (int wx = chunkBaseX - treePad; wx < chunkBaseX + voxel::Chunk::SX + treePad; ++wx) {
            const TerrainSample here = sampleTerrainAt(wx, wz);
            const Biome biome = here.biome;
            const int y = here.height - 1;
            if (y <= seaLevel + 1 || y + 8 >= voxel::Chunk::SY) {
                continue;
            }

            const bool biomeCanHaveTrees =
                (biome == Biome::Forest || biome == Biome::Plains || biome == Biome::Swamp ||
                 biome == Biome::Meadow || biome == Biome::DenseForest ||
                 biome == Biome::GiantGrove || biome == Biome::Taiga || biome == Biome::Snowy ||
                 biome == Biome::Rainforest || biome == Biome::Savanna ||
                 biome == Biome::Mountains || biome == Biome::Volcanic ||
                 biome == Biome::Highlands || biome == Biome::Alpine ||
                 biome == Biome::JaggedPeaks);
            if (!biomeCanHaveTrees) {
                continue;
            }

            const int maxNeighbor =
                std::max({sampleTerrainAt(wx - 1, wz).height, sampleTerrainAt(wx + 1, wz).height,
                          sampleTerrainAt(wx, wz - 1).height, sampleTerrainAt(wx, wz + 1).height});
            const int minNeighbor =
                std::min({sampleTerrainAt(wx - 1, wz).height, sampleTerrainAt(wx + 1, wz).height,
                          sampleTerrainAt(wx, wz - 1).height, sampleTerrainAt(wx, wz + 1).height});
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
            float treeChance = bp.treeBase + std::max(0.0f, here.moisture) * 0.02f;
            if (biome == Biome::Forest) {
                treeChance += 0.11f;
            } else if (biome == Biome::DenseForest) {
                treeChance += 0.12f;
            } else if (biome == Biome::GiantGrove) {
                treeChance += 0.14f;
            } else if (biome == Biome::Rainforest) {
                treeChance += 0.08f;
            } else if (biome == Biome::Meadow) {
                treeChance += 0.004f;
            } else if (biome == Biome::Savanna) {
                treeChance *= 0.55f;
            } else if (biome == Biome::Taiga) {
                treeChance += 0.02f;
            } else if (biome == Biome::Desert || biome == Biome::Badlands) {
                treeChance *= 0.05f;
            } else if (biome == Biome::Mountains) {
                treeChance *= 0.4f;
            } else if (biome == Biome::Highlands) {
                treeChance *= 0.26f;
            } else if (biome == Biome::Alpine) {
                treeChance *= 0.16f;
            } else if (biome == Biome::JaggedPeaks) {
                treeChance *= 0.08f;
            } else if (biome == Biome::Volcanic) {
                treeChance *= 0.12f;
            }

            if (hash01(wx, 12345, wz) >= treeChance) {
                continue;
            }

            int trunk = 4 + static_cast<int>(hash01(wx, 54321, wz) * 3.0f);
            voxel::BlockId trunkId = voxel::WOOD;
            voxel::BlockId leavesId = voxel::LEAVES;
            bool sparseCanopy = false;

            if (biome == Biome::Taiga) {
                trunk += 1;
                trunkId = voxel::SPRUCE_WOOD;
                leavesId = voxel::SPRUCE_LEAVES;
            } else if (biome == Biome::Rainforest) {
                trunk += 2 + static_cast<int>(hash01(wx, 65434, wz) * 2.0f);
                if (hash01(wx, 65433, wz) > 0.50f) {
                    trunkId = voxel::BIRCH_WOOD;
                    leavesId = voxel::BIRCH_LEAVES;
                }
            } else if (biome == Biome::DenseForest) {
                trunk += 2 + static_cast<int>(hash01(wx, 65436, wz) * 2.0f);
                if (hash01(wx, 65437, wz) > 0.65f) {
                    trunkId = voxel::BIRCH_WOOD;
                    leavesId = voxel::BIRCH_LEAVES;
                }
            } else if (biome == Biome::GiantGrove) {
                trunk += 5 + static_cast<int>(hash01(wx, 65438, wz) * 4.0f);
                trunkId = (hash01(wx, 65439, wz) > 0.50f) ? voxel::SPRUCE_WOOD : voxel::WOOD;
                leavesId = (trunkId == voxel::SPRUCE_WOOD) ? voxel::SPRUCE_LEAVES : voxel::LEAVES;
            } else if (biome == Biome::Savanna) {
                trunk = 4 + static_cast<int>(hash01(wx, 65435, wz) * 2.0f);
                trunkId = voxel::BIRCH_WOOD;
                leavesId = voxel::BIRCH_LEAVES;
                placeFlatCanopyTree(chunk, wx - chunkBaseX, y, wz - chunkBaseZ, trunk, trunkId,
                                    leavesId);
                continue;
            } else if (biome == Biome::Desert || biome == Biome::Badlands) {
                // Desert vegetation is handled by cactus placement below.
                continue;
            } else if (biome == Biome::Mountains || biome == Biome::Volcanic ||
                       biome == Biome::Highlands || biome == Biome::Alpine ||
                       biome == Biome::JaggedPeaks) {
                trunk = std::max(4, trunk);
                if (biome == Biome::Volcanic) {
                    placeDeadTree(chunk, wx - chunkBaseX, y, wz - chunkBaseZ, trunk,
                                  voxel::SPRUCE_WOOD,
                                  static_cast<std::uint32_t>(wx * 7349 + wz * 9151));
                    continue;
                }
                trunkId = voxel::SPRUCE_WOOD;
                leavesId = voxel::SPRUCE_LEAVES;
                sparseCanopy = true;
            } else if (biome == Biome::Forest && hash01(wx, 65432, wz) > 0.55f) {
                trunkId = voxel::BIRCH_WOOD;
                leavesId = voxel::BIRCH_LEAVES;
            }

            placeTree(chunk, wx - chunkBaseX, y, wz - chunkBaseZ, trunk, trunkId, leavesId,
                      sparseCanopy);
        }
    }

    // Desert/badlands cactus placement.
    for (int lx = 0; lx < voxel::Chunk::SX; ++lx) {
        for (int lz = 0; lz < voxel::Chunk::SZ; ++lz) {
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

    // Biome-specific flora accents.
    for (int lx = 0; lx < voxel::Chunk::SX; ++lx) {
        for (int lz = 0; lz < voxel::Chunk::SZ; ++lz) {
            const int wx = cc.x * voxel::Chunk::SX + lx;
            const int wz = cc.z * voxel::Chunk::SZ + lz;
            const int y = topHeights[lx][lz] - 1;
            if (y <= seaLevel + 1 || y + 2 >= voxel::Chunk::SY) {
                continue;
            }
            if (getAt(lx, y + 1, lz) != voxel::AIR) {
                continue;
            }
            const Biome biome = biomes[lx][lz];
            const voxel::BlockId ground = getAt(lx, y, lz);

            if (biome == Biome::Meadow && (ground == voxel::GRASS || ground == voxel::MOSS)) {
                const float patch =
                    fbm2D((wx + 4200.0f) * 0.036f, (wz - 1700.0f) * 0.036f, 2, 2.0f, 0.55f);
                if (patch > 0.26f && hash01(wx, 8101, wz) < 0.42f) {
                    setAt(lx, y + 1, lz,
                          (hash01(wx, 8102, wz) < 0.68f) ? voxel::WILDFLOWER : voxel::TALL_GRASS);
                }
            } else if (biome == Biome::Rainforest &&
                       (ground == voxel::MOSS || ground == voxel::MUD || ground == voxel::GRASS)) {
                if (hash01(wx, 8201, wz) < 0.24f) {
                    setAt(lx, y + 1, lz,
                          (hash01(wx, 8202, wz) < 0.70f) ? voxel::FERN : voxel::WILDFLOWER);
                }
            } else if ((biome == Biome::DenseForest || biome == Biome::GiantGrove) &&
                       (ground == voxel::MOSS || ground == voxel::GRASS || ground == voxel::DIRT)) {
                if (hash01(wx, 8251, wz) < (biome == Biome::GiantGrove ? 0.26f : 0.20f)) {
                    setAt(lx, y + 1, lz, hash01(wx, 8252, wz) < 0.75f ? voxel::FERN
                                                                       : voxel::WILDFLOWER);
                }
            } else if (biome == Biome::Savanna &&
                       (ground == voxel::RED_SAND || ground == voxel::SAND || ground == voxel::GRASS)) {
                if (hash01(wx, 8303, wz) < 0.12f && getAt(lx, y + 1, lz) == voxel::AIR) {
                    setAt(lx, y + 1, lz, voxel::DRY_GRASS);
                }
                if (hash01(wx, 8304, wz) < 0.035f && getAt(lx, y + 1, lz) == voxel::AIR) {
                    setAt(lx, y + 1, lz, voxel::DEAD_BUSH);
                }
            } else if (biome == Biome::Volcanic && ground == voxel::BASALT) {
                if (hash01(wx, 8402, wz) < 0.030f && getAt(lx, y + 1, lz) == voxel::AIR) {
                    setAt(lx, y + 1, lz, voxel::DEAD_BUSH);
                }
            }
        }
    }

    // Pass 4: small vegetation (tall grass + flowers) on grassy biomes.
    auto vegetationOdds = [&](Biome biome, float moisture) {
        float grassChance = 0.12f;
        float flowerChance = 0.02f;
        if (biome == Biome::Plains) {
            grassChance = 0.22f;
            flowerChance = 0.05f;
        } else if (biome == Biome::DenseForest) {
            grassChance = 0.20f;
            flowerChance = 0.035f;
        } else if (biome == Biome::GiantGrove) {
            grassChance = 0.18f;
            flowerChance = 0.025f;
        } else if (biome == Biome::Meadow) {
            grassChance = 0.26f;
            flowerChance = 0.08f;
        } else if (biome == Biome::Rainforest) {
            grassChance = 0.24f;
            flowerChance = 0.03f;
        } else if (biome == Biome::Savanna) {
            grassChance = 0.08f;
            flowerChance = 0.01f;
        } else if (biome == Biome::Forest) {
            grassChance = 0.14f;
            flowerChance = 0.025f;
        } else if (biome == Biome::Swamp) {
            grassChance = 0.10f;
            flowerChance = 0.01f;
        }
        const float moist = std::clamp((moisture + 1.0f) * 0.5f, 0.0f, 1.0f);
        grassChance += 0.10f * moist;
        flowerChance += 0.02f * moist;
        return std::pair<float, float>{grassChance, flowerChance};
    };

    auto vegetationScore = [&](int wx, int wz) {
        // Blended hash keeps placement random without obvious grid patterns.
        return hash01(wx, 9702, wz) * 0.67f + hash01(wx + 37, 9703, wz - 19) * 0.33f;
    };

    for (int lx = 0; lx < voxel::Chunk::SX; ++lx) {
        for (int lz = 0; lz < voxel::Chunk::SZ; ++lz) {
            const int wx = cc.x * voxel::Chunk::SX + lx;
            const int wz = cc.z * voxel::Chunk::SZ + lz;
            const int y = topHeights[lx][lz] - 1;
            if (y <= seaLevel + 1 || y + 1 >= voxel::Chunk::SY) {
                continue;
            }
            const voxel::BlockId ground = getAt(lx, y, lz);
            if (ground != voxel::GRASS && ground != voxel::MOSS) {
                continue;
            }
            if (getAt(lx, y + 1, lz) != voxel::AIR) {
                continue;
            }

            const Biome biome = biomes[lx][lz];
            if (biome != Biome::Plains && biome != Biome::Forest && biome != Biome::Swamp &&
                biome != Biome::Meadow && biome != Biome::Rainforest && biome != Biome::Savanna &&
                biome != Biome::DenseForest && biome != Biome::GiantGrove) {
                continue;
            }

            const auto [grassChance, flowerChance] = vegetationOdds(biome, moistures[lx][lz]);
            const float totalChance = std::min(0.85f, grassChance + flowerChance);

            const float pick = hash01(wx, 9701, wz);
            if (pick >= totalChance) {
                continue;
            }

            const float myScore = vegetationScore(wx, wz);
            bool dominated = false;
            // World-space neighborhood check avoids chunk-edge striping and spaces plants out.
            for (int dz = -2; dz <= 2 && !dominated; ++dz) {
                for (int dx = -2; dx <= 2; ++dx) {
                    if (dx == 0 && dz == 0) {
                        continue;
                    }
                    if (dx * dx + dz * dz > 4) {
                        continue;
                    }
                    const int nwx = wx + dx;
                    const int nwz = wz + dz;
                    const TerrainSample ns = sampleTerrainAt(nwx, nwz);
                    if (ns.biome != Biome::Plains && ns.biome != Biome::Forest &&
                        ns.biome != Biome::Swamp && ns.biome != Biome::Meadow &&
                        ns.biome != Biome::Rainforest && ns.biome != Biome::Savanna &&
                        ns.biome != Biome::DenseForest && ns.biome != Biome::GiantGrove) {
                        continue;
                    }
                    const auto [ngrassChance, nflowerChance] = vegetationOdds(ns.biome, ns.moisture);
                    const float ntotalChance = std::min(0.85f, ngrassChance + nflowerChance);
                    if (hash01(nwx, 9701, nwz) >= ntotalChance) {
                        continue;
                    }
                    if (vegetationScore(nwx, nwz) > myScore) {
                        dominated = true;
                        break;
                    }
                }
            }
            if (dominated) {
                continue;
            }

            const float typePick = hash01(wx, 9704, wz);
            const float flowerShare = (totalChance > 0.0f) ? (flowerChance / totalChance) : 0.0f;
            if (typePick < flowerShare) {
                setAt(lx, y + 1, lz, biome == Biome::Meadow ? voxel::WILDFLOWER : voxel::FLOWER);
            } else {
                if (biome == Biome::Savanna) {
                    setAt(lx, y + 1, lz, (typePick > 0.84f) ? voxel::DEAD_BUSH : voxel::DRY_GRASS);
                } else if (biome == Biome::Rainforest) {
                    setAt(lx, y + 1, lz, voxel::FERN);
                } else {
                    setAt(lx, y + 1, lz, voxel::TALL_GRASS);
                }
            }
        }
    }

    // Pass 5: underwater flora (seagrass, kelp, coral).
    for (int lx = 0; lx < voxel::Chunk::SX; ++lx) {
        for (int lz = 0; lz < voxel::Chunk::SZ; ++lz) {
            const int wx = cc.x * voxel::Chunk::SX + lx;
            const int wz = cc.z * voxel::Chunk::SZ + lz;
            const int floorY = topHeights[lx][lz] - 1;
            if (floorY < 2 || floorY >= seaLevel - 1) {
                continue;
            }
            if (getAt(lx, floorY + 1, lz) != voxel::WATER) {
                continue;
            }

            const voxel::BlockId floorId = getAt(lx, floorY, lz);
            const bool floraGround = (floorId == voxel::SAND || floorId == voxel::GRAVEL ||
                                      floorId == voxel::CLAY || floorId == voxel::MUD ||
                                      floorId == voxel::MOSS || floorId == voxel::BASALT);
            if (!floraGround) {
                continue;
            }

            const int depth = seaLevel - floorY;
            const Biome biome = biomes[lx][lz];
            const float floraNoise =
                fbm2D((wx + 2900.0f) * 0.038f, (wz - 6100.0f) * 0.038f, 2, 2.0f, 0.55f);

            // Coral in warm, shallow-to-mid waters.
            const bool warmCoast = (biome == Biome::Rainforest || biome == Biome::Savanna ||
                                    biome == Biome::Desert || biome == Biome::Badlands);
            if (warmCoast && depth >= 3 && depth <= 11 && floraNoise > 0.12f &&
                hash01(wx, 9101, wz) < 0.18f && getAt(lx, floorY + 1, lz) == voxel::WATER) {
                setAt(lx, floorY + 1, lz, voxel::CORAL);
                continue;
            }

            // Kelp forests in deeper coasts/ocean shelves.
            if (depth >= 6 && depth <= 24 && floraNoise > -0.06f && hash01(wx, 9102, wz) < 0.22f) {
                const int kelpHeight = 2 + static_cast<int>(hash01(wx, 9103, wz) * 5.0f);
                int placed = 0;
                for (int k = 1; k <= kelpHeight && floorY + k <= seaLevel; ++k) {
                    if (getAt(lx, floorY + k, lz) != voxel::WATER) {
                        break;
                    }
                    setAt(lx, floorY + k, lz, voxel::KELP);
                    ++placed;
                }
                if (placed > 0) {
                    continue;
                }
            }

            // Seagrass carpets in shallower waters.
            if (depth >= 2 && depth <= 9 && floraNoise > -0.22f && hash01(wx, 9104, wz) < 0.34f &&
                getAt(lx, floorY + 1, lz) == voxel::WATER) {
                setAt(lx, floorY + 1, lz, voxel::SEAGRASS);
            }
        }
    }
    chunk.dirty = true;
}

} // namespace world
