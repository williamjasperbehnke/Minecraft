#pragma once

#include "core/ThreadQueue.hpp"
#include "core/TickCounter.hpp"
#include "gfx/ChunkMesh.hpp"
#include "gfx/TextureAtlas.hpp"
#include "voxel/Block.hpp"
#include "voxel/Chunk.hpp"
#include "world/ChunkCoord.hpp"
#include "world/FurnaceState.hpp"
#include "world/WorldGen.hpp"

#include <glm/vec3.hpp>
#include <glm/mat4x4.hpp>

#include <atomic>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <mutex>
#include <limits>
#include <thread>
#include <deque>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace world {

struct WorldDebugStats {
    int loadedChunks = 0;
    int meshedChunks = 0;
    int pendingLoad = 0;
    int pendingRemesh = 0;
    int totalTriangles = 0;
};

class World {
  public:
    struct FluidDrop {
        voxel::BlockId id = voxel::AIR;
        int count = 0;
        glm::vec3 pos{0.0f};
    };
    struct FurnaceCoordKey {
        int x = 0;
        int y = 0;
        int z = 0;
        bool operator==(const FurnaceCoordKey &other) const {
            return x == other.x && y == other.y && z == other.z;
        }
    };

    struct FurnaceCoordKeyHash {
        std::size_t operator()(const FurnaceCoordKey &k) const {
            std::size_t h = static_cast<std::size_t>(static_cast<std::uint32_t>(k.x));
            h = (h * 1315423911u) ^ static_cast<std::size_t>(static_cast<std::uint32_t>(k.y));
            h = (h * 2654435761u) ^ static_cast<std::size_t>(static_cast<std::uint32_t>(k.z));
            return h;
        }
    };

    World(const gfx::TextureAtlas &atlas, std::filesystem::path saveRoot,
          std::uint32_t seed = 1337u);
    ~World();

    World(const World &) = delete;
    World &operator=(const World &) = delete;

    void updateStream(const glm::vec3 &playerPos, const glm::vec3 &cameraForward);
    void updateFluidSimulation(float dt);
    void uploadReadyMeshes();
    void draw(const glm::vec3 &cameraPos, float maxDrawDistance, const glm::mat4 &viewProj) const;
    void drawTransparent(const glm::vec3 &cameraPos, const glm::vec3 &cameraForward,
                         float maxDrawDistance, const glm::mat4 &viewProj) const;

    bool isSolidBlock(int wx, int wy, int wz) const;
    bool isTargetBlock(int wx, int wy, int wz) const;
    bool isChunkLoadedAt(int wx, int wz) const;
    glm::vec3 fluidCurrentAt(const glm::vec3 &pos) const;
    std::string biomeLabelAt(int wx, int wz) const;
    voxel::BlockId getBlock(int wx, int wy, int wz) const;
    bool setBlock(int wx, int wy, int wz, voxel::BlockId id);
    bool getFurnaceState(int wx, int wy, int wz, FurnaceState &out) const;
    void setFurnaceState(int wx, int wy, int wz, const FurnaceState &state);
    void clearFurnaceState(int wx, int wy, int wz);
    std::vector<glm::ivec3> loadedFurnacePositions() const;

    void setStreamingRadii(int loadRadius, int unloadRadius);
    void setSmoothLighting(bool enabled);
    std::vector<FluidDrop> consumeFluidDrops();
    WorldDebugStats debugStats() const;
    std::vector<ChunkCoord> loadedChunkCoords() const;

  private:
    enum class JobType { LoadOrGenerate, Remesh };
    struct FluidCoord {
        int x = 0;
        int y = 0;
        int z = 0;
        bool operator==(const FluidCoord &other) const {
            return x == other.x && y == other.y && z == other.z;
        }
    };
    struct FluidCoordHash {
        std::size_t operator()(const FluidCoord &c) const {
            std::size_t h = static_cast<std::size_t>(std::hash<int>{}(c.x));
            h ^= static_cast<std::size_t>(std::hash<int>{}(c.y)) + 0x9e3779b97f4a7c15ull + (h << 6) +
                 (h >> 2);
            h ^= static_cast<std::size_t>(std::hash<int>{}(c.z)) + 0x9e3779b97f4a7c15ull + (h << 6) +
                 (h >> 2);
            return h;
        }
    };

    struct WorkerJob {
        JobType type = JobType::LoadOrGenerate;
        ChunkCoord coord;
        bool urgent = false;
        std::shared_ptr<voxel::Chunk> chunkSnapshot;
        std::unordered_map<FluidCoord, std::uint8_t, FluidCoordHash> waterLevels;
        std::unordered_map<FluidCoord, std::uint8_t, FluidCoordHash> lavaLevels;
        std::shared_ptr<voxel::Chunk> px;
        std::shared_ptr<voxel::Chunk> nx;
        std::shared_ptr<voxel::Chunk> pz;
        std::shared_ptr<voxel::Chunk> nz;
        std::shared_ptr<voxel::Chunk> pxpz;
        std::shared_ptr<voxel::Chunk> pxnz;
        std::shared_ptr<voxel::Chunk> nxpz;
        std::shared_ptr<voxel::Chunk> nxnz;
    };

    struct WorkerResult {
        ChunkCoord coord;
        bool urgent = false;
        std::shared_ptr<voxel::Chunk> chunk;
        std::unique_ptr<gfx::CpuMesh> mesh;
        bool replaceChunk = false;
    };

