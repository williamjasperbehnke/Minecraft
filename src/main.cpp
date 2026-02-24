// Move all includes to the top
#include "core/Logger.hpp"
#include "game/AudioSystem.hpp"
#include "game/Camera.hpp"
#include "game/CraftingSystem.hpp"
#include "game/DebugMenu.hpp"
#include "game/GameRules.hpp"
#include "game/Inventory.hpp"
#include "game/ItemDropSystem.hpp"
#include "game/MapSystem.hpp"
#include "game/Player.hpp"
#include "game/SmeltingSystem.hpp"
#include "gfx/HudRenderer.hpp"
#include "gfx/Shader.hpp"
#include "gfx/TextureAtlas.hpp"
#include "voxel/Block.hpp"
#include "voxel/Raycaster.hpp"
#include "world/World.hpp"
#include <glad/glad.h>
#include <GLFW/glfw3.h>
#include <glm/ext/matrix_clip_space.hpp>
#include <glm/ext/scalar_constants.hpp>
#include <glm/geometric.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <ctime>
#include <exception>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iomanip>
#include <limits>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

namespace {
float gRecipeMenuScrollDelta = 0.0f;

void onFramebufferResize(GLFWwindow * /*window*/, int width, int height) {
    glViewport(0, 0, width, height);
}

void onMouseScroll(GLFWwindow * /*window*/, double /*xoffset*/, double yoffset) {
    gRecipeMenuScrollDelta += static_cast<float>(yoffset);
}

unsigned int compileInlineShader(unsigned int type, const char *src) {
    const unsigned int shader = glCreateShader(type);
    glShaderSource(shader, 1, &src, nullptr);
    glCompileShader(shader);
    int ok = 0;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[1024];
        glGetShaderInfoLog(shader, sizeof(log), nullptr, log);
        throw std::runtime_error(std::string("Sky shader compile failed: ") + log);
    }
    return shader;
}

unsigned int linkInlineProgram(unsigned int vs, unsigned int fs) {
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
        throw std::runtime_error(std::string("Sky shader link failed: ") + log);
    }
    return prog;
}

float hash01(int i, int salt) {
    const float x = static_cast<float>(i * 92821 + salt * 68917);
    const float s = std::sin(x * 0.000123f) * 43758.5453f;
    return s - std::floor(s);
}

constexpr int kCloudRenderRange = 11;
constexpr float kCloudCellSize = 16.0f;
constexpr float kCloudLayerY = 176.0f;
constexpr float kCloudDriftXSpeed = 0.35f;
constexpr float kCloudDriftZSpeed = 0.12f;
constexpr float kCloudQuadRadius = 0.500f;
constexpr float kCloudShadowRange = kCloudCellSize * static_cast<float>(kCloudRenderRange);

std::string compassTextFromForward(const glm::vec3 &forward) {
    const float x = forward.x;
    const float z = forward.z;
    if (std::abs(x) < 1e-4f && std::abs(z) < 1e-4f) {
        return "Compass: N";
    }
    // 0 rad is North, pi/2 East, pi South, 3pi/2 West.
    float a = std::atan2(x, -z);
    if (a < 0.0f) {
        a += glm::pi<float>() * 2.0f;
    }
    const int oct = static_cast<int>(std::floor((a + glm::pi<float>() / 8.0f) /
                                                 (glm::pi<float>() / 4.0f))) %
                    8;
    static const char *kDir[8] = {"N", "NE", "E", "SE", "S", "SW", "W", "NW"};
    return std::string("Compass: ") + kDir[oct];
}

class SkyBodyRenderer {
  public:
    enum class BodyType { Sun = 0, Moon = 1, Star = 2, Cloud = 3 };

    ~SkyBodyRenderer() {
        if (glfwGetCurrentContext() == nullptr) {
            return;
        }
        if (vbo_ != 0) {
            glDeleteBuffers(1, &vbo_);
        }
        if (vao_ != 0) {
            glDeleteVertexArrays(1, &vao_);
        }
        if (program_ != 0) {
            glDeleteProgram(program_);
        }
    }

    void draw(const glm::mat4 &proj, const glm::mat4 &view, const glm::vec3 &center,
              const glm::vec3 &camRight, const glm::vec3 &camUp, float radius,
              const glm::vec3 &color, float glow, BodyType type, float phase01 = 0.0f) {
        init();
        glUseProgram(program_);
        glUniformMatrix4fv(glGetUniformLocation(program_, "uProj"), 1, GL_FALSE, &proj[0][0]);
        glUniformMatrix4fv(glGetUniformLocation(program_, "uView"), 1, GL_FALSE, &view[0][0]);
        glUniform3f(glGetUniformLocation(program_, "uCenter"), center.x, center.y, center.z);
        glUniform3f(glGetUniformLocation(program_, "uRight"), camRight.x, camRight.y, camRight.z);
        glUniform3f(glGetUniformLocation(program_, "uUp"), camUp.x, camUp.y, camUp.z);
        glUniform1f(glGetUniformLocation(program_, "uRadius"), radius);
        glUniform3f(glGetUniformLocation(program_, "uColor"), color.r, color.g, color.b);
        glUniform1f(glGetUniformLocation(program_, "uGlow"), glow);
        int bodyType = 0;
        switch (type) {
        case BodyType::Sun:
            bodyType = 0;
            break;
        case BodyType::Moon:
            bodyType = 1;
            break;
        case BodyType::Star:
            bodyType = 2;
            break;
        case BodyType::Cloud:
            bodyType = 3;
            break;
        }
        glUniform1i(glGetUniformLocation(program_, "uBodyType"), bodyType);
        glUniform1f(glGetUniformLocation(program_, "uPhase01"), phase01);
        glBindVertexArray(vao_);
        glDrawArrays(GL_TRIANGLES, 0, 6);
    }

  private:
    void init() {
        if (ready_) {
            return;
        }
        const char *vs = R"(
#version 330 core
layout(location = 0) in vec2 aCorner;
uniform mat4 uProj;
uniform mat4 uView;
uniform vec3 uCenter;
uniform vec3 uRight;
uniform vec3 uUp;
uniform float uRadius;
out vec2 vUV;
void main() {
  vec3 worldPos = uCenter + (aCorner.x * uRight + aCorner.y * uUp) * uRadius;
  vUV = aCorner * 0.5 + 0.5;
  gl_Position = uProj * uView * vec4(worldPos, 1.0);
}
)";
        const char *fs = R"(
#version 330 core
in vec2 vUV;
uniform vec3 uColor;
uniform float uGlow;
uniform int uBodyType;
uniform float uPhase01;
out vec4 FragColor;
void main() {
  vec2 p = vUV - vec2(0.5);
  float edge = max(abs(p.x), abs(p.y));
  if (edge > 0.5) discard;
  float bodyMask = 1.0 - smoothstep(0.47, 0.50, edge);

  vec3 c = uColor;
  if (uBodyType == 0) {
    float core = 1.0 - smoothstep(0.10, 0.40, edge);
    c = uColor * (0.92 + 0.18 * core) + vec3(uGlow) * (0.25 + 0.75 * core);
  } else if (uBodyType == 1) {
    vec2 g = floor(vUV * 8.0);
    float n = fract(sin(dot(g, vec2(12.9898, 78.233))) * 43758.5453);
    float crater = step(0.86, n) * 0.11;
    c = uColor - vec3(crater);

    // Moon phase: 0.0=new, 0.5=full, with waxing/waning side swap.
    float phase = fract(uPhase01);
    float illum = 0.5 - 0.5 * cos(phase * 6.28318530718);
    float side = (phase < 0.5) ? 1.0 : -1.0;
    float px = p.x * side;

    float lit = 0.0;
    if (illum >= 0.995) {
      lit = 1.0;
    } else if (illum <= 0.005) {
      lit = 0.0;
    } else {
      float terminator = mix(0.50, -0.50, illum);
      lit = smoothstep(terminator - 0.04, terminator + 0.04, px);
    }
    c *= mix(0.16, 1.0, lit);
  } else if (uBodyType == 2) {
    float d = length(p) * 2.0;
    float core = 1.0 - smoothstep(0.05, 0.26, d);
    float halo = 1.0 - smoothstep(0.10, 0.52, d);
    c = uColor * (0.40 + 0.60 * core) + vec3(uGlow) * halo * 1.7;
    bodyMask = clamp(core + halo * 0.85, 0.0, 1.0);
  } else {
    // Keep cloud tiles uniform so adjacent quads merge without visible seam patterns.
    c = uColor;
    bodyMask = 0.82;
  }

  FragColor = vec4(c, bodyMask);
}
)";
        program_ = linkInlineProgram(compileInlineShader(GL_VERTEX_SHADER, vs),
                                     compileInlineShader(GL_FRAGMENT_SHADER, fs));

        const float quad[12] = {
            -1.0f, -1.0f, 1.0f, -1.0f, 1.0f, 1.0f, -1.0f, -1.0f, 1.0f, 1.0f, -1.0f, 1.0f,
        };
        glGenVertexArrays(1, &vao_);
        glGenBuffers(1, &vbo_);
        glBindVertexArray(vao_);
        glBindBuffer(GL_ARRAY_BUFFER, vbo_);
        glBufferData(GL_ARRAY_BUFFER, sizeof(quad), quad, GL_STATIC_DRAW);
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 2 * sizeof(float), nullptr);
        ready_ = true;
    }

    bool ready_ = false;
    unsigned int program_ = 0;
    unsigned int vao_ = 0;
    unsigned int vbo_ = 0;
};

class ChunkBorderRenderer {
  public:
    ~ChunkBorderRenderer() {
        if (glfwGetCurrentContext() == nullptr) {
            return;
        }
        if (vbo_ != 0) {
            glDeleteBuffers(1, &vbo_);
        }
        if (vao_ != 0) {
            glDeleteVertexArrays(1, &vao_);
        }
        if (program_ != 0) {
            glDeleteProgram(program_);
        }
    }

    void draw(const glm::mat4 &proj, const glm::mat4 &view, const std::vector<glm::vec3> &verts) {
        if (verts.empty()) {
            return;
        }
        init();
        glUseProgram(program_);
        glUniformMatrix4fv(glGetUniformLocation(program_, "uProj"), 1, GL_FALSE, &proj[0][0]);
        glUniformMatrix4fv(glGetUniformLocation(program_, "uView"), 1, GL_FALSE, &view[0][0]);
        glBindVertexArray(vao_);
        glBindBuffer(GL_ARRAY_BUFFER, vbo_);
        glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(verts.size() * sizeof(glm::vec3)),
                     verts.data(), GL_DYNAMIC_DRAW);
        glDrawArrays(GL_LINES, 0, static_cast<GLsizei>(verts.size()));
    }

  private:
    void init() {
        if (ready_) {
            return;
        }
        const char *vs = R"(
#version 330 core
layout(location = 0) in vec3 aPos;
uniform mat4 uProj;
uniform mat4 uView;
void main() {
  gl_Position = uProj * uView * vec4(aPos, 1.0);
}
)";
        const char *fs = R"(
#version 330 core
out vec4 FragColor;
void main() {
  FragColor = vec4(1.0, 0.88, 0.20, 0.90);
}
)";
        program_ = linkInlineProgram(compileInlineShader(GL_VERTEX_SHADER, vs),
                                     compileInlineShader(GL_FRAGMENT_SHADER, fs));

        glGenVertexArrays(1, &vao_);
        glGenBuffers(1, &vbo_);
        glBindVertexArray(vao_);
        glBindBuffer(GL_ARRAY_BUFFER, vbo_);
        glBufferData(GL_ARRAY_BUFFER, 0, nullptr, GL_DYNAMIC_DRAW);
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(glm::vec3), nullptr);
        ready_ = true;
    }

    bool ready_ = false;
    unsigned int program_ = 0;
    unsigned int vao_ = 0;
    unsigned int vbo_ = 0;
};

struct WorldEntry {
    std::string name;
    std::filesystem::path path;
    std::uint32_t seed = 1337u;
};

struct WorldSelection {
    bool start = false;
    std::string name;
    std::filesystem::path path;
    std::uint32_t seed = 1337u;
};

struct TitleTextInputState {
    bool active = false;
    bool editSeed = false;
    std::string *name = nullptr;
    std::string *seed = nullptr;
};

TitleTextInputState *gTitleInputState = nullptr;
std::string *gRecipeSearchText = nullptr;

void onTitleCharInput(GLFWwindow * /*window*/, unsigned int codepoint) {
    if (gTitleInputState == nullptr || !gTitleInputState->active) {
        return;
    }
    if (codepoint > 127u) {
        return;
    }

    const char ch = static_cast<char>(codepoint);
    if (gTitleInputState->editSeed) {
        if (gTitleInputState->seed == nullptr) {
            return;
        }
        if (!std::isdigit(static_cast<unsigned char>(ch)) &&
            !(ch == '-' && gTitleInputState->seed->empty())) {
            return;
        }
        if (gTitleInputState->seed->size() < 16) {
            gTitleInputState->seed->push_back(ch);
        }
        return;
    }

    if (gTitleInputState->name == nullptr) {
        return;
    }
    const bool allowed =
        std::isalnum(static_cast<unsigned char>(ch)) || ch == ' ' || ch == '_' || ch == '-';
    if (allowed && gTitleInputState->name->size() < 24) {
        gTitleInputState->name->push_back(ch);
    }
}

void onRecipeSearchCharInput(GLFWwindow * /*window*/, unsigned int codepoint) {
    if (gRecipeSearchText == nullptr) {
        return;
    }
    if (codepoint > 127u) {
        return;
    }
    const char ch = static_cast<char>(codepoint);
    if (std::isprint(static_cast<unsigned char>(ch)) && gRecipeSearchText->size() < 32) {
        gRecipeSearchText->push_back(ch);
    }
}

std::filesystem::path worldsRootPath() {
    return "worlds";
}

bool hasChunkFiles(const std::filesystem::path &worldDir) {
    if (!std::filesystem::exists(worldDir) || !std::filesystem::is_directory(worldDir)) {
        return false;
    }
    for (const auto &entry : std::filesystem::directory_iterator(worldDir)) {
        if (!entry.is_regular_file()) {
            continue;
        }
        const std::string name = entry.path().filename().string();
        if (name.rfind("chunk_", 0) == 0 && entry.path().extension() == ".bin") {
            return true;
        }
    }
    return false;
}

bool loadWorldMeta(const std::filesystem::path &worldDir, std::string &outName,
                   std::uint32_t &outSeed) {
    std::ifstream in(worldDir / "world.meta");
    if (!in) {
        return false;
    }
    std::string line;
    bool hasName = false;
    bool hasSeed = false;
    while (std::getline(in, line)) {
        const std::size_t eq = line.find('=');
        if (eq == std::string::npos) {
            continue;
        }
        const std::string key = line.substr(0, eq);
        const std::string value = line.substr(eq + 1);
        if (key == "name") {
            outName = value;
            hasName = true;
        } else if (key == "seed") {
            try {
                outSeed = static_cast<std::uint32_t>(std::stoll(value));
                hasSeed = true;
            } catch (...) {
                hasSeed = false;
            }
        }
    }
    return hasName && hasSeed;
}

void saveWorldMeta(const std::filesystem::path &worldDir, const std::string &name,
                   std::uint32_t seed) {
    std::filesystem::create_directories(worldDir);
    std::ofstream out(worldDir / "world.meta", std::ios::trunc);
    if (!out) {
        return;
    }
    out << "version=1\n";
    out << "name=" << name << "\n";
    out << "seed=" << seed << "\n";
}

std::string sanitizeWorldFolderName(const std::string &name) {
    std::string out;
    out.reserve(name.size());
    for (char c : name) {
        if (std::isalnum(static_cast<unsigned char>(c))) {
            out.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
        } else if (c == '-' || c == '_' || c == ' ') {
            out.push_back('_');
        }
    }
    while (!out.empty() && out.front() == '_') {
        out.erase(out.begin());
    }
    while (!out.empty() && out.back() == '_') {
        out.pop_back();
    }
    if (out.empty()) {
        out = "world";
    }
    return out;
}

