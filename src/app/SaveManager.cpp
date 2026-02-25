#include "app/SaveManager.hpp"

#include "game/Inventory.hpp"
#include "voxel/Block.hpp"
#include "voxel/Chunk.hpp"
#include "world/ChunkCoord.hpp"
#include "world/FurnaceState.hpp"
#include "world/World.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <fstream>
#include <iomanip>
#include <limits>
#include <string>

namespace app {

namespace {

constexpr std::uint32_t kFurnaceSectionMagic = 0x31465246u; // FRF1

std::filesystem::path chunkPath(const std::filesystem::path &root, world::ChunkCoord cc) {
    return root / ("chunk_" + std::to_string(cc.x) + "_" + std::to_string(cc.z) + ".bin");
}

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

world::FurnaceState SaveManager::toWorldFurnaceState(const game::SmeltingSystem::State &src) {
    world::FurnaceState out{};
    out.input.id = src.input.id;
    out.input.count = src.input.count;
    out.fuel.id = src.fuel.id;
    out.fuel.count = src.fuel.count;
    out.output.id = src.output.id;
    out.output.count = src.output.count;
    out.progressSeconds = src.progressSeconds;
    out.burnSecondsRemaining = src.burnSecondsRemaining;
    out.burnSecondsCapacity = src.burnSecondsCapacity;
    return out;
}

game::SmeltingSystem::State SaveManager::fromWorldFurnaceState(const world::FurnaceState &src) {
    game::SmeltingSystem::State out{};
    out.input.id = src.input.id;
    out.input.count = src.input.count;
    out.fuel.id = src.fuel.id;
    out.fuel.count = src.fuel.count;
    out.output.id = src.output.id;
    out.output.count = src.output.count;
    out.progressSeconds = src.progressSeconds;
    out.burnSecondsRemaining = src.burnSecondsRemaining;
    out.burnSecondsCapacity = src.burnSecondsCapacity;
    return out;
}

bool SaveManager::loadPlayerData(const std::filesystem::path &worldDir, glm::vec3 &cameraPos,
                                 int &selectedSlot, bool &ghostMode, game::Inventory &inventory,
                                 game::SmeltingSystem::State &smelting) {
    std::ifstream in(worldDir / "player.dat");
    if (!in) {
        return false;
    }

    std::string magic;
    in >> magic;
    if (!in || (magic != "VXP1" && magic != "VXP2" && magic != "VXP3")) {
        return false;
    }

    glm::vec3 loadedPos{};
    int loadedSelected = 0;
    int loadedMode = 1;
    in >> loadedPos.x >> loadedPos.y >> loadedPos.z;
    in >> loadedSelected;
    if (magic == "VXP2" || magic == "VXP3") {
        in >> loadedMode;
    }
    if (!in) {
        return false;
    }

    std::array<game::Inventory::Slot, game::Inventory::kSlotCount> loadedSlots{};
    for (int i = 0; i < game::Inventory::kSlotCount; ++i) {
        int id = 0;
        int count = 0;
        in >> id >> count;
        if (!in) {
            return false;
        }
        count = std::clamp(count, 0, game::Inventory::kMaxStack);
        if (count == 0) {
            loadedSlots[i] = {};
            continue;
        }
        if (id < 0 || id > std::numeric_limits<std::uint16_t>::max()) {
            loadedSlots[i] = {};
            continue;
        }
        loadedSlots[i].id = static_cast<voxel::BlockId>(id);
        loadedSlots[i].count = count;
    }

    for (int i = 0; i < game::Inventory::kSlotCount; ++i) {
        inventory.slot(i) = loadedSlots[i];
    }
    selectedSlot = std::clamp(loadedSelected, 0, game::Inventory::kHotbarSize - 1);
    ghostMode = (loadedMode != 0);
    cameraPos = loadedPos;
    smelting = {};
    if (magic == "VXP3") {
        auto loadSmeltSlot = [&](game::Inventory::Slot &slot) {
            int id = 0;
            int count = 0;
            in >> id >> count;
            if (!in) {
                return false;
            }
            count = std::clamp(count, 0, game::Inventory::kMaxStack);
            if (count <= 0 || id < 0 || id > std::numeric_limits<std::uint16_t>::max()) {
                slot = {};
                return true;
            }
            slot.id = static_cast<voxel::BlockId>(id);
            slot.count = count;
            return true;
        };
        if (!loadSmeltSlot(smelting.input) || !loadSmeltSlot(smelting.fuel) ||
            !loadSmeltSlot(smelting.output)) {
            return false;
        }
        in >> smelting.progressSeconds >> smelting.burnSecondsRemaining >>
            smelting.burnSecondsCapacity;
        if (!in) {
            return false;
        }
        smelting.progressSeconds =
            std::clamp(smelting.progressSeconds, 0.0f, game::SmeltingSystem::kSmeltSeconds);
        smelting.burnSecondsRemaining = std::max(0.0f, smelting.burnSecondsRemaining);
        smelting.burnSecondsCapacity = std::max(0.0f, smelting.burnSecondsCapacity);
        if (smelting.burnSecondsCapacity > 0.0f &&
            smelting.burnSecondsRemaining > smelting.burnSecondsCapacity) {
            smelting.burnSecondsRemaining = smelting.burnSecondsCapacity;
        }
        if (smelting.fuel.id == voxel::AIR || smelting.fuel.count <= 0) {
            smelting.fuel = {};
        }
        if (smelting.input.id == voxel::AIR || smelting.input.count <= 0) {
            smelting.input = {};
            smelting.progressSeconds = 0.0f;
        }
        if (smelting.output.id == voxel::AIR || smelting.output.count <= 0) {
            smelting.output = {};
        }
    }
    return true;
}

void SaveManager::savePlayerData(const std::filesystem::path &worldDir, const glm::vec3 &cameraPos,
                                 int selectedSlot, bool ghostMode,
                                 const game::Inventory &inventory,
                                 const game::SmeltingSystem::State &smelting) {
    std::filesystem::create_directories(worldDir);
    std::ofstream out(worldDir / "player.dat", std::ios::trunc);
    if (!out) {
        return;
    }

    out << "VXP3\n";
    out << std::fixed << std::setprecision(std::numeric_limits<float>::max_digits10) << cameraPos.x
        << ' ' << cameraPos.y << ' ' << cameraPos.z << '\n';
    out << std::clamp(selectedSlot, 0, game::Inventory::kHotbarSize - 1) << '\n';
    out << (ghostMode ? 1 : 0) << '\n';
    for (int i = 0; i < game::Inventory::kSlotCount; ++i) {
        const auto &slot = inventory.slot(i);
        out << static_cast<int>(slot.id) << ' '
            << std::clamp(slot.count, 0, game::Inventory::kMaxStack) << '\n';
    }
    auto writeSmeltSlot = [&](const game::Inventory::Slot &slot) {
        out << static_cast<int>(slot.id) << ' '
            << std::clamp(slot.count, 0, game::Inventory::kMaxStack) << '\n';
    };
    writeSmeltSlot(smelting.input);
    writeSmeltSlot(smelting.fuel);
    writeSmeltSlot(smelting.output);
    out << std::max(0.0f, smelting.progressSeconds) << ' '
        << std::max(0.0f, smelting.burnSecondsRemaining) << ' '
        << std::max(0.0f, smelting.burnSecondsCapacity) << '\n';
}

void SaveManager::persistFurnaceState(world::World &world,
                                      const std::optional<glm::ivec3> &activeFurnaceCell,
                                      const game::SmeltingSystem::State &smelting) {
    if (!activeFurnaceCell.has_value()) {
        return;
    }
    const bool hasItems = (smelting.input.id != voxel::AIR && smelting.input.count > 0) ||
                          (smelting.fuel.id != voxel::AIR && smelting.fuel.count > 0) ||
                          (smelting.output.id != voxel::AIR && smelting.output.count > 0);
    const bool hasWork = smelting.progressSeconds > 0.0f || smelting.burnSecondsRemaining > 0.0f ||
                         smelting.burnSecondsCapacity > 0.0f;
    const glm::ivec3 c = activeFurnaceCell.value();
    if (hasItems || hasWork) {
        world.setFurnaceState(c.x, c.y, c.z, toWorldFurnaceState(smelting));
    } else {
        world.clearFurnaceState(c.x, c.y, c.z);
    }
}

void SaveManager::loadFurnaceState(world::World &world,
                                   const std::optional<glm::ivec3> &activeFurnaceCell,
                                   game::SmeltingSystem::State &smelting) {
    smelting = {};
    if (!activeFurnaceCell.has_value()) {
        return;
    }
    world::FurnaceState loaded{};
    const glm::ivec3 c = activeFurnaceCell.value();
    if (world.getFurnaceState(c.x, c.y, c.z, loaded)) {
        smelting = fromWorldFurnaceState(loaded);
    }
}

bool SaveManager::saveChunk(const std::filesystem::path &worldDir, std::uint32_t generatorVersion,
                            const voxel::Chunk &chunk, world::ChunkCoord cc,
                            const std::vector<world::FurnaceRecordLocal> *furnaces) {
    std::filesystem::create_directories(worldDir);
    std::ofstream out(chunkPath(worldDir, cc), std::ios::binary);
    if (!out) {
        return false;
    }

    const std::uint32_t magic = 0x31584C56u; // VXL1
    const std::uint32_t version = generatorVersion;
    const std::uint16_t sx = voxel::Chunk::SX;
    const std::uint16_t sy = voxel::Chunk::SY;
    const std::uint16_t sz = voxel::Chunk::SZ;

    out.write(reinterpret_cast<const char *>(&magic), sizeof(magic));
    out.write(reinterpret_cast<const char *>(&version), sizeof(version));
    out.write(reinterpret_cast<const char *>(&sx), sizeof(sx));
    out.write(reinterpret_cast<const char *>(&sy), sizeof(sy));
    out.write(reinterpret_cast<const char *>(&sz), sizeof(sz));

    const auto &blocks = chunk.data();
    std::size_t i = 0;
    while (i < blocks.size()) {
        const voxel::BlockId id = blocks[i];
        std::uint16_t run = 1;
        while (i + run < blocks.size() && blocks[i + run] == id && run < 0xFFFFu) {
            ++run;
        }
        out.write(reinterpret_cast<const char *>(&id), sizeof(id));
        out.write(reinterpret_cast<const char *>(&run), sizeof(run));
        i += run;
    }

    const std::uint32_t sectionMagic = kFurnaceSectionMagic;
    const std::uint16_t furnaceCount = static_cast<std::uint16_t>(
        std::min<std::size_t>(furnaces ? furnaces->size() : 0, 0xFFFFu));
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

bool SaveManager::loadChunk(const std::filesystem::path &worldDir, std::uint32_t generatorVersion,
                            voxel::Chunk &chunk, world::ChunkCoord cc,
                            std::vector<world::FurnaceRecordLocal> *furnacesOut) {
    std::ifstream in(chunkPath(worldDir, cc), std::ios::binary);
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

    if (!in || magic != 0x31584C56u || version != generatorVersion || sx != voxel::Chunk::SX ||
        sy != voxel::Chunk::SY || sz != voxel::Chunk::SZ) {
        return false;
    }

    auto &blocks = chunk.data();
    blocks.assign(voxel::Chunk::SX * voxel::Chunk::SY * voxel::Chunk::SZ, voxel::AIR);

    std::size_t writePos = 0;
    while (in && writePos < blocks.size()) {
        voxel::BlockId id = voxel::AIR;
        std::uint16_t run = 0;
        in.read(reinterpret_cast<char *>(&id), sizeof(id));
        in.read(reinterpret_cast<char *>(&run), sizeof(run));
        if (!in) {
            break;
        }
        for (std::uint16_t ri = 0; ri < run && writePos < blocks.size(); ++ri) {
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
    for (std::uint16_t fi = 0; fi < furnaceCount; ++fi) {
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

} // namespace app
