#include "voxel/LightingSolver.hpp"

#include <algorithm>
#include <array>
#include <utility>

namespace voxel {
namespace {

constexpr std::array<std::array<int, 3>, 6> kDirs = {{
    {{1, 0, 0}},
    {{-1, 0, 0}},
    {{0, 1, 0}},
    {{0, -1, 0}},
    {{0, 0, 1}},
    {{0, 0, -1}},
}};

} // namespace

LightingSolver::LightingSolver(const Chunk &chunk, const BlockRegistry &registry,
                               const NeighborChunks &neighbors, bool smoothLighting)
    : chunk_(chunk), registry_(registry), neighbors_(neighbors), smoothLighting_(smoothLighting) {
    light_ = buildLocalLight(chunk_);
    if (neighbors_.px != nullptr) {
        pxLight_ = buildLocalLight(*neighbors_.px);
    }
    if (neighbors_.nx != nullptr) {
        nxLight_ = buildLocalLight(*neighbors_.nx);
    }
    if (neighbors_.pz != nullptr) {
        pzLight_ = buildLocalLight(*neighbors_.pz);
    }
    if (neighbors_.nz != nullptr) {
        nzLight_ = buildLocalLight(*neighbors_.nz);
    }
    if (neighbors_.pxpz != nullptr) {
        pxpzLight_ = buildLocalLight(*neighbors_.pxpz);
    }
    if (neighbors_.pxnz != nullptr) {
        pxnzLight_ = buildLocalLight(*neighbors_.pxnz);
    }
    if (neighbors_.nxpz != nullptr) {
        nxpzLight_ = buildLocalLight(*neighbors_.nxpz);
    }
    if (neighbors_.nxnz != nullptr) {
        nxnzLight_ = buildLocalLight(*neighbors_.nxnz);
    }

    buildExtendedSkyLight();
    buildExtendedBlockLight();
}

int LightingSolver::lightIndex(int x, int y, int z) {
    return x + Chunk::SX * (z + Chunk::SZ * y);
}

float LightingSolver::lightLevelToFloat(std::uint8_t level) {
    return static_cast<float>(level) / 15.0f;
}

bool LightingSolver::isOpaque(BlockId id) const {
    if (id == AIR) {
        return false;
    }
    const auto &def = registry_.get(id);
    return def.solid && !def.transparent;
}

bool LightingSolver::canTransmitLight(BlockId id) const {
    return !isOpaque(id);
}

LightingSolver::LocalLight LightingSolver::buildLocalLight(const Chunk &src) const {
    LocalLight ll;
    ll.sky.assign(Chunk::SX * Chunk::SY * Chunk::SZ, 0);
    ll.block.assign(Chunk::SX * Chunk::SY * Chunk::SZ, 0);
    std::vector<int> queue;
    queue.reserve(ll.sky.size());

    for (int x = 0; x < Chunk::SX; ++x) {
        for (int z = 0; z < Chunk::SZ; ++z) {
            std::uint8_t beam = 15;
            for (int y = Chunk::SY - 1; y >= 0; --y) {
                const BlockId id = src.getUnchecked(x, y, z);
                if (!canTransmitLight(id)) {
                    beam = 0;
                    continue;
                }
                const int idx = lightIndex(x, y, z);
                if (beam > ll.sky[idx]) {
                    ll.sky[idx] = beam;
                    queue.push_back(idx);
                }
            }
        }
    }

    std::size_t head = 0;
    while (head < queue.size()) {
        const int idx = queue[head++];
        const int y = idx / (Chunk::SX * Chunk::SZ);
        const int rem = idx - y * (Chunk::SX * Chunk::SZ);
        const int z = rem / Chunk::SX;
        const int x = rem - z * Chunk::SX;
        const std::uint8_t level = ll.sky[idx];
        if (level <= 1) {
            continue;
        }
        const std::uint8_t nextLevel = static_cast<std::uint8_t>(level - 1);
        for (const auto &d : kDirs) {
            const int nx = x + d[0];
            const int ny = y + d[1];
            const int nz = z + d[2];
            if (nx < 0 || nx >= Chunk::SX || ny < 0 || ny >= Chunk::SY || nz < 0 ||
                nz >= Chunk::SZ) {
                continue;
            }
            if (!canTransmitLight(src.getUnchecked(nx, ny, nz))) {
                continue;
            }
            const int nidx = lightIndex(nx, ny, nz);
            if (nextLevel > ll.sky[nidx]) {
                ll.sky[nidx] = nextLevel;
                queue.push_back(nidx);
            }
        }
    }

    queue.clear();
    for (int x = 0; x < Chunk::SX; ++x) {
        for (int y = 0; y < Chunk::SY; ++y) {
            for (int z = 0; z < Chunk::SZ; ++z) {
                const std::uint8_t emission = emittedBlockLight(src.getUnchecked(x, y, z));
                if (emission > 0) {
                    const int idx = lightIndex(x, y, z);
                    ll.block[idx] = emission;
                    queue.push_back(idx);
                }
            }
        }
    }

    head = 0;
    while (head < queue.size()) {
        const int idx = queue[head++];
        const int y = idx / (Chunk::SX * Chunk::SZ);
        const int rem = idx - y * (Chunk::SX * Chunk::SZ);
        const int z = rem / Chunk::SX;
        const int x = rem - z * Chunk::SX;
        const std::uint8_t level = ll.block[idx];
        if (level <= 1) {
            continue;
        }
        const std::uint8_t nextLevel = static_cast<std::uint8_t>(level - 1);
        for (const auto &d : kDirs) {
            const int nx = x + d[0];
            const int ny = y + d[1];
            const int nz = z + d[2];
            if (nx < 0 || nx >= Chunk::SX || ny < 0 || ny >= Chunk::SY || nz < 0 ||
                nz >= Chunk::SZ) {
                continue;
            }
            if (!canTransmitLight(src.getUnchecked(nx, ny, nz))) {
                continue;
            }
            const int nidx = lightIndex(nx, ny, nz);
            if (nextLevel > ll.block[nidx]) {
                ll.block[nidx] = nextLevel;
                queue.push_back(nidx);
            }
        }
    }

    return ll;
}

void LightingSolver::applySkyBoundarySeeding() {
    std::vector<int> seedQueue;
    seedQueue.reserve(Chunk::SX * Chunk::SZ * 4);

    auto seedEdge = [&](int tx, int ty, int tz, const std::optional<LocalLight> &src, int sx,
                        int sy, int sz) {
        if (!src.has_value() || !canTransmitLight(chunk_.getUnchecked(tx, ty, tz))) {
            return;
        }
        const std::uint8_t srcL = src->sky[lightIndex(sx, sy, sz)];
        if (srcL <= 1) {
            return;
        }
        const std::uint8_t cand = static_cast<std::uint8_t>(srcL - 1);
        const int idx = lightIndex(tx, ty, tz);
        if (cand > light_.sky[idx]) {
            light_.sky[idx] = cand;
            seedQueue.push_back(idx);
        }
    };

    for (int y = 0; y < Chunk::SY; ++y) {
        for (int z = 0; z < Chunk::SZ; ++z) {
            seedEdge(0, y, z, nxLight_, Chunk::SX - 1, y, z);
            seedEdge(Chunk::SX - 1, y, z, pxLight_, 0, y, z);
        }
    }
    for (int y = 0; y < Chunk::SY; ++y) {
        for (int x = 0; x < Chunk::SX; ++x) {
            seedEdge(x, y, 0, nzLight_, x, y, Chunk::SZ - 1);
            seedEdge(x, y, Chunk::SZ - 1, pzLight_, x, y, 0);
        }
    }
    // Diagonal corner seed for chunk-corner skylight continuity.
    for (int y = 0; y < Chunk::SY; ++y) {
        seedEdge(0, y, 0, nxnzLight_, Chunk::SX - 1, y, Chunk::SZ - 1);
        seedEdge(0, y, Chunk::SZ - 1, nxpzLight_, Chunk::SX - 1, y, 0);
        seedEdge(Chunk::SX - 1, y, 0, pxnzLight_, 0, y, Chunk::SZ - 1);
        seedEdge(Chunk::SX - 1, y, Chunk::SZ - 1, pxpzLight_, 0, y, 0);
    }

    std::size_t head = 0;
    while (head < seedQueue.size()) {
        const int idx = seedQueue[head++];
        const int y = idx / (Chunk::SX * Chunk::SZ);
        const int rem = idx - y * (Chunk::SX * Chunk::SZ);
        const int z = rem / Chunk::SX;
        const int x = rem - z * Chunk::SX;
        const std::uint8_t level = light_.sky[idx];
        if (level <= 1) {
            continue;
        }
        const std::uint8_t nextLevel = static_cast<std::uint8_t>(level - 1);
        for (const auto &d : kDirs) {
            const int nx = x + d[0];
            const int ny = y + d[1];
            const int nz = z + d[2];
            if (nx < 0 || nx >= Chunk::SX || ny < 0 || ny >= Chunk::SY || nz < 0 ||
                nz >= Chunk::SZ) {
                continue;
            }
            if (!canTransmitLight(chunk_.getUnchecked(nx, ny, nz))) {
                continue;
            }
            const int nidx = lightIndex(nx, ny, nz);
            if (nextLevel > light_.sky[nidx]) {
                light_.sky[nidx] = nextLevel;
                seedQueue.push_back(nidx);
            }
        }
    }
}

void LightingSolver::buildExtendedBlockLight() {
    constexpr int kExtSX = Chunk::SX * 3;
    constexpr int kExtSZ = Chunk::SZ * 3;
    auto extIndex = [&](int ex, int y, int ez) { return ex + kExtSX * (ez + kExtSZ * y); };
    auto extInBounds = [&](int ex, int y, int ez) {
        return ex >= 0 && ex < kExtSX && y >= 0 && y < Chunk::SY && ez >= 0 && ez < kExtSZ;
    };

    auto blockAtExt = [&](int ex, int y, int ez) -> BlockId {
        if (!extInBounds(ex, y, ez)) {
            return AIR;
        }
        const int lx = ex - Chunk::SX;
        const int lz = ez - Chunk::SZ;
        if (lx >= 0 && lx < Chunk::SX && lz >= 0 && lz < Chunk::SZ) {
            return chunk_.getUnchecked(lx, y, lz);
        }
        if (lx < 0 && lz >= 0 && lz < Chunk::SZ && neighbors_.nx != nullptr) {
            return neighbors_.nx->getUnchecked(lx + Chunk::SX, y, lz);
        }
        if (lx >= Chunk::SX && lz >= 0 && lz < Chunk::SZ && neighbors_.px != nullptr) {
            return neighbors_.px->getUnchecked(lx - Chunk::SX, y, lz);
        }
        if (lz < 0 && lx >= 0 && lx < Chunk::SX && neighbors_.nz != nullptr) {
            return neighbors_.nz->getUnchecked(lx, y, lz + Chunk::SZ);
        }
        if (lz >= Chunk::SZ && lx >= 0 && lx < Chunk::SX && neighbors_.pz != nullptr) {
            return neighbors_.pz->getUnchecked(lx, y, lz - Chunk::SZ);
        }
        if (lx < 0 && lz < 0 && neighbors_.nxnz != nullptr) {
            return neighbors_.nxnz->getUnchecked(lx + Chunk::SX, y, lz + Chunk::SZ);
        }
        if (lx < 0 && lz >= Chunk::SZ && neighbors_.nxpz != nullptr) {
            return neighbors_.nxpz->getUnchecked(lx + Chunk::SX, y, lz - Chunk::SZ);
        }
        if (lx >= Chunk::SX && lz < 0 && neighbors_.pxnz != nullptr) {
            return neighbors_.pxnz->getUnchecked(lx - Chunk::SX, y, lz + Chunk::SZ);
        }
        if (lx >= Chunk::SX && lz >= Chunk::SZ && neighbors_.pxpz != nullptr) {
            return neighbors_.pxpz->getUnchecked(lx - Chunk::SX, y, lz - Chunk::SZ);
        }
        return AIR;
    };

    extBlockLight_.assign(kExtSX * Chunk::SY * kExtSZ, 0);
    std::vector<int> queue;
    queue.reserve(extBlockLight_.size() / 8);

    for (int ex = 0; ex < kExtSX; ++ex) {
        for (int y = 0; y < Chunk::SY; ++y) {
            for (int ez = 0; ez < kExtSZ; ++ez) {
                const std::uint8_t emission = emittedBlockLight(blockAtExt(ex, y, ez));
                if (emission > 0) {
                    const int idx = extIndex(ex, y, ez);
                    extBlockLight_[idx] = emission;
                    queue.push_back(idx);
                }
            }
        }
    }

    std::size_t head = 0;
    while (head < queue.size()) {
        const int idx = queue[head++];
        const int y = idx / (kExtSX * kExtSZ);
        const int rem = idx - y * (kExtSX * kExtSZ);
        const int ez = rem / kExtSX;
        const int ex = rem - ez * kExtSX;
        const std::uint8_t level = extBlockLight_[idx];
        if (level <= 1) {
            continue;
        }
        const std::uint8_t nextLevel = static_cast<std::uint8_t>(level - 1);
        for (const auto &d : kDirs) {
            const int nx = ex + d[0];
            const int ny = y + d[1];
            const int nz = ez + d[2];
            if (!extInBounds(nx, ny, nz)) {
                continue;
            }
            if (!canTransmitLight(blockAtExt(nx, ny, nz))) {
                continue;
            }
            const int nidx = extIndex(nx, ny, nz);
            if (nextLevel > extBlockLight_[nidx]) {
                extBlockLight_[nidx] = nextLevel;
                queue.push_back(nidx);
            }
        }
    }

    for (int x = 0; x < Chunk::SX; ++x) {
        for (int y = 0; y < Chunk::SY; ++y) {
            for (int z = 0; z < Chunk::SZ; ++z) {
                light_.block[lightIndex(x, y, z)] =
                    extBlockLight_[extIndex(x + Chunk::SX, y, z + Chunk::SZ)];
            }
        }
    }
}

void LightingSolver::buildExtendedSkyLight() {
    constexpr int kExtSX = Chunk::SX * 3;
    constexpr int kExtSZ = Chunk::SZ * 3;
    auto extIndex = [&](int ex, int y, int ez) { return ex + kExtSX * (ez + kExtSZ * y); };
    auto extInBounds = [&](int ex, int y, int ez) {
        return ex >= 0 && ex < kExtSX && y >= 0 && y < Chunk::SY && ez >= 0 && ez < kExtSZ;
    };

    auto blockAtExt = [&](int ex, int y, int ez) -> BlockId {
        if (!extInBounds(ex, y, ez)) {
            return AIR;
        }
        const int lx = ex - Chunk::SX;
        const int lz = ez - Chunk::SZ;
        if (lx >= 0 && lx < Chunk::SX && lz >= 0 && lz < Chunk::SZ) {
            return chunk_.getUnchecked(lx, y, lz);
        }
        if (lx < 0 && lz >= 0 && lz < Chunk::SZ && neighbors_.nx != nullptr) {
            return neighbors_.nx->getUnchecked(lx + Chunk::SX, y, lz);
        }
        if (lx >= Chunk::SX && lz >= 0 && lz < Chunk::SZ && neighbors_.px != nullptr) {
            return neighbors_.px->getUnchecked(lx - Chunk::SX, y, lz);
        }
        if (lz < 0 && lx >= 0 && lx < Chunk::SX && neighbors_.nz != nullptr) {
            return neighbors_.nz->getUnchecked(lx, y, lz + Chunk::SZ);
        }
        if (lz >= Chunk::SZ && lx >= 0 && lx < Chunk::SX && neighbors_.pz != nullptr) {
            return neighbors_.pz->getUnchecked(lx, y, lz - Chunk::SZ);
        }
        if (lx < 0 && lz < 0 && neighbors_.nxnz != nullptr) {
            return neighbors_.nxnz->getUnchecked(lx + Chunk::SX, y, lz + Chunk::SZ);
        }
        if (lx < 0 && lz >= Chunk::SZ && neighbors_.nxpz != nullptr) {
            return neighbors_.nxpz->getUnchecked(lx + Chunk::SX, y, lz - Chunk::SZ);
        }
        if (lx >= Chunk::SX && lz < 0 && neighbors_.pxnz != nullptr) {
            return neighbors_.pxnz->getUnchecked(lx - Chunk::SX, y, lz + Chunk::SZ);
        }
        if (lx >= Chunk::SX && lz >= Chunk::SZ && neighbors_.pxpz != nullptr) {
            return neighbors_.pxpz->getUnchecked(lx - Chunk::SX, y, lz - Chunk::SZ);
        }
        return AIR;
    };

    extSkyLight_.assign(kExtSX * Chunk::SY * kExtSZ, 0);
    std::vector<int> queue;
    queue.reserve(extSkyLight_.size() / 6);

    // Top-down sky beams.
    for (int ex = 0; ex < kExtSX; ++ex) {
        for (int ez = 0; ez < kExtSZ; ++ez) {
            std::uint8_t beam = 15;
            for (int y = Chunk::SY - 1; y >= 0; --y) {
                if (!canTransmitLight(blockAtExt(ex, y, ez))) {
                    beam = 0;
                    continue;
                }
                const int idx = extIndex(ex, y, ez);
                if (beam > extSkyLight_[idx]) {
                    extSkyLight_[idx] = beam;
                    queue.push_back(idx);
                }
            }
        }
    }

    std::size_t head = 0;
    while (head < queue.size()) {
        const int idx = queue[head++];
        const int y = idx / (kExtSX * kExtSZ);
        const int rem = idx - y * (kExtSX * kExtSZ);
        const int ez = rem / kExtSX;
        const int ex = rem - ez * kExtSX;
        const std::uint8_t level = extSkyLight_[idx];
        if (level <= 1) {
            continue;
        }
        const std::uint8_t nextLevel = static_cast<std::uint8_t>(level - 1);
        for (const auto &d : kDirs) {
            const int nx = ex + d[0];
            const int ny = y + d[1];
            const int nz = ez + d[2];
            if (!extInBounds(nx, ny, nz)) {
                continue;
            }
            if (!canTransmitLight(blockAtExt(nx, ny, nz))) {
                continue;
            }
            const int nidx = extIndex(nx, ny, nz);
            if (nextLevel > extSkyLight_[nidx]) {
                extSkyLight_[nidx] = nextLevel;
                queue.push_back(nidx);
            }
        }
    }

    for (int x = 0; x < Chunk::SX; ++x) {
        for (int y = 0; y < Chunk::SY; ++y) {
            for (int z = 0; z < Chunk::SZ; ++z) {
                light_.sky[lightIndex(x, y, z)] =
                    extSkyLight_[extIndex(x + Chunk::SX, y, z + Chunk::SZ)];
            }
        }
    }
}

std::uint8_t LightingSolver::sampledSky(int x, int y, int z) const {
    if (y < 0) {
        return 0;
    }
    if (y >= Chunk::SY) {
        return 15;
    }
    constexpr int kExtSX = Chunk::SX * 3;
    constexpr int kExtSZ = Chunk::SZ * 3;
    auto extIndex = [&](int ex, int ey, int ez) { return ex + kExtSX * (ez + kExtSZ * ey); };
    const int ex = x + Chunk::SX;
    const int ez = z + Chunk::SZ;
    if (ex < 0 || ex >= kExtSX || ez < 0 || ez >= kExtSZ) {
        return 0;
    }
    return extSkyLight_[extIndex(ex, y, ez)];
}

std::uint8_t LightingSolver::sampledBlock(int x, int y, int z) const {
    constexpr int kExtSX = Chunk::SX * 3;
    constexpr int kExtSZ = Chunk::SZ * 3;
    auto extIndex = [&](int ex, int ey, int ez) { return ex + kExtSX * (ez + kExtSZ * ey); };

    if (y < 0 || y >= Chunk::SY) {
        return 0;
    }
    if (x >= 0 && x < Chunk::SX && z >= 0 && z < Chunk::SZ) {
        return light_.block[lightIndex(x, y, z)];
    }
    const int ex = x + Chunk::SX;
    const int ez = z + Chunk::SZ;
    if (ex < 0 || ex >= kExtSX || ez < 0 || ez >= kExtSZ) {
        return 0;
    }
    return extBlockLight_[extIndex(ex, y, ez)];
}

float LightingSolver::faceSkyLight(int adjX, int adjY, int adjZ, float bias) const {
    const float sky = smoothLighting_ ? smoothedSkyLight(adjX, adjY, adjZ)
                                      : lightLevelToFloat(sampledSky(adjX, adjY, adjZ));
    return std::clamp(sky * bias, 0.0f, 1.0f);
}

float LightingSolver::faceBlockLight(int adjX, int adjY, int adjZ) const {
    const float block = smoothLighting_ ? smoothedBlockLight(adjX, adjY, adjZ)
                                        : lightLevelToFloat(sampledBlock(adjX, adjY, adjZ));
    return std::clamp(block, 0.0f, 1.0f);
}

float LightingSolver::smoothedSkyLight(int x, int y, int z) const {
    int weighted = static_cast<int>(sampledSky(x, y, z)) * 4;
    int weight = 4;
    for (const auto &d : kDirs) {
        weighted += static_cast<int>(sampledSky(x + d[0], y + d[1], z + d[2]));
        weight += 1;
    }
    return (weight > 0) ? (static_cast<float>(weighted) / static_cast<float>(weight) / 15.0f)
                        : 0.0f;
}

float LightingSolver::smoothedBlockLight(int x, int y, int z) const {
    int weighted = static_cast<int>(sampledBlock(x, y, z)) * 4;
    int weight = 4;
    for (const auto &d : kDirs) {
        weighted += static_cast<int>(sampledBlock(x + d[0], y + d[1], z + d[2]));
        weight += 1;
    }
    const float center = lightLevelToFloat(sampledBlock(x, y, z));
    const float blended = (weight > 0) ? (static_cast<float>(weighted) / static_cast<float>(weight) / 15.0f)
                                       : 0.0f;
    return std::max(center * 0.80f, blended);
}

} // namespace voxel