std::uint32_t parseSeedString(const std::string &seedText) {
    if (seedText.empty()) {
        return static_cast<std::uint32_t>(std::rand());
    }
    try {
        return static_cast<std::uint32_t>(std::stoll(seedText));
    } catch (...) {
        return static_cast<std::uint32_t>(std::hash<std::string>{}(seedText));
    }
}

std::vector<WorldEntry> discoverWorlds() {
    std::vector<WorldEntry> worlds;
    const std::filesystem::path root = worldsRootPath();
    std::filesystem::create_directories(root);

    for (const auto &entry : std::filesystem::directory_iterator(root)) {
        if (!entry.is_directory()) {
            continue;
        }
        const std::filesystem::path path = entry.path();
        std::string name = path.filename().string();
        std::uint32_t seed = 1337u;
        const bool hasMeta = loadWorldMeta(path, name, seed);
        if (!hasMeta && !hasChunkFiles(path)) {
            continue;
        }
        worlds.push_back(WorldEntry{name, path, seed});
    }

    const std::filesystem::path legacy = "world";
    if (std::filesystem::exists(legacy) && std::filesystem::is_directory(legacy) &&
        hasChunkFiles(legacy)) {
        std::string name = "Legacy World";
        std::uint32_t seed = 1337u;
        loadWorldMeta(legacy, name, seed);
        worlds.push_back(WorldEntry{name, legacy, seed});
    }

    std::sort(worlds.begin(), worlds.end(),
              [](const WorldEntry &a, const WorldEntry &b) { return a.name < b.name; });
    return worlds;
}

std::filesystem::path allocateWorldPath(const std::string &worldName) {
    const std::filesystem::path root = worldsRootPath();
    std::filesystem::create_directories(root);
    const std::string baseName = sanitizeWorldFolderName(worldName);
    std::filesystem::path candidate = root / baseName;
    int suffix = 2;
    while (std::filesystem::exists(candidate)) {
        candidate = root / (baseName + "_" + std::to_string(suffix));
        ++suffix;
    }
    return candidate;
}

bool loadPlayerData(const std::filesystem::path &worldDir, glm::vec3 &cameraPos, int &selectedSlot,
                    bool &ghostMode, game::Inventory &inventory,
                    game::SmeltingSystem::State &smelting) {
    std::ifstream in(worldDir / "player.dat");
    if (!in) {
        return false;
    }

    std::string magic;
    in >> magic;
    if (!in || (magic != "VXP1" && magic != "VXP2" && magic != "VXP3")) {
        return false;
    }

    glm::vec3 loadedPos{};
    int loadedSelected = 0;
    int loadedMode = 1;
    in >> loadedPos.x >> loadedPos.y >> loadedPos.z;
    in >> loadedSelected;
    if (magic == "VXP2" || magic == "VXP3") {
        in >> loadedMode;
    }
    if (!in) {
        return false;
    }

    std::array<game::Inventory::Slot, game::Inventory::kSlotCount> loadedSlots{};
    for (int i = 0; i < game::Inventory::kSlotCount; ++i) {
        int id = 0;
        int count = 0;
        in >> id >> count;
        if (!in) {
            return false;
        }
        count = std::clamp(count, 0, game::Inventory::kMaxStack);
        if (count == 0) {
            loadedSlots[i] = {};
            continue;
        }
        if (id < 0 || id > std::numeric_limits<std::uint16_t>::max()) {
            loadedSlots[i] = {};
            continue;
        }
        loadedSlots[i].id = static_cast<voxel::BlockId>(id);
        loadedSlots[i].count = count;
    }

    for (int i = 0; i < game::Inventory::kSlotCount; ++i) {
        inventory.slot(i) = loadedSlots[i];
    }
    selectedSlot = std::clamp(loadedSelected, 0, game::Inventory::kHotbarSize - 1);
    ghostMode = (loadedMode != 0);
    cameraPos = loadedPos;
    smelting = {};
    if (magic == "VXP3") {
        auto loadSmeltSlot = [&](game::Inventory::Slot &slot) {
            int id = 0;
            int count = 0;
            in >> id >> count;
            if (!in) {
                return false;
            }
            count = std::clamp(count, 0, game::Inventory::kMaxStack);
            if (count <= 0 || id < 0 || id > std::numeric_limits<std::uint16_t>::max()) {
                slot = {};
                return true;
            }
            slot.id = static_cast<voxel::BlockId>(id);
            slot.count = count;
            return true;
        };
        if (!loadSmeltSlot(smelting.input) || !loadSmeltSlot(smelting.fuel) ||
            !loadSmeltSlot(smelting.output)) {
            return false;
        }
        in >> smelting.progressSeconds >> smelting.burnSecondsRemaining >>
            smelting.burnSecondsCapacity;
        if (!in) {
            return false;
        }
        smelting.progressSeconds =
            std::clamp(smelting.progressSeconds, 0.0f, game::SmeltingSystem::kSmeltSeconds);
        smelting.burnSecondsRemaining = std::max(0.0f, smelting.burnSecondsRemaining);
        smelting.burnSecondsCapacity = std::max(0.0f, smelting.burnSecondsCapacity);
        if (smelting.burnSecondsCapacity > 0.0f &&
            smelting.burnSecondsRemaining > smelting.burnSecondsCapacity) {
            smelting.burnSecondsRemaining = smelting.burnSecondsCapacity;
        }
        if (smelting.fuel.id == voxel::AIR || smelting.fuel.count <= 0) {
            smelting.fuel = {};
        }
        if (smelting.input.id == voxel::AIR || smelting.input.count <= 0) {
            smelting.input = {};
            smelting.progressSeconds = 0.0f;
        }
        if (smelting.output.id == voxel::AIR || smelting.output.count <= 0) {
            smelting.output = {};
        }
    }
    return true;
}

void savePlayerData(const std::filesystem::path &worldDir, const glm::vec3 &cameraPos,
                    int selectedSlot, bool ghostMode, const game::Inventory &inventory,
                    const game::SmeltingSystem::State &smelting) {
    std::filesystem::create_directories(worldDir);
    std::ofstream out(worldDir / "player.dat", std::ios::trunc);
    if (!out) {
        return;
    }

    out << "VXP3\n";
    out << std::fixed << std::setprecision(std::numeric_limits<float>::max_digits10) << cameraPos.x
        << ' ' << cameraPos.y << ' '
        << cameraPos.z << '\n';
    out << std::clamp(selectedSlot, 0, game::Inventory::kHotbarSize - 1) << '\n';
    out << (ghostMode ? 1 : 0) << '\n';
    for (int i = 0; i < game::Inventory::kSlotCount; ++i) {
        const auto &slot = inventory.slot(i);
        out << static_cast<int>(slot.id) << ' '
            << std::clamp(slot.count, 0, game::Inventory::kMaxStack) << '\n';
    }
    auto writeSmeltSlot = [&](const game::Inventory::Slot &slot) {
        out << static_cast<int>(slot.id) << ' '
            << std::clamp(slot.count, 0, game::Inventory::kMaxStack) << '\n';
    };
    writeSmeltSlot(smelting.input);
    writeSmeltSlot(smelting.fuel);
    writeSmeltSlot(smelting.output);
    out << std::max(0.0f, smelting.progressSeconds) << ' '
        << std::max(0.0f, smelting.burnSecondsRemaining) << ' '
        << std::max(0.0f, smelting.burnSecondsCapacity) << '\n';
}

WorldSelection runTitleMenu(GLFWwindow *window, gfx::HudRenderer &hud) {
    auto worlds = discoverWorlds();
    int selectedWorld = worlds.empty() ? -1 : 0;

    std::string createName = "New World";
    std::string createSeed = std::to_string(static_cast<int>(std::rand()));
    bool createMode = false;
    bool editSeed = false;

    bool prevBackspace = false;
    bool prevLeftMouse = false;
    float lastRowClickTime = -10.0f;
    int lastRowClickIndex = -1;

    TitleTextInputState inputState{};
    gTitleInputState = &inputState;
    glfwSetCharCallback(window, onTitleCharInput);
    glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_NORMAL);

    while (!glfwWindowShouldClose(window)) {
        glfwPollEvents();

        inputState.active = createMode;
        inputState.editSeed = editSeed;
        inputState.name = &createName;
        inputState.seed = &createSeed;

        const bool backspace = glfwGetKey(window, GLFW_KEY_BACKSPACE) == GLFW_PRESS;
        const bool leftMouse = glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_LEFT) == GLFW_PRESS;
        double mx = 0.0;
        double my = 0.0;
        glfwGetCursorPos(window, &mx, &my);

        int winW = 1;
        int winH = 1;
        glfwGetWindowSize(window, &winW, &winH);
        const float w = static_cast<float>(winW);
        const float h = static_cast<float>(winH);
        const float cx = w * 0.5f;
        const float panelW = std::min(760.0f, w - 60.0f);
        const float panelX = cx - panelW * 0.5f;
        const float panelY = 118.0f;
        const float panelH = h - 170.0f;
        const float rowX = panelX + 18.0f;
        const float rowW = panelW - 36.0f;
        const float rowY0 = panelY + 48.0f;
        const float rowH = 30.0f;
        const float listH = panelH - 130.0f;
        const int maxRows = static_cast<int>(std::max(1.0f, (listH - 4.0f) / rowH));
        int scroll = 0;
        if (selectedWorld >= maxRows) {
            scroll = selectedWorld - maxRows + 1;
        }
        const int visibleRows =
            std::max(0, std::min(maxRows, static_cast<int>(worlds.size()) - scroll));
        const float btnY = panelY + panelH - 68.0f;
        const float btnW = (rowW - 12.0f) * 0.25f;
        const float btnH = 36.0f;

        const float modalW = std::min(560.0f, w - 80.0f);
        const float modalH = 238.0f;
        const float modalX = cx - modalW * 0.5f;
        const float modalY = h * 0.5f - modalH * 0.5f;
        const float fieldX = modalX + 16.0f;
        const float fieldW = modalW - 32.0f;
        const float nameY = modalY + 58.0f;
        const float seedY = modalY + 104.0f;
        const float createBtnY = modalY + modalH - 48.0f;
        const float createBtnW = (fieldW - 8.0f) * 0.5f;
        const float createBtnX = fieldX;
        const float cancelBtnX = fieldX + createBtnW + 8.0f;
        const float modalBtnH = 34.0f;

        if (leftMouse && !prevLeftMouse) {
            if (!createMode) {
                for (int i = 0; i < visibleRows; ++i) {
                    const int idx = scroll + i;
                    const float y = rowY0 + static_cast<float>(i) * rowH;
                    if (mx >= rowX && mx <= (rowX + rowW) && my >= y && my <= (y + rowH - 4.0f)) {
                        selectedWorld = idx;
                        const float now = static_cast<float>(glfwGetTime());
                        if (lastRowClickIndex == idx && (now - lastRowClickTime) <= 0.30f) {
                            glfwSetCharCallback(window, nullptr);
                            gTitleInputState = nullptr;
                            return WorldSelection{true, worlds[idx].name, worlds[idx].path,
                                                  worlds[idx].seed};
                        }
                        lastRowClickIndex = idx;
                        lastRowClickTime = now;
                    }
                }
            }

            // Main action buttons.
            if (!createMode && mx >= rowX && mx <= (rowX + btnW) && my >= btnY &&
                my <= (btnY + btnH)) {
                if (!worlds.empty() && selectedWorld >= 0 &&
                    selectedWorld < static_cast<int>(worlds.size())) {
                    glfwSetCharCallback(window, nullptr);
                    gTitleInputState = nullptr;
                    return WorldSelection{true, worlds[selectedWorld].name,
                                          worlds[selectedWorld].path, worlds[selectedWorld].seed};
                }
            } else if (mx >= (rowX + btnW + 4.0f) && mx <= (rowX + 2.0f * btnW + 4.0f) &&
                       my >= btnY && my <= (btnY + btnH)) {
                createMode = true;
                editSeed = false;
            } else if (!createMode && mx >= (rowX + 2.0f * (btnW + 4.0f)) &&
                       mx <= (rowX + 3.0f * btnW + 8.0f) && my >= btnY && my <= (btnY + btnH)) {
                worlds = discoverWorlds();
                if (worlds.empty()) {
                    selectedWorld = -1;
                } else if (selectedWorld < 0 || selectedWorld >= static_cast<int>(worlds.size())) {
                    selectedWorld = 0;
                }
            } else if (!createMode && mx >= (rowX + 3.0f * (btnW + 4.0f)) &&
                       mx <= (rowX + 4.0f * btnW + 12.0f) && my >= btnY && my <= (btnY + btnH)) {
                glfwSetCharCallback(window, nullptr);
                gTitleInputState = nullptr;
                return {};
            }

            if (createMode) {
                if (mx >= fieldX && mx <= (fieldX + fieldW) && my >= nameY &&
                    my <= (nameY + 30.0f)) {
                    editSeed = false;
                } else if (mx >= fieldX && mx <= (fieldX + fieldW) && my >= seedY &&
                           my <= (seedY + 30.0f)) {
                    editSeed = true;
                } else if (mx >= createBtnX && mx <= (createBtnX + createBtnW) &&
                           my >= createBtnY && my <= (createBtnY + modalBtnH)) {
                    const std::string finalName = createName.empty() ? "New World" : createName;
                    const std::uint32_t seed = parseSeedString(createSeed);
                    const std::filesystem::path worldPath = allocateWorldPath(finalName);
                    saveWorldMeta(worldPath, finalName, seed);
                    glfwSetCharCallback(window, nullptr);
                    gTitleInputState = nullptr;
                    return WorldSelection{true, finalName, worldPath, seed};
                } else if (mx >= cancelBtnX && mx <= (cancelBtnX + createBtnW) &&
                           my >= createBtnY && my <= (createBtnY + modalBtnH)) {
                    createMode = false;
                }
            }
        }
        if (createMode && backspace && !prevBackspace) {
            std::string &target = editSeed ? createSeed : createName;
            if (!target.empty()) {
                target.pop_back();
            }
        }
        prevBackspace = backspace;

        std::vector<std::string> worldLines;
        worldLines.reserve(worlds.size());
        for (const auto &world : worlds) {
            worldLines.push_back(world.name + "  [seed " + std::to_string(world.seed) + "]");
        }

        glClearColor(0.12f, 0.18f, 0.24f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        hud.renderTitleScreen(winW, winH, worldLines, selectedWorld, createMode, editSeed,
                              createName, createSeed, static_cast<float>(mx),
                              static_cast<float>(my));
        glfwSwapBuffers(window);
        prevLeftMouse = leftMouse;
    }

    glfwSetCharCallback(window, nullptr);
    gTitleInputState = nullptr;
    return {};
}

constexpr int kCraftInputCount = game::CraftingSystem::kInputCount;
constexpr int kCraftOutputSlot = game::CraftingSystem::kOutputSlotIndex;
constexpr int kUiSlotCount = game::CraftingSystem::kUiSlotCount;
constexpr int kTrashSlot = kUiSlotCount;
constexpr int kUiSlotCountWithTrash = kTrashSlot + 1;

