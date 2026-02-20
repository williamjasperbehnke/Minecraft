#include "game/ItemDropSystem.hpp"

#include "world/World.hpp"

#include <glad/glad.h>
#include <GLFW/glfw3.h>
#include <glm/common.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <stdexcept>

namespace game {
namespace {

constexpr float kItemHalf = 0.18f;
constexpr float kItemHeight = 0.36f;
constexpr float kItemRenderHalf = 0.22f;
constexpr float kGravity = 18.0f;
constexpr float kGroundDrag = 7.0f;
constexpr float kAirDrag = 1.2f;
constexpr float kPickupRadius = 1.20f;
constexpr float kMaxLifetime = 180.0f;

unsigned int compile(unsigned int type, const char *src) {
    const unsigned int shader = glCreateShader(type);
    glShaderSource(shader, 1, &src, nullptr);
    glCompileShader(shader);
    int ok = 0;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[1024];
        glGetShaderInfoLog(shader, sizeof(log), nullptr, log);
        throw std::runtime_error(std::string("Item shader compile failed: ") + log);
    }
    return shader;
}

unsigned int link(unsigned int vs, unsigned int fs) {
    const unsigned int prog = glCreateProgram();
    glAttachShader(prog, vs);
    glAttachShader(prog, fs);
    glLinkProgram(prog);
    int ok = 0;
    glGetProgramiv(prog, GL_LINK_STATUS, &ok);
    glDeleteShader(vs);
    glDeleteShader(fs);
    if (!ok) {
        char log[1024];
        glGetProgramInfoLog(prog, sizeof(log), nullptr, log);
        throw std::runtime_error(std::string("Item shader link failed: ") + log);
    }
    return prog;
}

float frand(float minV, float maxV) {
    const float t = static_cast<float>(std::rand()) / static_cast<float>(RAND_MAX);
    return minV + (maxV - minV) * t;
}

} // namespace

ItemDropSystem::~ItemDropSystem() {
    if (glfwGetCurrentContext() == nullptr) {
        return;
    }
    if (vbo_ != 0) {
        glDeleteBuffers(1, &vbo_);
    }
    if (vao_ != 0) {
        glDeleteVertexArrays(1, &vao_);
    }
    if (shader_ != 0) {
        glDeleteProgram(shader_);
    }
}

void ItemDropSystem::initGl() {
    if (ready_) {
        return;
    }

    const char *vs = R"(
#version 330 core
layout(location = 0) in vec3 aPos;
layout(location = 1) in vec2 aUV;
layout(location = 2) in float aLight;
uniform mat4 uProj;
uniform mat4 uView;
out vec2 vUV;
out float vLight;
void main() {
    vUV = aUV;
    vLight = aLight;
    gl_Position = uProj * uView * vec4(aPos, 1.0);
}
)";

    const char *fs = R"(
#version 330 core
in vec2 vUV;
in float vLight;
uniform sampler2D uAtlas;
out vec4 FragColor;
void main() {
    vec4 texel = texture(uAtlas, vUV);
    if (texel.a < 0.1) discard;
    FragColor = vec4(texel.rgb * vLight, texel.a);
}
)";

    shader_ = link(compile(GL_VERTEX_SHADER, vs), compile(GL_FRAGMENT_SHADER, fs));

    glGenVertexArrays(1, &vao_);
    glGenBuffers(1, &vbo_);
    glBindVertexArray(vao_);
    glBindBuffer(GL_ARRAY_BUFFER, vbo_);
    glBufferData(GL_ARRAY_BUFFER, 0, nullptr, GL_DYNAMIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), reinterpret_cast<void *>(0));
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, sizeof(Vertex),
                          reinterpret_cast<void *>(3 * sizeof(float)));
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(2, 1, GL_FLOAT, GL_FALSE, sizeof(Vertex),
                          reinterpret_cast<void *>(5 * sizeof(float)));

    ready_ = true;
}

