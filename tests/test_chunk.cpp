#include "voxel/Chunk.hpp"

#include <cassert>

int main() {
    voxel::Chunk chunk;

    assert(chunk.inBounds(0, 0, 0));
    assert(chunk.inBounds(voxel::Chunk::SX - 1, voxel::Chunk::SY - 1, voxel::Chunk::SZ - 1));
    assert(!chunk.inBounds(-1, 0, 0));
    assert(!chunk.inBounds(voxel::Chunk::SX, 0, 0));

    chunk.set(1, 2, 3, voxel::STONE);
    assert(chunk.get(1, 2, 3) == voxel::STONE);

    chunk.set(-1, 2, 3, voxel::DIRT);
    assert(chunk.get(-1, 2, 3) == voxel::AIR);

    return 0;
}