int inventorySlotAtCursor(double mx, double my, int width, int height, bool showInventory,
                          float hudScale, int craftingGridSize, bool usingFurnace) {
    if (!showInventory) {
        return -1;
    }
    const float cx = width * 0.5f;
    const float uiScale = std::clamp(hudScale, 0.8f, 1.8f);
    const float slot = 48.0f * uiScale;
    const float gap = 10.0f * uiScale;
    const float totalW = static_cast<float>(game::Inventory::kHotbarSize) * slot +
                         static_cast<float>(game::Inventory::kHotbarSize - 1) * gap;
    const float x0 = cx - totalW * 0.5f;
    const float y0 = height - 90.0f * uiScale;

    int nearestSlot = -1;
    float nearestDist2 = std::numeric_limits<float>::max();
    auto considerSlot = [&](int idx, float sx, float sy, float size) {
        if (mx >= sx && mx <= sx + size && my >= sy && my <= sy + size) {
            nearestSlot = idx;
            nearestDist2 = 0.0f;
            return;
        }
        const float nx = static_cast<float>(
            std::clamp(mx, static_cast<double>(sx), static_cast<double>(sx + size)));
        const float ny = static_cast<float>(
            std::clamp(my, static_cast<double>(sy), static_cast<double>(sy + size)));
        const float dx = static_cast<float>(mx) - nx;
        const float dy = static_cast<float>(my) - ny;
        const float dist2 = dx * dx + dy * dy;
        if (dist2 < nearestDist2) {
            nearestDist2 = dist2;
            nearestSlot = idx;
        }
    };

    for (int i = 0; i < game::Inventory::kHotbarSize; ++i) {
        const float sx = x0 + i * (slot + gap);
        considerSlot(i, sx, y0, slot);
    }

    const int cols = game::Inventory::kColumns;
    const int rows = game::Inventory::kRows - 1;
    const float invSlot = 48.0f * uiScale;
    const float invGap = 10.0f * uiScale;
    const float invW = cols * invSlot + (cols - 1) * invGap;
    const float invH = rows * invSlot + (rows - 1) * invGap;
    const int craftGrid = std::clamp(craftingGridSize, game::CraftingSystem::kGridSizeInventory,
                                     game::CraftingSystem::kGridSizeTable);
    const float craftSlot = invSlot;
    const float craftGap = invGap;
    const float craftGridW =
        static_cast<float>(craftGrid) * craftSlot + static_cast<float>(craftGrid - 1) * craftGap;
    const float craftPanelGap = 26.0f * uiScale;
    const float craftPanelW = craftGridW + 44.0f * uiScale + craftSlot;
    const float totalPanelW = invW + craftPanelGap + craftPanelW;
    const float tableGridW = 3.0f * craftSlot + 2.0f * craftGap;
    const float tablePanelW = tableGridW + 44.0f * uiScale + craftSlot;
    const float invX = cx - totalPanelW * 0.5f;
    const float invY = y0 - 44.0f * uiScale - invH;

    for (int row = 0; row < rows; ++row) {
        for (int col = 0; col < cols; ++col) {
            const float sx = invX + col * (invSlot + invGap);
            const float sy = invY + row * (invSlot + invGap);
            considerSlot(game::Inventory::kHotbarSize + row * cols + col, sx, sy, invSlot);
        }
    }

    // Crafting panel: 2x2 inventory or 3x3 crafting table.
    const float craftX = invX + invW + craftPanelGap;
    const float trashGap = 10.0f * uiScale;
    const float craftRegionH = craftGridW + trashGap + craftSlot;
    const float craftY = usingFurnace
                             ? invY
                             : ((craftGrid == game::CraftingSystem::kGridSizeTable)
                                    ? invY
                                    : (invY + (invH - craftRegionH) * 0.5f));
    const float furnaceInXBase = craftX + 4.0f * uiScale;
    const float furnaceInYBase = craftY + 2.0f * uiScale;
    const float furnaceFuelYBase = furnaceInYBase + craftSlot + 20.0f * uiScale;
    const float furnaceMidX = furnaceInXBase + craftSlot + 16.0f * uiScale;
    const float furnaceOutXBase = furnaceMidX + 42.0f * uiScale;
    if (usingFurnace) {
        considerSlot(game::Inventory::kSlotCount, furnaceInXBase, furnaceInYBase, craftSlot);
        considerSlot(game::Inventory::kSlotCount + 1, furnaceInXBase, furnaceFuelYBase, craftSlot);
    } else {
        for (int r = 0; r < craftGrid; ++r) {
            for (int c = 0; c < craftGrid; ++c) {
                const int idx = game::Inventory::kSlotCount + r * craftGrid + c;
                const float sx = craftX + c * (craftSlot + craftGap);
                const float sy = craftY + r * (craftSlot + craftGap);
                considerSlot(idx, sx, sy, craftSlot);
            }
        }
    }
    const float outX = usingFurnace ? furnaceOutXBase : (craftX + craftGridW + 44.0f * uiScale);
    const float outY = usingFurnace ? furnaceInYBase : (craftY + (craftGridW - craftSlot) * 0.5f);
    considerSlot(kCraftOutputSlot, outX, outY, craftSlot);
    const float trashX = craftX - 10.0f * uiScale;
    const float invBorderBottomY = invY + invH + 10.0f * uiScale;
    const float trashY = invBorderBottomY - craftSlot - 2.0f;
    considerSlot(kTrashSlot, trashX, trashY, craftSlot);
    const float snapRadius = 9.0f * uiScale;
    if (nearestSlot >= 0 && nearestSlot < kUiSlotCountWithTrash &&
        nearestDist2 <= snapRadius * snapRadius) {
        return nearestSlot;
    }
    return -1;
}

struct RecipeMenuLayout {
    float panelX = 0.0f;
    float panelY = 0.0f;
    float panelW = 0.0f;
    float panelH = 0.0f;
    float contentX = 0.0f;
    float contentY = 0.0f;
    float contentW = 0.0f;
    float contentH = 0.0f;
    int columns = 2;
    float cellW = 0.0f;
    float cellH = 0.0f;
    float cellGapX = 0.0f;
    float cellGapY = 0.0f;
    float gridInsetLeft = 0.0f;
    float gridInsetRight = 0.0f;
    float rowStride = 0.0f;
    float trackX = 0.0f;
    float trackY = 0.0f;
    float trackW = 0.0f;
    float trackH = 0.0f;
    float thumbH = 0.0f;
    float thumbY = 0.0f;
    float maxScroll = 0.0f;
    float searchX = 0.0f;
    float searchY = 0.0f;
    float searchW = 0.0f;
    float searchH = 0.0f;
    float craftableFilterX = 0.0f;
    float craftableFilterY = 0.0f;
    float craftableFilterSize = 0.0f;
    float craftableFilterW = 0.0f;
    float craftableFilterH = 0.0f;
    float ingredientTagX = 0.0f;
    float ingredientTagY = 0.0f;
    float ingredientTagW = 0.0f;
    float ingredientTagH = 0.0f;
    float ingredientTagCloseX = 0.0f;
    float ingredientTagCloseY = 0.0f;
    float ingredientTagCloseS = 0.0f;
};

RecipeMenuLayout computeRecipeMenuLayout(int width, int height, float hudScale, int craftingGridSize,
                                         bool usingFurnace, std::size_t recipeCount) {
    const float cx = width * 0.5f;
    const float uiScale = std::clamp(hudScale, 0.8f, 1.8f);
    const float y0 = height - 90.0f * uiScale;
    const int cols = game::Inventory::kColumns;
    const int rows = game::Inventory::kRows - 1;
    const float invSlot = 48.0f * uiScale;
    const float invGap = 10.0f * uiScale;
    const float invW = cols * invSlot + (cols - 1) * invGap;
    const float invH = rows * invSlot + (rows - 1) * invGap;
    const int craftGrid = std::clamp(craftingGridSize, game::CraftingSystem::kGridSizeInventory,
                                     game::CraftingSystem::kGridSizeTable);
    const float craftSlot = invSlot;
    const float craftGap = invGap;
    const float craftGridW =
        static_cast<float>(craftGrid) * craftSlot + static_cast<float>(craftGrid - 1) * craftGap;
    const float craftPanelGap = 26.0f * uiScale;
    const float craftPanelW = craftGridW + 44.0f * uiScale + craftSlot;
    const float totalPanelW = invW + craftPanelGap + craftPanelW;
    const float tableGridW = 3.0f * craftSlot + 2.0f * craftGap;
    const float tablePanelW = tableGridW + 44.0f * uiScale + craftSlot;
    const float invX = cx - totalPanelW * 0.5f;
    const float invY = y0 - 44.0f * uiScale - invH;

    const float recipeHeaderH = 34.0f * uiScale;
    const float recipeBodyH = 220.0f * uiScale;
    const float recipeH = recipeHeaderH + recipeBodyH;
    const float recipeY = invY - recipeH - 44.0f * uiScale;
    const float recipeW = totalPanelW;
    const float searchX = invX + 88.0f * uiScale;
    const float searchY = recipeY + 6.0f * uiScale;
    const float searchW = recipeW - 372.0f * uiScale;
    const float searchH = 22.0f * uiScale;
    const float craftableFilterSize = 14.0f * uiScale;
    const float craftableFilterW = 86.0f * uiScale;
    const float craftableFilterH = 16.0f * uiScale;
    const float craftableFilterX = recipeW + invX - 124.0f * uiScale;
    const float craftableFilterY = recipeY + 10.0f * uiScale;
    const float ingredientTagX = craftableFilterX - 154.0f * uiScale;
    const float ingredientTagY = craftableFilterY - 1.0f * uiScale;
    const float ingredientTagW = 146.0f * uiScale;
    const float ingredientTagH = 16.0f * uiScale;
    const float ingredientTagCloseS = 12.0f * uiScale;
    const float ingredientTagCloseX = ingredientTagX + ingredientTagW - ingredientTagCloseS -
                                      2.0f * uiScale;
    const float ingredientTagCloseY = ingredientTagY + 2.0f * uiScale;
    const float contentX = invX + 10.0f * uiScale;
    const float contentY = recipeY + recipeHeaderH;
    const float contentW = recipeW - 28.0f * uiScale;
    const float contentH = recipeBodyH - 10.0f * uiScale;
    const int columns = 2;
    const float cellGapX = 10.0f * uiScale;
    const float cellGapY = 10.0f * uiScale;
    const float gridInsetRight = 30.0f * uiScale;
    const float gridInsetLeft = gridInsetRight;
    const float cellW = (contentW - gridInsetLeft - gridInsetRight - cellGapX) * 0.5f;
    const float cellH = 78.0f * uiScale;
    const float rowStride = cellH + cellGapY;
    const int rowsCount = (static_cast<int>(recipeCount) + columns - 1) / columns;
    const float totalContentH = static_cast<float>(rowsCount) * cellH +
                                static_cast<float>(std::max(0, rowsCount - 1)) * cellGapY;
    const float maxScroll = std::max(0.0f, totalContentH - contentH);
    const float trackW = 8.0f * uiScale;
    const float trackX = invX + recipeW - 12.0f * uiScale;
    const float trackY = contentY;
    const float trackH = contentH;
    const float thumbH =
        (maxScroll > 0.0f) ? std::max(22.0f * uiScale, trackH * (contentH / totalContentH)) : trackH;

    return RecipeMenuLayout{
        invX,    recipeY, recipeW,  recipeH,  contentX, contentY, contentW, contentH, columns,
        cellW,   cellH,   cellGapX, cellGapY, gridInsetLeft, gridInsetRight, rowStride, trackX,
        trackY,  trackW,  trackH,
        thumbH,  trackY,  maxScroll, searchX, searchY,   searchW, searchH, craftableFilterX,
        craftableFilterY, craftableFilterSize, craftableFilterW, craftableFilterH, ingredientTagX,
        ingredientTagY, ingredientTagW, ingredientTagH, ingredientTagCloseX, ingredientTagCloseY,
        ingredientTagCloseS,
    };
}

int recipeRowAtCursor(double mx, double my, const RecipeMenuLayout &layout, float scroll,
                      std::size_t recipeCount) {
    if (mx < (layout.contentX + layout.gridInsetLeft) ||
        mx > (layout.contentX + layout.contentW - layout.gridInsetRight)) {
        return -1;
    }
    const int rowsCount = (static_cast<int>(recipeCount) + layout.columns - 1) / layout.columns;
    for (int row = 0; row < rowsCount; ++row) {
        const float ry = layout.contentY + static_cast<float>(row) * layout.rowStride - scroll;
        if (ry + layout.cellH < layout.contentY || ry > layout.contentY + layout.contentH) {
            continue;
        }
        for (int col = 0; col < layout.columns; ++col) {
            const int idx = row * layout.columns + col;
            if (idx >= static_cast<int>(recipeCount)) {
                break;
            }
            const float rx = layout.contentX + layout.gridInsetLeft +
                             static_cast<float>(col) * (layout.cellW + layout.cellGapX);
            if (mx >= rx && mx <= (rx + layout.cellW) && my >= ry && my <= (ry + layout.cellH)) {
                return idx;
            }
        }
    }
    return -1;
}

struct CreativeMenuLayout {
    float panelX = 0.0f;
    float panelY = 0.0f;
    float panelW = 0.0f;
    float panelH = 0.0f;
    float contentX = 0.0f;
    float contentY = 0.0f;
    float contentW = 0.0f;
    float contentH = 0.0f;
    float searchX = 0.0f;
    float searchY = 0.0f;
    float searchW = 0.0f;
    float searchH = 0.0f;
    int columns = 1;
    float cell = 0.0f;
    float cellGap = 0.0f;
    float gridInset = 0.0f;
    float rowStride = 0.0f;
    float maxScroll = 0.0f;
    float trackX = 0.0f;
    float trackY = 0.0f;
    float trackW = 0.0f;
    float trackH = 0.0f;
    float thumbH = 0.0f;
};

struct MapOverlayLayout {
    float panelX = 0.0f;
    float panelY = 0.0f;
    float panelW = 0.0f;
    float panelH = 0.0f;
    float gridX = 0.0f;
    float gridY = 0.0f;
    float gridW = 0.0f;
    float gridH = 0.0f;
    float cell = 4.0f;
};

struct MiniMapLayout {
    float panelX = 0.0f;
    float panelY = 0.0f;
    float panelW = 0.0f;
    float panelH = 0.0f;
    float compassX = 0.0f;
    float compassY = 0.0f;
    float compassW = 0.0f;
    float compassH = 0.0f;
    float followX = 0.0f;
    float followY = 0.0f;
    float followW = 0.0f;
    float followH = 0.0f;
    float waypointX = 0.0f;
    float waypointY = 0.0f;
    float waypointW = 0.0f;
    float waypointH = 0.0f;
    float minusX = 0.0f;
    float minusY = 0.0f;
    float minusW = 0.0f;
    float minusH = 0.0f;
    float plusX = 0.0f;
    float plusY = 0.0f;
    float plusW = 0.0f;
    float plusH = 0.0f;
};

struct WaypointEditorLayout {
    float panelX = 0.0f;
    float panelY = 0.0f;
    float panelW = 0.0f;
    float panelH = 0.0f;
    float nameX = 0.0f;
    float nameY = 0.0f;
    float nameW = 0.0f;
    float nameH = 0.0f;
    float colorX = 0.0f;
    float colorY = 0.0f;
    float colorS = 0.0f;
    float colorGap = 0.0f;
    float iconX = 0.0f;
    float iconY = 0.0f;
    float iconS = 0.0f;
    float iconGap = 0.0f;
    float closeX = 0.0f;
    float closeY = 0.0f;
    float closeS = 0.0f;
    float delX = 0.0f;
    float delY = 0.0f;
    float delW = 0.0f;
    float delH = 0.0f;
    float visibilityX = 0.0f;
    float visibilityY = 0.0f;
    float visibilityW = 0.0f;
    float visibilityH = 0.0f;
};

MapOverlayLayout computeMapOverlayLayout(int width, int height, float zoom) {
    const float w = static_cast<float>(width);
    const float h = static_cast<float>(height);
    const float panelW = std::max(320.0f, std::min(w - 80.0f, w * 0.86f));
    const float panelH = std::max(260.0f, std::min(h - 60.0f, h * 0.82f));
    const float panelX = (w - panelW) * 0.5f;
    const float panelY = (h - panelH) * 0.5f;
    const float innerPad = 14.0f;
    const float gridX = panelX + innerPad;
    const float gridY = panelY + innerPad + 18.0f;
    const float gridW = panelW - innerPad * 2.0f;
    const float gridH = panelH - innerPad * 2.0f - 24.0f;
    const float cell = std::clamp(4.0f * zoom, 2.0f, 14.0f);
    return {panelX, panelY, panelW, panelH, gridX, gridY, gridW, gridH, cell};
}