    struct ChunkEntry {
        std::shared_ptr<voxel::Chunk> chunk;
        std::unique_ptr<gfx::ChunkMesh> mesh;
        int triangleCount = 0;
    };
    struct TransparentDrawItem {
        const gfx::ChunkMesh *mesh = nullptr;
        ChunkCoord coord{};
        float depth = 0.0f;
        float dist2 = 0.0f;
    };
    struct FluidState {
        std::uint8_t level = 0;
        bool source = false;
    };

    void enqueueLoadIfNeeded(ChunkCoord cc);
    void enqueueRemesh(ChunkCoord cc, bool force, bool urgent = false);
    void scheduleWorkerJob(WorkerJob job);
    void workerLoop();
    voxel::BlockId getBlockLoadedLocked(int wx, int wy, int wz) const;
    void enqueueFluidCellLocked(int wx, int wy, int wz);
    void activateFluidCellLocked(int wx, int wy, int wz);
    void enqueueFluidNeighborsLocked(int wx, int wy, int wz);
    void seedFluidFrontierForChunkLocked(ChunkCoord cc, const voxel::Chunk &chunk);
    void appendFluidRemeshNeighborhoodLocked(ChunkCoord cc, bool waterLike,
                                             std::unordered_set<ChunkCoord, ChunkCoordHash> &out) const;
    int fluidLevelAtLocked(voxel::BlockId fluidId, int wx, int wy, int wz) const;
    void setFluidStateLocked(voxel::BlockId fluidId, int wx, int wy, int wz, std::uint8_t level,
                             bool source);
    void clearFluidStateLocked(int wx, int wy, int wz);
    void processFluidFrontierLocked(voxel::BlockId fluidId, std::deque<FluidCoord> &frontier,
                                    std::unordered_set<FluidCoord, FluidCoordHash> &queued,
                                    std::unordered_set<ChunkCoord, ChunkCoordHash> &remeshChunks);
    std::unique_ptr<gfx::CpuMesh> acquireMeshBuffer();
    void recycleMeshBuffer(std::unique_ptr<gfx::CpuMesh> mesh);

    static int floorDiv(int a, int b);
    static int floorMod(int a, int b);
    static ChunkCoord worldToChunk(int wx, int wz);

    int loadRadius_ = 8;
    int unloadRadius_ = 10;
    std::atomic<std::int64_t> playerChunkPacked_{0};
    std::atomic<std::int64_t> lastStreamChunkPacked_{std::numeric_limits<std::int64_t>::min()};
    std::atomic<std::int64_t> lastStreamForwardPacked_{std::numeric_limits<std::int64_t>::min()};
    std::atomic<bool> streamDirty_{true};

    const gfx::TextureAtlas &atlas_;
    voxel::BlockRegistry blockRegistry_;
    world::WorldGen gen_;
    std::filesystem::path saveRoot_;

    std::unordered_map<ChunkCoord, ChunkEntry, ChunkCoordHash> chunks_;
    mutable std::mutex chunksMutex_;

    core::ThreadQueue<WorkerJob> workerJobs_;
    core::ThreadQueue<WorkerResult> completed_;

    std::unordered_set<ChunkCoord, ChunkCoordHash> pendingLoad_;
    std::unordered_set<ChunkCoord, ChunkCoordHash> pendingRemesh_;
    std::unordered_set<ChunkCoord, ChunkCoordHash> pendingRemeshDirty_;
    std::unordered_map<FurnaceCoordKey, FurnaceState, FurnaceCoordKeyHash> furnaceStates_;

    std::vector<std::thread> workers_;
    std::mutex meshBufferPoolMutex_;
    std::vector<std::unique_ptr<gfx::CpuMesh>> meshBufferPool_;
    std::atomic<bool> running_ = true;
    std::atomic<bool> smoothLighting_{false};
    std::deque<FluidCoord> waterFrontier_;
    std::deque<FluidCoord> lavaFrontier_;
    std::unordered_set<FluidCoord, FluidCoordHash> waterQueued_;
    std::unordered_set<FluidCoord, FluidCoordHash> lavaQueued_;
    std::unordered_map<FluidCoord, FluidState, FluidCoordHash> waterState_;
    std::unordered_map<FluidCoord, FluidState, FluidCoordHash> lavaState_;
    std::unordered_map<ChunkCoord, std::unordered_set<FluidCoord, FluidCoordHash>, ChunkCoordHash>
        waterStateByChunk_;
    std::unordered_map<ChunkCoord, std::unordered_set<FluidCoord, FluidCoordHash>, ChunkCoordHash>
        lavaStateByChunk_;
    std::unordered_map<ChunkCoord, std::unordered_set<FurnaceCoordKey, FurnaceCoordKeyHash>,
                       ChunkCoordHash>
        furnaceStateByChunk_;
    std::vector<FluidDrop> pendingFluidDrops_;
    std::atomic<std::uint64_t> worldRevision_{1};
    std::atomic<std::uint32_t> meshReserveVertices_{8192};
    std::atomic<std::uint32_t> meshReserveIndices_{12288};
    mutable std::vector<TransparentDrawItem> transparentDrawCache_;
    mutable glm::vec3 transparentCachePos_{0.0f};
    mutable glm::vec3 transparentCacheForward_{0.0f, 0.0f, -1.0f};
    mutable float transparentCacheMaxDist_ = -1.0f;
    mutable std::uint64_t transparentCacheRevision_ = 0;
    mutable bool transparentCacheValid_ = false;
    core::TickCounter fluidTicks_{1.0f / 20.0f, 0};
};

} // namespace world
