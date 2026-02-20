#include "voxel/Raycaster.hpp"

#include <cassert>

int main() {
    auto isSolid = [](int x, int y, int z) { return x == 3 && y == 2 && z == 1; };

    const auto hit = voxel::Raycaster::cast(glm::vec3(0.5f, 2.5f, 1.5f),
                                            glm::vec3(1.0f, 0.0f, 0.0f), 10.0f, isSolid);

    assert(hit.has_value());
    assert(hit->block.x == 3);
    assert(hit->block.y == 2);
    assert(hit->block.z == 1);
    assert(hit->normal.x == -1);

    const auto miss = voxel::Raycaster::cast(glm::vec3(0.5f, 0.5f, 0.5f),
                                             glm::vec3(0.0f, 1.0f, 0.0f), 2.0f, isSolid);
    assert(!miss.has_value());

    return 0;
}