WaypointEditorLayout computeWaypointEditorLayout(const MapOverlayLayout &mapLayout) {
    const float panelW = 236.0f;
    const float panelH = 96.0f;
    const float panelX = mapLayout.panelX + (mapLayout.panelW - panelW) * 0.5f;
    const float panelY = mapLayout.panelY + (mapLayout.panelH - panelH) * 0.5f;
    const float nameX = panelX + 8.0f;
    const float nameY = panelY + 32.0f;
    const float nameW = panelW - 16.0f;
    const float nameH = 20.0f;
    const float colorX = panelX + 8.0f;
    const float colorY = panelY + 58.0f;
    const float colorS = 16.0f;
    const float colorGap = 6.0f;
    const float iconX = panelX + 118.0f;
    const float iconY = colorY;
    const float iconS = 18.0f;
    const float iconGap = 5.0f;
    const float closeS = 16.0f;
    const float closeX = panelX + panelW - closeS - 6.0f;
    const float closeY = panelY + 6.0f;
    const float visibilityW = 16.0f;
    const float visibilityH = 16.0f;
    const float delW = 54.0f;
    const float delH = 18.0f;
    const float visibilityX = closeX - 4.0f - visibilityW;
    const float delY = panelY + 6.0f;
    const float delX = visibilityX - 4.0f - delW;
    const float visibilityY = panelY + 7.0f;
    return {panelX, panelY, panelW, panelH, nameX, nameY, nameW, nameH, colorX, colorY,
            colorS, colorGap, iconX, iconY, iconS, iconGap, closeX, closeY, closeS, delX, delY,
            delW, delH, visibilityX, visibilityY, visibilityW, visibilityH};
}

MiniMapLayout computeMiniMapLayout(int width) {
    const float w = static_cast<float>(width);
    const float panelW = 190.0f;
    const float panelH = 190.0f;
    const float margin = 14.0f;
    const float panelX = w - panelW - margin;
    const float panelY = margin;
    const float btnH = 16.0f;
    const float btnY = panelY + panelH - btnH - 7.0f;
    const float compassW = 22.0f;
    const float followW = 22.0f;
    const float waypointW = 22.0f;
    const float minusW = 24.0f;
    const float plusW = 24.0f;
    const float gap = 4.0f;
    const float totalW = compassW + followW + waypointW + minusW + plusW + gap * 4.0f;
    const float btnX0 = panelX + panelW - totalW - 8.0f;
    const float followX = btnX0 + compassW + gap;
    const float waypointX = followX + followW + gap;
    const float minusX = waypointX + waypointW + gap;
    const float plusX = minusX + minusW + gap;
    return {panelX, panelY, panelW, panelH, btnX0, btnY, compassW, btnH, followX, btnY, followW,
            btnH, waypointX, btnY, waypointW, btnH, minusX, btnY, minusW, btnH, plusX, btnY, plusW,
            btnH};
}

CreativeMenuLayout computeCreativeMenuLayout(int width, int height, float hudScale,
                                             int craftingGridSize, bool usingFurnace,
                                             std::size_t itemCount) {
    const float cx = width * 0.5f;
    const float uiScale = std::clamp(hudScale, 0.8f, 1.8f);
    const float y0 = height - 90.0f * uiScale;
    const int cols = game::Inventory::kColumns;
    const int rows = game::Inventory::kRows - 1;
    const float invSlot = 48.0f * uiScale;
    const float invGap = 10.0f * uiScale;
    const float invW = cols * invSlot + (cols - 1) * invGap;
    const float invH = rows * invSlot + (rows - 1) * invGap;
    const int craftGrid = std::clamp(craftingGridSize, game::CraftingSystem::kGridSizeInventory,
                                     game::CraftingSystem::kGridSizeTable);
    const float craftSlot = invSlot;
    const float craftGap = invGap;
    const float craftGridW =
        static_cast<float>(craftGrid) * craftSlot + static_cast<float>(craftGrid - 1) * craftGap;
    const float craftPanelGap = 26.0f * uiScale;
    const float craftPanelW = craftGridW + 44.0f * uiScale + craftSlot;
    const float totalPanelW = invW + craftPanelGap + craftPanelW;
    const float tableGridW = 3.0f * craftSlot + 2.0f * craftGap;
    const float tablePanelW = tableGridW + 44.0f * uiScale + craftSlot;
    const float furnaceCenterOffset = usingFurnace ? (tablePanelW - craftPanelW) * 0.35f : 0.0f;
    const float invX = cx - totalPanelW * 0.5f + furnaceCenterOffset;
    const float invY = y0 - 44.0f * uiScale - invH;

    const float panelHeaderH = 34.0f * uiScale;
    const float panelBodyH = 220.0f * uiScale;
    const float panelH = panelHeaderH + panelBodyH;
    const float panelY = invY - panelH - 44.0f * uiScale;
    const float panelW = totalPanelW;

    const float searchX = invX + 88.0f * uiScale;
    const float searchY = panelY + 6.0f * uiScale;
    const float searchW = panelW - 126.0f * uiScale;
    const float searchH = 22.0f * uiScale;

    const float contentX = invX + 10.0f * uiScale;
    const float contentY = panelY + panelHeaderH;
    const float contentW = panelW - 28.0f * uiScale;
    const float contentH = panelBodyH - 10.0f * uiScale;

    const float cell = 40.0f * uiScale;
    const float cellGap = 8.0f * uiScale;
    const int columns =
        std::max(1, std::min(9, static_cast<int>((contentW + cellGap) / (cell + cellGap))));
    const float usedW =
        static_cast<float>(columns) * cell + static_cast<float>(columns - 1) * cellGap;
    const float gridInset = std::max(0.0f, (contentW - usedW) * 0.5f);
    const float rowStride = cell + cellGap;
    const int rowsCount = (static_cast<int>(itemCount) + columns - 1) / columns;
    const float totalContentH = static_cast<float>(rowsCount) * cell +
                                static_cast<float>(std::max(0, rowsCount - 1)) * cellGap;
    const float maxScroll = std::max(0.0f, totalContentH - contentH);

    const float trackX = invX + panelW - 12.0f * uiScale;
    const float trackY = contentY;
    const float trackW = 8.0f * uiScale;
    const float trackH = contentH;
    const float thumbH =
        (maxScroll > 0.0f) ? std::max(22.0f * uiScale, trackH * (contentH / totalContentH)) : trackH;

    return CreativeMenuLayout{
        invX, panelY, panelW, panelH, contentX, contentY, contentW, contentH, searchX, searchY,
        searchW, searchH, columns, cell, cellGap, gridInset, rowStride, maxScroll, trackX, trackY,
        trackW, trackH, thumbH,
    };
}

int creativeItemAtCursor(double mx, double my, const CreativeMenuLayout &layout, float scroll,
                         std::size_t itemCount) {
    if (mx < (layout.contentX + layout.gridInset) ||
        mx > (layout.contentX + layout.contentW - layout.gridInset) || my < layout.contentY ||
        my > (layout.contentY + layout.contentH)) {
        return -1;
    }
    const int rowsCount = (static_cast<int>(itemCount) + layout.columns - 1) / layout.columns;
    for (int row = 0; row < rowsCount; ++row) {
        const float sy = layout.contentY + static_cast<float>(row) * layout.rowStride - scroll;
        if (sy + layout.cell < layout.contentY || sy > (layout.contentY + layout.contentH)) {
            continue;
        }
        for (int col = 0; col < layout.columns; ++col) {
            const int idx = row * layout.columns + col;
            if (idx >= static_cast<int>(itemCount)) {
                break;
            }
            const float sx =
                layout.contentX + layout.gridInset + static_cast<float>(col) * (layout.cell + layout.cellGap);
            if (mx >= sx && mx <= (sx + layout.cell) && my >= sy && my <= (sy + layout.cell)) {
                return idx;
            }
        }
    }
    return -1;
}

