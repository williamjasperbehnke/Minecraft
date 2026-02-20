#include "voxel/ChunkIO.hpp"

#include <cstdint>
#include <filesystem>
#include <fstream>

namespace voxel {

ChunkIO::ChunkIO(std::filesystem::path root, std::uint32_t generatorVersion)
    : root_(std::move(root)), generatorVersion_(generatorVersion) {
    std::filesystem::create_directories(root_);
}

std::filesystem::path ChunkIO::chunkPath(world::ChunkCoord cc) const {
    return root_ / ("chunk_" + std::to_string(cc.x) + "_" + std::to_string(cc.z) + ".bin");
}

bool ChunkIO::save(const Chunk &chunk, world::ChunkCoord cc) const {
    std::ofstream out(chunkPath(cc), std::ios::binary);
    if (!out) {
        return false;
    }

    const std::uint32_t magic = 0x31584C56u; // VXL1
    const std::uint32_t version = generatorVersion_;
    const std::uint16_t sx = Chunk::SX;
    const std::uint16_t sy = Chunk::SY;
    const std::uint16_t sz = Chunk::SZ;

    out.write(reinterpret_cast<const char *>(&magic), sizeof(magic));
    out.write(reinterpret_cast<const char *>(&version), sizeof(version));
    out.write(reinterpret_cast<const char *>(&sx), sizeof(sx));
    out.write(reinterpret_cast<const char *>(&sy), sizeof(sy));
    out.write(reinterpret_cast<const char *>(&sz), sizeof(sz));

    const auto &blocks = chunk.data();
    std::size_t i = 0;
    while (i < blocks.size()) {
        const BlockId id = blocks[i];
        std::uint16_t run = 1;
        while (i + run < blocks.size() && blocks[i + run] == id && run < 0xFFFFu) {
            ++run;
        }
        out.write(reinterpret_cast<const char *>(&id), sizeof(id));
        out.write(reinterpret_cast<const char *>(&run), sizeof(run));
        i += run;
    }

    return static_cast<bool>(out);
}

bool ChunkIO::load(Chunk &chunk, world::ChunkCoord cc) const {
    std::ifstream in(chunkPath(cc), std::ios::binary);
    if (!in) {
        return false;
    }

    std::uint32_t magic = 0;
    std::uint32_t version = 0;
    std::uint16_t sx = 0;
    std::uint16_t sy = 0;
    std::uint16_t sz = 0;

    in.read(reinterpret_cast<char *>(&magic), sizeof(magic));
    in.read(reinterpret_cast<char *>(&version), sizeof(version));
    in.read(reinterpret_cast<char *>(&sx), sizeof(sx));
    in.read(reinterpret_cast<char *>(&sy), sizeof(sy));
    in.read(reinterpret_cast<char *>(&sz), sizeof(sz));

    if (!in || magic != 0x31584C56u || version != generatorVersion_ || sx != Chunk::SX ||
        sy != Chunk::SY || sz != Chunk::SZ) {
        return false;
    }

    auto &blocks = chunk.data();
    blocks.assign(Chunk::SX * Chunk::SY * Chunk::SZ, AIR);

    std::size_t writePos = 0;
    while (in && writePos < blocks.size()) {
        BlockId id = AIR;
        std::uint16_t run = 0;
        in.read(reinterpret_cast<char *>(&id), sizeof(id));
        in.read(reinterpret_cast<char *>(&run), sizeof(run));
        if (!in) {
            break;
        }
        for (std::uint16_t i = 0; i < run && writePos < blocks.size(); ++i) {
            blocks[writePos++] = id;
        }
    }

    return writePos == blocks.size();
}

} // namespace voxel
