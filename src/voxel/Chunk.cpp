#include "voxel/Chunk.hpp"

namespace voxel {

Chunk::Chunk() : blocks_(SX * SY * SZ, AIR) {}

bool Chunk::inBounds(int x, int y, int z) const {
    return x >= 0 && x < SX && y >= 0 && y < SY && z >= 0 && z < SZ;
}

BlockId Chunk::get(int x, int y, int z) const {
    if (!inBounds(x, y, z)) {
        return AIR;
    }
    return getUnchecked(x, y, z);
}

void Chunk::set(int x, int y, int z, BlockId id) {
    if (!inBounds(x, y, z)) {
        return;
    }
    setUnchecked(x, y, z, id);
}

void Chunk::fillLayered() {
    for (int x = 0; x < SX; ++x) {
        for (int z = 0; z < SZ; ++z) {
            for (int y = 0; y < 58; ++y) {
                setRaw(x, y, z, STONE);
            }
            for (int y = 58; y < 62; ++y) {
                setRaw(x, y, z, DIRT);
            }
            setRaw(x, 62, z, GRASS);
        }
    }
    dirty = true;
}

} // namespace voxel