std::string toLowerAscii(std::string s) {
    for (char &c : s) {
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    return s;
}

const std::vector<voxel::BlockId> &creativeCatalog() {
    static const std::vector<voxel::BlockId> kItems = {
        voxel::GRASS,        voxel::DIRT,        voxel::STONE,        voxel::SAND,
        voxel::WATER,        voxel::WOOD,        voxel::LEAVES,       voxel::SPRUCE_WOOD,
        voxel::SPRUCE_LEAVES, voxel::BIRCH_WOOD, voxel::BIRCH_LEAVES, voxel::CACTUS,
        voxel::SANDSTONE,    voxel::GRAVEL,      voxel::CLAY,         voxel::SNOW_BLOCK,
        voxel::ICE,          voxel::COAL_ORE,    voxel::COPPER_ORE,   voxel::IRON_ORE,
        voxel::GOLD_ORE,     voxel::DIAMOND_ORE, voxel::EMERALD_ORE,  voxel::TALL_GRASS,
        voxel::FLOWER,       voxel::TORCH,       voxel::CRAFTING_TABLE, voxel::OAK_PLANKS,
        voxel::SPRUCE_PLANKS, voxel::BIRCH_PLANKS, voxel::STICK, voxel::FURNACE,
        voxel::GLASS,        voxel::BRICKS,      voxel::IRON_INGOT,    voxel::COPPER_INGOT,
        voxel::GOLD_INGOT,
    };
    return kItems;
}

bool creativeItemMatchesSearch(voxel::BlockId id, const std::string &search) {
    const std::string needle = toLowerAscii(search);
    if (needle.empty()) {
        return true;
    }
    return toLowerAscii(game::blockName(id)).find(needle) != std::string::npos;
}

bool recipeMatchesSearch(const game::CraftingSystem::RecipeInfo &recipe, const std::string &search) {
    const std::string needle = toLowerAscii(search);
    if (needle.empty()) {
        return true;
    }
    if (toLowerAscii(recipe.label).find(needle) != std::string::npos) {
        return true;
    }
    if (toLowerAscii(game::blockName(recipe.outputId)).find(needle) != std::string::npos) {
        return true;
    }
    for (const auto &ingredient : recipe.ingredients) {
        if (ingredient.allowAnyWood) {
            if (std::string("wood").find(needle) != std::string::npos ||
                std::string("log").find(needle) != std::string::npos) {
                return true;
            }
        }
        if (ingredient.allowAnyPlanks) {
            if (std::string("plank").find(needle) != std::string::npos ||
                std::string("wood").find(needle) != std::string::npos) {
                return true;
            }
        }
        if (toLowerAscii(game::blockName(ingredient.id)).find(needle) != std::string::npos) {
            return true;
        }
    }
    return false;
}

bool recipeUsesIngredient(const game::CraftingSystem::RecipeInfo &recipe, voxel::BlockId targetId) {
    const bool isWoodTarget = (targetId == voxel::WOOD || targetId == voxel::SPRUCE_WOOD ||
                               targetId == voxel::BIRCH_WOOD);
    const bool isPlankTarget = (targetId == voxel::OAK_PLANKS ||
                                targetId == voxel::SPRUCE_PLANKS ||
                                targetId == voxel::BIRCH_PLANKS);
    for (const auto &ingredient : recipe.ingredients) {
        if (ingredient.allowAnyWood && isWoodTarget) {
            return true;
        }
        if (ingredient.allowAnyPlanks && isPlankTarget) {
            return true;
        }
        if (ingredient.id == targetId) {
            return true;
        }
    }
    for (const auto &cell : recipe.shapedCells) {
        if (cell.ingredient.allowAnyWood && isWoodTarget) {
            return true;
        }
        if (cell.ingredient.allowAnyPlanks && isPlankTarget) {
            return true;
        }
        if (cell.ingredient.id == targetId) {
            return true;
        }
    }
    return false;
}

bool smeltingRecipeMatchesSearch(const game::SmeltingSystem::Recipe &recipe,
                                 const std::string &search) {
    const std::string needle = toLowerAscii(search);
    if (needle.empty()) {
        return true;
    }
    if (toLowerAscii(game::blockName(recipe.input)).find(needle) != std::string::npos) {
        return true;
    }
    if (toLowerAscii(game::blockName(recipe.output)).find(needle) != std::string::npos) {
        return true;
    }
    return false;
}

bool recipeIngredientMatches(const game::CraftingSystem::RecipeInfo::IngredientInfo &ingredient,
                             voxel::BlockId id) {
    if (ingredient.allowAnyWood) {
        return id == voxel::WOOD || id == voxel::SPRUCE_WOOD || id == voxel::BIRCH_WOOD;
    }
    if (ingredient.allowAnyPlanks) {
        return id == voxel::OAK_PLANKS || id == voxel::SPRUCE_PLANKS || id == voxel::BIRCH_PLANKS;
    }
    return ingredient.id == id;
}

bool takeIngredientFromInventory(game::Inventory &inventory,
                                 const game::CraftingSystem::RecipeInfo::IngredientInfo &ingredient,
                                 voxel::BlockId &takenId) {
    for (int i = 0; i < game::Inventory::kSlotCount; ++i) {
        auto &slot = inventory.slot(i);
        if (slot.id == voxel::AIR || slot.count <= 0 || !recipeIngredientMatches(ingredient, slot.id)) {
            continue;
        }
        takenId = slot.id;
        slot.count -= 1;
        if (slot.count <= 0) {
            slot = {};
        }
        return true;
    }
    return false;
}

bool tryAddRecipeSet(const game::CraftingSystem::RecipeInfo &recipe, game::Inventory &inventory,
                     game::CraftingSystem::State &crafting, int activeInputs) {
    game::Inventory invBackup = inventory;
    game::CraftingSystem::State craftBackup = crafting;

    auto compatible = [&](const game::Inventory::Slot &slot,
                          const game::CraftingSystem::RecipeInfo::IngredientInfo &ingredient) {
        if (slot.id == voxel::AIR || slot.count <= 0) {
            return false;
        }
        return recipeIngredientMatches(ingredient, slot.id);
    };

    if (!recipe.shapedCells.empty()) {
        const int gridSize = static_cast<int>(std::round(std::sqrt(static_cast<float>(activeInputs))));
        if (gridSize * gridSize != activeInputs) {
            inventory = invBackup;
            crafting = craftBackup;
            return false;
        }
        const int baseSize = std::max(2, recipe.minGridSize);
        int minRow = baseSize;
        int minCol = baseSize;
        int maxRow = -1;
        int maxCol = -1;
        for (const auto &cell : recipe.shapedCells) {
            if (cell.slot < 0 || cell.slot >= baseSize * baseSize) {
                inventory = invBackup;
                crafting = craftBackup;
                return false;
            }
            const int row = cell.slot / baseSize;
            const int col = cell.slot % baseSize;
            minRow = std::min(minRow, row);
            minCol = std::min(minCol, col);
            maxRow = std::max(maxRow, row);
            maxCol = std::max(maxCol, col);
        }
        const int patternH = maxRow - minRow + 1;
        const int patternW = maxCol - minCol + 1;
        if (patternH <= 0 || patternW <= 0 || patternH > gridSize || patternW > gridSize) {
            inventory = invBackup;
            crafting = craftBackup;
            return false;
        }

        std::vector<int> mappedSlots(recipe.shapedCells.size(), -1);
        int bestScore = -1;
        bool foundPlacement = false;
        for (int offY = 0; offY <= (gridSize - patternH); ++offY) {
            for (int offX = 0; offX <= (gridSize - patternW); ++offX) {
                bool ok = true;
                int score = 0;
                std::vector<int> trialSlots(recipe.shapedCells.size(), -1);
                for (std::size_t i = 0; i < recipe.shapedCells.size(); ++i) {
                    const auto &cell = recipe.shapedCells[i];
                    const int row = cell.slot / baseSize;
                    const int col = cell.slot % baseSize;
                    const int relRow = row - minRow;
                    const int relCol = col - minCol;
                    const int mapped = (offY + relRow) * gridSize + (offX + relCol);
                    if (mapped < 0 || mapped >= activeInputs) {
                        ok = false;
                        break;
                    }
                    trialSlots[i] = mapped;
                    const auto &dst = crafting.input[mapped];
                    if (dst.id != voxel::AIR && dst.count > 0) {
                        if (!compatible(dst, cell.ingredient)) {
                            ok = false;
                            break;
                        }
                        score += 1;
                    }
                    if (dst.count + cell.ingredient.count > game::Inventory::kMaxStack) {
                        ok = false;
                        break;
                    }
                }
                if (ok && score > bestScore) {
                    mappedSlots = std::move(trialSlots);
                    bestScore = score;
                    foundPlacement = true;
                }
            }
        }
        if (!foundPlacement) {
            inventory = invBackup;
            crafting = craftBackup;
            return false;
        }

        for (std::size_t i = 0; i < recipe.shapedCells.size(); ++i) {
            const auto &cell = recipe.shapedCells[i];
            auto &dst = crafting.input[mappedSlots[i]];
            for (int n = 0; n < cell.ingredient.count; ++n) {
                voxel::BlockId taken = voxel::AIR;
                if (!takeIngredientFromInventory(inventory, cell.ingredient, taken)) {
                    inventory = invBackup;
                    crafting = craftBackup;
                    return false;
                }
                if (dst.id == voxel::AIR || dst.count <= 0) {
                    dst.id = taken;
                    dst.count = 0;
                }
                dst.count += 1;
            }
        }
        return true;
    }

    for (const auto &ingredient : recipe.ingredients) {
        for (int n = 0; n < ingredient.count; ++n) {
            voxel::BlockId taken = voxel::AIR;
            if (!takeIngredientFromInventory(inventory, ingredient, taken)) {
                inventory = invBackup;
                crafting = craftBackup;
                return false;
            }

            int placeIdx = -1;
            int firstEmpty = -1;
            int matchingOccupied = 0;
            int leastMatchingIdx = -1;
            int leastMatchingCount = game::Inventory::kMaxStack + 1;
            for (int i = 0; i < activeInputs; ++i) {
                const auto &dst = crafting.input[i];
                if (dst.id == voxel::AIR || dst.count <= 0) {
                    if (firstEmpty < 0) {
                        firstEmpty = i;
                    }
                    continue;
                }
                if (!recipeIngredientMatches(ingredient, dst.id)) {
                    continue;
                }
                ++matchingOccupied;
                if (dst.count < game::Inventory::kMaxStack && dst.count < leastMatchingCount) {
                    leastMatchingCount = dst.count;
                    leastMatchingIdx = i;
                }
            }

            // Keep shapeless recipes stable across repeated clicks:
            // first fill a fixed number of slots for this ingredient, then stack those slots.
            const int desiredSlotsForIngredient = std::max(1, ingredient.count);
            if (matchingOccupied < desiredSlotsForIngredient && firstEmpty >= 0) {
                placeIdx = firstEmpty;
            } else if (leastMatchingIdx >= 0) {
                placeIdx = leastMatchingIdx;
            } else if (firstEmpty >= 0) {
                placeIdx = firstEmpty;
            }
            if (placeIdx < 0) {
                inventory = invBackup;
                crafting = craftBackup;
                return false;
            }

            auto &dst = crafting.input[placeIdx];
            if (dst.id == voxel::AIR || dst.count <= 0) {
                dst.id = taken;
                dst.count = 0;
            }
            dst.count += 1;
        }
    }

    return true;
}

bool placeCellIntersectsPlayer(const glm::ivec3 &placeCell, const glm::vec3 &cameraPos) {
    constexpr float kHalfWidth = 0.30f;
    constexpr float kHeight = 1.80f;
    constexpr float kEyeOffset = 1.62f;

    const glm::vec3 playerMin = cameraPos + glm::vec3(-kHalfWidth, -kEyeOffset, -kHalfWidth);
    const glm::vec3 playerMax = cameraPos + glm::vec3(kHalfWidth, kHeight - kEyeOffset, kHalfWidth);
    const glm::vec3 blockMin = glm::vec3(placeCell);
    const glm::vec3 blockMax = blockMin + glm::vec3(1.0f);

    const bool overlapX = playerMin.x < blockMax.x && playerMax.x > blockMin.x;
    const bool overlapY = playerMin.y < blockMax.y && playerMax.y > blockMin.y;
    const bool overlapZ = playerMin.z < blockMax.z && playerMax.z > blockMin.z;
    return overlapX && overlapY && overlapZ;
}

bool isCollisionSolidPlacement(voxel::BlockId id) {
    return !(id == voxel::AIR || id == voxel::WATER || id == voxel::TALL_GRASS ||
             id == voxel::FLOWER || voxel::isTorch(id));
}

voxel::BlockId torchIdForPlacementNormal(const glm::ivec3 &normal) {
    if (normal.y == 1) {
        return voxel::TORCH;
    }
    if (normal.x == 1) {
        return voxel::TORCH_WALL_POS_X;
    }
    if (normal.x == -1) {
        return voxel::TORCH_WALL_NEG_X;
    }
    if (normal.z == 1) {
        return voxel::TORCH_WALL_POS_Z;
    }
    if (normal.z == -1) {
        return voxel::TORCH_WALL_NEG_Z;
    }
    return voxel::AIR;
}

glm::ivec3 torchOutwardNormal(voxel::BlockId id) {
    switch (id) {
    case voxel::TORCH_WALL_POS_X:
        return {1, 0, 0};
    case voxel::TORCH_WALL_NEG_X:
        return {-1, 0, 0};
    case voxel::TORCH_WALL_POS_Z:
        return {0, 0, 1};
    case voxel::TORCH_WALL_NEG_Z:
        return {0, 0, -1};
    default:
        return {0, 0, 0};
    }
}

voxel::BlockId orientedFurnaceFromForward(const glm::vec3 &forward, bool lit) {
    if (std::abs(forward.x) >= std::abs(forward.z)) {
        if (forward.x >= 0.0f) {
            return lit ? voxel::LIT_FURNACE_POS_X : voxel::FURNACE_POS_X;
        }
        return lit ? voxel::LIT_FURNACE_NEG_X : voxel::FURNACE_NEG_X;
    }
    if (forward.z >= 0.0f) {
        return lit ? voxel::LIT_FURNACE_POS_Z : voxel::FURNACE_POS_Z;
    }
    return lit ? voxel::LIT_FURNACE_NEG_Z : voxel::FURNACE_NEG_Z;
}

bool torchHasSupport(const world::World &world, const glm::ivec3 &cell, voxel::BlockId id) {
    if (!voxel::isTorch(id)) {
        return true;
    }
    if (id == voxel::TORCH) {
        return world.isSolidBlock(cell.x, cell.y - 1, cell.z);
    }
    const glm::ivec3 outward = torchOutwardNormal(id);
    const glm::ivec3 support = cell - outward;
    return world.isSolidBlock(support.x, support.y, support.z);
}

bool isSupportPlant(voxel::BlockId id) {
    return id == voxel::TALL_GRASS || id == voxel::FLOWER;
}

bool plantHasSupport(const world::World &world, const glm::ivec3 &cell, voxel::BlockId id) {
    if (!isSupportPlant(id)) {
        return true;
    }
    return world.isSolidBlock(cell.x, cell.y - 1, cell.z);
}

void dropUnsupportedPlantsAround(world::World &world, game::ItemDropSystem &itemDrops,
                                 const glm::ivec3 &changedCell) {
    const glm::ivec3 above = changedCell + glm::ivec3(0, 1, 0);
    const voxel::BlockId id = world.getBlock(above.x, above.y, above.z);
    if (!isSupportPlant(id) || plantHasSupport(world, above, id)) {
        return;
    }
    if (world.setBlock(above.x, above.y, above.z, voxel::AIR)) {
        itemDrops.spawn(id, glm::vec3(above) + glm::vec3(0.5f, 0.02f, 0.5f), 1);
    }
}

void dropUnsupportedTorchesAround(world::World &world, game::ItemDropSystem &itemDrops,
                                  const glm::ivec3 &changedCell) {
    constexpr std::array<glm::ivec3, 6> kDirs = {
        glm::ivec3{1, 0, 0}, glm::ivec3{-1, 0, 0}, glm::ivec3{0, 1, 0},
        glm::ivec3{0, -1, 0}, glm::ivec3{0, 0, 1}, glm::ivec3{0, 0, -1},
    };
    for (const glm::ivec3 &d : kDirs) {
        const glm::ivec3 n = changedCell + d;
        const voxel::BlockId nid = world.getBlock(n.x, n.y, n.z);
        if (!voxel::isTorch(nid)) {
            continue;
        }
        if (torchHasSupport(world, n, nid)) {
            continue;
        }
        if (world.setBlock(n.x, n.y, n.z, voxel::AIR)) {
            itemDrops.spawn(voxel::TORCH, glm::vec3(n) + glm::vec3(0.5f, 0.2f, 0.5f), 1);
        }
    }
}

bool isLookingAtCraftingTable(world::World &world, const game::Camera &camera, float raycastDistance) {
    const std::optional<voxel::RayHit> hit =
        voxel::Raycaster::cast(world, camera.position(), camera.forward(), raycastDistance);
    if (!hit.has_value()) {
        return false;
    }
    const voxel::BlockId id = world.getBlock(hit->block.x, hit->block.y, hit->block.z);
    return id == voxel::CRAFTING_TABLE;
}

std::optional<glm::ivec3> lookedAtFurnaceCell(world::World &world, const game::Camera &camera,
                                              float raycastDistance) {
    const std::optional<voxel::RayHit> hit =
        voxel::Raycaster::cast(world, camera.position(), camera.forward(), raycastDistance);
    if (!hit.has_value()) {
        return std::nullopt;
    }
    const voxel::BlockId id = world.getBlock(hit->block.x, hit->block.y, hit->block.z);
    if (!voxel::isFurnace(id)) {
        return std::nullopt;
    }
    return hit->block;
}

bool isLookingAtFurnace(world::World &world, const game::Camera &camera, float raycastDistance) {
    return lookedAtFurnaceCell(world, camera, raycastDistance).has_value();
}

int pauseMenuButtonAtCursor(double mx, double my, int width, int height) {
    const float cx = static_cast<float>(width) * 0.5f;
    const float cy = static_cast<float>(height) * 0.5f;
    const float x = cx - 140.0f;
    const float w = 280.0f;
    const float h = 36.0f;
    const float resumeY = cy - 70.0f;
    const float saveY = cy - 24.0f;
    const float titleY = cy + 22.0f;
    const float quitY = cy + 68.0f;
    if (mx >= x && mx <= (x + w) && my >= resumeY && my <= (resumeY + h)) {
        return 1;
    }
    if (mx >= x && mx <= (x + w) && my >= saveY && my <= (saveY + h)) {
        return 2;
    }
    if (mx >= x && mx <= (x + w) && my >= titleY && my <= (titleY + h)) {
        return 3;
    }
    if (mx >= x && mx <= (x + w) && my >= quitY && my <= (quitY + h)) {
        return 4;
    }
    return 0;
}

} // namespace
// main.cpp — closest-first streaming + background GEN + background MESH (CPU) + main-thread GPU upload
// ✅ GEN (blocks) on worker threads (priority + urgent lane)
// ✅ MESH (greedy) on worker threads (priority + urgent lane)
// ✅ OpenGL upload (VBO/EBO/VAO) on main thread only (urgent uploads first)
// ✅ Prioritize chunks closest to player (min-heap PQ)
// ✅ Never draw "unseen" faces on chunk borders: UNKNOWN suppression
// ✅ Never draw bottom-of-world face (-Y at worldY==0)
// ✅ Raycast (voxel DDA) + block breaking (LMB) + placing (RMB)
// ✅ Highlight selected block with a white wireframe outline
//
// FIXED: slow mine/place while chunks loading
// - edits now enqueue URGENT MESH for the edited chunk and edge neighbors
// - AND enqueue URGENT GEN for any neighbor chunks that are missing/unready when you edit on a border
// - mesh workers pop urgent first (dist2 = -1)
// - main thread uploads urgent meshes first, with a bigger upload budget while urgent uploads exist

#include <glad/glad.h>
#include <GLFW/glfw3.h>

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <cmath>
#include <condition_variable>
#include <iostream>
#include <memory>
#include <mutex>
#include <optional>
#include <queue>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <deque>

// ----------------------------
// Mouse look + camera front
// ----------------------------
glm::vec3 gCameraFront(0.0f, 0.0f, -1.0f);
float gYaw = -90.0f;
float gPitch = 0.0f;
float gLastX = 0.0f, gLastY = 0.0f;
bool  gFirstMouse = true;
float gMouseSensitivity = 0.10f;

static void mouse_callback(GLFWwindow*, double xpos, double ypos)
{
    if (gFirstMouse) {
        gLastX = (float)xpos;
        gLastY = (float)ypos;
        gFirstMouse = false;
    }

    float xoffset = (float)xpos - gLastX;
    float yoffset = gLastY - (float)ypos; // invert y

    gLastX = (float)xpos;
    gLastY = (float)ypos;

    xoffset *= gMouseSensitivity;
    yoffset *= gMouseSensitivity;

    gYaw   += xoffset;
    gPitch += yoffset;

    if (gPitch > 89.0f)  gPitch = 89.0f;
    if (gPitch < -89.0f) gPitch = -89.0f;

    glm::vec3 front;
    front.x = cos(glm::radians(gYaw)) * cos(glm::radians(gPitch));
    front.y = sin(glm::radians(gPitch));
    front.z = sin(glm::radians(gYaw)) * cos(glm::radians(gPitch));
    gCameraFront = glm::normalize(front);
}

// ----------------------------
// Mesh data
// ----------------------------
struct ChunkMesh2 {
    std::vector<float> verts;        // [x y z r g b]...
    std::vector<uint32_t> indices;   // triangles
};

static inline int idx3(int x, int y, int z, int sx, int /*sy*/, int sz) {
    return x + sx * (z + sz * y); // (x,z,y)
}

static inline void pushVertex(std::vector<float>& v, float x, float y, float z, float r, float g, float b) {
    v.push_back(x); v.push_back(y); v.push_back(z);
    v.push_back(r); v.push_back(g); v.push_back(b);
}

static void addQuad(ChunkMesh2& m,
                    const glm::vec3& a, const glm::vec3& b,
                    const glm::vec3& c, const glm::vec3& d,
                    const glm::vec3& color)
{
    uint32_t base = (uint32_t)(m.verts.size() / 6);
    pushVertex(m.verts, a.x,a.y,a.z, color.r,color.g,color.b);
    pushVertex(m.verts, b.x,b.y,b.z, color.r,color.g,color.b);
    pushVertex(m.verts, c.x,c.y,c.z, color.r,color.g,color.b);
    pushVertex(m.verts, d.x,d.y,d.z, color.r,color.g,color.b);

    m.indices.push_back(base + 0);
    m.indices.push_back(base + 1);
    m.indices.push_back(base + 2);
    m.indices.push_back(base + 0);
    m.indices.push_back(base + 2);
    m.indices.push_back(base + 3);
}

// ----------------------------
// Chunk container
// ----------------------------
struct GpuChunk {
    glm::ivec2 cpos;
    glm::vec3 origin;

    std::vector<uint8_t> blocks;
    std::mutex blocksMutex;

    ChunkMesh2 mesh;
    GLuint vao=0, vbo=0, ebo=0;

    std::atomic<bool> inGen{false};
    std::atomic<bool> inMesh{false};
    std::atomic<int>  pins{0};

    std::atomic<bool> hasBlocks{false};
    std::atomic<bool> hasMesh{false};
    std::atomic<bool> dirtyMesh{true};

    ChunkMesh2 stagedMesh;
    std::atomic<bool> stagedReady{false};
    std::mutex stagedMutex;
};

