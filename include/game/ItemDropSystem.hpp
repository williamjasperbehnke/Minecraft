#pragma once

#include "gfx/TextureAtlas.hpp"
#include "voxel/Block.hpp"

#include <glm/mat4x4.hpp>
#include <glm/vec3.hpp>

#include <cstddef>
#include <utility>
#include <vector>

namespace world {
class World;
}

namespace game {

class ItemDropSystem {
  public:
    struct Pickup {
        voxel::BlockId id = voxel::AIR;
        int count = 0;
        glm::vec3 pos{0.0f};
    };

    ItemDropSystem() = default;
    ~ItemDropSystem();

    ItemDropSystem(const ItemDropSystem &) = delete;
    ItemDropSystem &operator=(const ItemDropSystem &) = delete;

    void spawn(voxel::BlockId id, const glm::vec3 &worldPos, int count = 1);
    void update(const world::World &world, const glm::vec3 &playerPos, float dt);
    std::vector<Pickup> consumePickups();
    void render(const glm::mat4 &proj, const glm::mat4 &view, const gfx::TextureAtlas &atlas,
                const voxel::BlockRegistry &registry);

    std::size_t activeCount() const {
        return items_.size();
    }

  private:
    struct Item {
        voxel::BlockId id = voxel::AIR;
        glm::vec3 pos{0.0f};
        glm::vec3 vel{0.0f};
        float age = 0.0f;
        float pickupDelay = 0.0f;
    };

    struct Vertex {
        float x;
        float y;
        float z;
        float u;
        float v;
        float light;
    };

    void initGl();
    bool collidesSolid(const world::World &world, const glm::vec3 &pos) const;

    unsigned int shader_ = 0;
    unsigned int vao_ = 0;
    unsigned int vbo_ = 0;
    bool ready_ = false;

    std::vector<Vertex> verts_;
    std::vector<Item> items_;
    std::vector<Pickup> pendingPickups_;
};

} // namespace game
