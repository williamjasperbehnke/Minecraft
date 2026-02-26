#pragma once

#include "voxel/Block.hpp"
#include "world/ChunkCoord.hpp"

#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <filesystem>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace world {
class World;
}

namespace game {

class MapSystem {
  public:
    struct Waypoint {
        int x = 0;
        int z = 0;
        std::string name;
        std::uint8_t r = 255;
        std::uint8_t g = 96;
        std::uint8_t b = 96;
        std::uint8_t icon = 0;
        bool visible = true;
    };

    MapSystem();
    ~MapSystem();

    MapSystem(const MapSystem &) = delete;
    MapSystem &operator=(const MapSystem &) = delete;

    void observeLoadedChunks(const world::World &world);
    bool sample(int wx, int wz, voxel::BlockId &outId) const;
    bool sampleHeight(int wx, int wz, int &outY) const;
    bool sampleWaterCover(int wx, int wz, bool &outCovered) const;
    const std::vector<Waypoint> &waypoints() const;
    std::vector<Waypoint> &waypoints();

    bool load(const std::filesystem::path &worldDir);
    bool save(const std::filesystem::path &worldDir) const;

  private:
    static std::uint64_t keyFor(int x, int z);
    void workerLoop();
    void enqueueScan(const world::World &world, std::vector<world::ChunkCoord> chunks);
    void consumeWorkerResult();

    std::unordered_map<std::uint64_t, voxel::BlockId> tiles_;
    std::unordered_map<std::uint64_t, voxel::BlockId> liveTiles_;
    std::unordered_map<std::uint64_t, std::uint8_t> heights_;
    std::unordered_map<std::uint64_t, std::uint8_t> liveHeights_;
    std::unordered_map<std::uint64_t, std::uint8_t> waterCover_;
    std::unordered_map<std::uint64_t, std::uint8_t> liveWaterCover_;
    std::unordered_set<world::ChunkCoord, world::ChunkCoordHash> knownLoadedChunks_;
    std::vector<Waypoint> waypoints_;
    std::size_t refreshCursor_ = 0;

    std::thread worker_;
    std::mutex workerMutex_;
    std::condition_variable workerCv_;
    bool running_ = true;
    bool requestPending_ = false;
    bool workerBusy_ = false;
    const world::World *requestWorld_ = nullptr;
    std::vector<world::ChunkCoord> requestChunks_;

    std::mutex resultMutex_;
    bool hasResult_ = false;
    std::unordered_map<std::uint64_t, voxel::BlockId> resultLiveTiles_;
    std::unordered_map<std::uint64_t, std::uint8_t> resultLiveHeights_;
    std::unordered_map<std::uint64_t, std::uint8_t> resultLiveWaterCover_;

    std::chrono::steady_clock::time_point lastScanEnqueue_{};
};

} // namespace game