// OpenGL upload (main thread only)
static void uploadChunkMesh(GpuChunk& c) {
    if (c.vao == 0) {
        glGenVertexArrays(1, &c.vao);
        glGenBuffers(1, &c.vbo);
        glGenBuffers(1, &c.ebo);
    }

    glBindVertexArray(c.vao);

    glBindBuffer(GL_ARRAY_BUFFER, c.vbo);
    GLsizeiptr vbytes = (GLsizeiptr)(c.mesh.verts.size() * sizeof(float));
    glBufferData(GL_ARRAY_BUFFER, vbytes, nullptr, GL_DYNAMIC_DRAW);
    if (vbytes > 0) glBufferSubData(GL_ARRAY_BUFFER, 0, vbytes, c.mesh.verts.data());

    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, c.ebo);
    GLsizeiptr ibytes = (GLsizeiptr)(c.mesh.indices.size() * sizeof(uint32_t));
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, ibytes, nullptr, GL_DYNAMIC_DRAW);
    if (ibytes > 0) glBufferSubData(GL_ELEMENT_ARRAY_BUFFER, 0, ibytes, c.mesh.indices.data());

    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float), (void*)(3 * sizeof(float)));
    glEnableVertexAttribArray(1);

    glBindVertexArray(0);
}

static void unloadChunkGL(GpuChunk& ch) {
    if (ch.vao) glDeleteVertexArrays(1, &ch.vao);
    if (ch.vbo) glDeleteBuffers(1, &ch.vbo);
    if (ch.ebo) glDeleteBuffers(1, &ch.ebo);
    ch.vao = ch.vbo = ch.ebo = 0;

    {
        std::lock_guard<std::mutex> bl(ch.blocksMutex);
        ch.blocks.clear();
    }

    ch.mesh.verts.clear();
    ch.mesh.indices.clear();

    {
        std::lock_guard<std::mutex> lock(ch.stagedMutex);
        ch.stagedMesh.verts.clear();
        ch.stagedMesh.indices.clear();
        ch.stagedReady.store(false, std::memory_order_release);
    }

    ch.hasBlocks.store(false, std::memory_order_release);
    ch.hasMesh.store(false, std::memory_order_release);
    ch.dirtyMesh.store(true, std::memory_order_release);
}

// ----------------------------
// Helpers
// ----------------------------
static inline uint64_t key64(int cx, int cz) {
    return (uint64_t)(uint32_t)cx << 32 | (uint32_t)cz;
}

static inline int floorDivInt(int a, int b) {
    int q = a / b;
    int r = a % b;
    if ((r != 0) && ((r > 0) != (b > 0))) q -= 1;
    return q;
}

static inline int worldToChunkCoord(float world, float chunkWorldSize) {
    return (int)std::floor(world / chunkWorldSize);
}

static inline int clampi(int v, int lo, int hi) { return std::max(lo, std::min(hi, v)); }

// ----------------------------
// Priority work items
// ----------------------------
struct WorkItem {
    int dist2;      // -1 => URGENT
    uint32_t seq;
    int cx, cz;
};

struct WorkCmp {
    bool operator()(const WorkItem& a, const WorkItem& b) const {
        if (a.dist2 != b.dist2) return a.dist2 > b.dist2; // min-heap
        return a.seq > b.seq;
    }
};

static inline int dist2Chunks(int cx, int cz, int pcx, int pcz) {
    int dx = cx - pcx;
    int dz = cz - pcz;
    return dx*dx + dz*dz;
}

struct MeshDone { int cx, cz; };

// ============================================================
// Refactored greedy mesher (NO world callback)
// - Inputs are snapshots of 3x3 neighbor chunk blocks (copies)
// - UNKNOWN suppression: if either side UNKNOWN => don't emit face
// - Bottom face suppression: don't emit -Y faces at worldY==0
// ============================================================
struct ChunkSnapshot {
    bool ready = false;
    std::vector<uint8_t> blocks;
};

static ChunkMesh2 buildChunkMeshGreedy_3x3Snapshots(
    const ChunkSnapshot neigh[3][3],   // neigh[1][1] is center
    int sx, int sy, int sz,
    const glm::vec3& origin,
    float blockSize
) {
    ChunkMesh2 mesh;

    static constexpr uint8_t AIR     = 0;
    static constexpr uint8_t SOLID   = 1;
    static constexpr uint8_t UNKNOWN = 255;

    const glm::vec3 colRight  = {0.0f, 0.0f, 1.0f}; // +X
    const glm::vec3 colLeft   = {0.0f, 1.0f, 0.0f}; // -X
    const glm::vec3 colTop    = {1.0f, 0.0f, 1.0f}; // +Y
    const glm::vec3 colBottom = {1.0f, 1.0f, 0.0f}; // -Y
    const glm::vec3 colFront  = {0.0f, 1.0f, 1.0f}; // +Z
    const glm::vec3 colBack   = {1.0f, 0.0f, 0.0f}; // -Z

    auto faceColor = [&](int axis, int dir) -> glm::vec3 {
        if (axis == 0) return (dir > 0) ? colRight  : colLeft;
        if (axis == 1) return (dir > 0) ? colTop    : colBottom;
        return          (dir > 0) ? colFront  : colBack;
    };

    auto sample = [&](int x, int y, int z) -> uint8_t {
        if (y < 0 || y >= sy) return AIR;

        int ox = 0, oz = 0;
        int lx = x, lz = z;

        if (lx < 0)        { ox = -1; lx += sx; }
        else if (lx >= sx) { ox = +1; lx -= sx; }

        if (lz < 0)        { oz = -1; lz += sz; }
        else if (lz >= sz) { oz = +1; lz -= sz; }

        if (lx < 0 || lx >= sx || lz < 0 || lz >= sz) return UNKNOWN;

        int ni = ox + 1;
        int nj = oz + 1;
        if (ni < 0 || ni > 2 || nj < 0 || nj > 2) return UNKNOWN;

        const ChunkSnapshot& sn = neigh[ni][nj];
        if (!sn.ready) return UNKNOWN;

        const int id = idx3(lx, y, lz, sx, sy, sz);
        return sn.blocks[id] ? SOLID : AIR;
    };

    struct MaskCell { uint8_t id = 0; int dir = 0; };
    const int dims[3] = { sx, sy, sz };

    int maxDim = std::max(sx, std::max(sy, sz));
    std::vector<MaskCell> mask(maxDim * maxDim);

    auto emitQuad = [&](int axis, int dir, int i, int u0, int v0, int u1, int v1) {
        // Never draw bottom-of-world face
        if (axis == 1 && dir == -1) {
            int worldY = (i - 1);
            if (worldY <= 0) return;
        }

        const float s = blockSize;
        const glm::vec3 col = faceColor(axis, dir);

        int uAxis = (axis + 1) % 3;
        int vAxis = (axis + 2) % 3;

        auto corner = [&](int a, int u, int v) -> glm::vec3 {
            int c[3] = {0,0,0};
            c[axis]  = a;
            c[uAxis] = u;
            c[vAxis] = v;
            return origin + glm::vec3(float(c[0]) * s, float(c[1]) * s, float(c[2]) * s);
        };

        glm::vec3 p00 = corner(i,  u0, v0);
        glm::vec3 p10 = corner(i,  u1, v0);
        glm::vec3 p11 = corner(i,  u1, v1);
        glm::vec3 p01 = corner(i,  u0, v1);

        glm::vec3 shift(-0.5f * s, -0.5f * s, -0.5f * s);
        glm::vec3 a = p00 + shift;
        glm::vec3 b = p10 + shift;
        glm::vec3 c = p11 + shift;
        glm::vec3 d = p01 + shift;

        glm::vec3 axisVec(0.0f); axisVec[axis] = 1.0f;
        glm::vec3 expected = axisVec * float(dir);

        glm::vec3 n = glm::cross(b - a, c - a);
        if (glm::dot(n, expected) < 0.0f) std::swap(b, d);

        addQuad(mesh, a, b, c, d, col);
    };

    for (int axis = 0; axis < 3; ++axis) {
        int uAxis = (axis + 1) % 3;
        int vAxis = (axis + 2) % 3;

        int A = dims[axis];
        int U = dims[uAxis];
        int V = dims[vAxis];

        for (int i = 0; i <= A; ++i) {
            for (int v = 0; v < V; ++v) {
                for (int u = 0; u < U; ++u) {
                    int c0[3] = {0,0,0};
                    int c1[3] = {0,0,0};

                    c0[axis] = i - 1;
                    c1[axis] = i;

                    c0[uAxis] = u; c0[vAxis] = v;
                    c1[uAxis] = u; c1[vAxis] = v;

                    uint8_t aS = sample(c0[0], c0[1], c0[2]);
                    uint8_t bS = sample(c1[0], c1[1], c1[2]);

                    MaskCell cell{};
                    if (aS == UNKNOWN || bS == UNKNOWN) {
                        cell = {};
                    } else if (aS == SOLID && bS == AIR) {
                        cell.id = SOLID; cell.dir = +1;
                    } else if (aS == AIR && bS == SOLID) {
                        cell.id = SOLID; cell.dir = -1;
                    } else {
                        cell = {};
                    }
                    mask[u + U * v] = cell;
                }
            }

            for (int v = 0; v < V; ++v) {
                for (int u = 0; u < U; ) {
                    MaskCell cur = mask[u + U * v];
                    if (cur.id == 0) { ++u; continue; }

                    int w = 1;
                    while (u + w < U) {
                        MaskCell nxt = mask[(u + w) + U * v];
                        if (nxt.id != cur.id || nxt.dir != cur.dir) break;
                        ++w;
                    }

                    int h = 1;
                    bool done = false;
                    while (v + h < V && !done) {
                        for (int k = 0; k < w; ++k) {
                            MaskCell nxt = mask[(u + k) + U * (v + h)];
                            if (nxt.id != cur.id || nxt.dir != cur.dir) { done = true; break; }
                        }
                        if (!done) ++h;
                    }

                    emitQuad(axis, cur.dir, i, u, v, u + w, v + h);

                    for (int dv = 0; dv < h; ++dv)
                        for (int du = 0; du < w; ++du)
                            mask[(u + du) + U * (v + dv)] = {};

                    u += w;
                }
            }
        }
    }

    return mesh;
}

