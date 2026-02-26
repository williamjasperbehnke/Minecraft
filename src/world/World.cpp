#include "world/World.hpp"

#include "app/SaveManager.hpp"
#include "voxel/ChunkMesher.hpp"

#include <glm/geometric.hpp>
#include <glm/mat4x4.hpp>
#include <glm/vec2.hpp>
#include <glm/vec4.hpp>

#include <algorithm>
#include <array>
#include <cerrno>
#include <cmath>
#include <cstdlib>
#include <limits>
#include <utility>
#include <vector>

namespace world {
namespace {

constexpr unsigned int kMinWorkerThreads = 2u;
constexpr unsigned int kDefaultMaxWorkerThreads = 12u;
constexpr int kMaxUnloadsPerUpdate = 2;
constexpr int kDefaultMaxUploadsPerFrame = 6;
constexpr int kWaterTicksPerStep = 4;
constexpr int kLavaTicksPerStep = 12;
constexpr int kWaterMaxFlowLevel = 7;
constexpr int kLavaMaxFlowLevel = 4;

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

std::int64_t packForwardXZ(const glm::vec3 &fwd) {
    glm::vec2 xz(fwd.x, fwd.z);
    const float len = glm::length(xz);
    if (len <= 1e-6f) {
        xz = glm::vec2(0.0f, -1.0f);
    } else {
        xz /= len;
    }
    const std::int32_t ix = static_cast<std::int32_t>(
        std::clamp(std::lround(xz.x * 32767.0f), -32767l, 32767l));
    const std::int32_t iz = static_cast<std::int32_t>(
        std::clamp(std::lround(xz.y * 32767.0f), -32767l, 32767l));
    return (static_cast<std::int64_t>(ix) << 32) |
           static_cast<std::uint32_t>(static_cast<std::int32_t>(iz));
}

glm::vec2 unpackForwardXZ(std::int64_t packed) {
    const std::int32_t ix = static_cast<std::int32_t>(packed >> 32);
    const std::int32_t iz = static_cast<std::int32_t>(packed & 0xFFFFFFFF);
    glm::vec2 xz(static_cast<float>(ix) / 32767.0f, static_cast<float>(iz) / 32767.0f);
    const float len = glm::length(xz);
    if (len <= 1e-6f) {
        return glm::vec2(0.0f, -1.0f);
    }
    return xz / len;
}

World::FurnaceCoordKey makeFurnaceKey(int x, int y, int z) {
    return World::FurnaceCoordKey{x, y, z};
}

bool isWaterBlock(voxel::BlockId id) {
    return id == voxel::WATER || id == voxel::WATER_SOURCE;
}

bool isLavaBlock(voxel::BlockId id) {
    return id == voxel::LAVA || id == voxel::LAVA_SOURCE;
}

bool isSameFluidBlock(voxel::BlockId fluidId, voxel::BlockId id) {
    if (fluidId == voxel::WATER) {
        return isWaterBlock(id);
    }
    if (fluidId == voxel::LAVA) {
        return isLavaBlock(id);
    }
    return id == fluidId;
}

bool chunkHasFluidLikeBlock(const voxel::Chunk *chunk) {
    if (chunk == nullptr) {
        return false;
    }
    for (int y = 0; y < voxel::Chunk::SY; ++y) {
        for (int z = 0; z < voxel::Chunk::SZ; ++z) {
            for (int x = 0; x < voxel::Chunk::SX; ++x) {
                const voxel::BlockId id = chunk->getUnchecked(x, y, z);
                if (voxel::isWaterLike(id) || voxel::isLavaLike(id)) {
                    return true;
                }
            }
        }
    }
    return false;
}

struct FrustumPlane {
    glm::vec3 n{0.0f};
    float d = 0.0f;
};

struct FrustumPlanes {
    std::array<FrustumPlane, 6> p{};
};

glm::vec4 row(const glm::mat4 &m, int r) {
    return {m[0][r], m[1][r], m[2][r], m[3][r]};
}

FrustumPlane normalizePlane(const glm::vec4 &eq) {
    const glm::vec3 n(eq.x, eq.y, eq.z);
    const float len = glm::length(n);
    if (len <= 1e-6f) {
        return FrustumPlane{};
    }
    return FrustumPlane{n / len, eq.w / len};
}

FrustumPlanes extractFrustumPlanes(const glm::mat4 &viewProj) {
    const glm::vec4 r0 = row(viewProj, 0);
    const glm::vec4 r1 = row(viewProj, 1);
    const glm::vec4 r2 = row(viewProj, 2);
    const glm::vec4 r3 = row(viewProj, 3);
    FrustumPlanes out;
    out.p[0] = normalizePlane(r3 + r0); // left
    out.p[1] = normalizePlane(r3 - r0); // right
    out.p[2] = normalizePlane(r3 + r1); // bottom
    out.p[3] = normalizePlane(r3 - r1); // top
    out.p[4] = normalizePlane(r3 + r2); // near
    out.p[5] = normalizePlane(r3 - r2); // far
    return out;
}

bool sphereInFrustum(const FrustumPlanes &frustum, const glm::vec3 &center, float radius) {
    for (const FrustumPlane &pl : frustum.p) {
        const float dist = glm::dot(pl.n, center) + pl.d;
        if (dist < -radius) {
            return false;
        }
    }
    return true;
}

int readEnvInt(const char *name, int fallback, int minValue, int maxValue) {
    const char *raw = std::getenv(name);
    if (raw == nullptr || raw[0] == '\0') {
        return fallback;
    }
    char *end = nullptr;
    errno = 0;
    const long parsed = std::strtol(raw, &end, 10);
    if (errno != 0 || end == raw || (end != nullptr && *end != '\0')) {
        return fallback;
    }
    const long clamped = std::clamp(parsed, static_cast<long>(minValue), static_cast<long>(maxValue));
    return static_cast<int>(clamped);
}

unsigned int configuredMaxWorkerThreads() {
    return static_cast<unsigned int>(readEnvInt("VOXEL_MAX_WORKER_THREADS",
                                                static_cast<int>(kDefaultMaxWorkerThreads), 1, 64));
}

int configuredMaxUploadsPerFrame() {
    return readEnvInt("VOXEL_MAX_UPLOADS_PER_FRAME", kDefaultMaxUploadsPerFrame, 1, 64);
}

} // namespace

World::World(const gfx::TextureAtlas &atlas, std::filesystem::path saveRoot, std::uint32_t seed)
    : atlas_(atlas), gen_(seed), saveRoot_(std::move(saveRoot)) {
    const unsigned int hardwareThreads = std::max(1u, std::thread::hardware_concurrency());
    const unsigned int maxWorkers = std::max(kMinWorkerThreads, configuredMaxWorkerThreads());
    const unsigned int workerCount =
        std::clamp(hardwareThreads > 1 ? hardwareThreads - 1 : 1u, kMinWorkerThreads, maxWorkers);
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
            const auto fit = furnaceStateByChunk_.find(coord);
            if (fit != furnaceStateByChunk_.end()) {
                for (const FurnaceCoordKey &fKey : fit->second) {
                    const auto stIt = furnaceStates_.find(fKey);
                    if (stIt == furnaceStates_.end()) {
                        continue;
                    }
                    if (fKey.y < 0 || fKey.y >= voxel::Chunk::SY) {
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
                    rec.state = stIt->second;
                    localFurnaces.push_back(rec);
                }
            }
            app::SaveManager::saveChunk(saveRoot_, WorldGen::kGeneratorVersion, *entry.chunk, coord,
                                        &localFurnaces);
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

voxel::BlockId World::getBlockLoadedLocked(int wx, int wy, int wz) const {
    if (wy < 0 || wy >= voxel::Chunk::SY) {
        return voxel::AIR;
    }
    const ChunkCoord cc = worldToChunk(wx, wz);
    const auto it = chunks_.find(cc);
    if (it == chunks_.end() || !it->second.chunk) {
        return voxel::AIR;
    }
    const int lx = floorMod(wx, voxel::Chunk::SX);
    const int lz = floorMod(wz, voxel::Chunk::SZ);
    return it->second.chunk->get(lx, wy, lz);
}

void World::enqueueFluidCellLocked(int wx, int wy, int wz) {
    if (wy < 0 || wy >= voxel::Chunk::SY) {
        return;
    }
    const voxel::BlockId id = getBlockLoadedLocked(wx, wy, wz);
    if (isWaterBlock(id)) {
        const FluidCoord c{wx, wy, wz};
        if (waterState_.find(c) == waterState_.end()) {
            activateFluidCellLocked(wx, wy, wz);
            if (waterState_.find(c) == waterState_.end()) {
                return;
            }
        }
        if (waterQueued_.insert(c).second) {
            waterFrontier_.push_back(c);
        }
        return;
    }
    if (isLavaBlock(id)) {
        const FluidCoord c{wx, wy, wz};
        if (lavaState_.find(c) == lavaState_.end()) {
            activateFluidCellLocked(wx, wy, wz);
            if (lavaState_.find(c) == lavaState_.end()) {
                return;
            }
        }
        if (lavaQueued_.insert(c).second) {
            lavaFrontier_.push_back(c);
        }
    }
}

void World::activateFluidCellLocked(int wx, int wy, int wz) {
    if (wy < 0 || wy >= voxel::Chunk::SY) {
        return;
    }
    const voxel::BlockId id = getBlockLoadedLocked(wx, wy, wz);
    const bool water = isWaterBlock(id);
    const bool lava = isLavaBlock(id);
    if (!water && !lava) {
        return;
    }
    const FluidCoord c{wx, wy, wz};
    auto &stateMap = water ? waterState_ : lavaState_;
    const auto it = stateMap.find(c);
    if (it != stateMap.end()) {
        return;
    }
    const int maxLevel = water ? kWaterMaxFlowLevel : kLavaMaxFlowLevel;
    const bool hasAbove = water ? isWaterBlock(getBlockLoadedLocked(wx, wy + 1, wz))
                                : isLavaBlock(getBlockLoadedLocked(wx, wy + 1, wz));
    const bool source = water ? (id == voxel::WATER_SOURCE || voxel::isWaterloggedPlant(id))
                              : (id == voxel::LAVA_SOURCE);
    // Do not infer sources from neighborhood shape. Source status should come
    // from explicit stored state (e.g., placed source blocks).
    const std::uint8_t lvl = static_cast<std::uint8_t>(source ? 0 : (hasAbove ? 0 : maxLevel));
    setFluidStateLocked(water ? voxel::WATER : voxel::LAVA, wx, wy, wz, lvl, source);
}

void World::enqueueFluidNeighborsLocked(int wx, int wy, int wz) {
    enqueueFluidCellLocked(wx, wy, wz);
    enqueueFluidCellLocked(wx, wy - 1, wz);
    enqueueFluidCellLocked(wx, wy + 1, wz);
    enqueueFluidCellLocked(wx + 1, wy, wz);
    enqueueFluidCellLocked(wx - 1, wy, wz);
    enqueueFluidCellLocked(wx, wy, wz + 1);
    enqueueFluidCellLocked(wx, wy, wz - 1);
}

int World::fluidLevelAtLocked(voxel::BlockId fluidId, int wx, int wy, int wz) const {
    const FluidCoord c{wx, wy, wz};
    const auto &stateMap = (fluidId == voxel::LAVA) ? lavaState_ : waterState_;
    const auto it = stateMap.find(c);
    if (it != stateMap.end()) {
        return static_cast<int>(it->second.level);
    }
    if (fluidId == voxel::WATER) {
        const voxel::BlockId id = getBlockLoadedLocked(wx, wy, wz);
        if (id == voxel::WATER_SOURCE || voxel::isWaterloggedPlant(id)) {
            return 0;
        }
    } else if (fluidId == voxel::LAVA) {
        const voxel::BlockId id = getBlockLoadedLocked(wx, wy, wz);
        if (id == voxel::LAVA_SOURCE) {
            return 0;
        }
    }
    // Unknown state should not act as an infinite feed source in simulation.
    const int maxLevel = (fluidId == voxel::WATER) ? kWaterMaxFlowLevel : kLavaMaxFlowLevel;
    return maxLevel;
}

void World::setFluidStateLocked(voxel::BlockId fluidId, int wx, int wy, int wz, std::uint8_t level,
                                bool source) {
    const FluidCoord c{wx, wy, wz};
    auto &stateMap = (fluidId == voxel::LAVA) ? lavaState_ : waterState_;
    auto &byChunk = (fluidId == voxel::LAVA) ? lavaStateByChunk_ : waterStateByChunk_;
    stateMap[c] = FluidState{level, source};
    byChunk[worldToChunk(wx, wz)].insert(c);
}

void World::clearFluidStateLocked(int wx, int wy, int wz) {
    const FluidCoord c{wx, wy, wz};
    const ChunkCoord cc = worldToChunk(wx, wz);
    if (waterState_.erase(c) > 0) {
        waterQueued_.erase(c);
        const auto byChunkIt = waterStateByChunk_.find(cc);
        if (byChunkIt != waterStateByChunk_.end()) {
            byChunkIt->second.erase(c);
            if (byChunkIt->second.empty()) {
                waterStateByChunk_.erase(byChunkIt);
            }
        }
    }
    if (lavaState_.erase(c) > 0) {
        lavaQueued_.erase(c);
        const auto byChunkIt = lavaStateByChunk_.find(cc);
        if (byChunkIt != lavaStateByChunk_.end()) {
            byChunkIt->second.erase(c);
            if (byChunkIt->second.empty()) {
                lavaStateByChunk_.erase(byChunkIt);
            }
        }
    }
}

void World::seedFluidFrontierForChunkLocked(ChunkCoord cc, const voxel::Chunk &chunk) {
    const int baseX = cc.x * voxel::Chunk::SX;
    const int baseZ = cc.z * voxel::Chunk::SZ;
    for (int lx = 0; lx < voxel::Chunk::SX; ++lx) {
        for (int lz = 0; lz < voxel::Chunk::SZ; ++lz) {
            for (int y = 0; y < voxel::Chunk::SY; ++y) {
                const voxel::BlockId id = chunk.get(lx, y, lz);
                if (!isLavaBlock(id)) {
                    continue;
                }
                const int wx = baseX + lx;
                const int wz = baseZ + lz;
                const bool source = (id == voxel::LAVA_SOURCE);
                const std::uint8_t inferred = static_cast<std::uint8_t>(
                    source ? 0 : kLavaMaxFlowLevel);
                setFluidStateLocked(voxel::LAVA, wx, y, wz, inferred, source);
                const bool exposed = (getBlockLoadedLocked(wx, y - 1, wz) == voxel::AIR) ||
                                     (getBlockLoadedLocked(wx + 1, y, wz) == voxel::AIR) ||
                                     (getBlockLoadedLocked(wx - 1, y, wz) == voxel::AIR) ||
                                     (getBlockLoadedLocked(wx, y, wz + 1) == voxel::AIR) ||
                                     (getBlockLoadedLocked(wx, y, wz - 1) == voxel::AIR);
                if (exposed) {
                    enqueueFluidCellLocked(wx, y, wz);
                }
            }
        }
    }
}

void World::appendFluidRemeshNeighborhoodLocked(
    ChunkCoord cc, bool waterLike, std::unordered_set<ChunkCoord, ChunkCoordHash> &out) const {
    out.insert(cc);
    out.insert(ChunkCoord{cc.x + 1, cc.z});
    out.insert(ChunkCoord{cc.x - 1, cc.z});
    out.insert(ChunkCoord{cc.x, cc.z + 1});
    out.insert(ChunkCoord{cc.x, cc.z - 1});
    out.insert(ChunkCoord{cc.x + 1, cc.z + 1});
    out.insert(ChunkCoord{cc.x + 1, cc.z - 1});
    out.insert(ChunkCoord{cc.x - 1, cc.z + 1});
    out.insert(ChunkCoord{cc.x - 1, cc.z - 1});
    if (waterLike) {
        out.insert(ChunkCoord{cc.x + 2, cc.z});
        out.insert(ChunkCoord{cc.x - 2, cc.z});
        out.insert(ChunkCoord{cc.x, cc.z + 2});
        out.insert(ChunkCoord{cc.x, cc.z - 2});
        out.insert(ChunkCoord{cc.x + 2, cc.z + 2});
        out.insert(ChunkCoord{cc.x + 2, cc.z - 2});
        out.insert(ChunkCoord{cc.x - 2, cc.z + 2});
        out.insert(ChunkCoord{cc.x - 2, cc.z - 2});
    }
}

void World::processFluidFrontierLocked(
    voxel::BlockId fluidId, std::deque<FluidCoord> &frontier,
    std::unordered_set<FluidCoord, FluidCoordHash> &queued,
    std::unordered_set<ChunkCoord, ChunkCoordHash> &remeshChunks) {
    static constexpr std::array<glm::ivec2, 4> kDirs = {
        glm::ivec2{1, 0}, glm::ivec2{-1, 0}, glm::ivec2{0, 1}, glm::ivec2{0, -1}};
    static constexpr std::array<glm::ivec3, 6> kDirs3 = {glm::ivec3{1, 0, 0}, glm::ivec3{-1, 0, 0},
                                                         glm::ivec3{0, 1, 0}, glm::ivec3{0, -1, 0},
                                                         glm::ivec3{0, 0, 1}, glm::ivec3{0, 0, -1}};
    const bool waterLikeFluid = (fluidId == voxel::WATER);
    const int maxFlowLevel = (fluidId == voxel::WATER) ? kWaterMaxFlowLevel : kLavaMaxFlowLevel;
    auto isReplaceableForFluid = [fluidId](voxel::BlockId id) {
        if (fluidId == voxel::WATER && voxel::isWaterloggedPlant(id)) {
            // Waterlogged plants co-exist with water and should not be replaced by flow.
            return false;
        }
        if (id == voxel::AIR || voxel::isPlant(id) || voxel::isTorch(id)) {
            return true;
        }
        return isSameFluidBlock(fluidId, id);
    };
    auto queueFluidReplacementDrop = [this](voxel::BlockId replaced, int wx, int wy, int wz) {
        if (!(voxel::isPlant(replaced) || voxel::isTorch(replaced))) {
            return;
        }
        const voxel::BlockId dropId = voxel::isTorch(replaced) ? voxel::TORCH : replaced;
        const float dropY = voxel::isPlant(replaced) ? 0.02f : 0.20f;
        pendingFluidDrops_.push_back(
            FluidDrop{dropId, 1, glm::vec3(wx, wy, wz) + glm::vec3(0.5f, dropY, 0.5f)});
    };
    auto lavaTouchesWater = [this](int wx, int wy, int wz) -> bool {
        for (const glm::ivec3 d : kDirs3) {
            if (isWaterBlock(getBlockLoadedLocked(wx + d.x, wy + d.y, wz + d.z))) {
                return true;
            }
        }
        return false;
    };
    auto solidifyLavaAt = [this, &remeshChunks, &lavaTouchesWater](int wx, int wy, int wz) -> bool {
        if (wy < 0 || wy >= voxel::Chunk::SY) {
            return false;
        }
        if (!isLavaBlock(getBlockLoadedLocked(wx, wy, wz)) || !lavaTouchesWater(wx, wy, wz)) {
            return false;
        }
        const ChunkCoord cc = worldToChunk(wx, wz);
        auto it = chunks_.find(cc);
        if (it == chunks_.end() || !it->second.chunk) {
            return false;
        }
        const int lx = floorMod(wx, voxel::Chunk::SX);
        const int lz = floorMod(wz, voxel::Chunk::SZ);
        it->second.chunk->set(lx, wy, lz, voxel::BASALT);
        clearFluidStateLocked(wx, wy, wz);
        appendFluidRemeshNeighborhoodLocked(cc, true, remeshChunks);
        enqueueFluidNeighborsLocked(wx, wy, wz);
        return true;
    };
    int processed = 0;
    const int frontierAtTickStart = static_cast<int>(frontier.size());
    const int maxProcessThisTick = frontierAtTickStart;
    while (processed < maxProcessThisTick && !frontier.empty()) {
        const FluidCoord cell = frontier.front();
        frontier.pop_front();
        queued.erase(cell);
        ++processed;

        const voxel::BlockId id = getBlockLoadedLocked(cell.x, cell.y, cell.z);
        if (!isSameFluidBlock(fluidId, id)) {
            clearFluidStateLocked(cell.x, cell.y, cell.z);
            continue;
        }
        if (fluidId == voxel::LAVA && solidifyLavaAt(cell.x, cell.y, cell.z)) {
            continue;
        }
        if (fluidId == voxel::WATER) {
            for (const glm::ivec3 d : kDirs3) {
                solidifyLavaAt(cell.x + d.x, cell.y + d.y, cell.z + d.z);
            }
        }

        const int wy = cell.y;
        FluidCoord key{cell.x, wy, cell.z};
        auto &stateMap = (fluidId == voxel::WATER) ? waterState_ : lavaState_;
        auto stIt = stateMap.find(key);
        FluidState st = (stIt != stateMap.end()) ? stIt->second : FluidState{};
        const bool source = (fluidId == voxel::WATER)
                                ? (id == voxel::WATER_SOURCE || voxel::isWaterloggedPlant(id))
                                : (id == voxel::LAVA_SOURCE);
        const int prevLevel = static_cast<int>(st.level);

        // Recompute own level from feeding neighbors (or source) so disconnected flow retracts.
        int nextLevel = 0;
        if (!source) {
            nextLevel = maxFlowLevel + 1;
            if (isSameFluidBlock(fluidId, getBlockLoadedLocked(cell.x, wy + 1, cell.z))) {
                nextLevel = 0;
            } else {
                for (const glm::ivec2 d : kDirs) {
                    const int nx = cell.x + d.x;
                    const int nz = cell.z + d.y;
                    if (!isSameFluidBlock(fluidId, getBlockLoadedLocked(nx, wy, nz))) {
                        continue;
                    }
                    nextLevel = std::min(nextLevel, fluidLevelAtLocked(fluidId, nx, wy, nz) + 1);
                }
            }
            if (nextLevel > maxFlowLevel) {
                const ChunkCoord cc = worldToChunk(cell.x, cell.z);
                auto it = chunks_.find(cc);
                if (it != chunks_.end() && it->second.chunk) {
                    const int lx = floorMod(cell.x, voxel::Chunk::SX);
                    const int lz = floorMod(cell.z, voxel::Chunk::SZ);
                    it->second.chunk->set(lx, wy, lz, voxel::AIR);
                    clearFluidStateLocked(cell.x, wy, cell.z);
                    appendFluidRemeshNeighborhoodLocked(cc, waterLikeFluid, remeshChunks);
                    enqueueFluidNeighborsLocked(cell.x, wy, cell.z);
                }
                continue;
            }
            st.level = static_cast<std::uint8_t>(nextLevel);
            st.source = false;
            stateMap[key] = st;
            if (nextLevel != prevLevel) {
                const ChunkCoord cc = worldToChunk(cell.x, cell.z);
                appendFluidRemeshNeighborhoodLocked(cc, waterLikeFluid, remeshChunks);
                enqueueFluidNeighborsLocked(cell.x, wy, cell.z);
            }
        }

        bool changed = false;
        const int currLevel = source ? 0 : static_cast<int>(st.level);
        const bool canFlowLaterally = (wy == 0) ? true : !isReplaceableForFluid(getBlockLoadedLocked(cell.x, wy - 1, cell.z));

        // Gravity-first: place fluid below as strongest flow.
        if (wy > 0) {
            const voxel::BlockId below = getBlockLoadedLocked(cell.x, wy - 1, cell.z);
            if (isReplaceableForFluid(below) && !isSameFluidBlock(fluidId, below)) {
                const ChunkCoord downCc = worldToChunk(cell.x, cell.z);
                auto downIt = chunks_.find(downCc);
                if (downIt != chunks_.end() && downIt->second.chunk) {
                    queueFluidReplacementDrop(below, cell.x, wy - 1, cell.z);
                    const int lx = floorMod(cell.x, voxel::Chunk::SX);
                    const int lz = floorMod(cell.z, voxel::Chunk::SZ);
                    downIt->second.chunk->set(lx, wy - 1, lz, fluidId);
                    setFluidStateLocked(fluidId, cell.x, wy - 1, cell.z, 0, false);
                    appendFluidRemeshNeighborhoodLocked(downCc, waterLikeFluid, remeshChunks);
                    enqueueFluidNeighborsLocked(cell.x, wy - 1, cell.z);
                    changed = true;
                }
            }
        }

        const int dirSeed = (cell.x * 73428767) ^ (cell.y * 912931) ^ (cell.z * 19349663);
        const int start = std::abs(dirSeed) & 3;
        const int lateralLimit = (fluidId == voxel::LAVA) ? 1 : 2;
        const int outLevel = currLevel + 1;
        int lateralPlaced = 0;
        if (canFlowLaterally && outLevel <= maxFlowLevel) {
            for (int i = 0; i < 4 && lateralPlaced < lateralLimit; ++i) {
                const glm::ivec2 d = kDirs[(start + i) & 3];
                const int nx = cell.x + d.x;
                const int nz = cell.z + d.y;
                const voxel::BlockId nId = getBlockLoadedLocked(nx, wy, nz);
                if (!isReplaceableForFluid(nId)) {
                    continue;
                }
                if (isSameFluidBlock(fluidId, nId)) {
                    const FluidCoord nk{nx, wy, nz};
                    const auto nitState = stateMap.find(nk);
                    const bool nSource = (fluidId == voxel::WATER)
                                             ? (nId == voxel::WATER_SOURCE || voxel::isWaterloggedPlant(nId))
                                             : ((nitState != stateMap.end()) ? nitState->second.source : false);
                    const int nLevel = fluidLevelAtLocked(fluidId, nx, wy, nz);
                    if (nSource || nLevel <= outLevel) {
                        continue;
                    }
                }
                const ChunkCoord ncc = worldToChunk(nx, nz);
                auto nit = chunks_.find(ncc);
                if (nit == chunks_.end() || !nit->second.chunk) {
                    continue;
                }
                queueFluidReplacementDrop(nId, nx, wy, nz);
                const int lx = floorMod(nx, voxel::Chunk::SX);
                const int lz = floorMod(nz, voxel::Chunk::SZ);
                nit->second.chunk->set(lx, wy, lz, fluidId);
                setFluidStateLocked(fluidId, nx, wy, nz, static_cast<std::uint8_t>(outLevel),
                                    false);
                appendFluidRemeshNeighborhoodLocked(ncc, waterLikeFluid, remeshChunks);
                enqueueFluidNeighborsLocked(nx, wy, nz);
                ++lateralPlaced;
                changed = true;
            }
        }

        if (changed) {
            enqueueFluidNeighborsLocked(cell.x, wy, cell.z);
        }
    }
}

std::vector<World::FluidDrop> World::consumeFluidDrops() {
    std::lock_guard<std::mutex> lock(chunksMutex_);
    auto out = std::move(pendingFluidDrops_);
    pendingFluidDrops_.clear();
    return out;
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

void World::enqueueRemesh(ChunkCoord cc, bool force, bool urgent) {
    auto pendingIt = pendingRemesh_.find(cc);
    if (pendingIt != pendingRemesh_.end()) {
        if (force) {
            pendingRemeshDirty_.insert(cc);
        }
        return;
    }
    pendingRemesh_.insert(cc);
    pendingRemeshDirty_.erase(cc);

    auto it = chunks_.find(cc);
    if (it == chunks_.end() || !it->second.chunk) {
        pendingRemesh_.erase(cc);
        pendingRemeshDirty_.erase(cc);
        return;
    }

    WorkerJob job;
    job.type = JobType::Remesh;
    job.coord = cc;
    job.urgent = urgent;
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

    const int minX = (cc.x - 1) * voxel::Chunk::SX;
    const int maxX = (cc.x + 2) * voxel::Chunk::SX - 1;
    const int minZ = (cc.z - 1) * voxel::Chunk::SZ;
    const int maxZ = (cc.z + 2) * voxel::Chunk::SZ - 1;
    const bool neighborhoodHasFluid =
        chunkHasFluidLikeBlock(job.chunkSnapshot.get()) || chunkHasFluidLikeBlock(job.px.get()) ||
        chunkHasFluidLikeBlock(job.nx.get()) || chunkHasFluidLikeBlock(job.pz.get()) ||
        chunkHasFluidLikeBlock(job.nz.get()) || chunkHasFluidLikeBlock(job.pxpz.get()) ||
        chunkHasFluidLikeBlock(job.pxnz.get()) || chunkHasFluidLikeBlock(job.nxpz.get()) ||
        chunkHasFluidLikeBlock(job.nxnz.get());
    if (neighborhoodHasFluid) {
        for (const auto &[fc, fs] : waterState_) {
            if (fc.x < minX || fc.x > maxX || fc.z < minZ || fc.z > maxZ) {
                continue;
            }
            job.waterLevels.emplace(fc, fs.level);
        }
        for (const auto &[fc, fs] : lavaState_) {
            if (fc.x < minX || fc.x > maxX || fc.z < minZ || fc.z > maxZ) {
                continue;
            }
            job.lavaLevels.emplace(fc, fs.level);
        }
    }

    scheduleWorkerJob(std::move(job));
}

void World::scheduleWorkerJob(WorkerJob job) {
    workerJobs_.push(std::move(job));
}

std::unique_ptr<gfx::CpuMesh> World::acquireMeshBuffer() {
    std::lock_guard<std::mutex> lock(meshBufferPoolMutex_);
    if (meshBufferPool_.empty()) {
        return std::make_unique<gfx::CpuMesh>();
    }
    auto mesh = std::move(meshBufferPool_.back());
    meshBufferPool_.pop_back();
    return mesh;
}

void World::recycleMeshBuffer(std::unique_ptr<gfx::CpuMesh> mesh) {
    if (!mesh) {
        return;
    }
    mesh->vertices.clear();
    mesh->indices.clear();
    std::lock_guard<std::mutex> lock(meshBufferPoolMutex_);
    constexpr std::size_t kMaxPooledMeshes = 64;
    if (meshBufferPool_.size() >= kMaxPooledMeshes) {
        return;
    }
    meshBufferPool_.push_back(std::move(mesh));
}

void World::setStreamingRadii(int loadRadius, int unloadRadius) {
    std::lock_guard<std::mutex> lock(chunksMutex_);
    loadRadius_ = std::clamp(loadRadius, 2, 64);
    unloadRadius_ = std::clamp(std::max(unloadRadius, loadRadius_ + 1), 3, 72);
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

void World::updateStream(const glm::vec3 &playerPos, const glm::vec3 &cameraForward) {
    const int pChunkX = floorDiv(static_cast<int>(std::floor(playerPos.x)), voxel::Chunk::SX);
    const int pChunkZ = floorDiv(static_cast<int>(std::floor(playerPos.z)), voxel::Chunk::SZ);
    const std::int64_t packed = packChunkCoord(pChunkX, pChunkZ);
    const std::int64_t forwardPacked = packForwardXZ(cameraForward);
    playerChunkPacked_.store(packed, std::memory_order_relaxed);
    const bool streamDirty = streamDirty_.load(std::memory_order_relaxed);
    const std::int64_t lastPacked = lastStreamChunkPacked_.load(std::memory_order_relaxed);
    const std::int64_t lastForwardPacked =
        lastStreamForwardPacked_.load(std::memory_order_relaxed);
    if (!streamDirty && lastPacked == packed && lastForwardPacked == forwardPacked) {
        return;
    }
    lastStreamChunkPacked_.store(packed, std::memory_order_relaxed);
    lastStreamForwardPacked_.store(forwardPacked, std::memory_order_relaxed);
    streamDirty_.store(false, std::memory_order_relaxed);

    std::lock_guard<std::mutex> lock(chunksMutex_);

    for (int dz = -loadRadius_; dz <= loadRadius_; ++dz) {
        for (int dx = -loadRadius_; dx <= loadRadius_; ++dx) {
            const ChunkCoord cc{pChunkX + dx, pChunkZ + dz};
            enqueueLoadIfNeeded(cc);
        }
    }

    const glm::vec2 viewDir = unpackForwardXZ(forwardPacked);
    // Ensure loaded chunks without mesh are meshed when potentially visible.
    // Keep a small safety ring around player always meshed to avoid near pop-in.
    for (const auto &[cc, entry] : chunks_) {
        if (!entry.chunk || entry.mesh) {
            continue;
        }
        const int ddx = cc.x - pChunkX;
        const int ddz = cc.z - pChunkZ;
        const int dist = std::max(std::abs(ddx), std::abs(ddz));
        if (dist > 2) {
            const glm::vec2 toChunk(static_cast<float>(ddx), static_cast<float>(ddz));
            const float len = glm::length(toChunk);
            if (len > 1e-3f) {
                const float facing = glm::dot(toChunk / len, viewDir);
                // Rough 140-degree forward cone for meshing.
                if (facing < -0.18f) {
                    continue;
                }
            }
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
            const auto fit = furnaceStateByChunk_.find(cc);
            if (fit != furnaceStateByChunk_.end()) {
                for (const FurnaceCoordKey &fKey : fit->second) {
                    const auto stIt = furnaceStates_.find(fKey);
                    if (stIt == furnaceStates_.end()) {
                        continue;
                    }
                    if (fKey.y < 0 || fKey.y >= voxel::Chunk::SY) {
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
                    rec.state = stIt->second;
                    localFurnaces.push_back(rec);
                }
            }
            app::SaveManager::saveChunk(saveRoot_, WorldGen::kGeneratorVersion, *it->second.chunk,
                                        cc, &localFurnaces);
            if (fit != furnaceStateByChunk_.end()) {
                for (const FurnaceCoordKey &fKey : fit->second) {
                    furnaceStates_.erase(fKey);
                }
                furnaceStateByChunk_.erase(fit);
            }
        }
        chunks_.erase(it);
        worldRevision_.fetch_add(1, std::memory_order_relaxed);
        transparentCacheValid_ = false;
        pendingLoad_.erase(cc);
        pendingRemesh_.erase(cc);
        pendingRemeshDirty_.erase(cc);
        auto eraseFluidChunkState = [cc](auto &stateMap, auto &stateByChunk, auto &queued) {
            const auto bit = stateByChunk.find(cc);
            if (bit == stateByChunk.end()) {
                return;
            }
            for (const auto &fc : bit->second) {
                stateMap.erase(fc);
                queued.erase(fc);
            }
            stateByChunk.erase(bit);
        };
        eraseFluidChunkState(waterState_, waterStateByChunk_, waterQueued_);
        eraseFluidChunkState(lavaState_, lavaStateByChunk_, lavaQueued_);
        // Unloading a chunk exposes border faces on neighboring chunks.
        enqueueNeighborRingRemesh(cc);
        ++unloadsProcessed;
    }
    if (unloadsProcessed < static_cast<int>(toUnload.size())) {
        // Continue streaming work next frame even if player remains in the same chunk.
        streamDirty_.store(true, std::memory_order_relaxed);
    }
}

void World::updateFluidSimulation(float dt) {
    std::lock_guard<std::mutex> lock(chunksMutex_);
    const std::uint64_t prevTicks = fluidTicks_.tickCount();
    const int ticks = fluidTicks_.consume(dt);
    if (ticks <= 0) {
        return;
    }

    std::unordered_set<ChunkCoord, ChunkCoordHash> remeshChunks;
    for (int i = 0; i < ticks; ++i) {
        const std::uint64_t tick = prevTicks + static_cast<std::uint64_t>(i + 1);
        if ((tick % kWaterTicksPerStep) == 0) {
            processFluidFrontierLocked(voxel::WATER, waterFrontier_, waterQueued_, remeshChunks);
        }
        if ((tick % kLavaTicksPerStep) == 0) {
            processFluidFrontierLocked(voxel::LAVA, lavaFrontier_, lavaQueued_, remeshChunks);
        }
    }
    for (const ChunkCoord cc : remeshChunks) {
        // Fluid levels can change rapidly; force replacement of older queued
        // remesh jobs so mesh snapshots stay in sync.
        enqueueRemesh(cc, true, true);
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
    const int maxUploadsPerFrame = configuredMaxUploadsPerFrame();
    WorkerResult result;
    while (uploadsThisFrame < maxUploadsPerFrame &&
           completed_.tryPopBest(result, [this](const WorkerResult &a, const WorkerResult &b) {
               if (a.urgent != b.urgent) {
                   return a.urgent;
               }
               const ChunkCoord center =
                   unpackChunkCoord(playerChunkPacked_.load(std::memory_order_relaxed));
               const int da = chunkDistance(a.coord, center);
               const int db = chunkDistance(b.coord, center);
               if (da != db) {
                   return da < db;
               }
               if (a.replaceChunk != b.replaceChunk) {
                   return a.replaceChunk;
               }
               return false;
           })) {
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
                pendingRemeshDirty_.erase(result.coord);
                recycleMeshBuffer(std::move(result.mesh));
                continue;
            }
        }

        auto &entry = chunks_[result.coord];
        if (result.replaceChunk) {
            entry.chunk = std::move(result.chunk);
            pendingLoad_.erase(result.coord);
            if (entry.chunk) {
                seedFluidFrontierForChunkLocked(result.coord, *entry.chunk);
            }
            // Build this chunk mesh only after load completes in the normal remesh
            // path, so boundary face culling can use available neighbors.
            enqueueRemesh(result.coord, true);
            // New chunk may occlude neighbor border faces.
            enqueueNeighborRingRemesh(result.coord);
            worldRevision_.fetch_add(1, std::memory_order_relaxed);
            transparentCacheValid_ = false;
            // Don't spend mesh-upload budget on load completion records.
            recycleMeshBuffer(std::move(result.mesh));
            continue;
        } else {
            pendingRemesh_.erase(result.coord);
        }
        const bool needsAnotherRemesh = (pendingRemeshDirty_.erase(result.coord) > 0);
        if (!result.mesh) {
            continue;
        }

        if (!entry.mesh) {
            entry.mesh = std::make_unique<gfx::ChunkMesh>();
        }
        entry.mesh->upload(*result.mesh);
        entry.triangleCount = static_cast<int>(result.mesh->indices.size() / 3);
        worldRevision_.fetch_add(1, std::memory_order_relaxed);
        transparentCacheValid_ = false;
        if (needsAnotherRemesh) {
            enqueueRemesh(result.coord, false, result.urgent);
        }
        recycleMeshBuffer(std::move(result.mesh));
        ++uploadsThisFrame;
    }
}

void World::draw(const glm::vec3 &cameraPos, float maxDrawDistance, const glm::mat4 &viewProj) const {
    std::lock_guard<std::mutex> lock(chunksMutex_);
    const float maxDist = std::max(maxDrawDistance, static_cast<float>(voxel::Chunk::SX));
    const float maxDist2 = maxDist * maxDist;
    const FrustumPlanes frustum = extractFrustumPlanes(viewProj);
    const float chunkRadius = 0.5f * std::sqrt(static_cast<float>(voxel::Chunk::SX * voxel::Chunk::SX +
                                                                   voxel::Chunk::SY * voxel::Chunk::SY +
                                                                   voxel::Chunk::SZ * voxel::Chunk::SZ));
    for (const auto &[coord, entry] : chunks_) {
        if (!entry.mesh) {
            continue;
        }
        const float cx = static_cast<float>(coord.x * voxel::Chunk::SX + voxel::Chunk::SX / 2);
        const float cy = static_cast<float>(voxel::Chunk::SY) * 0.5f;
        const float cz = static_cast<float>(coord.z * voxel::Chunk::SZ + voxel::Chunk::SZ / 2);
        const float dx = cx - cameraPos.x;
        const float dz = cz - cameraPos.z;
        if ((dx * dx + dz * dz) > maxDist2) {
            continue;
        }
        if (!sphereInFrustum(frustum, glm::vec3(cx, cy, cz), chunkRadius)) {
            continue;
        }
        entry.mesh->draw();
    }
}

void World::drawTransparent(const glm::vec3 &cameraPos, const glm::vec3 &cameraForward,
                            float maxDrawDistance, const glm::mat4 &viewProj) const {
    std::lock_guard<std::mutex> lock(chunksMutex_);
    const glm::vec3 fwd = (std::abs(cameraForward.x) < 1e-6f && std::abs(cameraForward.y) < 1e-6f &&
                           std::abs(cameraForward.z) < 1e-6f)
                              ? glm::vec3(0.0f, 0.0f, -1.0f)
                              : glm::normalize(cameraForward);
    const float maxDist = std::max(maxDrawDistance, static_cast<float>(voxel::Chunk::SX));
    const float maxDist2 = maxDist * maxDist;
    const FrustumPlanes frustum = extractFrustumPlanes(viewProj);
    const float chunkRadius = 0.5f * std::sqrt(static_cast<float>(voxel::Chunk::SX * voxel::Chunk::SX +
                                                                   voxel::Chunk::SY * voxel::Chunk::SY +
                                                                   voxel::Chunk::SZ * voxel::Chunk::SZ));
    const std::uint64_t revision = worldRevision_.load(std::memory_order_relaxed);
    const glm::vec3 posDelta = cameraPos - transparentCachePos_;
    const glm::vec3 forwardDelta = fwd - transparentCacheForward_;
    const bool samePos = glm::dot(posDelta, posDelta) < 1e-8f;
    const bool sameForward = glm::dot(forwardDelta, forwardDelta) < 1e-8f;
    const bool sameMaxDist = std::abs(transparentCacheMaxDist_ - maxDist) < 1e-6f;
    if (!transparentCacheValid_ || transparentCacheRevision_ != revision || !samePos || !sameForward ||
        !sameMaxDist) {
        transparentDrawCache_.clear();
        transparentDrawCache_.reserve(chunks_.size());
        for (const auto &[coord, entry] : chunks_) {
            if (!entry.mesh) {
                continue;
            }
            const float cx = static_cast<float>(coord.x * voxel::Chunk::SX + voxel::Chunk::SX / 2);
            const float cy = static_cast<float>(voxel::Chunk::SY) * 0.5f;
            const float cz = static_cast<float>(coord.z * voxel::Chunk::SZ + voxel::Chunk::SZ / 2);
            const glm::vec3 toChunk(cx - cameraPos.x, 0.0f, cz - cameraPos.z);
            const float dx = toChunk.x;
            const float dz = toChunk.z;
            const float dist2 = dx * dx + dz * dz;
            if (dist2 > maxDist2) {
                continue;
            }
            if (!sphereInFrustum(frustum, glm::vec3(cx, cy, cz), chunkRadius)) {
                continue;
            }
            transparentDrawCache_.push_back(
                TransparentDrawItem{entry.mesh.get(), coord, glm::dot(toChunk, fwd), dist2});
        }
        std::sort(transparentDrawCache_.begin(), transparentDrawCache_.end(),
                  [](const TransparentDrawItem &a, const TransparentDrawItem &b) {
                      if (a.depth != b.depth) {
                          return a.depth > b.depth; // farther along view direction first
                      }
                      if (a.dist2 != b.dist2) {
                          return a.dist2 > b.dist2;
                      }
                      if (a.coord.x != b.coord.x) {
                          return a.coord.x < b.coord.x;
                      }
                      return a.coord.z < b.coord.z;
                  });
        transparentCachePos_ = cameraPos;
        transparentCacheForward_ = fwd;
        transparentCacheMaxDist_ = maxDist;
        transparentCacheRevision_ = revision;
        transparentCacheValid_ = true;
    }

    for (const TransparentDrawItem &item : transparentDrawCache_) {
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
    if (id == voxel::AIR || voxel::isFluid(id) || voxel::isPlant(id) || voxel::isTorch(id)) {
        return false;
    }
    return blockRegistry_.get(id).solid;
}

bool World::isTargetBlock(int wx, int wy, int wz) const {
    const voxel::BlockId id = getBlock(wx, wy, wz);
    if (id == voxel::AIR || (voxel::isFluid(id) && !voxel::isWaterloggedPlant(id))) {
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

glm::vec3 World::fluidCurrentAt(const glm::vec3 &pos) const {
    const int wx = static_cast<int>(std::floor(pos.x));
    const int wy = static_cast<int>(std::floor(pos.y));
    const int wz = static_cast<int>(std::floor(pos.z));

    std::lock_guard<std::mutex> lock(chunksMutex_);
    const voxel::BlockId id = getBlockLoadedLocked(wx, wy, wz);
    const bool waterLike = (isWaterBlock(id) || voxel::isWaterloggedPlant(id));
    const bool lavaLike = isLavaBlock(id);
    if (!waterLike && !lavaLike) {
        return glm::vec3(0.0f);
    }
    const voxel::BlockId fluidId = lavaLike ? voxel::LAVA : voxel::WATER;
    const int maxFlow = (fluidId == voxel::WATER) ? kWaterMaxFlowLevel : kLavaMaxFlowLevel;

    auto isSameFluidAt = [&](int x, int y, int z) {
        const voxel::BlockId bid = getBlockLoadedLocked(x, y, z);
        return (fluidId == voxel::WATER) ? (isWaterBlock(bid) || voxel::isWaterloggedPlant(bid))
                                         : isLavaBlock(bid);
    };
    auto isOpenAt = [&](int x, int y, int z) {
        const voxel::BlockId bid = getBlockLoadedLocked(x, y, z);
        if (bid == voxel::AIR || voxel::isPlant(bid) || voxel::isTorch(bid)) {
            return true;
        }
        return false;
    };
    auto levelAt = [&](int x, int y, int z) -> int {
        if (!isSameFluidAt(x, y, z)) {
            return -1;
        }
        return fluidLevelAtLocked(fluidId, x, y, z);
    };

    const int centerLevel = std::max(0, levelAt(wx, wy, wz));
    glm::vec3 flow(0.0f);
    const std::array<glm::ivec2, 4> kDirs = {
        glm::ivec2{1, 0}, glm::ivec2{-1, 0}, glm::ivec2{0, 1}, glm::ivec2{0, -1}};
    for (const glm::ivec2 d : kDirs) {
        const int nx = wx + d.x;
        const int nz = wz + d.y;
        const int nLevel = levelAt(nx, wy, nz);
        if (nLevel >= 0) {
            // Flow from stronger sources (low level) toward weaker cells (high level).
            flow += glm::vec3(static_cast<float>(d.x), 0.0f, static_cast<float>(d.y)) *
                    static_cast<float>(nLevel - centerLevel);
        } else if (isOpenAt(nx, wy, nz)) {
            flow += glm::vec3(static_cast<float>(d.x), 0.0f, static_cast<float>(d.y)) *
                    static_cast<float>(maxFlow - centerLevel + 1);
        }
    }
    if (!isSameFluidAt(wx, wy - 1, wz) && isOpenAt(wx, wy - 1, wz)) {
        flow.y -= 1.5f;
    }

    const float len = glm::length(flow);
    if (len <= 1e-5f) {
        return glm::vec3(0.0f);
    }
    const float strength = (fluidId == voxel::WATER) ? 16.0f : 8.0f;
    return (flow / len) * strength;
}

std::string World::biomeLabelAt(int wx, int wz) const {
    return gen_.biomeLabelAt(wx, wz);
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
    voxel::BlockId nextId = id;
    // Waterlogged underwater flora should leave water behind when removed.
    if (nextId == voxel::AIR && voxel::isWaterloggedPlant(prevId)) {
        nextId = voxel::WATER;
    }
    // Bedrock at world bottom is immutable.
    if (prevId == voxel::BEDROCK && nextId != voxel::BEDROCK) {
        return false;
    }

    // Player-placed water is always a source block. But when removing a
    // waterlogged plant we restore flowing water in-place, not a new source.
    if (nextId == voxel::WATER && !voxel::isWaterloggedPlant(prevId)) {
        nextId = voxel::WATER_SOURCE;
    }
    // Player-placed lava behaves like water source placement.
    if (nextId == voxel::LAVA) {
        nextId = voxel::LAVA_SOURCE;
    }

    if (prevId == nextId) {
        if (nextId == voxel::LAVA_SOURCE) {
            setFluidStateLocked(voxel::LAVA, wx, wy, wz, 0, true);
            enqueueFluidNeighborsLocked(wx, wy, wz);
            enqueueRemesh(cc, true, true);
        } else if (nextId == voxel::WATER_SOURCE) {
            setFluidStateLocked(voxel::WATER, wx, wy, wz, 0, true);
            enqueueFluidNeighborsLocked(wx, wy, wz);
            enqueueRemesh(cc, true, true);
        }
        return true;
    }
    it->second.chunk->set(lx, wy, lz, nextId);
    clearFluidStateLocked(wx, wy, wz);
    if (isWaterBlock(nextId) || isLavaBlock(nextId)) {
        // Player-placed fluid blocks are explicit sources.
        if (nextId == voxel::LAVA_SOURCE) {
            setFluidStateLocked(voxel::LAVA, wx, wy, wz, 0, true);
        } else if (nextId == voxel::LAVA) {
            setFluidStateLocked(voxel::LAVA, wx, wy, wz, kLavaMaxFlowLevel, false);
        } else if (nextId == voxel::WATER_SOURCE) {
            setFluidStateLocked(voxel::WATER, wx, wy, wz, 0, true);
        } else {
            setFluidStateLocked(voxel::WATER, wx, wy, wz, kWaterMaxFlowLevel, false);
        }
    }
    activateFluidCellLocked(wx, wy, wz);
    activateFluidCellLocked(wx, wy - 1, wz);
    activateFluidCellLocked(wx, wy + 1, wz);
    activateFluidCellLocked(wx + 1, wy, wz);
    activateFluidCellLocked(wx - 1, wy, wz);
    activateFluidCellLocked(wx, wy, wz + 1);
    activateFluidCellLocked(wx, wy, wz - 1);
    // Any edit can open/close fluid paths nearby.
    enqueueFluidNeighborsLocked(wx, wy, wz);
    enqueueRemesh(cc, true, true);

    // Remesh only seam-sharing neighbors when the edited block touches chunk borders.
    if (lx == 0) {
        enqueueRemesh(ChunkCoord{cc.x - 1, cc.z}, true, true);
    }
    if (lx == voxel::Chunk::SX - 1) {
        enqueueRemesh(ChunkCoord{cc.x + 1, cc.z}, true, true);
    }
    if (lz == 0) {
        enqueueRemesh(ChunkCoord{cc.x, cc.z - 1}, true, true);
    }
    if (lz == voxel::Chunk::SZ - 1) {
        enqueueRemesh(ChunkCoord{cc.x, cc.z + 1}, true, true);
    }
    // Lighting can cross chunk borders either from emission changes or from
    // transmissivity changes (placing/removing an occluder).
    const auto transmitsLight = [this](voxel::BlockId bid) -> bool {
        if (bid == voxel::AIR) {
            return true;
        }
        const auto &def = blockRegistry_.get(bid);
        return !(def.solid && !def.transparent);
    };
    int lightReach = static_cast<int>(
        std::max(voxel::emittedBlockLight(prevId), voxel::emittedBlockLight(nextId)));
    if (transmitsLight(prevId) != transmitsLight(nextId)) {
        // Torch/skylight attenuation bounds effective cross-chunk influence.
        lightReach = std::max(lightReach, 15);
    }
    if (lightReach > 0) {
        const bool crossNegX = (lx < lightReach);
        const bool crossPosX = ((voxel::Chunk::SX - 1 - lx) < lightReach);
        const bool crossNegZ = (lz < lightReach);
        const bool crossPosZ = ((voxel::Chunk::SZ - 1 - lz) < lightReach);
        if (crossNegX) {
            enqueueRemesh(ChunkCoord{cc.x - 1, cc.z}, true, true);
        }
        if (crossPosX) {
            enqueueRemesh(ChunkCoord{cc.x + 1, cc.z}, true, true);
        }
        if (crossNegZ) {
            enqueueRemesh(ChunkCoord{cc.x, cc.z - 1}, true, true);
        }
        if (crossPosZ) {
            enqueueRemesh(ChunkCoord{cc.x, cc.z + 1}, true, true);
        }
        if (crossNegX && crossNegZ) {
            enqueueRemesh(ChunkCoord{cc.x - 1, cc.z - 1}, true, true);
        }
        if (crossNegX && crossPosZ) {
            enqueueRemesh(ChunkCoord{cc.x - 1, cc.z + 1}, true, true);
        }
        if (crossPosX && crossNegZ) {
            enqueueRemesh(ChunkCoord{cc.x + 1, cc.z - 1}, true, true);
        }
        if (crossPosX && crossPosZ) {
            enqueueRemesh(ChunkCoord{cc.x + 1, cc.z + 1}, true, true);
        }
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
    const FurnaceCoordKey key = makeFurnaceKey(wx, wy, wz);
    furnaceStates_[key] = state;
    furnaceStateByChunk_[worldToChunk(wx, wz)].insert(key);
}

void World::clearFurnaceState(int wx, int wy, int wz) {
    std::lock_guard<std::mutex> lock(chunksMutex_);
    const FurnaceCoordKey key = makeFurnaceKey(wx, wy, wz);
    if (furnaceStates_.erase(key) <= 0) {
        return;
    }
    const ChunkCoord cc = worldToChunk(wx, wz);
    const auto bit = furnaceStateByChunk_.find(cc);
    if (bit == furnaceStateByChunk_.end()) {
        return;
    }
    bit->second.erase(key);
    if (bit->second.empty()) {
        furnaceStateByChunk_.erase(bit);
    }
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
            if (a.urgent != b.urgent) {
                return a.urgent;
            }
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
            if (!app::SaveManager::loadChunk(saveRoot_, WorldGen::kGeneratorVersion, *chunk,
                                             job.coord, &loadedFurnaces)) {
                gen_.fillChunk(*chunk, job.coord);
            } else if (!loadedFurnaces.empty()) {
                std::lock_guard<std::mutex> lock(chunksMutex_);
                for (const auto &rec : loadedFurnaces) {
                    const int wx = job.coord.x * voxel::Chunk::SX + static_cast<int>(rec.x);
                    const int wy = static_cast<int>(rec.y);
                    const int wz = job.coord.z * voxel::Chunk::SZ + static_cast<int>(rec.z);
                    const FurnaceCoordKey key = makeFurnaceKey(wx, wy, wz);
                    furnaceStates_[key] = rec.state;
                    furnaceStateByChunk_[job.coord].insert(key);
                }
            }
            // Defer mesh build to remesh jobs after chunk registration so
            // neighbor-aware edge culling happens before any faces are rendered.
            completed_.push(
                WorkerResult{job.coord, job.urgent, std::move(chunk), nullptr, true});
        } else {
            if (!job.chunkSnapshot) {
                continue;
            }
            const voxel::ChunkMesher::NeighborChunks neighbors{
                job.px.get(),   job.nx.get(),   job.pz.get(),   job.nz.get(),
                job.pxpz.get(), job.pxnz.get(), job.nxpz.get(), job.nxnz.get()};
            const auto fluidLevelLookup = [&](voxel::BlockId fluidId, int wx, int wy, int wz) -> int {
                const auto &levels = (fluidId == voxel::LAVA) ? job.lavaLevels : job.waterLevels;
                const auto it = levels.find(FluidCoord{wx, wy, wz});
                if (it == levels.end()) {
                    return -1;
                }
                return static_cast<int>(it->second);
            };

            const std::size_t reserveVertices =
                static_cast<std::size_t>(meshReserveVertices_.load(std::memory_order_relaxed));
            const std::size_t reserveIndices =
                static_cast<std::size_t>(meshReserveIndices_.load(std::memory_order_relaxed));
            auto mesh = acquireMeshBuffer();
            voxel::ChunkMesher::buildFaceCulledInto(
                *mesh, *job.chunkSnapshot, atlas_, blockRegistry_,
                glm::ivec2(job.coord.x, job.coord.z), neighbors,
                smoothLighting_.load(std::memory_order_relaxed), fluidLevelLookup,
                reserveVertices, reserveIndices);
            if (!mesh->vertices.empty()) {
                const std::uint32_t prev = meshReserveVertices_.load(std::memory_order_relaxed);
                const std::uint32_t curr = static_cast<std::uint32_t>(mesh->vertices.size());
                const std::uint32_t blended =
                    (prev * 7u + curr + 3u) / 8u; // smooth moving average
                meshReserveVertices_.store(std::max(1024u, blended), std::memory_order_relaxed);
            }
            if (!mesh->indices.empty()) {
                const std::uint32_t prev = meshReserveIndices_.load(std::memory_order_relaxed);
                const std::uint32_t curr = static_cast<std::uint32_t>(mesh->indices.size());
                const std::uint32_t blended =
                    (prev * 7u + curr + 3u) / 8u; // smooth moving average
                meshReserveIndices_.store(std::max(1536u, blended), std::memory_order_relaxed);
            }
            completed_.push(WorkerResult{job.coord, job.urgent, nullptr, std::move(mesh), false});
        }
    }
}

} // namespace world
