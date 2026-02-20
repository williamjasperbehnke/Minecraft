#include "voxel/Raycaster.hpp"

#include "world/World.hpp"

#include <cmath>
#include <glm/geometric.hpp>
#include <limits>

namespace voxel {

std::optional<RayHit> Raycaster::cast(const glm::vec3 &origin, const glm::vec3 &dir, float maxDist,
                                      const std::function<bool(int, int, int)> &isSolid) {
    const glm::vec3 d = glm::normalize(dir);

    int x = static_cast<int>(std::floor(origin.x));
    int y = static_cast<int>(std::floor(origin.y));
    int z = static_cast<int>(std::floor(origin.z));

    const int stepX = (d.x > 0.0f) ? 1 : (d.x < 0.0f ? -1 : 0);
    const int stepY = (d.y > 0.0f) ? 1 : (d.y < 0.0f ? -1 : 0);
    const int stepZ = (d.z > 0.0f) ? 1 : (d.z < 0.0f ? -1 : 0);

    auto tDelta = [&](float v) {
        if (v == 0.0f) {
            return std::numeric_limits<float>::infinity();
        }
        return std::abs(1.0f / v);
    };

    const float tDeltaX = tDelta(d.x);
    const float tDeltaY = tDelta(d.y);
    const float tDeltaZ = tDelta(d.z);

    auto tMaxAxis = [&](float o, float v, int voxel, int step) {
        if (step == 0) {
            return std::numeric_limits<float>::infinity();
        }
        const float nextBoundary = static_cast<float>(voxel + (step > 0 ? 1 : 0));
        return (nextBoundary - o) / v;
    };

    float tMaxX = tMaxAxis(origin.x, d.x, x, stepX);
    float tMaxY = tMaxAxis(origin.y, d.y, y, stepY);
    float tMaxZ = tMaxAxis(origin.z, d.z, z, stepZ);

    glm::ivec3 lastNormal(0);
    float t = 0.0f;

    while (t <= maxDist) {
        if (isSolid(x, y, z)) {
            return RayHit{glm::ivec3(x, y, z), lastNormal, t};
        }

        if (tMaxX < tMaxY && tMaxX < tMaxZ) {
            x += stepX;
            t = tMaxX;
            tMaxX += tDeltaX;
            lastNormal = glm::ivec3(-stepX, 0, 0);
        } else if (tMaxY < tMaxZ) {
            y += stepY;
            t = tMaxY;
            tMaxY += tDeltaY;
            lastNormal = glm::ivec3(0, -stepY, 0);
        } else {
            z += stepZ;
            t = tMaxZ;
            tMaxZ += tDeltaZ;
            lastNormal = glm::ivec3(0, 0, -stepZ);
        }
    }

    return std::nullopt;
}

std::optional<RayHit> Raycaster::cast(const world::World &world, const glm::vec3 &origin,
                                      const glm::vec3 &dir, float maxDist) {
    return cast(origin, dir, maxDist,
                [&](int x, int y, int z) { return world.isTargetBlock(x, y, z); });
}

} // namespace voxel