int main() {
    if (!glfwInit()) return -1;

    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);

    GLFWmonitor* monitor = glfwGetPrimaryMonitor();
    const GLFWvidmode* mode = glfwGetVideoMode(monitor);
    if (!monitor || !mode) { glfwTerminate(); return -1; }

    GLFWwindow* window = glfwCreateWindow(mode->width, mode->height, "Voxel World", monitor, nullptr);
    if (!window) { glfwTerminate(); return -1; }
    glfwMakeContextCurrent(window);

    if (!gladLoadGLLoader((GLADloadproc)glfwGetProcAddress)) {
        glfwDestroyWindow(window);
        glfwTerminate();
        return -1;
    }

    glfwSetCursorPosCallback(window, mouse_callback);
    glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);

    glViewport(0, 0, mode->width, mode->height);
    glEnable(GL_DEPTH_TEST);

    glm::vec3 cameraPos = glm::vec3(0.0f, 40.0f, 80.0f);
    glm::vec3 cameraUp  = glm::vec3(0.0f, 1.0f, 0.0f);
    float cameraSpeed   = 25.0f;
    float deltaTime     = 0.0f;
    float lastFrame     = 0.0f;

    bool wireframe = false;
    bool f1WasDown = false;

    // ----------------------------
    // Shader program
    // ----------------------------
    const char* vs =
        "#version 330 core\n"
        "layout(location = 0) in vec3 aPos;\n"
        "layout(location = 1) in vec3 aColor;\n"
        "uniform mat4 MVP;\n"
        "out vec3 vColor;\n"
        "void main(){ gl_Position = MVP * vec4(aPos,1.0); vColor=aColor; }\n";

    const char* fs =
        "#version 330 core\n"
        "in vec3 vColor;\n"
        "out vec4 FragColor;\n"
        "void main(){ FragColor = vec4(vColor,1.0); }\n";

    GLuint vert = glCreateShader(GL_VERTEX_SHADER);
    glShaderSource(vert, 1, &vs, nullptr);
    glCompileShader(vert);

    GLuint frag = glCreateShader(GL_FRAGMENT_SHADER);
    glShaderSource(frag, 1, &fs, nullptr);
    glCompileShader(frag);

    GLuint program = glCreateProgram();
    glAttachShader(program, vert);
    glAttachShader(program, frag);
    glLinkProgram(program);

    glDeleteShader(vert);
    glDeleteShader(frag);

    GLint mvpLoc = glGetUniformLocation(program, "MVP");

    // ============================================================
    // World config
    // ============================================================
    const int   CHUNK_X = 16;
    const int   CHUNK_Z = 16;
    const int   CHUNK_Y = 128;
    const float BLOCK   = 1.0f;

    const float CHUNK_WORLD_X = float(CHUNK_X) * BLOCK;
    const float CHUNK_WORLD_Z = float(CHUNK_Z) * BLOCK;

    const int LOAD_RADIUS   = 64;
    const int GEN_RADIUS    = LOAD_RADIUS + 1;
    const int UNLOAD_RADIUS = LOAD_RADIUS + 3;

    const double UPLOAD_BUDGET_MS_NORMAL = 2.0;
    const double UPLOAD_BUDGET_MS_URGENT = 10.0;

    static constexpr uint8_t AIR     = 0;
    static constexpr uint8_t SOLID   = 1;
    static constexpr uint8_t UNKNOWN = 255;

    // ============================================================
    // Chunk storage (pointer-stable)
    // ============================================================
    std::unordered_map<uint64_t, std::unique_ptr<GpuChunk>> chunks;
    std::mutex chunksMutex;

    auto createChunkIfMissing = [&](int cx, int cz) -> GpuChunk* {
        std::lock_guard<std::mutex> lock(chunksMutex);
        uint64_t k = key64(cx, cz);
        auto it = chunks.find(k);
        if (it == chunks.end()) {
            auto c = std::make_unique<GpuChunk>();
            c->cpos = {cx, cz};
            c->origin = glm::vec3(float(cx * CHUNK_X) * BLOCK, 0.0f, float(cz * CHUNK_Z) * BLOCK);
            auto [it2, _] = chunks.emplace(k, std::move(c));
            return it2->second.get();
        }
        return it->second.get();
    };

    auto pinChunk = [&](int cx, int cz) -> GpuChunk* {
        std::lock_guard<std::mutex> lock(chunksMutex);
        auto it = chunks.find(key64(cx, cz));
        if (it == chunks.end()) return nullptr;
        GpuChunk* ch = it->second.get();
        ch->pins.fetch_add(1, std::memory_order_acq_rel);
        return ch;
    };

    auto unpinChunk = [&](GpuChunk* ch) {
        if (!ch) return;
        ch->pins.fetch_sub(1, std::memory_order_acq_rel);
    };

    // ============================================================
    // Height function
    // ============================================================
    auto heightAtWorld = [&](int wx, int wz) -> int {
        float fx = float(wx) * 0.08f;
        float fz = float(wz) * 0.08f;
        float h  = (sinf(fx) + cosf(fz)) * 10.0f + 32.0f;
        return clampi((int)h, 0, CHUNK_Y);
    };

    // Player chunk coords visible to workers
    std::atomic<int> gPlayerCX{0}, gPlayerCZ{0};

    // ============================================================
    // GEN / MESH queues
    // ============================================================
    std::priority_queue<WorkItem, std::vector<WorkItem>, WorkCmp> genPQ;
    std::unordered_map<uint64_t, int> bestGenDist2;
    std::mutex genMutex;
    std::condition_variable genCV;
    uint32_t genSeq = 0;

    std::priority_queue<WorkItem, std::vector<WorkItem>, WorkCmp> meshPQ;
    std::unordered_map<uint64_t, int> bestMeshDist2;
    std::mutex meshMutex;
    std::condition_variable meshCV;
    uint32_t meshSeq = 0;

    // Done queues: urgent uploads first
    std::deque<MeshDone> meshDoneUrgentQ;
    std::unordered_set<uint64_t> meshDoneUrgentSet;
    std::deque<MeshDone> meshDoneQ;
    std::unordered_set<uint64_t> meshDoneSet;
    std::mutex meshDoneMutex;

    auto queueGen = [&](int cx, int cz, int pcx, int pcz) {
        uint64_t k = key64(cx, cz);
        int d2 = dist2Chunks(cx, cz, pcx, pcz);
        {
            std::lock_guard<std::mutex> lock(genMutex);
            auto it = bestGenDist2.find(k);
            if (it == bestGenDist2.end() || d2 < it->second) {
                bestGenDist2[k] = d2;
                genPQ.push(WorkItem{d2, genSeq++, cx, cz});
            }
        }
        genCV.notify_one();
    };

    auto queueGenUrgent = [&](int cx, int cz) {
        uint64_t k = key64(cx, cz);
        int d2 = -1;
        {
            std::lock_guard<std::mutex> lock(genMutex);
            bestGenDist2[k] = d2;
            genPQ.push(WorkItem{d2, genSeq++, cx, cz});
        }
        genCV.notify_one();
    };

    auto queueMesh = [&](int cx, int cz, int pcx, int pcz) {
        uint64_t k = key64(cx, cz);
        int d2 = dist2Chunks(cx, cz, pcx, pcz);
        {
            std::lock_guard<std::mutex> lock(meshMutex);
            auto it = bestMeshDist2.find(k);
            if (it == bestMeshDist2.end() || d2 < it->second) {
                bestMeshDist2[k] = d2;
                meshPQ.push(WorkItem{d2, meshSeq++, cx, cz});
            }
        }
        meshCV.notify_one();
    };

    auto queueMeshUrgent = [&](int cx, int cz) {
        uint64_t k = key64(cx, cz);
        int d2 = -1;
        {
            std::lock_guard<std::mutex> lock(meshMutex);
            bestMeshDist2[k] = d2;
            meshPQ.push(WorkItem{d2, meshSeq++, cx, cz});
        }
        meshCV.notify_one();
    };

    // ============================================================
    // World accessor for raycast / edits (rare)
    // ============================================================
    auto solidAtWorld = [&](int wx, int wy, int wz) -> uint8_t {
        if (wy < 0 || wy >= CHUNK_Y) return AIR;

        int cx = floorDivInt(wx, CHUNK_X);
        int cz = floorDivInt(wz, CHUNK_Z);

        int lx = wx - cx * CHUNK_X;
        int lz = wz - cz * CHUNK_Z;
        if (lx < 0) lx += CHUNK_X;
        if (lz < 0) lz += CHUNK_Z;

        GpuChunk* ch = pinChunk(cx, cz);
        if (!ch) return UNKNOWN;

        uint8_t out = UNKNOWN;
        if (ch->hasBlocks.load(std::memory_order_acquire) && !ch->inGen.load(std::memory_order_acquire)) {
            std::lock_guard<std::mutex> bl(ch->blocksMutex);
            out = ch->blocks[idx3(lx, wy, lz, CHUNK_X, CHUNK_Y, CHUNK_Z)] ? SOLID : AIR;
        }

        unpinChunk(ch);
        return out;
    };

    // ============================================================
    // Border correctness helper
    // ============================================================
    auto dirtyAndQueueMesh = [&](int cx, int cz, bool urgent) {
        int pcx = gPlayerCX.load();
        int pcz = gPlayerCZ.load();

        if (GpuChunk* ch = pinChunk(cx, cz)) {
            if (ch->hasBlocks.load(std::memory_order_acquire) && !ch->inGen.load(std::memory_order_acquire)) {
                ch->dirtyMesh.store(true, std::memory_order_release);
            }
            unpinChunk(ch);
        }
        if (urgent) queueMeshUrgent(cx, cz);
        else        queueMesh(cx, cz, pcx, pcz);
    };

    // If an edit touches a chunk border and the neighbor is missing/unready, urgently generate it
    auto ensureNeighborGeneratedUrgent = [&](int ncx, int ncz) {
        // create placeholder so it exists in map
        createChunkIfMissing(ncx, ncz);

        if (GpuChunk* n = pinChunk(ncx, ncz)) {
            bool ready = n->hasBlocks.load(std::memory_order_acquire) && !n->inGen.load(std::memory_order_acquire);
            unpinChunk(n);
            if (!ready) queueGenUrgent(ncx, ncz);
        } else {
            queueGenUrgent(ncx, ncz);
        }
    };

    // ============================================================
    // World block editing
    // ============================================================
    auto worldToLocalXZ = [&](int wx, int wz, int& cx, int& cz, int& lx, int& lz) {
        cx = floorDivInt(wx, CHUNK_X);
        cz = floorDivInt(wz, CHUNK_Z);
        lx = wx - cx * CHUNK_X;
        lz = wz - cz * CHUNK_Z;
        if (lx < 0) lx += CHUNK_X;
        if (lz < 0) lz += CHUNK_Z;
    };

    auto setBlockWorld = [&](int wx, int wy, int wz, uint8_t value) -> bool {
        if (wy < 0 || wy >= CHUNK_Y) return false;

        int cx, cz, lx, lz;
        worldToLocalXZ(wx, wz, cx, cz, lx, lz);

        GpuChunk* ch = pinChunk(cx, cz);
        if (!ch) return false;

        if (!ch->hasBlocks.load(std::memory_order_acquire) || ch->inGen.load(std::memory_order_acquire)) {
            unpinChunk(ch);
            return false;
        }

        {
            std::lock_guard<std::mutex> bl(ch->blocksMutex);
            ch->blocks[idx3(lx, wy, lz, CHUNK_X, CHUNK_Y, CHUNK_Z)] = value;
        }

        ch->dirtyMesh.store(true, std::memory_order_release);

        // URGENT remesh self
        dirtyAndQueueMesh(cx, cz, true);

        // If border edit, urgently generate + urgent remesh neighbor so the seam updates ASAP
        if (lx == 0) {
            ensureNeighborGeneratedUrgent(cx - 1, cz);
            dirtyAndQueueMesh(cx - 1, cz, true);
        }
        if (lx == CHUNK_X - 1) {
            ensureNeighborGeneratedUrgent(cx + 1, cz);
            dirtyAndQueueMesh(cx + 1, cz, true);
        }
        if (lz == 0) {
            ensureNeighborGeneratedUrgent(cx, cz - 1);
            dirtyAndQueueMesh(cx, cz - 1, true);
        }
        if (lz == CHUNK_Z - 1) {
            ensureNeighborGeneratedUrgent(cx, cz + 1);
            dirtyAndQueueMesh(cx, cz + 1, true);
        }

        unpinChunk(ch);
        return true;
    };

    // ============================================================
    // Raycast (voxel DDA)
    // ============================================================
    struct RayHit {
        int wx=0, wy=0, wz=0;
        int nx=0, ny=0, nz=0;
        float t=0.0f;
    };

    auto ifloor = [&](float v) -> int { return (int)std::floor(v); };

    auto raycastBlocks = [&](const glm::vec3& origin, const glm::vec3& dir, float maxDist) -> std::optional<RayHit> {
        glm::vec3 d = glm::normalize(dir);
        if (!std::isfinite(d.x) || !std::isfinite(d.y) || !std::isfinite(d.z)) return std::nullopt;

        int x = ifloor(origin.x);
        int y = ifloor(origin.y);
        int z = ifloor(origin.z);

        int stepX = (d.x > 0) ? 1 : (d.x < 0 ? -1 : 0);
        int stepY = (d.y > 0) ? 1 : (d.y < 0 ? -1 : 0);
        int stepZ = (d.z > 0) ? 1 : (d.z < 0 ? -1 : 0);

        auto invAbs = [&](float a) -> float { return (a != 0.0f) ? (1.0f / std::abs(a)) : 1e30f; };

        float tDeltaX = invAbs(d.x);
        float tDeltaY = invAbs(d.y);
        float tDeltaZ = invAbs(d.z);

        auto nextBoundaryT = [&](float o, float dv, int cell, int step) -> float {
            if (step == 0) return 1e30f;
            float boundary = (step > 0) ? float(cell + 1) : float(cell);
            return (boundary - o) / dv;
        };

        float tMaxX = nextBoundaryT(origin.x, d.x, x, stepX);
        float tMaxY = nextBoundaryT(origin.y, d.y, y, stepY);
        float tMaxZ = nextBoundaryT(origin.z, d.z, z, stepZ);

        int lastNX=0, lastNY=0, lastNZ=0;
        float t = 0.0f;

        // starting cell check
        {
            uint8_t s = solidAtWorld(x, y, z);
            if (s == SOLID) {
                RayHit hit; hit.wx=x; hit.wy=y; hit.wz=z; hit.nx=0; hit.ny=0; hit.nz=0; hit.t=0.0f;
                return hit;
            }
        }

        while (t <= maxDist) {
            if (tMaxX < tMaxY && tMaxX < tMaxZ) {
                x += stepX;
                t = tMaxX;
                tMaxX += tDeltaX;
                lastNX = -stepX; lastNY = 0; lastNZ = 0;
            } else if (tMaxY < tMaxZ) {
                y += stepY;
                t = tMaxY;
                tMaxY += tDeltaY;
                lastNX = 0; lastNY = -stepY; lastNZ = 0;
            } else {
                z += stepZ;
                t = tMaxZ;
                tMaxZ += tDeltaZ;
                lastNX = 0; lastNY = 0; lastNZ = -stepZ;
            }

            if (t > maxDist) break;

            uint8_t s = solidAtWorld(x, y, z);
            // do NOT treat UNKNOWN as a hit (prevents border interactions into missing chunks)
            if (s == SOLID) {
                RayHit hit;
                hit.wx=x; hit.wy=y; hit.wz=z;
                hit.nx=lastNX; hit.ny=lastNY; hit.nz=lastNZ;
                hit.t=t;
                return hit;
            }
        }

        return std::nullopt;
    };

    // ============================================================
    // Outline (selected block highlight) — GL_LINES
    // ============================================================
    GLuint outlineVAO = 0, outlineVBO = 0;

    auto initOutlineGL = [&]() {
        if (outlineVAO != 0) return;
        glGenVertexArrays(1, &outlineVAO);
        glGenBuffers(1, &outlineVBO);

        glBindVertexArray(outlineVAO);
        glBindBuffer(GL_ARRAY_BUFFER, outlineVBO);
        glBufferData(GL_ARRAY_BUFFER, 24 * 6 * sizeof(float), nullptr, GL_DYNAMIC_DRAW);

        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float), (void*)0);
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float), (void*)(3 * sizeof(float)));
        glEnableVertexAttribArray(1);

        glBindVertexArray(0);
    };

    auto uploadOutlineBox = [&](int wx, int wy, int wz, float blockSize) {
        float s = blockSize;
        float e = 0.02f * s;

        float x0 = (float)wx - 0.5f * s - e;
        float y0 = (float)wy - 0.5f * s - e;
        float z0 = (float)wz - 0.5f * s - e;

        float x1 = (float)wx + 0.5f * s + e;
        float y1 = (float)wy + 0.5f * s + e;
        float z1 = (float)wz + 0.5f * s + e;

        float r=1.0f, g=1.0f, b=1.0f;

        float v[24 * 6];
        int n = 0;
        auto push = [&](float x, float y, float z) {
            v[n++] = x; v[n++] = y; v[n++] = z;
            v[n++] = r; v[n++] = g; v[n++] = b;
        };

        // bottom
        push(x0,y0,z0); push(x1,y0,z0);
        push(x1,y0,z0); push(x1,y0,z1);
        push(x1,y0,z1); push(x0,y0,z1);
        push(x0,y0,z1); push(x0,y0,z0);

        // top
        push(x0,y1,z0); push(x1,y1,z0);
        push(x1,y1,z0); push(x1,y1,z1);
        push(x1,y1,z1); push(x0,y1,z1);
        push(x0,y1,z1); push(x0,y1,z0);

        // verticals
        push(x0,y0,z0); push(x0,y1,z0);
        push(x1,y0,z0); push(x1,y1,z0);
        push(x1,y0,z1); push(x1,y1,z1);
        push(x0,y0,z1); push(x0,y1,z1);

        glBindBuffer(GL_ARRAY_BUFFER, outlineVBO);
        glBufferSubData(GL_ARRAY_BUFFER, 0, sizeof(v), v);
    };

    initOutlineGL();

    // ============================================================
    // GEN worker threads
    // ============================================================
    std::atomic<bool> genStop{false};
    int GEN_THREADS = (int)std::max(1u, std::thread::hardware_concurrency());
    GEN_THREADS = std::min(GEN_THREADS, 8);

    auto generateBlocksForChunk = [&](GpuChunk* ch) {
        bool expected = false;
        if (!ch->inGen.compare_exchange_strong(expected, true)) return;
        if (ch->hasBlocks.load(std::memory_order_acquire)) { ch->inGen.store(false); return; }

        {
            std::lock_guard<std::mutex> bl(ch->blocksMutex);
            ch->blocks.assign(CHUNK_X * CHUNK_Y * CHUNK_Z, 0);

            int baseWX = ch->cpos.x * CHUNK_X;
            int baseWZ = ch->cpos.y * CHUNK_Z;

            for (int zz = 0; zz < CHUNK_Z; ++zz) {
                for (int xx = 0; xx < CHUNK_X; ++xx) {
                    int wx = baseWX + xx;
                    int wz = baseWZ + zz;
                    int h  = heightAtWorld(wx, wz);
                    for (int yy = 0; yy < h; ++yy) {
                        ch->blocks[idx3(xx, yy, zz, CHUNK_X, CHUNK_Y, CHUNK_Z)] = 1;
                    }
                }
            }
        }

        ch->dirtyMesh.store(true, std::memory_order_release);
        ch->hasBlocks.store(true, std::memory_order_release);
        ch->inGen.store(false, std::memory_order_release);
    };

    std::vector<std::thread> genWorkers;

    auto genWorkerMain = [&]() {
        while (!genStop.load()) {
            WorkItem wi;
            {
                std::unique_lock<std::mutex> lock(genMutex);
                genCV.wait(lock, [&]{ return genStop.load() || !genPQ.empty(); });
                if (genStop.load()) return;

                wi = genPQ.top();
                genPQ.pop();

                uint64_t k = key64(wi.cx, wi.cz);
                auto it = bestGenDist2.find(k);
                if (it == bestGenDist2.end() || it->second != wi.dist2) continue;
                bestGenDist2.erase(k);
            }

            int pcx = gPlayerCX.load();
            int pcz = gPlayerCZ.load();
            if (std::abs(wi.cx - pcx) > GEN_RADIUS || std::abs(wi.cz - pcz) > GEN_RADIUS) continue;

            createChunkIfMissing(wi.cx, wi.cz);
            GpuChunk* ch = pinChunk(wi.cx, wi.cz);
            if (!ch) continue;

            generateBlocksForChunk(ch);
            unpinChunk(ch);

            // When a chunk becomes known, its neighbors’ seam can change (UNKNOWN -> known).
            // This is NOT urgent by default.
            dirtyAndQueueMesh(wi.cx, wi.cz, false);
            for (int nz=-1; nz<=1; ++nz)
                for (int nx=-1; nx<=1; ++nx)
                    dirtyAndQueueMesh(wi.cx + nx, wi.cz + nz, false);
        }
    };

    for (int t=0; t<GEN_THREADS; ++t) genWorkers.emplace_back(genWorkerMain);

    // ============================================================
    // MESH worker threads (snapshot 3x3 once per mesh)
    // ============================================================
    std::atomic<bool> meshStop{false};
    int MESH_THREADS = (int)std::max(1u, std::thread::hardware_concurrency());
    MESH_THREADS = std::min(MESH_THREADS, 8);

    std::vector<std::thread> meshWorkers;

    auto snapshotChunkBlocks = [&](int cx, int cz) -> ChunkSnapshot {
        ChunkSnapshot snap;
        GpuChunk* ch = pinChunk(cx, cz);
        if (!ch) return snap;

        if (ch->hasBlocks.load(std::memory_order_acquire) && !ch->inGen.load(std::memory_order_acquire)) {
            std::lock_guard<std::mutex> bl(ch->blocksMutex);
            snap.blocks = ch->blocks;
            snap.ready = (snap.blocks.size() == (size_t)(CHUNK_X * CHUNK_Y * CHUNK_Z));
        }

        unpinChunk(ch);
        return snap;
    };

    auto meshWorkerMain = [&]() {
        while (!meshStop.load()) {
            WorkItem wi;
            {
                std::unique_lock<std::mutex> lock(meshMutex);
                meshCV.wait(lock, [&]{ return meshStop.load() || !meshPQ.empty(); });
                if (meshStop.load()) return;

                wi = meshPQ.top();
                meshPQ.pop();

                uint64_t k = key64(wi.cx, wi.cz);
                auto it = bestMeshDist2.find(k);
                if (it == bestMeshDist2.end() || it->second != wi.dist2) continue;
                bestMeshDist2.erase(k);
            }

            const bool urgent = (wi.dist2 < 0);

            int pcx = gPlayerCX.load();
            int pcz = gPlayerCZ.load();
            if (!urgent) {
                if (std::abs(wi.cx - pcx) > LOAD_RADIUS || std::abs(wi.cz - pcz) > LOAD_RADIUS) continue;
            }

            GpuChunk* ch = pinChunk(wi.cx, wi.cz);
            if (!ch) continue;

            if (!ch->hasBlocks.load(std::memory_order_acquire) || ch->inGen.load(std::memory_order_acquire)) {
                unpinChunk(ch);
                continue;
            }

            bool expected = false;
            if (!ch->inMesh.compare_exchange_strong(expected, true)) {
                unpinChunk(ch);
                continue;
            }

            // If not dirty, skip (NOTE: edits set dirtyMesh=true; also dirtyAndQueueMesh marks dirty)
            if (!ch->dirtyMesh.load(std::memory_order_acquire)) {
                ch->inMesh.store(false, std::memory_order_release);
                unpinChunk(ch);
                continue;
            }

            ChunkSnapshot neigh[3][3];
            for (int dz=-1; dz<=1; ++dz) {
                for (int dx=-1; dx<=1; ++dx) {
                    neigh[dx+1][dz+1] = snapshotChunkBlocks(wi.cx + dx, wi.cz + dz);
                }
            }

            ChunkMesh2 built = buildChunkMeshGreedy_3x3Snapshots(
                neigh,
                CHUNK_X, CHUNK_Y, CHUNK_Z,
                ch->origin,
                BLOCK
            );

            {
                std::lock_guard<std::mutex> lock(ch->stagedMutex);
                ch->stagedMesh = std::move(built);
                ch->stagedReady.store(true, std::memory_order_release);
            }

            // enqueue upload (urgent uploads first)
            {
                uint64_t kk = key64(wi.cx, wi.cz);
                std::lock_guard<std::mutex> lock(meshDoneMutex);
                if (urgent) {
                    if (meshDoneUrgentSet.insert(kk).second) {
                        meshDoneUrgentQ.push_back(MeshDone{wi.cx, wi.cz});
                    }
                } else {
                    if (meshDoneSet.insert(kk).second) {
                        meshDoneQ.push_back(MeshDone{wi.cx, wi.cz});
                    }
                }
            }

            ch->inMesh.store(false, std::memory_order_release);
            unpinChunk(ch);
        }
    };

    for (int t=0; t<MESH_THREADS; ++t) meshWorkers.emplace_back(meshWorkerMain);

    // ============================================================
    // Streaming scheduling
    // ============================================================
    auto scheduleDelta = [&](int oldCX, int oldCZ, int newCX, int newCZ) {
        auto inOldGen = [&](int cx, int cz) {
            return std::abs(cx - oldCX) <= GEN_RADIUS && std::abs(cz - oldCZ) <= GEN_RADIUS;
        };
        auto inOldMesh = [&](int cx, int cz) {
            return std::abs(cx - oldCX) <= LOAD_RADIUS && std::abs(cz - oldCZ) <= LOAD_RADIUS;
        };

        for (int cz = newCZ - GEN_RADIUS; cz <= newCZ + GEN_RADIUS; ++cz) {
            for (int cx = newCX - GEN_RADIUS; cx <= newCX + GEN_RADIUS; ++cx) {
                if (inOldGen(cx, cz)) continue;
                createChunkIfMissing(cx, cz);
                queueGen(cx, cz, newCX, newCZ);
            }
        }

        for (int cz = newCZ - LOAD_RADIUS; cz <= newCZ + LOAD_RADIUS; ++cz) {
            for (int cx = newCX - LOAD_RADIUS; cx <= newCX + LOAD_RADIUS; ++cx) {
                if (inOldMesh(cx, cz)) continue;
                queueMesh(cx, cz, newCX, newCZ);
            }
        }
    };

    auto scheduleStreaming = [&](int pcx, int pcz){
        for (int r=0; r<=GEN_RADIUS; ++r) {
            for (int dz=-r; dz<=r; ++dz) {
                for (int dx=-r; dx<=r; ++dx) {
                    if (std::max(std::abs(dx), std::abs(dz)) != r) continue;
                    int cx = pcx + dx;
                    int cz = pcz + dz;
                    createChunkIfMissing(cx, cz);
                    queueGen(cx, cz, pcx, pcz);
                }
            }
        }

        for (int r=0; r<=LOAD_RADIUS; ++r) {
            for (int dz=-r; dz<=r; ++dz) {
                for (int dx=-r; dx<=r; ++dx) {
                    if (std::max(std::abs(dx), std::abs(dz)) != r) continue;
                    queueMesh(pcx + dx, pcz + dz, pcx, pcz);
                }
            }
        }
    };

    // ============================================================
    // Prime initial streaming
    // ============================================================
    int playerCX = worldToChunkCoord(cameraPos.x, CHUNK_WORLD_X);
    int playerCZ = worldToChunkCoord(cameraPos.z, CHUNK_WORLD_Z);
    gPlayerCX.store(playerCX);
    gPlayerCZ.store(playerCZ);
    scheduleStreaming(playerCX, playerCZ);

    // ============================================================
    // Click + hover
    // ============================================================
    bool lmbWasDown = false;
    bool rmbWasDown = false;
    const float EDIT_REACH = 10.0f;
    std::optional<RayHit> hoverHit;

    // ============================================================
    // Main loop
    // ============================================================
    while (!glfwWindowShouldClose(window)) {
        float now = (float)glfwGetTime();
        deltaTime = now - lastFrame;
        lastFrame = now;

        bool f1Down = glfwGetKey(window, GLFW_KEY_F1) == GLFW_PRESS;
        if (f1Down && !f1WasDown) wireframe = !wireframe;
        f1WasDown = f1Down;
        glPolygonMode(GL_FRONT_AND_BACK, wireframe ? GL_LINE : GL_FILL);

        // Movement
        if (glfwGetKey(window, GLFW_KEY_W) == GLFW_PRESS)
            cameraPos += cameraSpeed * deltaTime * gCameraFront;
        if (glfwGetKey(window, GLFW_KEY_S) == GLFW_PRESS)
            cameraPos -= cameraSpeed * deltaTime * gCameraFront;
        if (glfwGetKey(window, GLFW_KEY_A) == GLFW_PRESS)
            cameraPos -= glm::normalize(glm::cross(gCameraFront, cameraUp)) * cameraSpeed * deltaTime;
        if (glfwGetKey(window, GLFW_KEY_D) == GLFW_PRESS)
            cameraPos += glm::normalize(glm::cross(gCameraFront, cameraUp)) * cameraSpeed * deltaTime;

        if (glfwGetKey(window, GLFW_KEY_SPACE) == GLFW_PRESS)
            cameraPos.y += cameraSpeed * deltaTime;
        if (glfwGetKey(window, GLFW_KEY_LEFT_SHIFT) == GLFW_PRESS ||
            glfwGetKey(window, GLFW_KEY_RIGHT_SHIFT) == GLFW_PRESS)
            cameraPos.y -= cameraSpeed * deltaTime;

        // Chunk change (delta schedule)
        int newCX = worldToChunkCoord(cameraPos.x, CHUNK_WORLD_X);
        int newCZ = worldToChunkCoord(cameraPos.z, CHUNK_WORLD_Z);
        if (newCX != playerCX || newCZ != playerCZ) {
            int oldCX = playerCX;
            int oldCZ = playerCZ;
            playerCX = newCX;
            playerCZ = newCZ;
            gPlayerCX.store(playerCX);
            gPlayerCZ.store(playerCZ);
            scheduleDelta(oldCX, oldCZ, playerCX, playerCZ);
        } else {
            gPlayerCX.store(playerCX);
            gPlayerCZ.store(playerCZ);
        }

        // Hover raycast for highlight + click
        hoverHit = raycastBlocks(cameraPos, glm::normalize(gCameraFront), EDIT_REACH);

        // Click handling
        bool lmbDown = glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_LEFT) == GLFW_PRESS;
        bool rmbDown = glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_RIGHT) == GLFW_PRESS;
        bool lmbClick = lmbDown && !lmbWasDown;
        bool rmbClick = rmbDown && !rmbWasDown;
        lmbWasDown = lmbDown;
        rmbWasDown = rmbDown;

        if ((lmbClick || rmbClick) && hoverHit.has_value()) {
            RayHit hit = *hoverHit;

            if (lmbClick) {
                setBlockWorld(hit.wx, hit.wy, hit.wz, AIR);
            }

            if (rmbClick) {
                int px = hit.wx + hit.nx;
                int py = hit.wy + hit.ny;
                int pz = hit.wz + hit.nz;

                if (py >= 0 && py < CHUNK_Y) {
                    int camX = (int)std::floor(cameraPos.x);
                    int camY = (int)std::floor(cameraPos.y);
                    int camZ = (int)std::floor(cameraPos.z);

                    if (!(px == camX && py == camY && pz == camZ)) {
                        uint8_t s = solidAtWorld(px, py, pz);
                        if (s == AIR) setBlockWorld(px, py, pz, SOLID);
                    }
                }
            }
        }

        // ============================================================
        // Upload budget (urgent uploads first)
        // - if there are urgent uploads waiting, we use a much larger budget until they drain
        // ============================================================
        double budgetMs = UPLOAD_BUDGET_MS_NORMAL;
        {
            std::lock_guard<std::mutex> lock(meshDoneMutex);
            if (!meshDoneUrgentQ.empty()) budgetMs = UPLOAD_BUDGET_MS_URGENT;
        }

        double t0 = glfwGetTime();
        while (true) {
            double elapsedMs = (glfwGetTime() - t0) * 1000.0;
            if (elapsedMs > budgetMs) break;

            MeshDone md;
            bool isUrgent = false;

            {
                std::lock_guard<std::mutex> lock(meshDoneMutex);
                if (!meshDoneUrgentQ.empty()) {
                    md = meshDoneUrgentQ.front();
                    meshDoneUrgentQ.pop_front();
                    meshDoneUrgentSet.erase(key64(md.cx, md.cz));
                    isUrgent = true;
                } else if (!meshDoneQ.empty()) {
                    md = meshDoneQ.front();
                    meshDoneQ.pop_front();
                    meshDoneSet.erase(key64(md.cx, md.cz));
                    isUrgent = false;
                } else {
                    break;
                }
            }

            (void)isUrgent;

            GpuChunk* ch = pinChunk(md.cx, md.cz);
            if (!ch) continue;

            if (!ch->stagedReady.load(std::memory_order_acquire)) { unpinChunk(ch); continue; }

            {
                std::lock_guard<std::mutex> lock(ch->stagedMutex);
                if (!ch->stagedReady.load(std::memory_order_acquire)) { unpinChunk(ch); continue; }
                ch->mesh = std::move(ch->stagedMesh);
                ch->stagedMesh.verts.clear();
                ch->stagedMesh.indices.clear();
                ch->stagedReady.store(false, std::memory_order_release);
            }

            uploadChunkMesh(*ch);
            ch->hasMesh.store(true, std::memory_order_release);
            ch->dirtyMesh.store(false, std::memory_order_release);

            unpinChunk(ch);
        }

        // ============================================================
        // Unload far chunks (only if idle)
        // ============================================================
        std::vector<uint64_t> toErase;
        toErase.reserve(256);
        {
            std::lock_guard<std::mutex> lock(chunksMutex);
            for (auto& kv : chunks) {
                GpuChunk* ch = kv.second.get();
                int cx = ch->cpos.x;
                int cz = ch->cpos.y;

                if (std::abs(cx - playerCX) > UNLOAD_RADIUS || std::abs(cz - playerCZ) > UNLOAD_RADIUS) {
                    if (ch->pins.load(std::memory_order_acquire) != 0) continue;
                    if (ch->inGen.load(std::memory_order_acquire) || ch->inMesh.load(std::memory_order_acquire)) continue;

                    unloadChunkGL(*ch);
                    toErase.push_back(kv.first);
                }
            }
            for (uint64_t k : toErase) chunks.erase(k);
        }

        // ============================================================
        // Render
        // ============================================================
        glClearColor(0.1f, 0.1f, 0.1f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        int fbW=0, fbH=0;
        glfwGetFramebufferSize(window, &fbW, &fbH);
        if (fbW <= 0) fbW = mode->width;
        if (fbH <= 0) fbH = mode->height;

        glUseProgram(program);

        glm::mat4 view = glm::lookAt(cameraPos, cameraPos + glm::normalize(gCameraFront), cameraUp);
        glm::mat4 proj = glm::perspective(glm::radians(45.0f), float(fbW) / float(fbH), 0.1f, 5000.0f);
        glm::mat4 mvp  = proj * view;
        glUniformMatrix4fv(mvpLoc, 1, GL_FALSE, &mvp[0][0]);

        // Snapshot draw list
        std::vector<GpuChunk*> drawList;
        drawList.reserve(2048);
        {
            std::lock_guard<std::mutex> lock(chunksMutex);
            for (auto& kv : chunks) drawList.push_back(kv.second.get());
        }

        for (GpuChunk* ch : drawList) {
            ch->pins.fetch_add(1, std::memory_order_acq_rel);

            if (std::abs(ch->cpos.x - playerCX) > LOAD_RADIUS || std::abs(ch->cpos.y - playerCZ) > LOAD_RADIUS) {
                ch->pins.fetch_sub(1, std::memory_order_acq_rel);
                continue;
            }

            if (!ch->hasMesh.load(std::memory_order_acquire) || ch->vao == 0 || ch->mesh.indices.empty()) {
                ch->pins.fetch_sub(1, std::memory_order_acq_rel);
                continue;
            }

            glBindVertexArray(ch->vao);
            glDrawElements(GL_TRIANGLES, (GLsizei)ch->mesh.indices.size(), GL_UNSIGNED_INT, 0);

            ch->pins.fetch_sub(1, std::memory_order_acq_rel);
        }
        glBindVertexArray(0);

        // Highlight outline last (on top)
        if (hoverHit.has_value()) {
            uploadOutlineBox(hoverHit->wx, hoverHit->wy, hoverHit->wz, BLOCK);
            glDisable(GL_DEPTH_TEST);
            glBindVertexArray(outlineVAO);
            glDrawArrays(GL_LINES, 0, 24);
            glBindVertexArray(0);
            glEnable(GL_DEPTH_TEST);
        }

        glfwSwapBuffers(window);
        glfwPollEvents();
    }

    // ============================================================
    // Shutdown workers
    // ============================================================
    genStop = true;
    genCV.notify_all();
    for (auto& t : genWorkers) t.join();

    meshStop = true;
    meshCV.notify_all();
    for (auto& t : meshWorkers) t.join();

    // Cleanup GL
    {
        std::lock_guard<std::mutex> lock(chunksMutex);
        for (auto& kv : chunks) unloadChunkGL(*kv.second);
        chunks.clear();
    }

    if (outlineVAO) glDeleteVertexArrays(1, &outlineVAO);
    if (outlineVBO) glDeleteBuffers(1, &outlineVBO);

    glDeleteProgram(program);
    glfwDestroyWindow(window);
    glfwTerminate();
    return 0;
}