bool ItemDropSystem::collidesSolid(const world::World &world, const glm::vec3 &pos) const {
    const glm::vec3 minP = pos + glm::vec3(-kItemHalf, 0.0f, -kItemHalf);
    const glm::vec3 maxP = pos + glm::vec3(kItemHalf, kItemHeight, kItemHalf);
    const int minX = static_cast<int>(std::floor(minP.x));
    const int minY = static_cast<int>(std::floor(minP.y));
    const int minZ = static_cast<int>(std::floor(minP.z));
    const int maxX = static_cast<int>(std::floor(maxP.x));
    const int maxY = static_cast<int>(std::floor(maxP.y));
    const int maxZ = static_cast<int>(std::floor(maxP.z));
    for (int y = minY; y <= maxY; ++y) {
        for (int z = minZ; z <= maxZ; ++z) {
            for (int x = minX; x <= maxX; ++x) {
                if (world.isSolidBlock(x, y, z)) {
                    return true;
                }
            }
        }
    }
    return false;
}

void ItemDropSystem::spawn(voxel::BlockId id, const glm::vec3 &worldPos, int count) {
    if (id == voxel::AIR || count <= 0) {
        return;
    }
    for (int i = 0; i < count; ++i) {
        Item item;
        item.id = id;
        item.pos =
            worldPos + glm::vec3(frand(-0.15f, 0.15f), frand(0.02f, 0.20f), frand(-0.15f, 0.15f));
        item.vel = glm::vec3(frand(-1.2f, 1.2f), frand(1.8f, 3.5f), frand(-1.2f, 1.2f));
        item.pickupDelay = 0.18f;
        items_.push_back(item);
    }
}

void ItemDropSystem::update(const world::World &world, const glm::vec3 &playerPos, float dt) {
    for (std::size_t i = 0; i < items_.size();) {
        Item &it = items_[i];
        it.age += dt;
        it.pickupDelay -= dt;
        if (it.age >= kMaxLifetime) {
            items_[i] = items_.back();
            items_.pop_back();
            continue;
        }

        it.vel.y -= kGravity * dt;
        const float drag =
            std::max(0.0f, 1.0f - (it.pos.y <= playerPos.y + 0.5f ? kGroundDrag : kAirDrag) * dt);
        it.vel.x *= drag;
        it.vel.z *= drag;

        for (int axis = 0; axis < 3; ++axis) {
            glm::vec3 test = it.pos;
            test[axis] += it.vel[axis] * dt;
            if (collidesSolid(world, test)) {
                if (axis == 1 && it.vel.y < 0.0f) {
                    it.vel.y *= -0.15f;
                    if (std::abs(it.vel.y) < 0.8f) {
                        it.vel.y = 0.0f;
                    }
                } else {
                    it.vel[axis] = 0.0f;
                }
            } else {
                it.pos = test;
            }
        }

        // Use a body-like pickup volume so items at feet are still collectible.
        const glm::vec3 playerFeet = playerPos - glm::vec3(0.0f, 1.25f, 0.0f);
        const glm::vec3 d = it.pos - playerFeet;
        const float horiz2 = d.x * d.x + d.z * d.z;
        if (it.pickupDelay <= 0.0f && horiz2 <= kPickupRadius * kPickupRadius &&
            std::abs(d.y) <= 1.9f) {
            pendingPickups_.push_back({it.id, 1});
            items_[i] = items_.back();
            items_.pop_back();
            continue;
        }
        ++i;
    }
}

std::vector<std::pair<voxel::BlockId, int>> ItemDropSystem::consumePickups() {
    auto out = std::move(pendingPickups_);
    pendingPickups_.clear();
    return out;
}

