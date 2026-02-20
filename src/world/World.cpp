#include "world/World.hpp"

#include "voxel/ChunkMesher.hpp"

#include <algorithm>
#include <cmath>
#include <utility>
#include <vector>

namespace world {
namespace {} // namespace

World::World(const gfx::TextureAtlas &atlas, std::filesystem::path saveRoot, std::uint32_t seed)
    : atlas_(atlas), gen_(seed), io_(std::move(saveRoot), WorldGen::kGeneratorVersion) {
    worker_ = std::thread([this]() { workerLoop(); });
}

World::~World() {
    running_.store(false);
    workerJobs_.stop();
    completed_.stop();

    if (worker_.joinable()) {
        worker_.join();
    }

    std::lock_guard<std::mutex> lock(chunksMutex_);
    for (auto &[coord, entry] : chunks_) {
        if (entry.chunk) {
            io_.save(*entry.chunk, coord);
        }
    }
}

int World::floorDiv(int a, int b) {
    int q = a / b;
    int r = a % b;
    if (r != 0 && ((r > 0) != (b > 0))) {
        --q;
    }
    return q;
}

int World::floorMod(int a, int b) {
    const int m = a % b;
    return (m < 0) ? (m + b) : m;
}

ChunkCoord World::worldToChunk(int wx, int wz) {
    return ChunkCoord{floorDiv(wx, voxel::Chunk::SX), floorDiv(wz, voxel::Chunk::SZ)};
}

void World::enqueueLoadIfNeeded(ChunkCoord cc) {
    if (chunks_.find(cc) != chunks_.end()) {
        return;
    }
    if (!pendingLoad_.insert(cc).second) {
        return;
    }

    WorkerJob job;
    job.type = JobType::LoadOrGenerate;
    job.coord = cc;

    auto it = chunks_.find(ChunkCoord{cc.x + 1, cc.z});
    if (it != chunks_.end())
        job.px = it->second.chunk;
    it = chunks_.find(ChunkCoord{cc.x - 1, cc.z});
    if (it != chunks_.end())
        job.nx = it->second.chunk;
    it = chunks_.find(ChunkCoord{cc.x, cc.z + 1});
    if (it != chunks_.end())
        job.pz = it->second.chunk;
    it = chunks_.find(ChunkCoord{cc.x, cc.z - 1});
    if (it != chunks_.end())
        job.nz = it->second.chunk;

    workerJobs_.push(std::move(job));
}

void World::enqueueRemesh(ChunkCoord cc, bool force) {
    if (force) {
        pendingRemesh_.erase(cc);
    }
    if (!pendingRemesh_.insert(cc).second) {
        return;
    }

    auto it = chunks_.find(cc);
    if (it == chunks_.end() || !it->second.chunk) {
        pendingRemesh_.erase(cc);
        return;
    }

    WorkerJob job;
    job.type = JobType::Remesh;
    job.coord = cc;
    job.chunkSnapshot = it->second.chunk;

    auto nit = chunks_.find(ChunkCoord{cc.x + 1, cc.z});
    if (nit != chunks_.end())
        job.px = nit->second.chunk;
    nit = chunks_.find(ChunkCoord{cc.x - 1, cc.z});
    if (nit != chunks_.end())
        job.nx = nit->second.chunk;
    nit = chunks_.find(ChunkCoord{cc.x, cc.z + 1});
    if (nit != chunks_.end())
        job.pz = nit->second.chunk;
    nit = chunks_.find(ChunkCoord{cc.x, cc.z - 1});
    if (nit != chunks_.end())
        job.nz = nit->second.chunk;

    workerJobs_.push(std::move(job));
}

void World::setStreamingRadii(int loadRadius, int unloadRadius) {
    std::lock_guard<std::mutex> lock(chunksMutex_);
    loadRadius_ = std::clamp(loadRadius, 2, 16);
    unloadRadius_ = std::clamp(std::max(unloadRadius, loadRadius_ + 1), 3, 20);
}

WorldDebugStats World::debugStats() const {
    std::lock_guard<std::mutex> lock(chunksMutex_);

    WorldDebugStats stats;
    stats.loadedChunks = static_cast<int>(chunks_.size());
    stats.pendingLoad = static_cast<int>(pendingLoad_.size());
    stats.pendingRemesh = static_cast<int>(pendingRemesh_.size());

    for (const auto &[coord, entry] : chunks_) {
        (void)coord;
        if (entry.mesh) {
            ++stats.meshedChunks;
        }
        stats.totalTriangles += entry.triangleCount;
    }
    return stats;
}

void World::updateStream(const glm::vec3 &playerPos) {
    const int pChunkX = floorDiv(static_cast<int>(std::floor(playerPos.x)), voxel::Chunk::SX);
    const int pChunkZ = floorDiv(static_cast<int>(std::floor(playerPos.z)), voxel::Chunk::SZ);

    std::lock_guard<std::mutex> lock(chunksMutex_);

    for (int dz = -loadRadius_; dz <= loadRadius_; ++dz) {
        for (int dx = -loadRadius_; dx <= loadRadius_; ++dx) {
            const ChunkCoord cc{pChunkX + dx, pChunkZ + dz};
            enqueueLoadIfNeeded(cc);
        }
    }

    std::vector<ChunkCoord> toUnload;
    toUnload.reserve(chunks_.size());

    for (const auto &[cc, entry] : chunks_) {
        const int ddx = cc.x - pChunkX;
        const int ddz = cc.z - pChunkZ;
        const int dist = std::max(std::abs(ddx), std::abs(ddz));
        if (dist > unloadRadius_) {
            toUnload.push_back(cc);
        }
    }

    for (const ChunkCoord cc : toUnload) {
        auto it = chunks_.find(cc);
        if (it == chunks_.end()) {
            continue;
        }
        if (it->second.chunk) {
            io_.save(*it->second.chunk, cc);
        }
        chunks_.erase(it);
        pendingLoad_.erase(cc);
        pendingRemesh_.erase(cc);

        // Unloading a chunk exposes border faces on neighboring chunks.
        enqueueRemesh(ChunkCoord{cc.x + 1, cc.z}, true);
        enqueueRemesh(ChunkCoord{cc.x - 1, cc.z}, true);
        enqueueRemesh(ChunkCoord{cc.x, cc.z + 1}, true);
        enqueueRemesh(ChunkCoord{cc.x, cc.z - 1}, true);
    }
}

void World::uploadReadyMeshes() {
    WorkerResult result;
    while (completed_.tryPop(result)) {
        std::lock_guard<std::mutex> lock(chunksMutex_);

        // Drop stale load results for chunks that were unloaded before worker
        // completion.
        if (result.replaceChunk && chunks_.find(result.coord) == chunks_.end() &&
            pendingLoad_.find(result.coord) == pendingLoad_.end()) {
            continue;
        }

        auto &entry = chunks_[result.coord];
        if (result.replaceChunk) {
            entry.chunk = std::move(result.chunk);
            pendingLoad_.erase(result.coord);
            // Build this chunk mesh only after load completes in the normal remesh
            // path, so boundary face culling can use available neighbors.
            enqueueRemesh(result.coord, true);
            // New chunk may occlude neighbor border faces.
            enqueueRemesh(ChunkCoord{result.coord.x + 1, result.coord.z}, true);
            enqueueRemesh(ChunkCoord{result.coord.x - 1, result.coord.z}, true);
            enqueueRemesh(ChunkCoord{result.coord.x, result.coord.z + 1}, true);
            enqueueRemesh(ChunkCoord{result.coord.x, result.coord.z - 1}, true);
        } else {
            pendingRemesh_.erase(result.coord);
        }

        if (!entry.mesh) {
            entry.mesh = std::make_unique<gfx::ChunkMesh>();
        }
        entry.mesh->upload(result.mesh);
        entry.triangleCount = static_cast<int>(result.mesh.indices.size() / 3);
    }
}

void World::draw() const {
    std::lock_guard<std::mutex> lock(chunksMutex_);
    for (const auto &[coord, entry] : chunks_) {
        (void)coord;
        if (!entry.mesh) {
            continue;
        }
        entry.mesh->draw();
    }
}

void World::drawTransparent(const glm::vec3 &cameraPos) const {
    std::lock_guard<std::mutex> lock(chunksMutex_);

    struct DrawItem {
        const gfx::ChunkMesh *mesh = nullptr;
        float dist2 = 0.0f;
    };
    std::vector<DrawItem> items;
    items.reserve(chunks_.size());

    for (const auto &[coord, entry] : chunks_) {
        if (!entry.mesh) {
            continue;
        }
        const float cx = static_cast<float>(coord.x * voxel::Chunk::SX + voxel::Chunk::SX / 2);
        const float cz = static_cast<float>(coord.z * voxel::Chunk::SZ + voxel::Chunk::SZ / 2);
        const float dx = cx - cameraPos.x;
        const float dz = cz - cameraPos.z;
        items.push_back(DrawItem{entry.mesh.get(), dx * dx + dz * dz});
    }

    std::sort(items.begin(), items.end(), [](const DrawItem &a, const DrawItem &b) {
        return a.dist2 > b.dist2; // far-to-near for blending
    });

    for (const DrawItem &item : items) {
        item.mesh->draw();
    }
}

voxel::BlockId World::getBlock(int wx, int wy, int wz) const {
    if (wy < 0 || wy >= voxel::Chunk::SY) {
        return voxel::AIR;
    }

    const ChunkCoord cc = worldToChunk(wx, wz);
    const int lx = floorMod(wx, voxel::Chunk::SX);
    const int lz = floorMod(wz, voxel::Chunk::SZ);

    std::lock_guard<std::mutex> lock(chunksMutex_);
    const auto it = chunks_.find(cc);
    if (it == chunks_.end() || !it->second.chunk) {
        return voxel::AIR;
    }
    return it->second.chunk->get(lx, wy, lz);
}

bool World::isSolidBlock(int wx, int wy, int wz) const {
    const voxel::BlockId id = getBlock(wx, wy, wz);
    if (id == voxel::AIR || id == voxel::WATER || id == voxel::TALL_GRASS || id == voxel::FLOWER) {
        return false;
    }
    return blockRegistry_.get(id).solid;
}

bool World::isTargetBlock(int wx, int wy, int wz) const {
    const voxel::BlockId id = getBlock(wx, wy, wz);
    if (id == voxel::AIR || id == voxel::WATER) {
        return false;
    }
    return blockRegistry_.get(id).solid;
}

bool World::isChunkLoadedAt(int wx, int wz) const {
    const ChunkCoord cc = worldToChunk(wx, wz);
    std::lock_guard<std::mutex> lock(chunksMutex_);
    const auto it = chunks_.find(cc);
    return it != chunks_.end() && static_cast<bool>(it->second.chunk);
}

bool World::setBlock(int wx, int wy, int wz, voxel::BlockId id) {
    if (wy < 0 || wy >= voxel::Chunk::SY) {
        return false;
    }

    const ChunkCoord cc = worldToChunk(wx, wz);
    const int lx = floorMod(wx, voxel::Chunk::SX);
    const int lz = floorMod(wz, voxel::Chunk::SZ);

    std::lock_guard<std::mutex> lock(chunksMutex_);
    auto it = chunks_.find(cc);
    if (it == chunks_.end() || !it->second.chunk) {
        return false;
    }

    it->second.chunk->set(lx, wy, lz, id);
    enqueueRemesh(cc, false);

    if (lx == 0)
        enqueueRemesh(ChunkCoord{cc.x - 1, cc.z}, true);
    if (lx == voxel::Chunk::SX - 1)
        enqueueRemesh(ChunkCoord{cc.x + 1, cc.z}, true);
    if (lz == 0)
        enqueueRemesh(ChunkCoord{cc.x, cc.z - 1}, true);
    if (lz == voxel::Chunk::SZ - 1)
        enqueueRemesh(ChunkCoord{cc.x, cc.z + 1}, true);

    return true;
}

void World::workerLoop() {
    while (running_.load()) {
        auto maybeJob = workerJobs_.waitPop();
        if (!maybeJob.has_value()) {
            break;
        }

        WorkerJob job = std::move(*maybeJob);

        if (job.type == JobType::LoadOrGenerate) {
            auto chunk = std::make_shared<voxel::Chunk>();

            if (!io_.load(*chunk, job.coord)) {
                gen_.fillChunk(*chunk, job.coord);
            }
            // Defer mesh build to remesh jobs after chunk registration so
            // neighbor-aware edge culling happens before any faces are rendered.
            completed_.push(WorkerResult{job.coord, std::move(chunk), gfx::CpuMesh{}, true});
        } else {
            if (!job.chunkSnapshot) {
                continue;
            }
            const voxel::ChunkMesher::NeighborChunks neighbors{job.px.get(), job.nx.get(),
                                                               job.pz.get(), job.nz.get()};

            const gfx::CpuMesh mesh = voxel::ChunkMesher::buildFaceCulled(
                *job.chunkSnapshot, atlas_, blockRegistry_, glm::ivec2(job.coord.x, job.coord.z),
                neighbors);
            completed_.push(WorkerResult{job.coord, nullptr, mesh, false});
        }
    }
}

} // namespace world
