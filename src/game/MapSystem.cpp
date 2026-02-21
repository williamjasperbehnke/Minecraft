#include "game/MapSystem.hpp"

#include "voxel/Chunk.hpp"
#include "world/World.hpp"

#include <algorithm>
#include <cstdint>
#include <fstream>
#include <utility>

namespace game {
namespace {

constexpr char kMapMagicV1[4] = {'V', 'X', 'M', '1'};
constexpr char kMapMagicV2[4] = {'V', 'X', 'M', '2'};
constexpr char kMapMagicV3[4] = {'V', 'X', 'M', '3'};
constexpr auto kScanEnqueueInterval = std::chrono::milliseconds(350);

bool isMapSurfaceCandidate(voxel::BlockId id) {
    if (id == voxel::AIR) {
        return false;
    }
    if (id == voxel::TORCH_WALL_POS_X || id == voxel::TORCH_WALL_NEG_X ||
        id == voxel::TORCH_WALL_POS_Z || id == voxel::TORCH_WALL_NEG_Z) {
        return false;
    }
    return true;
}

} // namespace

MapSystem::MapSystem() {
    worker_ = std::thread([this]() { workerLoop(); });
}

MapSystem::~MapSystem() {
    {
        std::lock_guard<std::mutex> lock(workerMutex_);
        running_ = false;
        requestPending_ = false;
    }
    workerCv_.notify_all();
    if (worker_.joinable()) {
        worker_.join();
    }
}

std::uint64_t MapSystem::keyFor(int x, int z) {
    const std::uint64_t ux = static_cast<std::uint64_t>(static_cast<std::uint32_t>(x));
    const std::uint64_t uz = static_cast<std::uint64_t>(static_cast<std::uint32_t>(z));
    return (ux << 32u) | uz;
}

void MapSystem::workerLoop() {
    while (true) {
        const world::World *world = nullptr;
        std::vector<world::ChunkCoord> chunks;
        {
            std::unique_lock<std::mutex> lock(workerMutex_);
            workerCv_.wait(lock, [&]() { return requestPending_ || !running_; });
            if (!running_) {
                return;
            }
            world = requestWorld_;
            chunks = std::move(requestChunks_);
            requestChunks_.clear();
            requestPending_ = false;
            workerBusy_ = true;
        }

        std::unordered_map<std::uint64_t, voxel::BlockId> localLiveTiles;
        if (world != nullptr) {
            localLiveTiles.reserve(chunks.size() * voxel::Chunk::SX * voxel::Chunk::SZ);
            for (const auto &cc : chunks) {
                const int baseX = cc.x * voxel::Chunk::SX;
                const int baseZ = cc.z * voxel::Chunk::SZ;
                for (int lz = 0; lz < voxel::Chunk::SZ; ++lz) {
                    for (int lx = 0; lx < voxel::Chunk::SX; ++lx) {
                        const int wx = baseX + lx;
                        const int wz = baseZ + lz;
                        voxel::BlockId id = voxel::AIR;
                        for (int y = voxel::Chunk::SY - 1; y >= 0; --y) {
                            const voxel::BlockId s = world->getBlock(wx, y, wz);
                            if (!isMapSurfaceCandidate(s)) {
                                continue;
                            }
                            id = s;
                            break;
                        }
                        if (id != voxel::AIR) {
                            localLiveTiles[keyFor(wx, wz)] = id;
                        }
                    }
                }
            }
        }

        {
            std::lock_guard<std::mutex> lock(resultMutex_);
            resultLiveTiles_ = std::move(localLiveTiles);
            hasResult_ = true;
        }
        {
            std::lock_guard<std::mutex> lock(workerMutex_);
            workerBusy_ = false;
        }
    }
}

void MapSystem::enqueueScan(const world::World &world, std::vector<world::ChunkCoord> chunks) {
    std::lock_guard<std::mutex> lock(workerMutex_);
    requestWorld_ = &world;
    requestChunks_ = std::move(chunks);
    requestPending_ = true;
    workerCv_.notify_one();
}

void MapSystem::consumeWorkerResult() {
    std::unordered_map<std::uint64_t, voxel::BlockId> local;
    {
        std::lock_guard<std::mutex> lock(resultMutex_);
        if (!hasResult_) {
            return;
        }
        local = std::move(resultLiveTiles_);
        resultLiveTiles_.clear();
        hasResult_ = false;
    }
    liveTiles_ = std::move(local);
    for (const auto &[key, id] : liveTiles_) {
        tiles_[key] = id;
    }
}

bool MapSystem::sample(int wx, int wz, voxel::BlockId &outId) const {
    const std::uint64_t key = keyFor(wx, wz);
    const auto liveIt = liveTiles_.find(key);
    if (liveIt != liveTiles_.end()) {
        outId = liveIt->second;
        return true;
    }
    const auto it = tiles_.find(key);
    if (it == tiles_.end()) {
        return false;
    }
    outId = it->second;
    return true;
}

const std::vector<MapSystem::Waypoint> &MapSystem::waypoints() const {
    return waypoints_;
}

std::vector<MapSystem::Waypoint> &MapSystem::waypoints() {
    return waypoints_;
}

void MapSystem::observeLoadedChunks(const world::World &world) {
    consumeWorkerResult();
    const auto now = std::chrono::steady_clock::now();
    if (lastScanEnqueue_.time_since_epoch().count() != 0 &&
        (now - lastScanEnqueue_) < kScanEnqueueInterval) {
        return;
    }
    {
        std::lock_guard<std::mutex> lock(workerMutex_);
        if (requestPending_ || workerBusy_) {
            return;
        }
    }
    auto loaded = world.loadedChunkCoords();
    if (loaded.empty()) {
        return;
    }
    lastScanEnqueue_ = now;
    enqueueScan(world, std::move(loaded));
}

bool MapSystem::load(const std::filesystem::path &worldDir) {
    tiles_.clear();
    liveTiles_.clear();
    waypoints_.clear();
    std::ifstream in(worldDir / "map.dat", std::ios::binary);
    if (!in) {
        return false;
    }

    char magic[4] = {};
    in.read(magic, sizeof(magic));
    if (!in) {
        return false;
    }
    const bool isV1 = std::equal(std::begin(magic), std::end(magic), std::begin(kMapMagicV1));
    const bool isV2 = std::equal(std::begin(magic), std::end(magic), std::begin(kMapMagicV2));
    const bool isV3 = std::equal(std::begin(magic), std::end(magic), std::begin(kMapMagicV3));
    if (!isV1 && !isV2 && !isV3) {
        return false;
    }

    std::uint32_t count = 0;
    in.read(reinterpret_cast<char *>(&count), sizeof(count));
    if (!in) {
        return false;
    }

    for (std::uint32_t i = 0; i < count; ++i) {
        std::int32_t x = 0;
        std::int32_t z = 0;
        std::uint16_t id = 0;
        in.read(reinterpret_cast<char *>(&x), sizeof(x));
        in.read(reinterpret_cast<char *>(&z), sizeof(z));
        in.read(reinterpret_cast<char *>(&id), sizeof(id));
        if (!in) {
            return false;
        }
        if (id != voxel::AIR) {
            tiles_[keyFor(static_cast<int>(x), static_cast<int>(z))] =
                static_cast<voxel::BlockId>(id);
        }
    }
    if (isV2 || isV3) {
        std::uint32_t waypointCount = 0;
        in.read(reinterpret_cast<char *>(&waypointCount), sizeof(waypointCount));
        if (!in) {
            return false;
        }
        waypoints_.reserve(waypointCount);
        for (std::uint32_t i = 0; i < waypointCount; ++i) {
            Waypoint wp{};
            std::uint16_t nameLen = 0;
            in.read(reinterpret_cast<char *>(&wp.x), sizeof(std::int32_t));
            in.read(reinterpret_cast<char *>(&wp.z), sizeof(std::int32_t));
            in.read(reinterpret_cast<char *>(&wp.r), sizeof(std::uint8_t));
            in.read(reinterpret_cast<char *>(&wp.g), sizeof(std::uint8_t));
            in.read(reinterpret_cast<char *>(&wp.b), sizeof(std::uint8_t));
            in.read(reinterpret_cast<char *>(&wp.icon), sizeof(std::uint8_t));
            if (isV3) {
                std::uint8_t visible = 1;
                in.read(reinterpret_cast<char *>(&visible), sizeof(std::uint8_t));
                wp.visible = (visible != 0);
            } else {
                wp.visible = true;
            }
            in.read(reinterpret_cast<char *>(&nameLen), sizeof(std::uint16_t));
            if (!in) {
                return false;
            }
            wp.name.resize(nameLen);
            if (nameLen > 0) {
                in.read(wp.name.data(), nameLen);
                if (!in) {
                    return false;
                }
            }
            wp.icon = static_cast<std::uint8_t>(wp.icon % 5u);
            waypoints_.push_back(std::move(wp));
        }
    }
    return true;
}

bool MapSystem::save(const std::filesystem::path &worldDir) const {
    std::filesystem::create_directories(worldDir);
    std::ofstream out(worldDir / "map.dat", std::ios::binary | std::ios::trunc);
    if (!out) {
        return false;
    }

    out.write(kMapMagicV3, sizeof(kMapMagicV3));
    const std::uint32_t count = static_cast<std::uint32_t>(tiles_.size());
    out.write(reinterpret_cast<const char *>(&count), sizeof(count));
    for (const auto &[key, id] : tiles_) {
        const std::int32_t x = static_cast<std::int32_t>(key >> 32u);
        const std::int32_t z = static_cast<std::int32_t>(key & 0xFFFFFFFFu);
        const std::uint16_t rawId = static_cast<std::uint16_t>(id);
        out.write(reinterpret_cast<const char *>(&x), sizeof(x));
        out.write(reinterpret_cast<const char *>(&z), sizeof(z));
        out.write(reinterpret_cast<const char *>(&rawId), sizeof(rawId));
    }
    const std::uint32_t waypointCount = static_cast<std::uint32_t>(waypoints_.size());
    out.write(reinterpret_cast<const char *>(&waypointCount), sizeof(waypointCount));
    for (const Waypoint &wp : waypoints_) {
        const std::int32_t x = static_cast<std::int32_t>(wp.x);
        const std::int32_t z = static_cast<std::int32_t>(wp.z);
        const std::uint8_t icon = static_cast<std::uint8_t>(wp.icon % 5u);
        const std::uint16_t nameLen =
            static_cast<std::uint16_t>(std::min<std::size_t>(wp.name.size(), 64));
        out.write(reinterpret_cast<const char *>(&x), sizeof(x));
        out.write(reinterpret_cast<const char *>(&z), sizeof(z));
        out.write(reinterpret_cast<const char *>(&wp.r), sizeof(std::uint8_t));
        out.write(reinterpret_cast<const char *>(&wp.g), sizeof(std::uint8_t));
        out.write(reinterpret_cast<const char *>(&wp.b), sizeof(std::uint8_t));
        out.write(reinterpret_cast<const char *>(&icon), sizeof(icon));
        const std::uint8_t visible = wp.visible ? 1u : 0u;
        out.write(reinterpret_cast<const char *>(&visible), sizeof(visible));
        out.write(reinterpret_cast<const char *>(&nameLen), sizeof(nameLen));
        if (nameLen > 0) {
            out.write(wp.name.data(), nameLen);
        }
    }
    return static_cast<bool>(out);
}

} // namespace game