void ItemDropSystem::render(const glm::mat4 &proj, const glm::mat4 &view,
                            const gfx::TextureAtlas &atlas, const voxel::BlockRegistry &registry) {
    if (items_.empty()) {
        return;
    }
    initGl();
    verts_.clear();
    verts_.reserve(items_.size() * 36);

    auto appendQuad = [&](const glm::vec3 &p0, const glm::vec3 &p1, const glm::vec3 &p2,
                          const glm::vec3 &p3, const glm::vec4 &uv, float light) {
        verts_.push_back(Vertex{p0.x, p0.y, p0.z, uv.x, uv.w, light});
        verts_.push_back(Vertex{p1.x, p1.y, p1.z, uv.z, uv.w, light});
        verts_.push_back(Vertex{p2.x, p2.y, p2.z, uv.z, uv.y, light});
        verts_.push_back(Vertex{p0.x, p0.y, p0.z, uv.x, uv.w, light});
        verts_.push_back(Vertex{p2.x, p2.y, p2.z, uv.z, uv.y, light});
        verts_.push_back(Vertex{p3.x, p3.y, p3.z, uv.x, uv.y, light});
    };

    auto appendPlantSprite = [&](const glm::mat4 &model, const glm::vec4 &uv) {
        auto tp = [&](float x, float y, float z) {
            return glm::vec3(model * glm::vec4(x, y, z, 1.0f));
        };
        const float h = 0.20f;
        const float y0 = -0.20f;
        const float y1 = 0.20f;
        const glm::vec3 p0 = tp(-h, y0, -h);
        const glm::vec3 p1 = tp(h, y0, h);
        const glm::vec3 p2 = tp(h, y1, h);
        const glm::vec3 p3 = tp(-h, y1, -h);
        const glm::vec3 q0 = tp(h, y0, -h);
        const glm::vec3 q1 = tp(-h, y0, h);
        const glm::vec3 q2 = tp(-h, y1, h);
        const glm::vec3 q3 = tp(h, y1, -h);
        appendQuad(p0, p1, p2, p3, uv, 0.95f);
        appendQuad(q0, q1, q2, q3, uv, 0.95f);
    };

    for (const Item &item : items_) {
        const voxel::BlockDef &def = registry.get(item.id);
        const glm::vec4 uvSide = atlas.uvRect(def.sideTile);
        const glm::vec4 uvTop = atlas.uvRect(def.topTile);
        const glm::vec4 uvBottom = atlas.uvRect(def.bottomTile);
        const float bob = 0.08f * std::sin(item.age * 3.8f);
        const glm::vec3 center = item.pos + glm::vec3(0.0f, 0.23f + bob, 0.0f);
        const float ang = item.age * 1.8f;
        glm::mat4 model(1.0f);
        model = glm::translate(model, center);
        model = glm::rotate(model, ang, glm::vec3(0.0f, 1.0f, 0.0f));

        const bool plantItem = (item.id == voxel::TALL_GRASS || item.id == voxel::FLOWER);
        if (plantItem) {
            appendPlantSprite(model, uvSide);
            continue;
        }

        auto tp = [&](float x, float y, float z) {
            return glm::vec3(model * glm::vec4(x, y, z, 1.0f));
        };

        const float h = kItemRenderHalf;
        const glm::vec3 p000 = tp(-h, -h, -h);
        const glm::vec3 p001 = tp(-h, -h, h);
        const glm::vec3 p010 = tp(-h, h, -h);
        const glm::vec3 p011 = tp(-h, h, h);
        const glm::vec3 p100 = tp(h, -h, -h);
        const glm::vec3 p101 = tp(h, -h, h);
        const glm::vec3 p110 = tp(h, h, -h);
        const glm::vec3 p111 = tp(h, h, h);

        appendQuad(p001, p101, p111, p011, uvSide, 0.92f);   // +Z
        appendQuad(p100, p000, p010, p110, uvSide, 0.75f);   // -Z
        appendQuad(p000, p001, p011, p010, uvSide, 0.82f);   // -X
        appendQuad(p101, p100, p110, p111, uvSide, 0.88f);   // +X
        appendQuad(p010, p011, p111, p110, uvTop, 1.00f);    // +Y
        appendQuad(p000, p100, p101, p001, uvBottom, 0.65f); // -Y
    }

    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    // Keep depth writes enabled so spinning cube faces occlude correctly.
    glDepthMask(GL_TRUE);
    glUseProgram(shader_);
    glUniformMatrix4fv(glGetUniformLocation(shader_, "uProj"), 1, GL_FALSE, glm::value_ptr(proj));
    glUniformMatrix4fv(glGetUniformLocation(shader_, "uView"), 1, GL_FALSE, glm::value_ptr(view));
    glUniform1i(glGetUniformLocation(shader_, "uAtlas"), 0);
    atlas.bind(0);

    glBindVertexArray(vao_);
    glBindBuffer(GL_ARRAY_BUFFER, vbo_);
    glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(verts_.size() * sizeof(Vertex)),
                 verts_.data(), GL_DYNAMIC_DRAW);
    glDrawArrays(GL_TRIANGLES, 0, static_cast<GLsizei>(verts_.size()));
}

} // namespace game
