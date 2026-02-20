#pragma once

#include "voxel/Chunk.hpp"
#include "world/ChunkCoord.hpp"

#include <cstdint>

namespace world {

class WorldGen {
  public:
    static constexpr std::uint32_t kGeneratorVersion = 17;

    explicit WorldGen(std::uint32_t seed = 1337u) : seed_(seed) {}

    void fillChunk(voxel::Chunk &chunk, ChunkCoord cc) const;

  private:
    float hash01(std::int32_t x, std::int32_t y, std::int32_t z) const;
    static float smooth(float t);
    static float lerp(float a, float b, float t);
    float valueNoise2D(float x, float z) const;
    float valueNoise3D(float x, float y, float z) const;
    float fbm2D(float x, float z, int octaves, float lacunarity, float gain) const;
    float fbm3D(float x, float y, float z, int octaves, float lacunarity, float gain) const;

    std::uint32_t seed_;
};

} // namespace world
