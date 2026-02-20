#pragma once

#include "core/ThreadQueue.hpp"
#include "gfx/ChunkMesh.hpp"
#include "gfx/TextureAtlas.hpp"
#include "voxel/Block.hpp"
#include "voxel/Chunk.hpp"
#include "voxel/ChunkIO.hpp"
#include "world/ChunkCoord.hpp"
#include "world/WorldGen.hpp"

#include <glm/vec3.hpp>

#include <atomic>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <mutex>
#include <thread>
#include <unordered_map>
#include <unordered_set>

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
    World(const gfx::TextureAtlas &atlas, std::filesystem::path saveRoot,
          std::uint32_t seed = 1337u);
    ~World();

    World(const World &) = delete;
    World &operator=(const World &) = delete;

    void updateStream(const glm::vec3 &playerPos);
    void uploadReadyMeshes();
    void draw() const;
    void drawTransparent(const glm::vec3 &cameraPos) const;

    bool isSolidBlock(int wx, int wy, int wz) const;
    bool isTargetBlock(int wx, int wy, int wz) const;
    bool isChunkLoadedAt(int wx, int wz) const;
    voxel::BlockId getBlock(int wx, int wy, int wz) const;
    bool setBlock(int wx, int wy, int wz, voxel::BlockId id);

    void setStreamingRadii(int loadRadius, int unloadRadius);
    WorldDebugStats debugStats() const;

  private:
    enum class JobType { LoadOrGenerate, Remesh };

    struct WorkerJob {
        JobType type = JobType::LoadOrGenerate;
        ChunkCoord coord;
        std::shared_ptr<voxel::Chunk> chunkSnapshot;
        std::shared_ptr<voxel::Chunk> px;
        std::shared_ptr<voxel::Chunk> nx;
        std::shared_ptr<voxel::Chunk> pz;
        std::shared_ptr<voxel::Chunk> nz;
    };

    struct WorkerResult {
        ChunkCoord coord;
        std::shared_ptr<voxel::Chunk> chunk;
        gfx::CpuMesh mesh;
        bool replaceChunk = false;
    };

    struct ChunkEntry {
        std::shared_ptr<voxel::Chunk> chunk;
        std::unique_ptr<gfx::ChunkMesh> mesh;
        int triangleCount = 0;
    };

    void enqueueLoadIfNeeded(ChunkCoord cc);
    void enqueueRemesh(ChunkCoord cc, bool force);
    void workerLoop();

    static int floorDiv(int a, int b);
    static int floorMod(int a, int b);
    static ChunkCoord worldToChunk(int wx, int wz);

    int loadRadius_ = 8;
    int unloadRadius_ = 10;

    const gfx::TextureAtlas &atlas_;
    voxel::BlockRegistry blockRegistry_;
    world::WorldGen gen_;
    voxel::ChunkIO io_;

    std::unordered_map<ChunkCoord, ChunkEntry, ChunkCoordHash> chunks_;
    mutable std::mutex chunksMutex_;

    core::ThreadQueue<WorkerJob> workerJobs_;
    core::ThreadQueue<WorkerResult> completed_;

    std::unordered_set<ChunkCoord, ChunkCoordHash> pendingLoad_;
    std::unordered_set<ChunkCoord, ChunkCoordHash> pendingRemesh_;

    std::thread worker_;
    std::atomic<bool> running_ = true;
};

} // namespace world
