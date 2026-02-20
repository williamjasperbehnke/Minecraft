#pragma once

#include "voxel/Block.hpp"

#include <vector>

namespace voxel {

class Chunk {
  public:
    static constexpr int SX = 16;
    static constexpr int SY = 128;
    static constexpr int SZ = 16;

    Chunk();

    bool inBounds(int x, int y, int z) const;
    BlockId get(int x, int y, int z) const;
    void set(int x, int y, int z, BlockId id);
    BlockId getUnchecked(int x, int y, int z) const {
        return blocks_[index(x, y, z)];
    }
    void setUnchecked(int x, int y, int z, BlockId id) {
        setRaw(x, y, z, id);
        dirty = true;
    }
    void setRaw(int x, int y, int z, BlockId id) {
        blocks_[index(x, y, z)] = id;
    }

    void fillLayered();

    const std::vector<BlockId> &data() const {
        return blocks_;
    }
    std::vector<BlockId> &data() {
        return blocks_;
    }

    bool dirty = true;

  private:
    static int index(int x, int y, int z) {
        return x + SX * (z + SZ * y);
    }

    std::vector<BlockId> blocks_;
};

} // namespace voxel
