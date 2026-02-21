#include "voxel/ChunkIO.hpp"

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <fstream>

namespace voxel {
namespace {

constexpr std::uint32_t kFurnaceSectionMagic = 0x31465246u; // FRF1

void writeSlot(std::ofstream &out, const world::FurnaceSlotState &slot) {
    const std::uint16_t id = static_cast<std::uint16_t>(slot.id);
    const std::uint16_t count = static_cast<std::uint16_t>(std::clamp(slot.count, 0, 0xFFFF));
    out.write(reinterpret_cast<const char *>(&id), sizeof(id));
    out.write(reinterpret_cast<const char *>(&count), sizeof(count));
}

bool readSlot(std::ifstream &in, world::FurnaceSlotState &slot) {
    std::uint16_t id = 0;
    std::uint16_t count = 0;
    in.read(reinterpret_cast<char *>(&id), sizeof(id));
    in.read(reinterpret_cast<char *>(&count), sizeof(count));
    if (!in) {
        return false;
    }
    if (count == 0) {
        slot = {};
        return true;
    }
    slot.id = static_cast<voxel::BlockId>(id);
    slot.count = static_cast<int>(count);
    return true;
}

} // namespace

ChunkIO::ChunkIO(std::filesystem::path root, std::uint32_t generatorVersion)
    : root_(std::move(root)), generatorVersion_(generatorVersion) {
    std::filesystem::create_directories(root_);
}

std::filesystem::path ChunkIO::chunkPath(world::ChunkCoord cc) const {
    return root_ / ("chunk_" + std::to_string(cc.x) + "_" + std::to_string(cc.z) + ".bin");
}

bool ChunkIO::save(const Chunk &chunk, world::ChunkCoord cc,
                   const std::vector<world::FurnaceRecordLocal> *furnaces) const {
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

    const std::uint32_t sectionMagic = kFurnaceSectionMagic;
    const std::uint16_t furnaceCount =
        static_cast<std::uint16_t>(std::min<std::size_t>(furnaces ? furnaces->size() : 0, 0xFFFFu));
    out.write(reinterpret_cast<const char *>(&sectionMagic), sizeof(sectionMagic));
    out.write(reinterpret_cast<const char *>(&furnaceCount), sizeof(furnaceCount));
    if (furnaces != nullptr) {
        for (std::size_t fi = 0; fi < furnaceCount; ++fi) {
            const auto &r = (*furnaces)[fi];
            out.write(reinterpret_cast<const char *>(&r.x), sizeof(r.x));
            out.write(reinterpret_cast<const char *>(&r.y), sizeof(r.y));
            out.write(reinterpret_cast<const char *>(&r.z), sizeof(r.z));
            writeSlot(out, r.state.input);
            writeSlot(out, r.state.fuel);
            writeSlot(out, r.state.output);
            out.write(reinterpret_cast<const char *>(&r.state.progressSeconds),
                      sizeof(r.state.progressSeconds));
            out.write(reinterpret_cast<const char *>(&r.state.burnSecondsRemaining),
                      sizeof(r.state.burnSecondsRemaining));
            out.write(reinterpret_cast<const char *>(&r.state.burnSecondsCapacity),
                      sizeof(r.state.burnSecondsCapacity));
        }
    }

    return static_cast<bool>(out);
}

bool ChunkIO::load(Chunk &chunk, world::ChunkCoord cc,
                   std::vector<world::FurnaceRecordLocal> *furnacesOut) const {
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

    if (writePos != blocks.size()) {
        return false;
    }

    if (furnacesOut != nullptr) {
        furnacesOut->clear();
    }

    std::uint32_t sectionMagic = 0;
    in.read(reinterpret_cast<char *>(&sectionMagic), sizeof(sectionMagic));
    if (!in) {
        return true; // older chunk format without furnace section
    }
    if (sectionMagic != kFurnaceSectionMagic) {
        return true;
    }
    std::uint16_t furnaceCount = 0;
    in.read(reinterpret_cast<char *>(&furnaceCount), sizeof(furnaceCount));
    if (!in) {
        return false;
    }
    for (std::uint16_t i = 0; i < furnaceCount; ++i) {
        world::FurnaceRecordLocal rec{};
        in.read(reinterpret_cast<char *>(&rec.x), sizeof(rec.x));
        in.read(reinterpret_cast<char *>(&rec.y), sizeof(rec.y));
        in.read(reinterpret_cast<char *>(&rec.z), sizeof(rec.z));
        if (!in || !readSlot(in, rec.state.input) || !readSlot(in, rec.state.fuel) ||
            !readSlot(in, rec.state.output)) {
            return false;
        }
        in.read(reinterpret_cast<char *>(&rec.state.progressSeconds),
                sizeof(rec.state.progressSeconds));
        in.read(reinterpret_cast<char *>(&rec.state.burnSecondsRemaining),
                sizeof(rec.state.burnSecondsRemaining));
        in.read(reinterpret_cast<char *>(&rec.state.burnSecondsCapacity),
                sizeof(rec.state.burnSecondsCapacity));
        if (!in) {
            return false;
        }
        if (furnacesOut != nullptr) {
            furnacesOut->push_back(rec);
        }
    }

    return true;
}

} // namespace voxel
