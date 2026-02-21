#include "world/World.hpp"

#include "voxel/ChunkMesher.hpp"

#include <algorithm>
#include <cmath>
#include <utility>
#include <vector>

namespace world {
namespace {

constexpr unsigned int kMinWorkerThreads = 2u;
constexpr unsigned int kMaxWorkerThreads = 8u;
constexpr int kMaxUnloadsPerUpdate = 2;
constexpr int kMaxUploadsPerFrame = 2;

int chunkDistance(ChunkCoord a, ChunkCoord b) {
    return std::max(std::abs(a.x - b.x), std::abs(a.z - b.z));
}

std::int64_t packChunkCoord(int x, int z) {
    return (static_cast<std::int64_t>(x) << 32) |
           static_cast<std::uint32_t>(static_cast<std::int32_t>(z));
}

ChunkCoord unpackChunkCoord(std::int64_t packed) {
    return ChunkCoord{static_cast<int>(packed >> 32),
                      static_cast<int>(static_cast<std::int32_t>(packed & 0xFFFFFFFF))};
}

World::FurnaceCoordKey makeFurnaceKey(int x, int y, int z) {
    return World::FurnaceCoordKey{x, y, z};
}

} // namespace

World::World(const gfx::TextureAtlas &atlas, std::filesystem::path saveRoot, std::uint32_t seed)
    : atlas_(atlas), gen_(seed), io_(std::move(saveRoot), WorldGen::kGeneratorVersion) {
    const unsigned int hardwareThreads = std::max(1u, std::thread::hardware_concurrency());
    const unsigned int workerCount =
        std::clamp(hardwareThreads > 1 ? hardwareThreads - 1 : 1u, kMinWorkerThreads,
                   kMaxWorkerThreads);
    workers_.reserve(workerCount);
    for (unsigned int i = 0; i < workerCount; ++i) {
        workers_.emplace_back([this]() { workerLoop(); });
    }
}

World::~World() {
    running_.store(false);
    workerJobs_.stop();
    completed_.stop();

    for (std::thread &t : workers_) {
        if (t.joinable()) {
            t.join();
        }
    }

    std::lock_guard<std::mutex> lock(chunksMutex_);
    for (auto &[coord, entry] : chunks_) {
        if (entry.chunk) {
            std::vector<world::FurnaceRecordLocal> localFurnaces;
            localFurnaces.reserve(4);
            for (const auto &[fKey, fState] : furnaceStates_) {
                if (fKey.y < 0 || fKey.y >= voxel::Chunk::SY) {
                    continue;
                }
                const ChunkCoord fcc = worldToChunk(fKey.x, fKey.z);
                if (fcc.x != coord.x || fcc.z != coord.z) {
                    continue;
                }
                const int lx = floorMod(fKey.x, voxel::Chunk::SX);
                const int lz = floorMod(fKey.z, voxel::Chunk::SZ);
                if (!voxel::isFurnace(entry.chunk->get(lx, fKey.y, lz))) {
                    continue;
                }
                world::FurnaceRecordLocal rec{};
                rec.x = static_cast<std::uint8_t>(lx);
                rec.y = static_cast<std::uint8_t>(fKey.y);
                rec.z = static_cast<std::uint8_t>(lz);
                rec.state = fState;
                localFurnaces.push_back(rec);
            }
            io_.save(*entry.chunk, coord, &localFurnaces);
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
    it = chunks_.find(ChunkCoord{cc.x + 1, cc.z + 1});
    if (it != chunks_.end())
        job.pxpz = it->second.chunk;
    it = chunks_.find(ChunkCoord{cc.x + 1, cc.z - 1});
    if (it != chunks_.end())
        job.pxnz = it->second.chunk;
    it = chunks_.find(ChunkCoord{cc.x - 1, cc.z + 1});
    if (it != chunks_.end())
        job.nxpz = it->second.chunk;
    it = chunks_.find(ChunkCoord{cc.x - 1, cc.z - 1});
    if (it != chunks_.end())
        job.nxnz = it->second.chunk;

    scheduleWorkerJob(std::move(job));
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
    nit = chunks_.find(ChunkCoord{cc.x + 1, cc.z + 1});
    if (nit != chunks_.end())
        job.pxpz = nit->second.chunk;
    nit = chunks_.find(ChunkCoord{cc.x + 1, cc.z - 1});
    if (nit != chunks_.end())
        job.pxnz = nit->second.chunk;
    nit = chunks_.find(ChunkCoord{cc.x - 1, cc.z + 1});
    if (nit != chunks_.end())
        job.nxpz = nit->second.chunk;
    nit = chunks_.find(ChunkCoord{cc.x - 1, cc.z - 1});
    if (nit != chunks_.end())
        job.nxnz = nit->second.chunk;

    scheduleWorkerJob(std::move(job));
}

void World::scheduleWorkerJob(WorkerJob job) {
    workerJobs_.push(std::move(job));
}

void World::setStreamingRadii(int loadRadius, int unloadRadius) {
    std::lock_guard<std::mutex> lock(chunksMutex_);
    loadRadius_ = std::clamp(loadRadius, 2, 16);
    unloadRadius_ = std::clamp(std::max(unloadRadius, loadRadius_ + 1), 3, 20);
    streamDirty_.store(true, std::memory_order_relaxed);
}

void World::setSmoothLighting(bool enabled) {
    if (smoothLighting_.load(std::memory_order_relaxed) == enabled) {
        return;
    }
    smoothLighting_.store(enabled, std::memory_order_relaxed);
    std::lock_guard<std::mutex> lock(chunksMutex_);
    for (const auto &[cc, entry] : chunks_) {
        if (entry.chunk) {
            enqueueRemesh(cc, true);
        }
    }
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

std::vector<ChunkCoord> World::loadedChunkCoords() const {
    std::lock_guard<std::mutex> lock(chunksMutex_);
    std::vector<ChunkCoord> out;
    out.reserve(chunks_.size());
    for (const auto &[cc, entry] : chunks_) {
        if (entry.chunk) {
            out.push_back(cc);
        }
    }
    return out;
}

void World::updateStream(const glm::vec3 &playerPos) {
    const int pChunkX = floorDiv(static_cast<int>(std::floor(playerPos.x)), voxel::Chunk::SX);
    const int pChunkZ = floorDiv(static_cast<int>(std::floor(playerPos.z)), voxel::Chunk::SZ);
    const std::int64_t packed = packChunkCoord(pChunkX, pChunkZ);
    playerChunkPacked_.store(packed, std::memory_order_relaxed);
    const bool streamDirty = streamDirty_.load(std::memory_order_relaxed);
    const std::int64_t lastPacked = lastStreamChunkPacked_.load(std::memory_order_relaxed);
    if (!streamDirty && lastPacked == packed) {
        return;
    }
    lastStreamChunkPacked_.store(packed, std::memory_order_relaxed);
    streamDirty_.store(false, std::memory_order_relaxed);

    std::lock_guard<std::mutex> lock(chunksMutex_);

    for (int dz = -loadRadius_; dz <= loadRadius_; ++dz) {
        for (int dx = -loadRadius_; dx <= loadRadius_; ++dx) {
            const ChunkCoord cc{pChunkX + dx, pChunkZ + dz};
            enqueueLoadIfNeeded(cc);
        }
    }

    // Ensure loaded chunks without mesh are eventually meshed.
    for (const auto &[cc, entry] : chunks_) {
        if (!entry.chunk || entry.mesh) {
            continue;
        }
        enqueueRemesh(cc, false);
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

    auto enqueueNeighborRingRemesh = [this](ChunkCoord cc) {
        enqueueRemesh(ChunkCoord{cc.x + 1, cc.z}, true);
        enqueueRemesh(ChunkCoord{cc.x - 1, cc.z}, true);
        enqueueRemesh(ChunkCoord{cc.x, cc.z + 1}, true);
        enqueueRemesh(ChunkCoord{cc.x, cc.z - 1}, true);
        enqueueRemesh(ChunkCoord{cc.x + 1, cc.z + 1}, true);
        enqueueRemesh(ChunkCoord{cc.x + 1, cc.z - 1}, true);
        enqueueRemesh(ChunkCoord{cc.x - 1, cc.z + 1}, true);
        enqueueRemesh(ChunkCoord{cc.x - 1, cc.z - 1}, true);
    };

    int unloadsProcessed = 0;
    for (const ChunkCoord cc : toUnload) {
        if (unloadsProcessed >= kMaxUnloadsPerUpdate) {
            break;
        }
        auto it = chunks_.find(cc);
        if (it == chunks_.end()) {
            continue;
        }
        if (it->second.chunk) {
            std::vector<world::FurnaceRecordLocal> localFurnaces;
            localFurnaces.reserve(4);
            for (const auto &[fKey, fState] : furnaceStates_) {
                if (fKey.y < 0 || fKey.y >= voxel::Chunk::SY) {
                    continue;
                }
                const ChunkCoord fcc = worldToChunk(fKey.x, fKey.z);
                if (fcc.x != cc.x || fcc.z != cc.z) {
                    continue;
                }
                const int lx = floorMod(fKey.x, voxel::Chunk::SX);
                const int lz = floorMod(fKey.z, voxel::Chunk::SZ);
                if (!voxel::isFurnace(it->second.chunk->get(lx, fKey.y, lz))) {
                    continue;
                }
                world::FurnaceRecordLocal rec{};
                rec.x = static_cast<std::uint8_t>(lx);
                rec.y = static_cast<std::uint8_t>(fKey.y);
                rec.z = static_cast<std::uint8_t>(lz);
                rec.state = fState;
                localFurnaces.push_back(rec);
            }
            io_.save(*it->second.chunk, cc, &localFurnaces);
            for (auto fit = furnaceStates_.begin(); fit != furnaceStates_.end();) {
                const ChunkCoord fcc = worldToChunk(fit->first.x, fit->first.z);
                if (fcc.x == cc.x && fcc.z == cc.z) {
                    fit = furnaceStates_.erase(fit);
                } else {
                    ++fit;
                }
            }
        }
        chunks_.erase(it);
        pendingLoad_.erase(cc);
        pendingRemesh_.erase(cc);

        // Unloading a chunk exposes border faces on neighboring chunks.
        enqueueNeighborRingRemesh(cc);
        ++unloadsProcessed;
    }
    if (unloadsProcessed < static_cast<int>(toUnload.size())) {
        // Continue streaming work next frame even if player remains in the same chunk.
        streamDirty_.store(true, std::memory_order_relaxed);
    }
}

void World::uploadReadyMeshes() {
    auto enqueueNeighborRingRemesh = [this](ChunkCoord cc) {
        enqueueRemesh(ChunkCoord{cc.x + 1, cc.z}, true);
        enqueueRemesh(ChunkCoord{cc.x - 1, cc.z}, true);
        enqueueRemesh(ChunkCoord{cc.x, cc.z + 1}, true);
        enqueueRemesh(ChunkCoord{cc.x, cc.z - 1}, true);
        enqueueRemesh(ChunkCoord{cc.x + 1, cc.z + 1}, true);
        enqueueRemesh(ChunkCoord{cc.x + 1, cc.z - 1}, true);
        enqueueRemesh(ChunkCoord{cc.x - 1, cc.z + 1}, true);
        enqueueRemesh(ChunkCoord{cc.x - 1, cc.z - 1}, true);
    };

    int uploadsThisFrame = 0;
    WorkerResult result;
    while (uploadsThisFrame < kMaxUploadsPerFrame && completed_.tryPop(result)) {
        std::lock_guard<std::mutex> lock(chunksMutex_);

        // Drop stale load results for chunks that were unloaded before worker
        // completion.
        if (result.replaceChunk && chunks_.find(result.coord) == chunks_.end() &&
            pendingLoad_.find(result.coord) == pendingLoad_.end()) {
            continue;
        }
        // Drop stale remesh results for chunks that are no longer loaded.
        if (!result.replaceChunk) {
            const auto it = chunks_.find(result.coord);
            if (it == chunks_.end() || !it->second.chunk) {
                pendingRemesh_.erase(result.coord);
                continue;
            }
        }

        auto &entry = chunks_[result.coord];
        if (result.replaceChunk) {
            entry.chunk = std::move(result.chunk);
            pendingLoad_.erase(result.coord);
            // Build this chunk mesh only after load completes in the normal remesh
            // path, so boundary face culling can use available neighbors.
            enqueueRemesh(result.coord, true);
            // New chunk may occlude neighbor border faces.
            enqueueNeighborRingRemesh(result.coord);
        } else {
            pendingRemesh_.erase(result.coord);
        }

        if (!entry.mesh) {
            entry.mesh = std::make_unique<gfx::ChunkMesh>();
        }
        entry.mesh->upload(result.mesh);
        entry.triangleCount = static_cast<int>(result.mesh.indices.size() / 3);
        ++uploadsThisFrame;
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
    if (id == voxel::AIR || id == voxel::WATER || id == voxel::TALL_GRASS ||
        id == voxel::FLOWER || voxel::isTorch(id)) {
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

    const voxel::BlockId prevId = it->second.chunk->get(lx, wy, lz);
    if (prevId == id) {
        return true;
    }
    it->second.chunk->set(lx, wy, lz, id);
    enqueueRemesh(cc, true);

    // Lighting/faces can change across chunk seams even when the edited block is
    // not on a border, so always refresh direct neighbors.
    enqueueRemesh(ChunkCoord{cc.x - 1, cc.z}, true);
    enqueueRemesh(ChunkCoord{cc.x + 1, cc.z}, true);
    enqueueRemesh(ChunkCoord{cc.x, cc.z - 1}, true);
    enqueueRemesh(ChunkCoord{cc.x, cc.z + 1}, true);
    enqueueRemesh(ChunkCoord{cc.x - 1, cc.z - 1}, true);
    enqueueRemesh(ChunkCoord{cc.x - 1, cc.z + 1}, true);
    enqueueRemesh(ChunkCoord{cc.x + 1, cc.z - 1}, true);
    enqueueRemesh(ChunkCoord{cc.x + 1, cc.z + 1}, true);

    // Emissive block changes can cross chunk boundaries even when the modified
    // block is not on an edge, so refresh a second ring for seam correctness.
    if (voxel::emittedBlockLight(prevId) != voxel::emittedBlockLight(id)) {
        enqueueRemesh(ChunkCoord{cc.x - 2, cc.z}, true);
        enqueueRemesh(ChunkCoord{cc.x + 2, cc.z}, true);
        enqueueRemesh(ChunkCoord{cc.x, cc.z - 2}, true);
        enqueueRemesh(ChunkCoord{cc.x, cc.z + 2}, true);
    }

    return true;
}

bool World::getFurnaceState(int wx, int wy, int wz, FurnaceState &out) const {
    if (wy < 0 || wy >= voxel::Chunk::SY) {
        return false;
    }
    std::lock_guard<std::mutex> lock(chunksMutex_);
    const auto it = furnaceStates_.find(makeFurnaceKey(wx, wy, wz));
    if (it == furnaceStates_.end()) {
        return false;
    }
    out = it->second;
    return true;
}

void World::setFurnaceState(int wx, int wy, int wz, const FurnaceState &state) {
    if (wy < 0 || wy >= voxel::Chunk::SY) {
        return;
    }
    std::lock_guard<std::mutex> lock(chunksMutex_);
    furnaceStates_[makeFurnaceKey(wx, wy, wz)] = state;
}

void World::clearFurnaceState(int wx, int wy, int wz) {
    std::lock_guard<std::mutex> lock(chunksMutex_);
    furnaceStates_.erase(makeFurnaceKey(wx, wy, wz));
}

std::vector<glm::ivec3> World::loadedFurnacePositions() const {
    std::lock_guard<std::mutex> lock(chunksMutex_);
    std::vector<glm::ivec3> out;
    out.reserve(furnaceStates_.size());
    for (const auto &[k, st] : furnaceStates_) {
        (void)st;
        const ChunkCoord cc = worldToChunk(k.x, k.z);
        const auto cit = chunks_.find(cc);
        if (cit == chunks_.end() || !cit->second.chunk) {
            continue;
        }
        const int lx = floorMod(k.x, voxel::Chunk::SX);
        const int lz = floorMod(k.z, voxel::Chunk::SZ);
        if (!voxel::isFurnace(cit->second.chunk->get(lx, k.y, lz))) {
            continue;
        }
        out.emplace_back(k.x, k.y, k.z);
    }
    return out;
}

void World::workerLoop() {
    while (running_.load()) {
        auto maybeJob = workerJobs_.waitPopBest([this](const WorkerJob &a, const WorkerJob &b) {
            const ChunkCoord center =
                unpackChunkCoord(playerChunkPacked_.load(std::memory_order_relaxed));
            const int da = chunkDistance(a.coord, center);
            const int db = chunkDistance(b.coord, center);
            if (da != db) {
                return da < db;
            }
            // On equal distance, remesh is more visible than background loads.
            if (a.type != b.type) {
                return a.type == JobType::Remesh;
            }
            return false;
        });
        if (!maybeJob.has_value()) {
            break;
        }

        WorkerJob job = std::move(*maybeJob);

        if (job.type == JobType::LoadOrGenerate) {
            auto chunk = std::make_shared<voxel::Chunk>();
            std::vector<world::FurnaceRecordLocal> loadedFurnaces;
            if (!io_.load(*chunk, job.coord, &loadedFurnaces)) {
                gen_.fillChunk(*chunk, job.coord);
            } else if (!loadedFurnaces.empty()) {
                std::lock_guard<std::mutex> lock(chunksMutex_);
                for (const auto &rec : loadedFurnaces) {
                    const int wx = job.coord.x * voxel::Chunk::SX + static_cast<int>(rec.x);
                    const int wy = static_cast<int>(rec.y);
                    const int wz = job.coord.z * voxel::Chunk::SZ + static_cast<int>(rec.z);
                    furnaceStates_[makeFurnaceKey(wx, wy, wz)] = rec.state;
                }
            }
            // Defer mesh build to remesh jobs after chunk registration so
            // neighbor-aware edge culling happens before any faces are rendered.
            completed_.push(WorkerResult{job.coord, std::move(chunk), gfx::CpuMesh{}, true});
        } else {
            if (!job.chunkSnapshot) {
                continue;
            }
            const voxel::ChunkMesher::NeighborChunks neighbors{
                job.px.get(),   job.nx.get(),   job.pz.get(),   job.nz.get(),
                job.pxpz.get(), job.pxnz.get(), job.nxpz.get(), job.nxnz.get()};

            const gfx::CpuMesh mesh = voxel::ChunkMesher::buildFaceCulled(
                *job.chunkSnapshot, atlas_, blockRegistry_, glm::ivec2(job.coord.x, job.coord.z),
                neighbors, smoothLighting_.load(std::memory_order_relaxed));
            completed_.push(WorkerResult{job.coord, nullptr, mesh, false});
        }
    }
}

} // namespace world
