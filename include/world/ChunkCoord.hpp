#pragma once

#include <cstddef>
#include <cstdint>

namespace world {

struct ChunkCoord {
    int x = 0;
    int z = 0;

    bool operator==(const ChunkCoord &) const = default;
};

struct ChunkCoordHash {
    std::size_t operator()(const ChunkCoord &c) const {
        return static_cast<std::size_t>(
            (static_cast<std::uint64_t>(static_cast<std::uint32_t>(c.x)) << 32U) ^
            static_cast<std::uint32_t>(c.z));
    }
};

} // namespace world
