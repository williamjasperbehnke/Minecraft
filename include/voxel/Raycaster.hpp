#pragma once

#include <glm/vec3.hpp>

#include <functional>
#include <optional>

namespace world {
class World;
}

namespace voxel {

struct RayHit {
    glm::ivec3 block;
    glm::ivec3 normal;
    float t = 0.0f;
};

class Raycaster {
  public:
    static std::optional<RayHit> cast(const glm::vec3 &origin, const glm::vec3 &dir, float maxDist,
                                      const std::function<bool(int, int, int)> &isSolid);

    static std::optional<RayHit> cast(const world::World &world, const glm::vec3 &origin,
                                      const glm::vec3 &dir, float maxDist);
};

} // namespace voxel
