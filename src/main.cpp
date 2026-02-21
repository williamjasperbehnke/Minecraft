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

int main() {
    try {
        std::srand(static_cast<unsigned int>(std::time(nullptr)));
        if (!glfwInit()) {
            core::Logger::instance().error("Failed to initialize GLFW");
            return 1;
        }

        glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
        glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
        glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);

        GLFWwindow *window = glfwCreateWindow(1280, 720, "Voxel Clone", nullptr, nullptr);
        if (window == nullptr) {
            core::Logger::instance().error("Failed to create window");
            glfwTerminate();
            return 1;
        }

        glfwMakeContextCurrent(window);
        GLFWcursor *arrowCursor = glfwCreateStandardCursor(GLFW_ARROW_CURSOR);
        glfwSetFramebufferSizeCallback(window, onFramebufferResize);
        glfwSetScrollCallback(window, onMouseScroll);
        glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_NORMAL);
        glfwSetCursor(window, arrowCursor);

        if (!gladLoadGLLoader(reinterpret_cast<GLADloadproc>(glfwGetProcAddress))) {
            core::Logger::instance().error("Failed to initialize glad");
            glfwDestroyWindow(window);
            glfwTerminate();
            return 1;
        }

        glEnable(GL_DEPTH_TEST);
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

        gfx::Shader shader("shaders/chunk.vert", "shaders/chunk.frag");
        gfx::TextureAtlas atlas("assets/textures/atlas.png", 16, 16);
        gfx::HudRenderer hud;
        bool appRunning = true;
        while (appRunning && !glfwWindowShouldClose(window)) {
            const WorldSelection worldSelection = runTitleMenu(window, hud);
            if (!worldSelection.start) {
                appRunning = false;
                break;
            }
            glfwSetWindowTitle(window, ("Voxel Clone - " + worldSelection.name).c_str());
            glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
            glfwSetCursor(window, nullptr);

            world::World world(atlas, worldSelection.path, worldSelection.seed);
            game::Camera camera(glm::vec3(8.0f, 80.0f, 8.0f));
            game::DebugMenu debugMenu;
            game::ItemDropSystem itemDrops;
            game::MapSystem mapSystem;
            game::AudioSystem audio;
            voxel::BlockRegistry hudRegistry;

            audio.loadPickupSounds({
                "assets/audio/pickup/pickup.wav",
            });
            audio.loadBreakSounds(game::AudioSystem::SoundProfile::Default,
                                  {
                                      "assets/audio/break/break_1.wav",
                                      "assets/audio/break/break_2.wav",
                                      "assets/audio/break/break_3.wav",
                                  });
            audio.loadBreakSounds(game::AudioSystem::SoundProfile::Stone,
                                  {
                                      "assets/audio/break/break_stone_1.wav",
                                      "assets/audio/break/break_stone_2.wav",
                                      "assets/audio/break/break_stone_3.wav",
                                  });
            audio.loadBreakSounds(game::AudioSystem::SoundProfile::Dirt,
                                  {
                                      "assets/audio/break/break_dirt_1.wav",
                                      "assets/audio/break/break_dirt_2.wav",
                                      "assets/audio/break/break_dirt_3.wav",
                                  });
            audio.loadBreakSounds(game::AudioSystem::SoundProfile::Wood,
                                  {
                                      "assets/audio/break/break_wood_1.wav",
                                      "assets/audio/break/break_wood_2.wav",
                                      "assets/audio/break/break_wood_3.wav",
                                  });
            audio.loadBreakSounds(game::AudioSystem::SoundProfile::Foliage,
                                  {
                                      "assets/audio/break/break_foliage_1.wav",
                                      "assets/audio/break/break_foliage_2.wav",
                                      "assets/audio/break/break_foliage_3.wav",
                                  });
            audio.loadBreakSounds(game::AudioSystem::SoundProfile::Sand,
                                  {
                                      "assets/audio/break/break_sand_1.wav",
                                      "assets/audio/break/break_sand_2.wav",
                                      "assets/audio/break/break_sand_3.wav",
                                  });
            audio.loadBreakSounds(game::AudioSystem::SoundProfile::Snow,
                                  {
                                      "assets/audio/break/break_snow_1.wav",
                                      "assets/audio/break/break_snow_2.wav",
                                  });
            audio.loadBreakSounds(game::AudioSystem::SoundProfile::Ice,
                                  {
                                      "assets/audio/break/break_ice_1.wav",
                                      "assets/audio/break/break_ice_2.wav",
                                  });

            audio.loadFootstepSounds(game::AudioSystem::SoundProfile::Default,
                                     {
                                         "assets/audio/step/step_default_1.wav",
                                         "assets/audio/step/step_default_2.wav",
                                     });
            audio.loadFootstepSounds(game::AudioSystem::SoundProfile::Stone,
                                     {
                                         "assets/audio/step/step_stone_1.wav",
                                         "assets/audio/step/step_stone_2.wav",
                                         "assets/audio/step/step_stone_3.wav",
                                     });
            audio.loadFootstepSounds(game::AudioSystem::SoundProfile::Dirt,
                                     {
                                         "assets/audio/step/step_dirt_1.wav",
                                         "assets/audio/step/step_dirt_2.wav",
                                         "assets/audio/step/step_dirt_3.wav",
                                     });
            audio.loadFootstepSounds(game::AudioSystem::SoundProfile::Grass,
                                     {
                                         "assets/audio/step/step_grass_1.wav",
                                         "assets/audio/step/step_grass_2.wav",
                                         "assets/audio/step/step_grass_3.wav",
                                     });
            audio.loadFootstepSounds(game::AudioSystem::SoundProfile::Wood,
                                     {
                                         "assets/audio/step/step_wood_1.wav",
                                         "assets/audio/step/step_wood_2.wav",
                                         "assets/audio/step/step_wood_3.wav",
                                     });
            audio.loadFootstepSounds(game::AudioSystem::SoundProfile::Foliage,
                                     {
                                         "assets/audio/step/step_foliage_1.wav",
                                         "assets/audio/step/step_foliage_2.wav",
                                         "assets/audio/step/step_foliage_3.wav",
                                     });
            audio.loadFootstepSounds(game::AudioSystem::SoundProfile::Sand,
                                     {
                                         "assets/audio/step/step_sand_1.wav",
                                         "assets/audio/step/step_sand_2.wav",
                                         "assets/audio/step/step_sand_3.wav",
                                     });
            audio.loadFootstepSounds(game::AudioSystem::SoundProfile::Snow,
                                     {
                                         "assets/audio/step/step_snow_1.wav",
                                         "assets/audio/step/step_snow_2.wav",
                                     });
            audio.loadFootstepSounds(game::AudioSystem::SoundProfile::Ice,
                                     {
                                         "assets/audio/step/step_ice_1.wav",
                                         "assets/audio/step/step_ice_2.wav",
                                     });
            audio.loadPlaceSounds(game::AudioSystem::SoundProfile::Default,
                                  {
                                      "assets/audio/place/place_default_1.wav",
                                      "assets/audio/place/place_default_2.wav",
                                  });
            audio.loadPlaceSounds(game::AudioSystem::SoundProfile::Stone,
                                  {
                                      "assets/audio/place/place_stone_1.wav",
                                      "assets/audio/place/place_stone_2.wav",
                                  });
            audio.loadPlaceSounds(game::AudioSystem::SoundProfile::Dirt,
                                  {
                                      "assets/audio/place/place_dirt_1.wav",
                                      "assets/audio/place/place_dirt_2.wav",
                                  });
            audio.loadPlaceSounds(game::AudioSystem::SoundProfile::Wood,
                                  {
                                      "assets/audio/place/place_wood_1.wav",
                                      "assets/audio/place/place_wood_2.wav",
                                  });
            audio.loadPlaceSounds(game::AudioSystem::SoundProfile::Foliage,
                                  {
                                      "assets/audio/place/place_foliage_1.wav",
                                      "assets/audio/place/place_foliage_2.wav",
                                  });
            audio.loadPlaceSounds(game::AudioSystem::SoundProfile::Sand,
                                  {
                                      "assets/audio/place/place_sand_1.wav",
                                      "assets/audio/place/place_sand_2.wav",
                                  });
            audio.loadPlaceSounds(game::AudioSystem::SoundProfile::Snow,
                                  {
                                      "assets/audio/place/place_snow_1.wav",
                                      "assets/audio/place/place_snow_2.wav",
                                  });
            audio.loadPlaceSounds(game::AudioSystem::SoundProfile::Ice,
                                  {
                                      "assets/audio/place/place_ice_1.wav",
                                      "assets/audio/place/place_ice_2.wav",
                                  });
            audio.loadSwimSounds({
                "assets/audio/swim/swim_1.wav",
                "assets/audio/swim/swim_2.wav",
                "assets/audio/swim/swim_3.wav",
            });
            audio.loadWaterBobSounds({
                "assets/audio/bob/bob_1.wav",
                "assets/audio/bob/bob_2.wav",
                "assets/audio/bob/bob_3.wav",
            });

            game::DebugConfig debugCfg;
            debugCfg.moveSpeed = camera.moveSpeed();
            debugCfg.mouseSensitivity = camera.mouseSensitivity();
            bool lastSmoothLighting = debugCfg.smoothLighting;

            bool prevLeft = false;
            bool prevRight = false;
            bool prevHudToggle = false;
            bool prevModeToggle = false;
            bool prevTextureReloadToggle = false;
            bool prevInventoryToggle = false;
            bool prevMapToggle = false;
            bool prevMapZoomInToggle = false;
            bool prevMapZoomOutToggle = false;
            bool prevPauseToggle = false;
            bool hudVisible = true;
            bool inventoryVisible = false;
            bool mapOpen = false;
            float mapZoom = 1.0f;
            float miniMapZoom = 1.0f;
            bool miniMapNorthLocked = true;
            bool miniMapShowCompass = true;
            bool miniMapShowWaypoints = true;
            int selectedWaypointIndex = -1;
            bool waypointNameFocused = false;
            bool waypointEditorOpen = false;
            bool prevWaypointBackspace = false;
            bool prevWaypointEnter = false;
            bool prevWaypointEscape = false;
            bool prevWaypointDelete = false;
            bool waypointEditHasBackup = false;
            bool waypointEditWasNew = false;
            int waypointEditBackupIndex = -1;
            game::MapSystem::Waypoint waypointEditBackup{};
            float mapCenterWX = 0.0f;
            float mapCenterWZ = 0.0f;
            bool mapDragActive = false;
            float mapDragLastMouseX = 0.0f;
            float mapDragLastMouseY = 0.0f;
            bool pauseMenuOpen = false;
            bool ghostMode = true;
            game::Player player;
            game::Inventory inventory;
            game::CraftingSystem craftingSystem;
            game::CraftingSystem::State crafting;
            game::SmeltingSystem smeltingSystem;
            game::SmeltingSystem::State smelting;
            std::optional<glm::ivec3> activeFurnaceCell;
            float recipeMenuScroll = 0.0f;
            std::string recipeSearchText;
            float creativeMenuScroll = 0.0f;
            std::string creativeSearchText;
            std::optional<voxel::BlockId> recipeIngredientFilter;
            bool recipeCraftableOnly = false;
            int craftingGridSize = game::CraftingSystem::kGridSizeInventory;
            bool usingCraftingTable = false;
            bool usingFurnace = false;
            game::Inventory::Slot carriedSlot{};
            std::array<bool, game::Inventory::kHotbarSize> prevBlockKeys{};
            int selectedBlockIndex = 0;
            glm::vec3 loadedCameraPos = camera.position();
            bool pendingSpawnResolve = false;
            if (loadPlayerData(worldSelection.path, loadedCameraPos, selectedBlockIndex, ghostMode,
                               inventory, smelting)) {
                camera.setPosition(loadedCameraPos);
                if (!ghostMode) {
                    pendingSpawnResolve = true;
                }
            }
            mapSystem.load(worldSelection.path);
            mapCenterWX = camera.position().x;
            mapCenterWZ = camera.position().z;
            auto toWorldFurnaceState = [&](const game::SmeltingSystem::State &src) {
                world::FurnaceState out{};
                out.input.id = src.input.id;
                out.input.count = src.input.count;
                out.fuel.id = src.fuel.id;
                out.fuel.count = src.fuel.count;
                out.output.id = src.output.id;
                out.output.count = src.output.count;
                out.progressSeconds = src.progressSeconds;
                out.burnSecondsRemaining = src.burnSecondsRemaining;
                out.burnSecondsCapacity = src.burnSecondsCapacity;
                return out;
            };
            auto fromWorldFurnaceState = [&](const world::FurnaceState &src) {
                game::SmeltingSystem::State out{};
                out.input.id = src.input.id;
                out.input.count = src.input.count;
                out.fuel.id = src.fuel.id;
                out.fuel.count = src.fuel.count;
                out.output.id = src.output.id;
                out.output.count = src.output.count;
                out.progressSeconds = src.progressSeconds;
                out.burnSecondsRemaining = src.burnSecondsRemaining;
                out.burnSecondsCapacity = src.burnSecondsCapacity;
                return out;
            };
            auto persistActiveFurnaceState = [&]() {
                if (!activeFurnaceCell.has_value()) {
                    return;
                }
                const bool hasItems = (smelting.input.id != voxel::AIR && smelting.input.count > 0) ||
                                      (smelting.fuel.id != voxel::AIR && smelting.fuel.count > 0) ||
                                      (smelting.output.id != voxel::AIR && smelting.output.count > 0);
                const bool hasWork =
                    smelting.progressSeconds > 0.0f || smelting.burnSecondsRemaining > 0.0f ||
                    smelting.burnSecondsCapacity > 0.0f;
                if (hasItems || hasWork) {
                    const glm::ivec3 c = activeFurnaceCell.value();
                    world.setFurnaceState(c.x, c.y, c.z, toWorldFurnaceState(smelting));
                } else {
                    const glm::ivec3 c = activeFurnaceCell.value();
                    world.clearFurnaceState(c.x, c.y, c.z);
                }
            };
            auto loadActiveFurnaceState = [&]() {
                smelting = {};
                if (!activeFurnaceCell.has_value()) {
                    return;
                }
                world::FurnaceState loaded{};
                const glm::ivec3 c = activeFurnaceCell.value();
                if (world.getFurnaceState(c.x, c.y, c.z, loaded)) {
                    smelting = fromWorldFurnaceState(loaded);
                }
            };
            auto saveCurrentPlayer = [&]() {
                persistActiveFurnaceState();
                mapSystem.save(worldSelection.path);
                savePlayerData(worldSelection.path, camera.position(), selectedBlockIndex, ghostMode,
                               inventory, smelting);
            };
            camera.resetMouseLook(window);
            bool prevInventoryLeft = false;
            bool prevInventoryRight = false;
            bool prevRecipeMenuToggle = false;
            bool prevRecipeIngredientFilterKey = false;
            bool prevDropKey = false;
            bool prevRecipeSearchBackspace = false;
            float lastInventoryLeftClickTime = -1.0f;
            voxel::BlockId lastInventoryLeftClickId = voxel::AIR;
            bool recaptureMouseAfterInventoryClose = false;
            bool recipeMenuVisible = false;
            bool creativeMenuVisible = false;
            bool recipeScrollDragging = false;
            bool creativeScrollDragging = false;
            float recipeScrollGrabOffsetY = 0.0f;
            float creativeScrollGrabOffsetY = 0.0f;
            bool recipeSearchFocused = false;
            bool creativeSearchFocused = false;
            int lastRecipeFillIndex = -1;
            bool prevCreativeMenuToggle = false;
            bool prevCreativeSearchBackspace = false;
            bool inventorySpreadDragging = false;
            std::vector<int> inventorySpreadSlots;
            game::Inventory::Slot inventorySpreadStartCarried{};
            std::vector<std::pair<int, game::Inventory::Slot>> inventorySpreadOriginalSlots;
            int inventorySpreadPickupSourceSlot = -1;
            bool inventoryRightSpreadDragging = false;
            std::vector<int> inventoryRightSpreadSlots;
            game::Inventory::Slot inventoryRightSpreadStartCarried{};
            std::vector<std::pair<int, game::Inventory::Slot>> inventoryRightSpreadOriginalSlots;

            float lastTime = static_cast<float>(glfwGetTime());
            float titleAccum = 0.0f;
            float dayClockSeconds = 120.0f;
            constexpr float kDayLengthSeconds = 900.0f;
            SkyBodyRenderer skyBodyRenderer;
            ChunkBorderRenderer chunkBorderRenderer;
            world::WorldDebugStats stats{};
            bool wasMenuOpen = false;
            std::optional<glm::ivec3> miningBlock;
            float miningProgress = 0.0f;
            float mineCooldown = 0.0f;
            glm::vec3 prevStepCamPos = camera.position();
            float stepDistanceAccum = 0.0f;
            float stepCooldown = 0.0f;
            float swimCooldown = 0.0f;
            float bobCooldown = 0.0f;

            bool returnToTitle = false;
            while (!glfwWindowShouldClose(window)) {
                const float now = static_cast<float>(glfwGetTime());
                const float dt = now - lastTime;
                lastTime = now;
                const float fps = (dt > 0.0f) ? (1.0f / dt) : 0.0f;

                glfwPollEvents();
                debugMenu.update(window, debugCfg, stats, fps, dt * 1000.0f);
                const bool menuOpen = debugMenu.isOpen();
                const bool pauseToggleDown = glfwGetKey(window, GLFW_KEY_ESCAPE) == GLFW_PRESS;
                if (pauseToggleDown && !prevPauseToggle && !menuOpen && !inventoryVisible &&
                    !mapOpen) {
                    pauseMenuOpen = !pauseMenuOpen;
                }
                prevPauseToggle = pauseToggleDown;
                const bool blockInput = menuOpen || inventoryVisible || pauseMenuOpen || mapOpen;
                if (wasMenuOpen && !menuOpen && !inventoryVisible) {
                    camera.resetMouseLook(window);
                }
                wasMenuOpen = menuOpen;
                camera.setMoveSpeed(debugCfg.moveSpeed);
                camera.setMouseSensitivity(debugCfg.mouseSensitivity);
                world.setStreamingRadii(debugCfg.loadRadius, debugCfg.unloadRadius);
                if (debugCfg.smoothLighting != lastSmoothLighting) {
                    world.setSmoothLighting(debugCfg.smoothLighting);
                    lastSmoothLighting = debugCfg.smoothLighting;
                }

                const bool wantCursorNormal = blockInput;
                const int currentCursorMode = glfwGetInputMode(window, GLFW_CURSOR);
                const int desiredCursorMode =
                    wantCursorNormal ? GLFW_CURSOR_NORMAL : GLFW_CURSOR_DISABLED;
                if (currentCursorMode != desiredCursorMode) {
                    glfwSetInputMode(window, GLFW_CURSOR, desiredCursorMode);
                    glfwSetCursor(window, wantCursorNormal ? arrowCursor : nullptr);
                    if (desiredCursorMode == GLFW_CURSOR_DISABLED) {
                        camera.resetMouseLook(window);
                    }
                }
                bool justRecapturedMouse = false;
                if (recaptureMouseAfterInventoryClose && !blockInput) {
                    camera.resetMouseLook(window);
                    recaptureMouseAfterInventoryClose = false;
                    justRecapturedMouse = true;
                }
                if (!blockInput && !justRecapturedMouse) {
                    camera.handleMouse(window);
                }

                if (pendingSpawnResolve) {
                    const int swx = static_cast<int>(std::floor(loadedCameraPos.x));
                    const int swz = static_cast<int>(std::floor(loadedCameraPos.z));
                    if (world.isChunkLoadedAt(swx, swz)) {
                        // Restore exact saved position on reload (no collision adjustment).
                        player.setFromCamera(loadedCameraPos, world, false);
                        camera.setPosition(player.cameraPosition());
                        loadedCameraPos = camera.position();
                        pendingSpawnResolve = false;
                    }
                }

                if (ghostMode) {
                    if (!blockInput) {
                        camera.handleKeyboard(window, dt);
                    }
                } else {
                    if (!pendingSpawnResolve) {
                        player.update(window, world, camera, dt, !blockInput);
                        camera.setPosition(player.cameraPosition());
                    }
                }

                world.updateStream(camera.position());
                world.uploadReadyMeshes();
                stats = world.debugStats();
                mapSystem.observeLoadedChunks(world);
                if (debugCfg.overrideTime) {
                    dayClockSeconds =
                        std::clamp(debugCfg.timeOfDay01, 0.0f, 1.0f) * kDayLengthSeconds;
                } else {
                    dayClockSeconds += dt;
                    if (dayClockSeconds >= kDayLengthSeconds) {
                        dayClockSeconds = std::fmod(dayClockSeconds, kDayLengthSeconds);
                    }
                    debugCfg.timeOfDay01 = dayClockSeconds / kDayLengthSeconds;
                }
                mineCooldown = std::max(0.0f, mineCooldown - dt);
                itemDrops.update(world, camera.position(), dt);
                persistActiveFurnaceState();
                const auto loadedFurnaces = world.loadedFurnacePositions();
                for (const glm::ivec3 &fpos : loadedFurnaces) {
                    world::FurnaceState wstate{};
                    if (!world.getFurnaceState(fpos.x, fpos.y, fpos.z, wstate)) {
                        continue;
                    }
                    const voxel::BlockId furnaceBlockId = world.getBlock(fpos.x, fpos.y, fpos.z);
                    game::SmeltingSystem::State gstate = fromWorldFurnaceState(wstate);
                    smeltingSystem.update(gstate, dt);
                    const bool furnaceActive = gstate.burnSecondsRemaining > 0.0f;
                    if (voxel::isFurnace(furnaceBlockId)) {
                        const voxel::BlockId desiredId =
                            furnaceActive ? voxel::toLitFurnace(furnaceBlockId)
                                          : voxel::toUnlitFurnace(furnaceBlockId);
                        if (desiredId != furnaceBlockId) {
                            world.setBlock(fpos.x, fpos.y, fpos.z, desiredId);
                        }
                    }
                    const bool hasItems = (gstate.input.id != voxel::AIR && gstate.input.count > 0) ||
                                          (gstate.fuel.id != voxel::AIR && gstate.fuel.count > 0) ||
                                          (gstate.output.id != voxel::AIR && gstate.output.count > 0);
                    const bool hasWork =
                        gstate.progressSeconds > 0.0f || gstate.burnSecondsRemaining > 0.0f ||
                        gstate.burnSecondsCapacity > 0.0f;
                    if (!hasItems && !hasWork) {
                        world.clearFurnaceState(fpos.x, fpos.y, fpos.z);
                    } else {
                        world.setFurnaceState(fpos.x, fpos.y, fpos.z, toWorldFurnaceState(gstate));
                    }
                }
                loadActiveFurnaceState();
                for (const auto &pickup : itemDrops.consumePickups()) {
                    if (inventory.add(pickup.id, pickup.count)) {
                        audio.playPickup();
                    } else {
                        itemDrops.spawn(pickup.id, pickup.pos, pickup.count);
                    }
                }

                const bool hudToggleDown = glfwGetKey(window, GLFW_KEY_F2) == GLFW_PRESS;
                if (hudToggleDown && !prevHudToggle && !menuOpen) {
                    hudVisible = !hudVisible;
                }
                prevHudToggle = hudToggleDown;

                const bool mapToggleDown = glfwGetKey(window, GLFW_KEY_M) == GLFW_PRESS;
                if (mapToggleDown && !prevMapToggle && !menuOpen && !inventoryVisible &&
                    !waypointNameFocused &&
                    !pauseMenuOpen) {
                    mapOpen = !mapOpen;
                    if (mapOpen) {
                        mapCenterWX = camera.position().x;
                        mapCenterWZ = camera.position().z;
                        mapDragActive = false;
                        waypointEditorOpen = false;
                        waypointNameFocused = false;
                        prevWaypointEnter = false;
                        prevWaypointEscape = false;
                        prevWaypointDelete = false;
                        waypointEditHasBackup = false;
                        waypointEditWasNew = false;
                        waypointEditBackupIndex = -1;
                        gRecipeSearchText = nullptr;
                        glfwSetCharCallback(window, nullptr);
                    } else {
                        waypointEditorOpen = false;
                        waypointNameFocused = false;
                        prevWaypointEnter = false;
                        prevWaypointEscape = false;
                        prevWaypointDelete = false;
                        waypointEditHasBackup = false;
                        waypointEditWasNew = false;
                        waypointEditBackupIndex = -1;
                        gRecipeSearchText = nullptr;
                        glfwSetCharCallback(window, nullptr);
                    }
                }
                prevMapToggle = mapToggleDown;
                const bool mapZoomInDown = (glfwGetKey(window, GLFW_KEY_EQUAL) == GLFW_PRESS) ||
                                           (glfwGetKey(window, GLFW_KEY_KP_ADD) == GLFW_PRESS);
                if (mapZoomInDown && !prevMapZoomInToggle && mapOpen) {
                    mapZoom = std::clamp(mapZoom * 1.15f, 0.5f, 3.0f);
                }
                prevMapZoomInToggle = mapZoomInDown;
                const bool mapZoomOutDown = (glfwGetKey(window, GLFW_KEY_MINUS) == GLFW_PRESS) ||
                                            (glfwGetKey(window, GLFW_KEY_KP_SUBTRACT) == GLFW_PRESS);
                if (mapZoomOutDown && !prevMapZoomOutToggle && mapOpen) {
                    mapZoom = std::clamp(mapZoom / 1.15f, 0.5f, 3.0f);
                }
                prevMapZoomOutToggle = mapZoomOutDown;
                if (mapOpen && std::abs(gRecipeMenuScrollDelta) > 0.01f) {
                    const float factor = 1.0f + gRecipeMenuScrollDelta * 0.08f;
                    mapZoom = std::clamp(mapZoom * std::max(0.5f, factor), 0.5f, 3.0f);
                    gRecipeMenuScrollDelta = 0.0f;
                }

                const bool textureReloadDown = glfwGetKey(window, GLFW_KEY_F5) == GLFW_PRESS;
                if (textureReloadDown && !prevTextureReloadToggle && !menuOpen) {
                    if (atlas.reload("assets/textures/atlas.png")) {
                        core::Logger::instance().info("Reloaded texture atlas (F5)");
                    } else {
                        core::Logger::instance().warn(
                            "Failed to reload texture atlas (assets/textures/atlas.png)");
                    }
                }
                prevTextureReloadToggle = textureReloadDown;

                const bool modeToggleDown = glfwGetKey(window, GLFW_KEY_F4) == GLFW_PRESS;
                if (modeToggleDown && !prevModeToggle && !menuOpen && !inventoryVisible &&
                    !pauseMenuOpen && !mapOpen) {
                    ghostMode = !ghostMode;
                    if (!ghostMode) {
                        player.setFromCamera(camera.position(), world);
                    } else {
                        camera.setPosition(player.cameraPosition());
                    }
                }
                prevModeToggle = modeToggleDown;

                auto clearCraftInputs = [&]() {
                    if (usingFurnace) {
                        auto flushSmeltSlot = [&](game::Inventory::Slot &slot) {
                            if (slot.id == voxel::AIR || slot.count <= 0) {
                                slot = {};
                                return;
                            }
                            while (slot.count > 0 && inventory.add(slot.id, 1)) {
                                slot.count -= 1;
                            }
                            if (slot.count > 0) {
                                const glm::vec3 dropPos =
                                    camera.position() + camera.forward() * 2.10f +
                                    glm::vec3(0.0f, -0.30f, 0.0f);
                                itemDrops.spawn(slot.id, dropPos, slot.count);
                            }
                            slot = {};
                        };
                        flushSmeltSlot(smelting.input);
                        flushSmeltSlot(smelting.fuel);
                        flushSmeltSlot(smelting.output);
                        smelting.progressSeconds = 0.0f;
                        smelting.burnSecondsRemaining = 0.0f;
                        smelting.burnSecondsCapacity = 0.0f;
                        return;
                    }
                    for (int i = 0; i < kCraftInputCount; ++i) {
                        auto &slot = crafting.input[i];
                        if (slot.id == voxel::AIR || slot.count <= 0) {
                            slot = {};
                            continue;
                        }
                        while (slot.count > 0 && inventory.add(slot.id, 1)) {
                            slot.count -= 1;
                        }
                        if (slot.count > 0) {
                            const glm::vec3 dropPos =
                                camera.position() + camera.forward() * 2.10f +
                                glm::vec3(0.0f, -0.30f, 0.0f);
                            itemDrops.spawn(slot.id, dropPos, slot.count);
                        }
                        slot = {};
                    }
                    craftingSystem.updateOutput(crafting, craftingGridSize);
                    lastRecipeFillIndex = -1;
                };

                const bool invToggleDown = glfwGetKey(window, GLFW_KEY_E) == GLFW_PRESS;
                const bool suppressInventoryToggle =
                    ((recipeMenuVisible && recipeSearchFocused) ||
                     (creativeMenuVisible && creativeSearchFocused)) &&
                    !menuOpen && !pauseMenuOpen;
                if (invToggleDown && !prevInventoryToggle && !menuOpen && !pauseMenuOpen &&
                    !mapOpen && !suppressInventoryToggle) {
                    const bool openingInventory = !inventoryVisible;
                    inventoryVisible = openingInventory;
                    if (openingInventory) {
                        const auto lookedFurnace =
                            lookedAtFurnaceCell(world, camera, debugCfg.raycastDistance);
                        usingFurnace = lookedFurnace.has_value();
                        if (usingFurnace) {
                            activeFurnaceCell = lookedFurnace;
                            loadActiveFurnaceState();
                        } else {
                            activeFurnaceCell.reset();
                            smelting = {};
                        }
                        usingCraftingTable =
                            !usingFurnace &&
                            isLookingAtCraftingTable(world, camera, debugCfg.raycastDistance);
                        craftingGridSize = usingCraftingTable ? game::CraftingSystem::kGridSizeTable
                                                              : game::CraftingSystem::kGridSizeInventory;
                        craftingSystem.updateOutput(crafting, craftingGridSize);
                    } else {
                        persistActiveFurnaceState();
                        if (!usingFurnace) {
                            clearCraftInputs();
                        }
                        usingCraftingTable = false;
                        usingFurnace = false;
                        activeFurnaceCell.reset();
                        smelting = {};
                        craftingGridSize = game::CraftingSystem::kGridSizeInventory;
                        recipeMenuVisible = false;
                        creativeMenuVisible = false;
                        recipeMenuScroll = 0.0f;
                        creativeMenuScroll = 0.0f;
                        recipeScrollDragging = false;
                        creativeScrollDragging = false;
                        recipeSearchFocused = false;
                        creativeSearchFocused = false;
                        recipeSearchText.clear();
                        creativeSearchText.clear();
                        recipeIngredientFilter.reset();
                        recipeCraftableOnly = false;
                        gRecipeSearchText = nullptr;
                        glfwSetCharCallback(window, nullptr);
                        craftingSystem.updateOutput(crafting, craftingGridSize);
                        lastRecipeFillIndex = -1;
                    }
                    glfwSetInputMode(window, GLFW_CURSOR,
                                     inventoryVisible ? GLFW_CURSOR_NORMAL : GLFW_CURSOR_DISABLED);
                    glfwSetCursor(window, inventoryVisible ? arrowCursor : nullptr);
                    if (!inventoryVisible) {
                        recaptureMouseAfterInventoryClose = true;
                    }
                }
                prevInventoryToggle = invToggleDown;

                const bool recipeToggleDown = glfwGetKey(window, GLFW_KEY_R) == GLFW_PRESS;
                if (recipeToggleDown && !prevRecipeMenuToggle && inventoryVisible && !menuOpen &&
                    !pauseMenuOpen && !mapOpen && !(recipeMenuVisible && recipeSearchFocused) &&
                    !(creativeMenuVisible && creativeSearchFocused)) {
                    recipeMenuVisible = !recipeMenuVisible;
                    if (recipeMenuVisible) {
                        creativeMenuVisible = false;
                        creativeScrollDragging = false;
                        creativeSearchFocused = false;
                        creativeSearchText.clear();
                        recipeSearchFocused = false;
                        gRecipeSearchText = nullptr;
                        glfwSetCharCallback(window, nullptr);
                    } else {
                        recipeMenuScroll = 0.0f;
                        recipeScrollDragging = false;
                        recipeSearchFocused = false;
                        recipeSearchText.clear();
                        recipeIngredientFilter.reset();
                        recipeCraftableOnly = false;
                        gRecipeSearchText = nullptr;
                        glfwSetCharCallback(window, nullptr);
                    }
                }
                prevRecipeMenuToggle = recipeToggleDown;

                const bool creativeToggleDown = glfwGetKey(window, GLFW_KEY_F) == GLFW_PRESS;
                if (creativeToggleDown && !prevCreativeMenuToggle && !menuOpen && !pauseMenuOpen &&
                    !mapOpen &&
                    !(creativeMenuVisible && creativeSearchFocused) &&
                    !(recipeMenuVisible && recipeSearchFocused)) {
                    if (!inventoryVisible) {
                        inventoryVisible = true;
                        const auto lookedFurnace =
                            lookedAtFurnaceCell(world, camera, debugCfg.raycastDistance);
                        usingFurnace = lookedFurnace.has_value();
                        if (usingFurnace) {
                            activeFurnaceCell = lookedFurnace;
                            loadActiveFurnaceState();
                        } else {
                            activeFurnaceCell.reset();
                            smelting = {};
                        }
                        usingCraftingTable =
                            !usingFurnace &&
                            isLookingAtCraftingTable(world, camera, debugCfg.raycastDistance);
                        craftingGridSize = usingCraftingTable ? game::CraftingSystem::kGridSizeTable
                                                              : game::CraftingSystem::kGridSizeInventory;
                        craftingSystem.updateOutput(crafting, craftingGridSize);
                        glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_NORMAL);
                        glfwSetCursor(window, arrowCursor);
                    }
                    creativeMenuVisible = !creativeMenuVisible;
                    if (creativeMenuVisible) {
                        recipeMenuVisible = false;
                        recipeScrollDragging = false;
                        recipeSearchFocused = false;
                        recipeSearchText.clear();
                        recipeIngredientFilter.reset();
                        recipeCraftableOnly = false;
                        creativeSearchFocused = false;
                        gRecipeSearchText = nullptr;
                        glfwSetCharCallback(window, nullptr);
                    } else {
                        creativeMenuScroll = 0.0f;
                        creativeScrollDragging = false;
                        creativeSearchFocused = false;
                        creativeSearchText.clear();
                        gRecipeSearchText = nullptr;
                        glfwSetCharCallback(window, nullptr);
                    }
                }
                prevCreativeMenuToggle = creativeToggleDown;

                const bool recipeBackspace = glfwGetKey(window, GLFW_KEY_BACKSPACE) == GLFW_PRESS;
                if (recipeMenuVisible && recipeBackspace && !prevRecipeSearchBackspace &&
                    !recipeSearchText.empty()) {
                    recipeSearchText.pop_back();
                }
                prevRecipeSearchBackspace = recipeBackspace;
                if (creativeMenuVisible && recipeBackspace && !prevCreativeSearchBackspace &&
                    !creativeSearchText.empty()) {
                    creativeSearchText.pop_back();
                }
                prevCreativeSearchBackspace = recipeBackspace;

                std::vector<int> filteredRecipeIndices;
                std::vector<int> filteredSmeltingIndices;
                std::vector<unsigned char> recipeCraftableCache;
                std::vector<unsigned char> recipeCraftableComputed;
                const int recipeFilterActiveInputs =
                    game::CraftingSystem::activeInputCount(craftingGridSize);
                auto isRecipeCraftableNow = [&](int index) {
                    if (index < 0 || index >= static_cast<int>(craftingSystem.recipeInfos().size())) {
                        return false;
                    }
                    if (recipeCraftableComputed[index] != 0) {
                        return recipeCraftableCache[index] != 0;
                    }
                    recipeCraftableComputed[index] = 1;
                    const auto &recipe = craftingSystem.recipeInfos()[index];
                    if (craftingGridSize < recipe.minGridSize) {
                        recipeCraftableCache[index] = 0;
                        return false;
                    }
                    game::Inventory invSim = inventory;
                    game::CraftingSystem::State craftSim{};
                    const bool craftable =
                        tryAddRecipeSet(recipe, invSim, craftSim, recipeFilterActiveInputs);
                    recipeCraftableCache[index] = craftable ? 1 : 0;
                    return craftable;
                };

                if (recipeMenuVisible && !usingFurnace) {
                    filteredRecipeIndices.reserve(craftingSystem.recipeInfos().size());
                    recipeCraftableCache.assign(craftingSystem.recipeInfos().size(), 0);
                    recipeCraftableComputed.assign(craftingSystem.recipeInfos().size(), 0);
                    std::vector<int> filteredPlankRecipeIndices;
                    filteredPlankRecipeIndices.reserve(3);
                    int plankInsertPos = -1;
                    for (int i = 0; i < static_cast<int>(craftingSystem.recipeInfos().size()); ++i) {
                        const auto &recipe = craftingSystem.recipeInfos()[i];
                        const bool matchesSearch = recipeMatchesSearch(recipe, recipeSearchText);
                        const bool matchesIngredient =
                            !recipeIngredientFilter.has_value() ||
                            recipeUsesIngredient(recipe, recipeIngredientFilter.value());
                        const bool matchesCraftable = !recipeCraftableOnly || isRecipeCraftableNow(i);
                        if (matchesSearch && matchesIngredient && matchesCraftable) {
                            const bool isPlanksFamily = (recipe.outputId == voxel::OAK_PLANKS ||
                                                         recipe.outputId == voxel::SPRUCE_PLANKS ||
                                                         recipe.outputId == voxel::BIRCH_PLANKS);
                            if (isPlanksFamily) {
                                if (plankInsertPos < 0) {
                                    plankInsertPos = static_cast<int>(filteredRecipeIndices.size());
                                }
                                filteredPlankRecipeIndices.push_back(i);
                                continue;
                            }
                            filteredRecipeIndices.push_back(i);
                        }
                    }
                    if (!filteredPlankRecipeIndices.empty()) {
                        const int cycle = static_cast<int>(std::floor(std::max(0.0f, now) * 0.6f));
                        const int pick = filteredPlankRecipeIndices
                            [cycle % static_cast<int>(filteredPlankRecipeIndices.size())];
                        const int insertPos =
                            (plankInsertPos < 0 ||
                             plankInsertPos > static_cast<int>(filteredRecipeIndices.size()))
                                ? static_cast<int>(filteredRecipeIndices.size())
                                : plankInsertPos;
                        filteredRecipeIndices.insert(filteredRecipeIndices.begin() + insertPos, pick);
                    }
                }
                if (recipeMenuVisible && usingFurnace) {
                    const auto &smeltRecipes = smeltingSystem.recipes();
                    filteredSmeltingIndices.reserve(smeltRecipes.size());

                    int fuelCount = 0;
                    for (int i = 0; i < game::Inventory::kSlotCount; ++i) {
                        const auto &slot = inventory.slot(i);
                        if (slot.id != voxel::AIR && slot.count > 0 && smeltingSystem.isFuel(slot.id)) {
                            fuelCount += slot.count;
                        }
                    }

                    for (int i = 0; i < static_cast<int>(smeltRecipes.size()); ++i) {
                        const auto &recipe = smeltRecipes[i];
                        const bool matchesSearch =
                            smeltingRecipeMatchesSearch(recipe, recipeSearchText);
                        const bool matchesIngredient = !recipeIngredientFilter.has_value() ||
                                                       recipe.input == recipeIngredientFilter.value();
                        int inputCount = 0;
                        for (int s = 0; s < game::Inventory::kSlotCount; ++s) {
                            const auto &slot = inventory.slot(s);
                            if (slot.id == recipe.input && slot.count > 0) {
                                inputCount += slot.count;
                            }
                        }
                        const bool matchesCraftable =
                            !recipeCraftableOnly || (inputCount > 0 && fuelCount > 0);
                        if (matchesSearch && matchesIngredient && matchesCraftable) {
                            filteredSmeltingIndices.push_back(i);
                        }
                    }
                }

                std::vector<voxel::BlockId> filteredCreativeItems;
                if (creativeMenuVisible) {
                    filteredCreativeItems.reserve(creativeCatalog().size());
                    for (voxel::BlockId id : creativeCatalog()) {
                        if (creativeItemMatchesSearch(id, creativeSearchText)) {
                            filteredCreativeItems.push_back(id);
                        }
                    }
                }

                if (recipeMenuVisible && inventoryVisible && !menuOpen && !pauseMenuOpen) {
                    int winW = 1;
                    int winH = 1;
                    glfwGetWindowSize(window, &winW, &winH);
                    double mx = 0.0;
                    double my = 0.0;
                    glfwGetCursorPos(window, &mx, &my);
                    const bool leftNow =
                        glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_LEFT) == GLFW_PRESS;
                    recipeMenuScroll -= gRecipeMenuScrollDelta * (34.0f * debugCfg.hudScale);
                    const std::size_t recipeCount =
                        usingFurnace ? filteredSmeltingIndices.size() : filteredRecipeIndices.size();
                    const RecipeMenuLayout recipeLayout = computeRecipeMenuLayout(
                        winW, winH, debugCfg.hudScale, craftingGridSize, usingFurnace, recipeCount);

                    if (usingFurnace) {
                        if (leftNow && !prevLeft) {
                            const bool onCraftableFilter =
                                mx >= recipeLayout.craftableFilterX &&
                                mx <= (recipeLayout.craftableFilterX + recipeLayout.craftableFilterW) &&
                                my >= recipeLayout.craftableFilterY &&
                                my <= (recipeLayout.craftableFilterY + recipeLayout.craftableFilterH);
                            const bool onIngredientFilterClear =
                                recipeIngredientFilter.has_value() &&
                                mx >= recipeLayout.ingredientTagCloseX &&
                                mx <= (recipeLayout.ingredientTagCloseX + recipeLayout.ingredientTagCloseS) &&
                                my >= recipeLayout.ingredientTagCloseY &&
                                my <= (recipeLayout.ingredientTagCloseY + recipeLayout.ingredientTagCloseS);
                            if (onIngredientFilterClear) {
                                recipeIngredientFilter.reset();
                                recipeSearchFocused = false;
                                gRecipeSearchText = nullptr;
                                glfwSetCharCallback(window, nullptr);
                            } else if (onCraftableFilter) {
                                recipeCraftableOnly = !recipeCraftableOnly;
                                recipeSearchFocused = false;
                                gRecipeSearchText = nullptr;
                                glfwSetCharCallback(window, nullptr);
                            } else {
                                recipeSearchFocused = mx >= recipeLayout.searchX &&
                                                      mx <= (recipeLayout.searchX + recipeLayout.searchW) &&
                                                      my >= recipeLayout.searchY &&
                                                      my <= (recipeLayout.searchY + recipeLayout.searchH);
                            }
                            if (recipeSearchFocused) {
                                gRecipeSearchText = &recipeSearchText;
                                glfwSetCharCallback(window, onRecipeSearchCharInput);
                            }
                        }
                        if (leftNow && !prevLeft) {
                            const bool onTrack = mx >= (recipeLayout.trackX - 6.0f) &&
                                                 mx <= (recipeLayout.trackX + recipeLayout.trackW + 6.0f) &&
                                                 my >= recipeLayout.trackY &&
                                                 my <= (recipeLayout.trackY + recipeLayout.trackH);
                            if (onTrack) {
                                const float thumbTravel =
                                    std::max(0.0f, recipeLayout.trackH - recipeLayout.thumbH);
                                const float thumbY =
                                    recipeLayout.trackY +
                                    ((recipeLayout.maxScroll > 0.0f)
                                         ? (recipeMenuScroll / recipeLayout.maxScroll) * thumbTravel
                                         : 0.0f);
                                const bool onThumb =
                                    my >= thumbY && my <= (thumbY + recipeLayout.thumbH);
                                if (onThumb) {
                                    recipeScrollDragging = true;
                                    recipeScrollGrabOffsetY = static_cast<float>(my) - thumbY;
                                } else if (recipeLayout.maxScroll > 0.0f) {
                                    const float t = std::clamp(
                                        static_cast<float>((my - recipeLayout.trackY -
                                                            recipeLayout.thumbH * 0.5f) /
                                                           std::max(1.0f, thumbTravel)),
                                        0.0f, 1.0f);
                                    recipeMenuScroll = t * recipeLayout.maxScroll;
                                }
                            }
                        }
                        if (!leftNow) {
                            recipeScrollDragging = false;
                        }
                        creativeSearchFocused = false;
                        gRecipeSearchText = recipeSearchFocused ? &recipeSearchText : nullptr;
                        glfwSetCharCallback(window,
                                            recipeSearchFocused ? onRecipeSearchCharInput : nullptr);
                        if (recipeScrollDragging && recipeLayout.maxScroll > 0.0f) {
                            const float thumbTravel =
                                std::max(0.0f, recipeLayout.trackH - recipeLayout.thumbH);
                            const float thumbY = std::clamp(
                                static_cast<float>(my) - recipeScrollGrabOffsetY, recipeLayout.trackY,
                                recipeLayout.trackY + thumbTravel);
                            const float t = (thumbTravel > 0.0f)
                                                ? (thumbY - recipeLayout.trackY) / thumbTravel
                                                : 0.0f;
                            recipeMenuScroll = t * recipeLayout.maxScroll;
                        }
                        recipeMenuScroll =
                            std::clamp(recipeMenuScroll, 0.0f, recipeLayout.maxScroll);
                    } else {

                        if (leftNow && !prevLeft) {
                            const bool onCraftableFilter =
                                mx >= recipeLayout.craftableFilterX &&
                                mx <= (recipeLayout.craftableFilterX + recipeLayout.craftableFilterW) &&
                                my >= recipeLayout.craftableFilterY &&
                                my <= (recipeLayout.craftableFilterY + recipeLayout.craftableFilterH);
                            const bool onIngredientFilterClear =
                                recipeIngredientFilter.has_value() &&
                                mx >= recipeLayout.ingredientTagCloseX &&
                                mx <= (recipeLayout.ingredientTagCloseX + recipeLayout.ingredientTagCloseS) &&
                                my >= recipeLayout.ingredientTagCloseY &&
                                my <= (recipeLayout.ingredientTagCloseY + recipeLayout.ingredientTagCloseS);
                            if (onIngredientFilterClear) {
                                recipeIngredientFilter.reset();
                                recipeSearchFocused = false;
                                gRecipeSearchText = nullptr;
                                glfwSetCharCallback(window, nullptr);
                            } else if (onCraftableFilter) {
                                recipeCraftableOnly = !recipeCraftableOnly;
                                recipeSearchFocused = false;
                                gRecipeSearchText = nullptr;
                                glfwSetCharCallback(window, nullptr);
                            } else {
                                recipeSearchFocused = mx >= recipeLayout.searchX &&
                                                      mx <= (recipeLayout.searchX + recipeLayout.searchW) &&
                                                      my >= recipeLayout.searchY &&
                                                      my <= (recipeLayout.searchY + recipeLayout.searchH);
                            }
                            if (recipeSearchFocused) {
                                gRecipeSearchText = &recipeSearchText;
                                glfwSetCharCallback(window, onRecipeSearchCharInput);
                            }
                        }

                        if (leftNow && !prevLeft) {
                            const bool onTrack = mx >= (recipeLayout.trackX - 6.0f) &&
                                                 mx <= (recipeLayout.trackX + recipeLayout.trackW + 6.0f) &&
                                                 my >= recipeLayout.trackY &&
                                                 my <= (recipeLayout.trackY + recipeLayout.trackH);
                            if (onTrack) {
                                const float thumbTravel =
                                    std::max(0.0f, recipeLayout.trackH - recipeLayout.thumbH);
                                const float thumbY =
                                    recipeLayout.trackY +
                                    ((recipeLayout.maxScroll > 0.0f)
                                         ? (recipeMenuScroll / recipeLayout.maxScroll) * thumbTravel
                                         : 0.0f);
                                const bool onThumb =
                                    my >= thumbY && my <= (thumbY + recipeLayout.thumbH);
                                if (onThumb) {
                                    recipeScrollDragging = true;
                                    recipeScrollGrabOffsetY = static_cast<float>(my) - thumbY;
                                } else if (recipeLayout.maxScroll > 0.0f) {
                                    const float t = std::clamp(
                                        static_cast<float>((my - recipeLayout.trackY -
                                                            recipeLayout.thumbH * 0.5f) /
                                                           std::max(1.0f, thumbTravel)),
                                        0.0f, 1.0f);
                                    recipeMenuScroll = t * recipeLayout.maxScroll;
                                }
                            }
                        }
                        if (!leftNow) {
                            recipeScrollDragging = false;
                        }
                        creativeSearchFocused = false;
                        gRecipeSearchText = recipeSearchFocused ? &recipeSearchText : nullptr;
                        glfwSetCharCallback(window,
                                            recipeSearchFocused ? onRecipeSearchCharInput : nullptr);
                        if (recipeScrollDragging && recipeLayout.maxScroll > 0.0f) {
                            const float thumbTravel =
                                std::max(0.0f, recipeLayout.trackH - recipeLayout.thumbH);
                            const float thumbY = std::clamp(
                                static_cast<float>(my) - recipeScrollGrabOffsetY, recipeLayout.trackY,
                                recipeLayout.trackY + thumbTravel);
                            const float t = (thumbTravel > 0.0f)
                                                ? (thumbY - recipeLayout.trackY) / thumbTravel
                                                : 0.0f;
                            recipeMenuScroll = t * recipeLayout.maxScroll;
                        }
                        recipeMenuScroll =
                            std::clamp(recipeMenuScroll, 0.0f, recipeLayout.maxScroll);
                    }
                } else if (creativeMenuVisible && inventoryVisible && !menuOpen && !pauseMenuOpen) {
                    int winW = 1;
                    int winH = 1;
                    glfwGetWindowSize(window, &winW, &winH);
                    double mx = 0.0;
                    double my = 0.0;
                    glfwGetCursorPos(window, &mx, &my);
                    const bool leftNow =
                        glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_LEFT) == GLFW_PRESS;
                    creativeMenuScroll -= gRecipeMenuScrollDelta * (34.0f * debugCfg.hudScale);
                    const CreativeMenuLayout creativeLayout =
                        computeCreativeMenuLayout(winW, winH, debugCfg.hudScale, craftingGridSize,
                                                  usingFurnace, filteredCreativeItems.size());

                    if (leftNow && !prevLeft) {
                        creativeSearchFocused = mx >= creativeLayout.searchX &&
                                                mx <= (creativeLayout.searchX + creativeLayout.searchW) &&
                                                my >= creativeLayout.searchY &&
                                                my <= (creativeLayout.searchY + creativeLayout.searchH);
                    }
                    if (leftNow && !prevLeft) {
                        const bool onTrack = mx >= (creativeLayout.trackX - 6.0f) &&
                                             mx <= (creativeLayout.trackX + creativeLayout.trackW + 6.0f) &&
                                             my >= creativeLayout.trackY &&
                                             my <= (creativeLayout.trackY + creativeLayout.trackH);
                        if (onTrack) {
                            const float thumbTravel =
                                std::max(0.0f, creativeLayout.trackH - creativeLayout.thumbH);
                            const float thumbY =
                                creativeLayout.trackY +
                                ((creativeLayout.maxScroll > 0.0f)
                                     ? (creativeMenuScroll / creativeLayout.maxScroll) * thumbTravel
                                     : 0.0f);
                            const bool onThumb = my >= thumbY && my <= (thumbY + creativeLayout.thumbH);
                            if (onThumb) {
                                creativeScrollDragging = true;
                                creativeScrollGrabOffsetY = static_cast<float>(my) - thumbY;
                            } else if (creativeLayout.maxScroll > 0.0f) {
                                const float t = std::clamp(
                                    static_cast<float>((my - creativeLayout.trackY -
                                                        creativeLayout.thumbH * 0.5f) /
                                                       std::max(1.0f, thumbTravel)),
                                    0.0f, 1.0f);
                                creativeMenuScroll = t * creativeLayout.maxScroll;
                            }
                        }
                    }
                    if (!leftNow) {
                        creativeScrollDragging = false;
                    }
                    recipeSearchFocused = false;
                    gRecipeSearchText = creativeSearchFocused ? &creativeSearchText : nullptr;
                    glfwSetCharCallback(window,
                                        creativeSearchFocused ? onRecipeSearchCharInput : nullptr);
                    if (creativeScrollDragging && creativeLayout.maxScroll > 0.0f) {
                        const float thumbTravel =
                            std::max(0.0f, creativeLayout.trackH - creativeLayout.thumbH);
                        const float thumbY = std::clamp(
                            static_cast<float>(my) - creativeScrollGrabOffsetY, creativeLayout.trackY,
                            creativeLayout.trackY + thumbTravel);
                        const float t =
                            (thumbTravel > 0.0f) ? (thumbY - creativeLayout.trackY) / thumbTravel
                                                 : 0.0f;
                        creativeMenuScroll = t * creativeLayout.maxScroll;
                    }
                    creativeMenuScroll =
                        std::clamp(creativeMenuScroll, 0.0f, creativeLayout.maxScroll);
                } else {
                    recipeScrollDragging = false;
                    creativeScrollDragging = false;
                    recipeSearchFocused = false;
                    creativeSearchFocused = false;
                    gRecipeSearchText = nullptr;
                    glfwSetCharCallback(window, nullptr);
                }
                gRecipeMenuScrollDelta = 0.0f;

                int hoveredInventorySlot = -1;
                if (inventoryVisible && !menuOpen && !pauseMenuOpen) {
                    int winW = 1;
                    int winH = 1;
                    glfwGetWindowSize(window, &winW, &winH);
                    double mx = 0.0;
                    double my = 0.0;
                    glfwGetCursorPos(window, &mx, &my);
                    hoveredInventorySlot =
                        inventorySlotAtCursor(mx, my, winW, winH, true, debugCfg.hudScale,
                                              craftingGridSize, usingFurnace);
                }

                const bool recipeIngredientKeyDown = glfwGetKey(window, GLFW_KEY_U) == GLFW_PRESS;
                if (recipeIngredientKeyDown && !prevRecipeIngredientFilterKey && inventoryVisible &&
                    !menuOpen && !pauseMenuOpen) {
                    voxel::BlockId hoveredId = voxel::AIR;
                    if (hoveredInventorySlot >= 0 && hoveredInventorySlot < game::Inventory::kSlotCount) {
                        const auto &s = inventory.slot(hoveredInventorySlot);
                        if (s.count > 0 && s.id != voxel::AIR) {
                            hoveredId = s.id;
                        }
                    } else if (hoveredInventorySlot >= game::Inventory::kSlotCount &&
                               hoveredInventorySlot < game::Inventory::kSlotCount + kCraftInputCount) {
                        const auto &s = usingFurnace
                                            ? ((hoveredInventorySlot ==
                                                game::Inventory::kSlotCount + 1)
                                                   ? smelting.fuel
                                                   : smelting.input)
                                            : crafting.input[hoveredInventorySlot -
                                                             game::Inventory::kSlotCount];
                        if (s.count > 0 && s.id != voxel::AIR) {
                            hoveredId = s.id;
                        }
                    }

                    if (hoveredId != voxel::AIR) {
                        recipeMenuVisible = true;
                        creativeMenuVisible = false;
                        creativeScrollDragging = false;
                        creativeSearchFocused = false;
                        creativeSearchText.clear();
                        recipeMenuScroll = 0.0f;
                        recipeSearchFocused = false;
                        recipeIngredientFilter = hoveredId;
                        recipeSearchText.clear();
                        gRecipeSearchText = nullptr;
                        glfwSetCharCallback(window, nullptr);
                    } else {
                        recipeIngredientFilter.reset();
                    }
                }
                prevRecipeIngredientFilterKey = recipeIngredientKeyDown;

                // Placement block selection (keys 1..9).
                if (!pauseMenuOpen) {
                    for (int i = 0; i < game::Inventory::kHotbarSize; ++i) {
                        const int key = GLFW_KEY_1 + i;
                        const bool down = glfwGetKey(window, key) == GLFW_PRESS;
                        if (down && !prevBlockKeys[i]) {
                            if (inventoryVisible && !menuOpen && hoveredInventorySlot >= 0 &&
                                hoveredInventorySlot < game::Inventory::kSlotCount) {
                                std::swap(inventory.slot(i), inventory.slot(hoveredInventorySlot));
                            } else if (!inventoryVisible) {
                                selectedBlockIndex = i;
                                const voxel::BlockId selected =
                                    inventory.hotbarSlot(selectedBlockIndex).id;
                                core::Logger::instance().info(std::string("Selected slot block: ") +
                                                              game::blockName(selected));
                            }
                        }
                        prevBlockKeys[i] = down;
                    }
                } else {
                    prevBlockKeys.fill(false);
                }
                const bool dropDown = glfwGetKey(window, GLFW_KEY_Q) == GLFW_PRESS;
                if (dropDown && !prevDropKey && !menuOpen && !inventoryVisible && !pauseMenuOpen &&
                    !mapOpen) {
                    auto &handSlot = inventory.slot(selectedBlockIndex);
                    if (handSlot.id != voxel::AIR && handSlot.count > 0) {
                        glm::vec3 dropPos = camera.position() + camera.forward() * 2.10f +
                                            glm::vec3(0.0f, -0.30f, 0.0f);
                        itemDrops.spawn(handSlot.id, dropPos, 1);
                        handSlot.count -= 1;
                        if (handSlot.count <= 0) {
                            handSlot = {};
                        }
                    }
                }
                prevDropKey = dropDown;

                std::optional<voxel::RayHit> currentHit;
                if (!blockInput) {
                    currentHit = voxel::Raycaster::cast(world, camera.position(), camera.forward(),
                                                        debugCfg.raycastDistance);
                }

                const bool left = glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_LEFT) == GLFW_PRESS;
                const bool right =
                    glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_RIGHT) == GLFW_PRESS;

                if (mapOpen) {
                    int winW = 1;
                    int winH = 1;
                    glfwGetWindowSize(window, &winW, &winH);
                    double mx = 0.0;
                    double my = 0.0;
                    glfwGetCursorPos(window, &mx, &my);
                    auto closeWaypointEditor = [&]() {
                        waypointEditorOpen = false;
                        waypointNameFocused = false;
                        prevWaypointEnter = false;
                        prevWaypointEscape = false;
                        gRecipeSearchText = nullptr;
                        glfwSetCharCallback(window, nullptr);
                    };
                    auto cancelWaypointEdit = [&]() {
                        if (!waypointEditorOpen) {
                            return;
                        }
                        if (selectedWaypointIndex >= 0 &&
                            selectedWaypointIndex < static_cast<int>(mapSystem.waypoints().size())) {
                            if (waypointEditWasNew) {
                                auto &wps = mapSystem.waypoints();
                                wps.erase(wps.begin() + selectedWaypointIndex);
                                selectedWaypointIndex = -1;
                            } else if (waypointEditHasBackup &&
                                       waypointEditBackupIndex == selectedWaypointIndex) {
                                mapSystem.waypoints()[selectedWaypointIndex] = waypointEditBackup;
                            }
                        }
                        waypointEditHasBackup = false;
                        waypointEditWasNew = false;
                        waypointEditBackupIndex = -1;
                        closeWaypointEditor();
                    };
                    auto commitWaypointEdit = [&]() {
                        if (selectedWaypointIndex >= 0 &&
                            selectedWaypointIndex < static_cast<int>(mapSystem.waypoints().size())) {
                            auto &wp = mapSystem.waypoints()[selectedWaypointIndex];
                            if (wp.name.empty()) {
                                wp.name = "Waypoint";
                            }
                            wp.icon = static_cast<std::uint8_t>(wp.icon % 5u);
                        }
                        waypointEditHasBackup = false;
                        waypointEditWasNew = false;
                        waypointEditBackupIndex = -1;
                        closeWaypointEditor();
                    };
                    const MapOverlayLayout mapLayout = computeMapOverlayLayout(winW, winH, mapZoom);
                    const WaypointEditorLayout wpLayout = computeWaypointEditorLayout(mapLayout);
                    const bool inMapGrid = mx >= mapLayout.gridX &&
                                           mx <= (mapLayout.gridX + mapLayout.gridW) &&
                                           my >= mapLayout.gridY &&
                                           my <= (mapLayout.gridY + mapLayout.gridH);
                    int hoveredWaypointIndex = -1;
                    float hoveredWaypointDist = 9999.0f;
                    const int drawCols =
                        std::max(1, static_cast<int>(std::ceil(mapLayout.gridW / mapLayout.cell)));
                    const int drawRows =
                        std::max(1, static_cast<int>(std::ceil(mapLayout.gridH / mapLayout.cell)));
                    const float centerIx = (static_cast<float>(drawCols) - 1.0f) * 0.5f;
                    const float centerIz = (static_cast<float>(drawRows) - 1.0f) * 0.5f;
                    for (int i = 0; i < static_cast<int>(mapSystem.waypoints().size()); ++i) {
                        const auto &wp = mapSystem.waypoints()[i];
                        const float px = mapLayout.gridX +
                                         (static_cast<float>(wp.x) - mapCenterWX + centerIx) *
                                             mapLayout.cell;
                        const float py = mapLayout.gridY +
                                         (static_cast<float>(wp.z) - mapCenterWZ + centerIz) *
                                             mapLayout.cell;
                        const float dx = static_cast<float>(mx) - (px + 0.5f);
                        const float dy = static_cast<float>(my) - (py + 0.5f);
                        const float d = std::sqrt(dx * dx + dy * dy);
                        if (d < 13.0f && d < hoveredWaypointDist) {
                            hoveredWaypointDist = d;
                            hoveredWaypointIndex = i;
                        }
                    }
                    const bool inWaypointEditor =
                        waypointEditorOpen && mx >= wpLayout.panelX &&
                        mx <= (wpLayout.panelX + wpLayout.panelW) && my >= wpLayout.panelY &&
                        my <= (wpLayout.panelY + wpLayout.panelH);
                    if (right && !prevRight && inMapGrid) {
                        const int gx = static_cast<int>(std::floor((mx - mapLayout.gridX) / mapLayout.cell));
                        const int gz = static_cast<int>(std::floor((my - mapLayout.gridY) / mapLayout.cell));
                        const int mapCenterWXInt = static_cast<int>(std::round(mapCenterWX));
                        const int mapCenterWZInt = static_cast<int>(std::round(mapCenterWZ));
                        const int wx =
                            mapCenterWXInt + static_cast<int>(std::floor(static_cast<float>(gx) - centerIx));
                        const int wz =
                            mapCenterWZInt + static_cast<int>(std::floor(static_cast<float>(gz) - centerIz));

                        selectedWaypointIndex = -1;
                        int bestDist2 = 999999;
                        auto &wps = mapSystem.waypoints();
                        for (int i = 0; i < static_cast<int>(wps.size()); ++i) {
                            const int dx = wps[i].x - wx;
                            const int dz = wps[i].z - wz;
                            const int d2 = dx * dx + dz * dz;
                            if (d2 <= 4 && d2 < bestDist2) {
                                bestDist2 = d2;
                                selectedWaypointIndex = i;
                            }
                        }
                        if (selectedWaypointIndex < 0) {
                            game::MapSystem::Waypoint wp{};
                            wp.x = wx;
                            wp.z = wz;
                            wp.name = "Waypoint";
                            wp.r = 255;
                            wp.g = 96;
                            wp.b = 96;
                            wp.icon = 0;
                            mapSystem.waypoints().push_back(wp);
                            selectedWaypointIndex = static_cast<int>(mapSystem.waypoints().size()) - 1;
                            waypointEditWasNew = true;
                        } else {
                            waypointEditWasNew = false;
                        }
                        if (selectedWaypointIndex >= 0 &&
                            selectedWaypointIndex < static_cast<int>(mapSystem.waypoints().size())) {
                            waypointEditHasBackup = true;
                            waypointEditBackupIndex = selectedWaypointIndex;
                            waypointEditBackup = mapSystem.waypoints()[selectedWaypointIndex];
                        }
                        waypointEditorOpen = true;
                        waypointNameFocused = true;
                        if (selectedWaypointIndex >= 0) {
                            gRecipeSearchText = &mapSystem.waypoints()[selectedWaypointIndex].name;
                            glfwSetCharCallback(window, onRecipeSearchCharInput);
                        }
                    }
                    if (left && !prevLeft) {
                        if (waypointEditorOpen && inWaypointEditor &&
                            selectedWaypointIndex >= 0 &&
                            selectedWaypointIndex < static_cast<int>(mapSystem.waypoints().size())) {
                            auto &wp = mapSystem.waypoints()[selectedWaypointIndex];
                            if (mx >= wpLayout.closeX &&
                                mx <= (wpLayout.closeX + wpLayout.closeS) &&
                                my >= wpLayout.closeY &&
                                my <= (wpLayout.closeY + wpLayout.closeS)) {
                                cancelWaypointEdit();
                            } else if (mx >= wpLayout.delX &&
                                       mx <= (wpLayout.delX + wpLayout.delW) &&
                                       my >= wpLayout.delY &&
                                       my <= (wpLayout.delY + wpLayout.delH)) {
                                auto &wps = mapSystem.waypoints();
                                wps.erase(wps.begin() + selectedWaypointIndex);
                                selectedWaypointIndex = -1;
                                waypointEditHasBackup = false;
                                waypointEditWasNew = false;
                                waypointEditBackupIndex = -1;
                                closeWaypointEditor();
                            } else if (mx >= wpLayout.nameX &&
                                       mx <= (wpLayout.nameX + wpLayout.nameW) &&
                                       my >= wpLayout.nameY &&
                                       my <= (wpLayout.nameY + wpLayout.nameH)) {
                                waypointNameFocused = true;
                                gRecipeSearchText = &wp.name;
                                glfwSetCharCallback(window, onRecipeSearchCharInput);
                            } else {
                                waypointNameFocused = false;
                                gRecipeSearchText = nullptr;
                                glfwSetCharCallback(window, nullptr);
                            }
                            const glm::vec3 palette[5] = {
                                {1.00f, 0.38f, 0.38f}, {0.36f, 0.82f, 1.00f}, {0.40f, 0.92f, 0.42f},
                                {0.96f, 0.90f, 0.34f}, {0.92f, 0.52f, 0.96f},
                            };
                            for (int i = 0; i < 5; ++i) {
                                const float sx = wpLayout.colorX +
                                                 static_cast<float>(i) *
                                                     (wpLayout.colorS + wpLayout.colorGap);
                                if (mx >= sx && mx <= (sx + wpLayout.colorS) &&
                                    my >= wpLayout.colorY &&
                                    my <= (wpLayout.colorY + wpLayout.colorS)) {
                                    wp.r = static_cast<std::uint8_t>(std::round(palette[i].r * 255.0f));
                                    wp.g = static_cast<std::uint8_t>(std::round(palette[i].g * 255.0f));
                                    wp.b = static_cast<std::uint8_t>(std::round(palette[i].b * 255.0f));
                                }
                            }
                            for (int i = 0; i < 5; ++i) {
                                const float sx = wpLayout.iconX +
                                                 static_cast<float>(i) * (wpLayout.iconS + wpLayout.iconGap);
                                if (mx >= sx && mx <= (sx + wpLayout.iconS) &&
                                    my >= wpLayout.iconY && my <= (wpLayout.iconY + wpLayout.iconS)) {
                                    wp.icon = static_cast<std::uint8_t>(i);
                                }
                            }
                            if (mx >= wpLayout.visibilityX &&
                                mx <= (wpLayout.visibilityX + wpLayout.visibilityW) &&
                                my >= wpLayout.visibilityY &&
                                my <= (wpLayout.visibilityY + wpLayout.visibilityH)) {
                                wp.visible = !wp.visible;
                            }
                            mapDragActive = false;
                        } else if (inMapGrid) {
                            waypointNameFocused = false;
                            gRecipeSearchText = nullptr;
                            glfwSetCharCallback(window, nullptr);
                            if (hoveredWaypointIndex >= 0) {
                                if (selectedWaypointIndex == hoveredWaypointIndex) {
                                    selectedWaypointIndex = -1;
                                } else {
                                    selectedWaypointIndex = hoveredWaypointIndex;
                                }
                                mapDragActive = false;
                            } else {
                                mapDragActive = true;
                                mapDragLastMouseX = static_cast<float>(mx);
                                mapDragLastMouseY = static_cast<float>(my);
                            }
                        } else {
                            mapDragActive = false;
                        }
                    }
                    if (!left) {
                        mapDragActive = false;
                    }
                    if (mapDragActive) {
                        const float dx = static_cast<float>(mx) - mapDragLastMouseX;
                        const float dy = static_cast<float>(my) - mapDragLastMouseY;
                        mapCenterWX -= dx / std::max(1.0f, mapLayout.cell);
                        mapCenterWZ -= dy / std::max(1.0f, mapLayout.cell);
                        mapDragLastMouseX = static_cast<float>(mx);
                        mapDragLastMouseY = static_cast<float>(my);
                    }
                    if (waypointNameFocused && selectedWaypointIndex >= 0 &&
                        selectedWaypointIndex < static_cast<int>(mapSystem.waypoints().size())) {
                        auto &name = mapSystem.waypoints()[selectedWaypointIndex].name;
                        if (name.size() > 32) {
                            name.resize(32);
                        }
                        gRecipeSearchText = &name;
                        glfwSetCharCallback(window, onRecipeSearchCharInput);
                        const bool backspaceDown = glfwGetKey(window, GLFW_KEY_BACKSPACE) == GLFW_PRESS;
                        if (backspaceDown && !prevWaypointBackspace && !name.empty()) {
                            name.pop_back();
                        }
                        prevWaypointBackspace = backspaceDown;
                    } else {
                        prevWaypointBackspace = false;
                    }
                    const bool enterDown = (glfwGetKey(window, GLFW_KEY_ENTER) == GLFW_PRESS) ||
                                           (glfwGetKey(window, GLFW_KEY_KP_ENTER) == GLFW_PRESS);
                    if (enterDown && !prevWaypointEnter && waypointEditorOpen) {
                        commitWaypointEdit();
                    }
                    prevWaypointEnter = enterDown;
                    const bool escDown = glfwGetKey(window, GLFW_KEY_ESCAPE) == GLFW_PRESS;
                    if (escDown && !prevWaypointEscape && waypointEditorOpen) {
                        cancelWaypointEdit();
                    }
                    prevWaypointEscape = escDown;
                    const bool delDown = (glfwGetKey(window, GLFW_KEY_DELETE) == GLFW_PRESS) ||
                                         (!waypointNameFocused && !waypointEditorOpen &&
                                          glfwGetKey(window, GLFW_KEY_BACKSPACE) == GLFW_PRESS);
                    int deleteIndex = -1;
                    if (selectedWaypointIndex >= 0 &&
                        selectedWaypointIndex < static_cast<int>(mapSystem.waypoints().size())) {
                        deleteIndex = selectedWaypointIndex;
                    } else if (hoveredWaypointIndex >= 0) {
                        deleteIndex = hoveredWaypointIndex;
                    }
                    if (delDown && !prevWaypointDelete && deleteIndex >= 0) {
                        auto &wps = mapSystem.waypoints();
                        wps.erase(wps.begin() + deleteIndex);
                        selectedWaypointIndex = -1;
                        waypointEditHasBackup = false;
                        waypointEditWasNew = false;
                        waypointEditBackupIndex = -1;
                        closeWaypointEditor();
                    }
                    prevWaypointDelete = delDown;
                } else {
                    mapDragActive = false;
                    waypointEditorOpen = false;
                    waypointNameFocused = false;
                    prevWaypointBackspace = false;
                    prevWaypointEnter = false;
                    prevWaypointEscape = false;
                    prevWaypointDelete = false;
                    waypointEditHasBackup = false;
                    waypointEditWasNew = false;
                    waypointEditBackupIndex = -1;
                    if (hudVisible && currentCursorMode == GLFW_CURSOR_NORMAL && left && !prevLeft) {
                        int winW = 1;
                        int winH = 1;
                        glfwGetWindowSize(window, &winW, &winH);
                        double mx = 0.0;
                        double my = 0.0;
                        glfwGetCursorPos(window, &mx, &my);
                        const MiniMapLayout miniMapLayout = computeMiniMapLayout(winW);
                        if (mx >= miniMapLayout.compassX &&
                            mx <= (miniMapLayout.compassX + miniMapLayout.compassW) &&
                            my >= miniMapLayout.compassY &&
                            my <= (miniMapLayout.compassY + miniMapLayout.compassH)) {
                            miniMapShowCompass = !miniMapShowCompass;
                        } else if (mx >= miniMapLayout.followX &&
                                   mx <= (miniMapLayout.followX + miniMapLayout.followW) &&
                                   my >= miniMapLayout.followY &&
                                   my <= (miniMapLayout.followY + miniMapLayout.followH)) {
                            miniMapNorthLocked = !miniMapNorthLocked;
                        } else if (mx >= miniMapLayout.waypointX &&
                                   mx <= (miniMapLayout.waypointX + miniMapLayout.waypointW) &&
                                   my >= miniMapLayout.waypointY &&
                                   my <= (miniMapLayout.waypointY + miniMapLayout.waypointH)) {
                            miniMapShowWaypoints = !miniMapShowWaypoints;
                        } else if (mx >= miniMapLayout.minusX &&
                                   mx <= (miniMapLayout.minusX + miniMapLayout.minusW) &&
                                   my >= miniMapLayout.minusY &&
                                   my <= (miniMapLayout.minusY + miniMapLayout.minusH)) {
                            miniMapZoom = std::clamp(miniMapZoom / 1.15f, 0.6f, 2.4f);
                        } else if (mx >= miniMapLayout.plusX &&
                                   mx <= (miniMapLayout.plusX + miniMapLayout.plusW) &&
                                   my >= miniMapLayout.plusY &&
                                   my <= (miniMapLayout.plusY + miniMapLayout.plusH)) {
                            miniMapZoom = std::clamp(miniMapZoom * 1.15f, 0.6f, 2.4f);
                        }
                    }
                }

                if (pauseMenuOpen) {
                    int winW = 1;
                    int winH = 1;
                    glfwGetWindowSize(window, &winW, &winH);
                    double mx = 0.0;
                    double my = 0.0;
                    glfwGetCursorPos(window, &mx, &my);
                    if (left && !prevLeft) {
                        const int button = pauseMenuButtonAtCursor(mx, my, winW, winH);
                        if (button == 1) {
                            pauseMenuOpen = false;
                        } else if (button == 2) {
                            saveCurrentPlayer();
                        } else if (button == 3) {
                            saveCurrentPlayer();
                            returnToTitle = true;
                            break;
                        } else if (button == 4) {
                            saveCurrentPlayer();
                            glfwSetWindowShouldClose(window, GLFW_TRUE);
                        }
                    }
                }

                if (inventoryVisible && !menuOpen) {
                    int winW = 1;
                    int winH = 1;
                    glfwGetWindowSize(window, &winW, &winH);
                    double mx = 0.0;
                    double my = 0.0;
                    glfwGetCursorPos(window, &mx, &my);
                    const int slotIndex =
                        inventorySlotAtCursor(mx, my, winW, winH, true, debugCfg.hudScale,
                                              craftingGridSize, usingFurnace);
                    const bool shiftDown =
                        (glfwGetKey(window, GLFW_KEY_LEFT_SHIFT) == GLFW_PRESS) ||
                        (glfwGetKey(window, GLFW_KEY_RIGHT_SHIFT) == GLFW_PRESS);
                    auto clearIfEmpty = [](game::Inventory::Slot &s) {
                        if (s.count <= 0) {
                            s.count = 0;
                            s.id = voxel::AIR;
                        }
                    };
                    auto moveToRange = [&](int fromIndex, int start, int endExclusive) {
                        auto &from = inventory.slot(fromIndex);
                        if (from.count <= 0 || from.id == voxel::AIR) {
                            return;
                        }
                        for (int i = start; i < endExclusive && from.count > 0; ++i) {
                            auto &dst = inventory.slot(i);
                            if (dst.id == from.id && dst.count > 0 &&
                                dst.count < game::Inventory::kMaxStack) {
                                const int can = game::Inventory::kMaxStack - dst.count;
                                const int move = (from.count < can) ? from.count : can;
                                dst.count += move;
                                from.count -= move;
                            }
                        }
                        for (int i = start; i < endExclusive && from.count > 0; ++i) {
                            auto &dst = inventory.slot(i);
                            if (dst.count == 0 || dst.id == voxel::AIR) {
                                const int move = (from.count < game::Inventory::kMaxStack)
                                                     ? from.count
                                                     : game::Inventory::kMaxStack;
                                dst.id = from.id;
                                dst.count = move;
                                from.count -= move;
                            }
                        }
                        clearIfEmpty(from);
                    };
                    auto moveStackIntoInventory = [&](voxel::BlockId id, int &count) {
                        if (id == voxel::AIR || count <= 0) {
                            return;
                        }
                        for (int i = 0; i < game::Inventory::kSlotCount && count > 0; ++i) {
                            auto &dst = inventory.slot(i);
                            if (dst.id != id || dst.count <= 0 ||
                                dst.count >= game::Inventory::kMaxStack) {
                                continue;
                            }
                            const int can = game::Inventory::kMaxStack - dst.count;
                            const int move = (count < can) ? count : can;
                            dst.count += move;
                            count -= move;
                        }
                        for (int i = 0; i < game::Inventory::kSlotCount && count > 0; ++i) {
                            auto &dst = inventory.slot(i);
                            if (dst.count != 0 && dst.id != voxel::AIR) {
                                continue;
                            }
                            const int move =
                                (count < game::Inventory::kMaxStack) ? count : game::Inventory::kMaxStack;
                            dst.id = id;
                            dst.count = move;
                            count -= move;
                        }
                    };
                    auto inventoryCapacityFor = [&](voxel::BlockId id) {
                        int cap = 0;
                        for (int i = 0; i < game::Inventory::kSlotCount; ++i) {
                            const auto &dst = inventory.slot(i);
                            if (dst.count == 0 || dst.id == voxel::AIR) {
                                cap += game::Inventory::kMaxStack;
                            } else if (dst.id == id && dst.count < game::Inventory::kMaxStack) {
                                cap += (game::Inventory::kMaxStack - dst.count);
                            }
                        }
                        return cap;
                    };

                    auto slotFromUiIndex = [&](int idx) -> game::Inventory::Slot * {
                        if (idx >= 0 && idx < game::Inventory::kSlotCount) {
                            return &inventory.slot(idx);
                        }
                        if (usingFurnace) {
                            if (idx == game::Inventory::kSlotCount) {
                                return &smelting.input;
                            }
                            if (idx == game::Inventory::kSlotCount + 1) {
                                return &smelting.fuel;
                            }
                            return nullptr;
                        }
                        if (idx >= game::Inventory::kSlotCount &&
                            idx < game::Inventory::kSlotCount + kCraftInputCount) {
                            return &crafting.input[idx - game::Inventory::kSlotCount];
                        }
                        return nullptr;
                    };
                    const int furnaceInputSlot = game::Inventory::kSlotCount;
                    const int furnaceFuelSlot = game::Inventory::kSlotCount + 1;
                    auto furnaceSlotAccepts = [&](int idx, voxel::BlockId id) {
                        if (!usingFurnace || id == voxel::AIR) {
                            return true;
                        }
                        if (idx == furnaceInputSlot) {
                            return smeltingSystem.canSmelt(id);
                        }
                        if (idx == furnaceFuelSlot) {
                            return smeltingSystem.isFuel(id);
                        }
                        if (idx == kCraftOutputSlot) {
                            return false;
                        }
                        return true;
                    };
                    auto canAcceptIntoSlotForId = [&](int idx, voxel::BlockId id) {
                        if (idx == kCraftOutputSlot || idx == kTrashSlot) {
                            return false;
                        }
                        if (id == voxel::AIR) {
                            return false;
                        }
                        auto *slot = slotFromUiIndex(idx);
                        if (slot == nullptr) {
                            return false;
                        }
                        if (!furnaceSlotAccepts(idx, id)) {
                            return false;
                        }
                        if (slot->count <= 0 || slot->id == voxel::AIR) {
                            return true;
                        }
                        return slot->id == id && slot->count < game::Inventory::kMaxStack;
                    };
                    auto tryPlaceOneFromCarried = [&](int idx) {
                        if (idx == kCraftOutputSlot || idx == kTrashSlot) {
                            return false;
                        }
                        if (carriedSlot.id == voxel::AIR || carriedSlot.count <= 0) {
                            return false;
                        }
                        auto *dst = slotFromUiIndex(idx);
                        if (dst == nullptr || !furnaceSlotAccepts(idx, carriedSlot.id)) {
                            return false;
                        }
                        if (dst->count <= 0 || dst->id == voxel::AIR) {
                            dst->id = carriedSlot.id;
                            dst->count = 1;
                            carriedSlot.count -= 1;
                            clearIfEmpty(carriedSlot);
                            return true;
                        }
                        if (dst->id == carriedSlot.id && dst->count < game::Inventory::kMaxStack) {
                            dst->count += 1;
                            carriedSlot.count -= 1;
                            clearIfEmpty(carriedSlot);
                            return true;
                        }
                        return false;
                    };
                    auto recomputeSpreadPreview = [&]() {
                        carriedSlot = inventorySpreadStartCarried;
                        for (const auto &entry : inventorySpreadOriginalSlots) {
                            if (auto *slot = slotFromUiIndex(entry.first); slot != nullptr) {
                                *slot = entry.second;
                            }
                        }
                        bool progress = true;
                        while (carriedSlot.id != voxel::AIR && carriedSlot.count > 0 && progress) {
                            progress = false;
                            for (const int idx : inventorySpreadSlots) {
                                if (carriedSlot.id == voxel::AIR || carriedSlot.count <= 0) {
                                    break;
                                }
                                auto *dst = slotFromUiIndex(idx);
                                if (dst == nullptr || !furnaceSlotAccepts(idx, carriedSlot.id)) {
                                    continue;
                                }
                                if (dst->count > 0 && dst->id != carriedSlot.id) {
                                    continue;
                                }
                                if (dst->count >= game::Inventory::kMaxStack) {
                                    continue;
                                }
                                if (dst->count <= 0 || dst->id == voxel::AIR) {
                                    dst->id = carriedSlot.id;
                                    dst->count = 0;
                                }
                                dst->count += 1;
                                carriedSlot.count -= 1;
                                clearIfEmpty(carriedSlot);
                                progress = true;
                            }
                        }
                        if (!usingFurnace) {
                            craftingSystem.updateOutput(crafting, craftingGridSize);
                        }
                    };
                    auto recomputeRightSpreadPreview = [&]() {
                        carriedSlot = inventoryRightSpreadStartCarried;
                        for (const auto &entry : inventoryRightSpreadOriginalSlots) {
                            if (auto *slot = slotFromUiIndex(entry.first); slot != nullptr) {
                                *slot = entry.second;
                            }
                        }
                        bool progress = true;
                        while (carriedSlot.id != voxel::AIR && carriedSlot.count > 0 && progress) {
                            progress = false;
                            for (const int idx : inventoryRightSpreadSlots) {
                                if (carriedSlot.id == voxel::AIR || carriedSlot.count <= 0) {
                                    break;
                                }
                                auto *dst = slotFromUiIndex(idx);
                                if (dst == nullptr ||
                                    !furnaceSlotAccepts(idx, inventoryRightSpreadStartCarried.id)) {
                                    continue;
                                }
                                if (dst->count > 0 &&
                                    dst->id != inventoryRightSpreadStartCarried.id) {
                                    continue;
                                }
                                if (dst->count >= game::Inventory::kMaxStack) {
                                    continue;
                                }
                                if (dst->count <= 0 || dst->id == voxel::AIR) {
                                    dst->id = inventoryRightSpreadStartCarried.id;
                                    dst->count = 0;
                                }
                                dst->count += 1;
                                carriedSlot.count -= 1;
                                clearIfEmpty(carriedSlot);
                                progress = true;
                            }
                        }
                        if (!usingFurnace) {
                            craftingSystem.updateOutput(crafting, craftingGridSize);
                        }
                    };
                    auto resetLeftSpread = [&]() {
                        inventorySpreadDragging = false;
                        inventorySpreadSlots.clear();
                        inventorySpreadOriginalSlots.clear();
                        inventorySpreadPickupSourceSlot = -1;
                    };
                    auto startLeftSpread = [&](int idx) {
                        inventorySpreadDragging = true;
                        inventorySpreadStartCarried = carriedSlot;
                        inventorySpreadSlots.clear();
                        inventorySpreadOriginalSlots.clear();
                        inventorySpreadSlots.push_back(idx);
                        if (auto *slot = slotFromUiIndex(idx); slot != nullptr) {
                            inventorySpreadOriginalSlots.emplace_back(idx, *slot);
                        }
                        recomputeSpreadPreview();
                    };
                    auto resetRightSpread = [&]() {
                        inventoryRightSpreadDragging = false;
                        inventoryRightSpreadSlots.clear();
                        inventoryRightSpreadOriginalSlots.clear();
                    };
                    auto startRightSpread = [&](int idx) {
                        inventoryRightSpreadDragging = true;
                        inventoryRightSpreadStartCarried = carriedSlot;
                        inventoryRightSpreadSlots.clear();
                        inventoryRightSpreadOriginalSlots.clear();
                        inventoryRightSpreadSlots.push_back(idx);
                        if (auto *slot = slotFromUiIndex(idx); slot != nullptr) {
                            inventoryRightSpreadOriginalSlots.emplace_back(idx, *slot);
                        }
                    };
                    if (left && !prevInventoryLeft) {
                        resetLeftSpread();
                    }
                    if (!inventorySpreadDragging && left && prevInventoryLeft && !shiftDown &&
                        carriedSlot.id != voxel::AIR && carriedSlot.count > 0 &&
                        slotIndex != inventorySpreadPickupSourceSlot &&
                        canAcceptIntoSlotForId(slotIndex, carriedSlot.id)) {
                        startLeftSpread(slotIndex);
                    }
                    if (inventorySpreadDragging && left && !shiftDown &&
                        canAcceptIntoSlotForId(slotIndex, carriedSlot.id)) {
                        if (std::find(inventorySpreadSlots.begin(), inventorySpreadSlots.end(),
                                      slotIndex) == inventorySpreadSlots.end()) {
                            inventorySpreadSlots.push_back(slotIndex);
                            if (auto *slot = slotFromUiIndex(slotIndex); slot != nullptr) {
                                inventorySpreadOriginalSlots.emplace_back(slotIndex, *slot);
                            }
                            recomputeSpreadPreview();
                        }
                    }
                    if (inventorySpreadDragging && !left && prevInventoryLeft) {
                        resetLeftSpread();
                    }
                    if (!left && !prevInventoryLeft) {
                        resetLeftSpread();
                    }
                    if (right && !prevInventoryRight) {
                        resetRightSpread();
                        if (!shiftDown && carriedSlot.id != voxel::AIR && carriedSlot.count > 0 &&
                            canAcceptIntoSlotForId(slotIndex, carriedSlot.id)) {
                            startRightSpread(slotIndex);
                        }
                    }
                    if (!inventoryRightSpreadDragging && right && prevInventoryRight && !shiftDown &&
                        carriedSlot.id != voxel::AIR && carriedSlot.count > 0 &&
                        canAcceptIntoSlotForId(slotIndex, carriedSlot.id)) {
                        startRightSpread(slotIndex);
                    }
                    if (inventoryRightSpreadDragging && right && prevInventoryRight &&
                        canAcceptIntoSlotForId(slotIndex, inventoryRightSpreadStartCarried.id)) {
                        if (std::find(inventoryRightSpreadSlots.begin(),
                                      inventoryRightSpreadSlots.end(),
                                      slotIndex) == inventoryRightSpreadSlots.end()) {
                            inventoryRightSpreadSlots.push_back(slotIndex);
                            if (auto *slot = slotFromUiIndex(slotIndex); slot != nullptr) {
                                inventoryRightSpreadOriginalSlots.emplace_back(slotIndex, *slot);
                            }
                            if (inventoryRightSpreadSlots.size() > 1) {
                                recomputeRightSpreadPreview();
                            }
                        }
                    }
                    if (!right) {
                        resetRightSpread();
                    }

                    bool handledRecipeClick = false;
                    bool handledCreativeClick = false;
                    bool handledTrashClick = false;
                    if (creativeMenuVisible && (left && !prevInventoryLeft || right && !prevInventoryRight)) {
                        const CreativeMenuLayout creativeLayout =
                            computeCreativeMenuLayout(winW, winH, debugCfg.hudScale, craftingGridSize,
                                                      usingFurnace, filteredCreativeItems.size());
                        const int creativeIndex = creativeItemAtCursor(
                            mx, my, creativeLayout, creativeMenuScroll, filteredCreativeItems.size());
                        if (creativeIndex >= 0 &&
                            creativeIndex < static_cast<int>(filteredCreativeItems.size())) {
                            const voxel::BlockId id = filteredCreativeItems[creativeIndex];
                            const int addCount =
                                shiftDown ? game::Inventory::kMaxStack : 1;
                            inventory.add(id, addCount);
                            handledCreativeClick = true;
                        }
                    }
                    if (recipeMenuVisible && !usingFurnace && left && !prevInventoryLeft) {
                        const RecipeMenuLayout recipeLayout =
                            computeRecipeMenuLayout(winW, winH, debugCfg.hudScale, craftingGridSize,
                                                    usingFurnace, filteredRecipeIndices.size());
                        const int recipeIndex = recipeRowAtCursor(
                            mx, my, recipeLayout, recipeMenuScroll, filteredRecipeIndices.size());
                        if (recipeIndex >= 0 &&
                            recipeIndex < static_cast<int>(filteredRecipeIndices.size())) {
                            const int recipeFillIndex = filteredRecipeIndices[recipeIndex];
                            const auto &recipe = craftingSystem.recipeInfos()[recipeFillIndex];
                            bool ok = (craftingGridSize >= recipe.minGridSize);
                            const int activeInputs =
                                game::CraftingSystem::activeInputCount(craftingGridSize);
                            const bool switchingRecipe = (lastRecipeFillIndex != recipeFillIndex);
                            if (ok && switchingRecipe) {
                                bool hasCraftItems = false;
                                for (int i = 0; i < activeInputs; ++i) {
                                    const auto &slot = crafting.input[i];
                                    if (slot.id != voxel::AIR && slot.count > 0) {
                                        hasCraftItems = true;
                                        break;
                                    }
                                }
                                if (hasCraftItems) {
                                    clearCraftInputs();
                                }
                            }
                            if (ok) {
                                if (shiftDown) {
                                    const int maxSetsByOutput = std::max(
                                        1, game::Inventory::kMaxStack / std::max(1, recipe.outputCount));
                                    int setsAdded = 0;
                                    while (setsAdded < maxSetsByOutput &&
                                           tryAddRecipeSet(recipe, inventory, crafting, activeInputs)) {
                                        ++setsAdded;
                                    }
                                    ok = (setsAdded > 0);
                                } else {
                                    ok = tryAddRecipeSet(recipe, inventory, crafting, activeInputs);
                                }
                            }
                            // Track last clicked recipe so repeated clicks on the same recipe
                            // never trigger a clear/reseed cycle after a failed add.
                            lastRecipeFillIndex = recipeFillIndex;
                            craftingSystem.updateOutput(crafting, craftingGridSize);
                            handledRecipeClick = true;
                        }
                    }
                    if (recipeMenuVisible && usingFurnace && left && !prevInventoryLeft) {
                        const RecipeMenuLayout recipeLayout = computeRecipeMenuLayout(
                            winW, winH, debugCfg.hudScale, craftingGridSize, usingFurnace,
                            filteredSmeltingIndices.size());
                        const int recipeIndex = recipeRowAtCursor(
                            mx, my, recipeLayout, recipeMenuScroll, filteredSmeltingIndices.size());
                        if (recipeIndex >= 0 &&
                            recipeIndex < static_cast<int>(filteredSmeltingIndices.size())) {
                            const auto &smeltRecipes = smeltingSystem.recipes();
                            const auto &recipe = smeltRecipes[filteredSmeltingIndices[recipeIndex]];
                            int fuelCount = 0;
                            int inputCount = 0;
                            for (int i = 0; i < game::Inventory::kSlotCount; ++i) {
                                const auto &slot = inventory.slot(i);
                                if (slot.id != voxel::AIR && slot.count > 0) {
                                    if (smeltingSystem.isFuel(slot.id)) {
                                        fuelCount += slot.count;
                                    }
                                    if (slot.id == recipe.input) {
                                        inputCount += slot.count;
                                    }
                                }
                            }
                            const bool smeltableNow = (inputCount > 0 && fuelCount > 0);
                            if (!smeltableNow) {
                                handledRecipeClick = true;
                            } else {
                                const bool switchingRecipe =
                                    (smelting.input.id != voxel::AIR && smelting.input.count > 0 &&
                                     smelting.input.id != recipe.input);
                                bool abortRecipeClick = false;

                                if (switchingRecipe) {
                                    const auto stackCapacityFor = [&](voxel::BlockId id) {
                                        int cap = 0;
                                        for (int i = 0; i < game::Inventory::kSlotCount; ++i) {
                                            const auto &dst = inventory.slot(i);
                                            if (dst.count == 0 || dst.id == voxel::AIR) {
                                                cap += game::Inventory::kMaxStack;
                                            } else if (dst.id == id &&
                                                       dst.count < game::Inventory::kMaxStack) {
                                                cap += (game::Inventory::kMaxStack - dst.count);
                                            }
                                        }
                                        return cap;
                                    };

                                    int needInput = (smelting.input.id != voxel::AIR &&
                                                     smelting.input.count > 0)
                                                        ? smelting.input.count
                                                        : 0;
                                    int needOutput = (smelting.output.id != voxel::AIR &&
                                                      smelting.output.count > 0)
                                                         ? smelting.output.count
                                                         : 0;
                                    bool canStoreAll = true;
                                    if (needInput > 0 || needOutput > 0) {
                                        if (needInput > 0 && smelting.input.id == smelting.output.id) {
                                            canStoreAll =
                                                stackCapacityFor(smelting.input.id) >=
                                                (needInput + needOutput);
                                        } else {
                                            if (needInput > 0) {
                                                canStoreAll =
                                                    canStoreAll &&
                                                    (stackCapacityFor(smelting.input.id) >= needInput);
                                            }
                                            if (needOutput > 0) {
                                                canStoreAll =
                                                    canStoreAll &&
                                                    (stackCapacityFor(smelting.output.id) >= needOutput);
                                            }
                                        }
                                    }
                                    if (!canStoreAll) {
                                        handledRecipeClick = true;
                                        abortRecipeClick = true;
                                    }

                                    if (!abortRecipeClick) {
                                        if (needInput > 0) {
                                            int remaining = smelting.input.count;
                                            moveStackIntoInventory(smelting.input.id, remaining);
                                            smelting.input.count = remaining;
                                            clearIfEmpty(smelting.input);
                                        }
                                        if (needOutput > 0) {
                                            int remaining = smelting.output.count;
                                            moveStackIntoInventory(smelting.output.id, remaining);
                                            smelting.output.count = remaining;
                                            clearIfEmpty(smelting.output);
                                        }
                                        smelting.progressSeconds = 0.0f;
                                    }
                                }

                                if (!abortRecipeClick) {
                                    auto addOneInput = [&](voxel::BlockId id) {
                                        if (id == voxel::AIR) {
                                            return false;
                                        }
                                        if (smelting.input.id != voxel::AIR && smelting.input.id != id) {
                                            return false;
                                        }
                                        if (smelting.input.id == voxel::AIR ||
                                            smelting.input.count <= 0) {
                                            smelting.input.id = id;
                                            smelting.input.count = 0;
                                        }
                                        if (smelting.input.count >= game::Inventory::kMaxStack) {
                                            return false;
                                        }
                                        for (int i = 0; i < game::Inventory::kSlotCount; ++i) {
                                            auto &slot = inventory.slot(i);
                                            if (slot.id != id || slot.count <= 0) {
                                                continue;
                                            }
                                            slot.count -= 1;
                                            smelting.input.count += 1;
                                            if (slot.count <= 0) {
                                                slot = {};
                                            }
                                            return true;
                                        }
                                        return false;
                                    };
                                    if (shiftDown) {
                                        while (addOneInput(recipe.input)) {
                                        }
                                    } else {
                                        addOneInput(recipe.input);
                                    }
                                }
                                handledRecipeClick = true;
                            }
                        }
                    }

                    if (slotIndex == kTrashSlot &&
                        ((left && !prevInventoryLeft) || (right && !prevInventoryRight))) {
                        if (carriedSlot.id != voxel::AIR && carriedSlot.count > 0) {
                            carriedSlot = {};
                        }
                        handledTrashClick = true;
                    }

                    game::Inventory::Slot *clickedSlot =
                        (handledRecipeClick || handledCreativeClick || handledTrashClick)
                            ? nullptr
                            : slotFromUiIndex(slotIndex);

                    if (!handledRecipeClick && slotIndex == kCraftOutputSlot) {
                        if (usingFurnace) {
                            if (left && !prevInventoryLeft && smelting.output.id != voxel::AIR &&
                                smelting.output.count > 0) {
                                if (shiftDown && carriedSlot.count == 0) {
                                    int remaining = smelting.output.count;
                                    const voxel::BlockId id = smelting.output.id;
                                    moveStackIntoInventory(id, remaining);
                                    smelting.output.count = remaining;
                                    clearIfEmpty(smelting.output);
                                } else {
                                    const bool canTake = (carriedSlot.count == 0 ||
                                                          carriedSlot.id == voxel::AIR ||
                                                          carriedSlot.id == smelting.output.id);
                                    if (canTake) {
                                        if (carriedSlot.count == 0 || carriedSlot.id == voxel::AIR) {
                                            carriedSlot.id = smelting.output.id;
                                        }
                                        const int canStack =
                                            game::Inventory::kMaxStack - carriedSlot.count;
                                        if (canStack > 0) {
                                            const int move = std::min(canStack, smelting.output.count);
                                            carriedSlot.count += move;
                                            smelting.output.count -= move;
                                            clearIfEmpty(smelting.output);
                                        }
                                    }
                                }
                            }
                        } else if (left && !prevInventoryLeft && crafting.output.id != voxel::AIR &&
                                   crafting.output.count > 0) {
                            if (shiftDown && carriedSlot.count == 0) {
                                while (crafting.output.id != voxel::AIR && crafting.output.count > 0) {
                                    const voxel::BlockId craftedId = crafting.output.id;
                                    const int craftedCount = crafting.output.count;
                                    if (inventoryCapacityFor(craftedId) < craftedCount) {
                                        break;
                                    }
                                    if (!craftingSystem.consumeInputs(crafting, craftingGridSize)) {
                                        break;
                                    }
                                    int remaining = craftedCount;
                                    moveStackIntoInventory(craftedId, remaining);
                                }
                            } else {
                                const bool canTake = (carriedSlot.count == 0 ||
                                                      carriedSlot.id == voxel::AIR ||
                                                      carriedSlot.id == crafting.output.id);
                                if (canTake) {
                                    const voxel::BlockId craftedId = crafting.output.id;
                                    const int craftedCount = crafting.output.count;
                                    if (carriedSlot.count == 0 || carriedSlot.id == voxel::AIR) {
                                        carriedSlot.id = craftedId;
                                    }
                                    const int canStack =
                                        game::Inventory::kMaxStack - carriedSlot.count;
                                    if (canStack > 0 &&
                                        craftingSystem.consumeInputs(crafting, craftingGridSize)) {
                                        const int move = std::min(canStack, craftedCount);
                                        carriedSlot.count += move;
                                    }
                                }
                            }
                        }
                    } else if (!handledRecipeClick && !handledCreativeClick && !handledTrashClick &&
                               clickedSlot != nullptr) {
                        auto *dst = clickedSlot;
                        if (slotIndex >= game::Inventory::kSlotCount) {
                            lastRecipeFillIndex = -1;
                        }
                        if (left && !prevInventoryLeft) {
                            voxel::BlockId clickId = voxel::AIR;
                            if (carriedSlot.count > 0 && carriedSlot.id != voxel::AIR) {
                                clickId = carriedSlot.id;
                            } else if (dst->count > 0 && dst->id != voxel::AIR) {
                                clickId = dst->id;
                            }

                            const bool allowDoubleClick = slotIndex >= 0 &&
                                                          slotIndex < game::Inventory::kSlotCount;
                            const bool doubleClickCollect =
                                allowDoubleClick && !shiftDown && clickId != voxel::AIR &&
                                lastInventoryLeftClickId == clickId &&
                                lastInventoryLeftClickTime >= 0.0f &&
                                (now - lastInventoryLeftClickTime) <= 0.30f;

                            if (doubleClickCollect) {
                                if (carriedSlot.count == 0) {
                                    carriedSlot.id = clickId;
                                    carriedSlot.count = 0;
                                }
                                if (carriedSlot.id == clickId &&
                                    carriedSlot.count < game::Inventory::kMaxStack) {
                                    for (int i = 0; i < game::Inventory::kSlotCount; ++i) {
                                        auto &src = inventory.slot(i);
                                        if (src.id != clickId || src.count <= 0) {
                                            continue;
                                        }
                                        const int canTake =
                                            game::Inventory::kMaxStack - carriedSlot.count;
                                        if (canTake <= 0) {
                                            break;
                                        }
                                        const int move =
                                            (src.count < canTake) ? src.count : canTake;
                                        carriedSlot.count += move;
                                        src.count -= move;
                                        clearIfEmpty(src);
                                    }
                                }
                            } else if (shiftDown && carriedSlot.count == 0) {
                                if (slotIndex >= 0 && slotIndex < game::Inventory::kSlotCount) {
                                    if (usingFurnace) {
                                        auto &from = inventory.slot(slotIndex);
                                        bool movedAny = false;
                                        auto moveInto = [&](game::Inventory::Slot &dst) {
                                            if (from.count <= 0 || from.id == voxel::AIR) {
                                                return;
                                            }
                                            if (dst.count > 0 && dst.id != from.id) {
                                                return;
                                            }
                                            const int can = game::Inventory::kMaxStack - dst.count;
                                            if (can <= 0) {
                                                return;
                                            }
                                            const int move = std::min(can, from.count);
                                            if (move <= 0) {
                                                return;
                                            }
                                            if (dst.id == voxel::AIR || dst.count <= 0) {
                                                dst.id = from.id;
                                                dst.count = 0;
                                            }
                                            dst.count += move;
                                            from.count -= move;
                                            movedAny = true;
                                        };
                                        if (smeltingSystem.canSmelt(from.id)) {
                                            moveInto(smelting.input);
                                        }
                                        if (smeltingSystem.isFuel(from.id)) {
                                            moveInto(smelting.fuel);
                                        }
                                        if (!movedAny) {
                                            if (slotIndex < game::Inventory::kHotbarSize) {
                                                moveToRange(slotIndex, game::Inventory::kHotbarSize,
                                                            game::Inventory::kSlotCount);
                                            } else {
                                                moveToRange(slotIndex, 0, game::Inventory::kHotbarSize);
                                            }
                                        }
                                        clearIfEmpty(from);
                                    } else {
                                        if (slotIndex < game::Inventory::kHotbarSize) {
                                            moveToRange(slotIndex, game::Inventory::kHotbarSize,
                                                        game::Inventory::kSlotCount);
                                        } else {
                                            moveToRange(slotIndex, 0, game::Inventory::kHotbarSize);
                                        }
                                    }
                                } else if (slotIndex >= game::Inventory::kSlotCount &&
                                           slotIndex < game::Inventory::kSlotCount + kCraftInputCount) {
                                    int remaining = dst->count;
                                    const voxel::BlockId id = dst->id;
                                    moveStackIntoInventory(id, remaining);
                                    dst->count = remaining;
                                    clearIfEmpty(*dst);
                                }
                            } else if (carriedSlot.count == 0) {
                                carriedSlot = *dst;
                                *dst = {};
                                inventorySpreadPickupSourceSlot = slotIndex;
                            } else if (dst->count == 0) {
                                if (furnaceSlotAccepts(slotIndex, carriedSlot.id)) {
                                    *dst = carriedSlot;
                                    carriedSlot = {};
                                }
                            } else if (dst->id == carriedSlot.id &&
                                       dst->count < game::Inventory::kMaxStack) {
                                if (furnaceSlotAccepts(slotIndex, carriedSlot.id)) {
                                    const int canTake = game::Inventory::kMaxStack - dst->count;
                                    const int move =
                                        (carriedSlot.count < canTake) ? carriedSlot.count : canTake;
                                    dst->count += move;
                                    carriedSlot.count -= move;
                                    clearIfEmpty(carriedSlot);
                                }
                            } else {
                                if (furnaceSlotAccepts(slotIndex, carriedSlot.id)) {
                                    std::swap(*dst, carriedSlot);
                                }
                            }
                            lastInventoryLeftClickTime = now;
                            lastInventoryLeftClickId = clickId;
                        } else if (right && !prevInventoryRight) {
                            if (carriedSlot.count == 0) {
                                if (dst->count > 0 && dst->id != voxel::AIR) {
                                    const int take = (dst->count + 1) / 2;
                                    carriedSlot.id = dst->id;
                                    carriedSlot.count = take;
                                    dst->count -= take;
                                    clearIfEmpty(*dst);
                                }
                            } else {
                                if (tryPlaceOneFromCarried(slotIndex)) {
                                    // Single-slot right click behavior: place one.
                                }
                            }
                        }
                        if (!usingFurnace && slotIndex >= game::Inventory::kSlotCount) {
                            craftingSystem.updateOutput(crafting, craftingGridSize);
                        }
                    } else if (!handledRecipeClick && !handledCreativeClick && !handledTrashClick) {
                        const bool outsideClick =
                            (left && !prevInventoryLeft) || (right && !prevInventoryRight);
                        if (outsideClick && carriedSlot.id != voxel::AIR && carriedSlot.count > 0) {
                            const glm::vec3 dropPos = camera.position() + camera.forward() * 2.10f +
                                                      glm::vec3(0.0f, -0.30f, 0.0f);
                            itemDrops.spawn(carriedSlot.id, dropPos, carriedSlot.count);
                            carriedSlot = {};
                        }
                    }
                    prevInventoryLeft = left;
                    prevInventoryRight = right;
                } else {
                    prevInventoryLeft = false;
                    prevInventoryRight = false;
                }

                if (!blockInput && left && currentHit.has_value()) {
                    const glm::ivec3 target = currentHit->block;
                    if (!miningBlock.has_value() || miningBlock.value() != target) {
                        miningBlock = target;
                        miningProgress = 0.0f;
                    }
                    const voxel::BlockId targetId = world.getBlock(target.x, target.y, target.z);
                    if (targetId != voxel::AIR && mineCooldown <= 0.0f) {
                        miningProgress += dt / game::breakSeconds(targetId);
                        if (miningProgress >= 1.0f) {
                            if (world.setBlock(target.x, target.y, target.z, voxel::AIR)) {
                                if (voxel::isFurnace(targetId)) {
                                    world::FurnaceState wstate{};
                                    if (world.getFurnaceState(target.x, target.y, target.z, wstate)) {
                                        auto dropSmeltSlot = [&](const game::Inventory::Slot &slot) {
                                            if (slot.id != voxel::AIR && slot.count > 0) {
                                                itemDrops.spawn(slot.id, glm::vec3(target) +
                                                                             glm::vec3(0.5f, 0.2f, 0.5f),
                                                               slot.count);
                                            }
                                        };
                                        const game::SmeltingSystem::State gstate =
                                            fromWorldFurnaceState(wstate);
                                        dropSmeltSlot(gstate.input);
                                        dropSmeltSlot(gstate.fuel);
                                        dropSmeltSlot(gstate.output);
                                        world.clearFurnaceState(target.x, target.y, target.z);
                                    }
                                    if (activeFurnaceCell.has_value() &&
                                        activeFurnaceCell->x == target.x &&
                                        activeFurnaceCell->y == target.y &&
                                        activeFurnaceCell->z == target.z) {
                                        activeFurnaceCell.reset();
                                        smelting = {};
                                        usingFurnace = false;
                                    }
                                }
                                glm::vec3 dropPos = glm::vec3(target) + glm::vec3(0.5f, 0.2f, 0.5f);
                                if (targetId == voxel::TALL_GRASS || targetId == voxel::FLOWER) {
                                    dropPos.y = static_cast<float>(target.y) + 0.02f;
                                }
                                const voxel::BlockId dropId =
                                    voxel::isTorch(targetId)
                                        ? voxel::TORCH
                                        : (voxel::isFurnace(targetId) ? voxel::FURNACE : targetId);
                                itemDrops.spawn(dropId,
                                               dropPos, 1);
                                dropUnsupportedTorchesAround(world, itemDrops, target);
                                dropUnsupportedPlantsAround(world, itemDrops, target);
                                audio.playBreak(game::soundProfileForBlock(targetId));
                            }
                            miningProgress = 0.0f;
                            miningBlock.reset();
                            mineCooldown = 0.08f;
                        }
                    }
                } else {
                    miningBlock.reset();
                    miningProgress = 0.0f;
                }

                if (!blockInput && right && !prevRight && currentHit.has_value()) {
                    const glm::ivec3 place = currentHit->block + currentHit->normal;
                    const glm::vec3 camPos = camera.position();
                    const glm::ivec3 camCell(static_cast<int>(std::floor(camPos.x)),
                                             static_cast<int>(std::floor(camPos.y)),
                                             static_cast<int>(std::floor(camPos.z)));
                    const auto slot = inventory.hotbarSlot(selectedBlockIndex);
                    if (slot.id != voxel::AIR && slot.count > 0) {
                        bool placementAllowed = true;
                        voxel::BlockId placeId = slot.id;

                        // Sticks are inventory items, not placeable world blocks.
                        if (slot.id == voxel::STICK || slot.id == voxel::IRON_INGOT ||
                            slot.id == voxel::COPPER_INGOT || slot.id == voxel::GOLD_INGOT) {
                            placementAllowed = false;
                        }

                        // Only collision-solid blocks are prevented from intersecting the player.
                        const bool placingCollisionSolid = isCollisionSolidPlacement(slot.id);
                        if (placingCollisionSolid) {
                            const bool intersectsPlayer =
                                !ghostMode && placeCellIntersectsPlayer(place, camPos);
                            if (place == camCell || intersectsPlayer) {
                                placementAllowed = false;
                            }
                        }

                        // Torches must be attached to a solid block face (floor or wall, not ceiling).
                        if (placementAllowed && slot.id == voxel::TORCH) {
                            const glm::ivec3 normal = currentHit->normal;
                            placeId = torchIdForPlacementNormal(normal);
                            if (placeId == voxel::AIR) {
                                placementAllowed = false;
                            } else {
                                const glm::ivec3 support = place - normal;
                                if (!world.isSolidBlock(support.x, support.y, support.z)) {
                                    placementAllowed = false;
                                }
                            }
                        }
                        // Plants must stand on a solid block.
                        if (placementAllowed && isSupportPlant(slot.id)) {
                            if (!world.isSolidBlock(place.x, place.y - 1, place.z)) {
                                placementAllowed = false;
                            }
                        }

                        if (placementAllowed && placeId == voxel::FURNACE) {
                            placeId = orientedFurnaceFromForward(camera.forward(), false);
                        }

                        if (placementAllowed && world.setBlock(place.x, place.y, place.z, placeId)) {
                            inventory.consumeHotbar(selectedBlockIndex, 1);
                            audio.playPlace(game::soundProfileForBlock(slot.id));
                        }
                    }
                }
                prevLeft = left;
                prevRight = right;
                const glm::vec3 camPos = camera.position();
                const glm::vec2 stepDelta(camPos.x - prevStepCamPos.x, camPos.z - prevStepCamPos.z);
                const float horizDist = glm::length(stepDelta);
                const float horizSpeed = horizDist / std::max(dt, 1e-4f);
                if (!ghostMode && !blockInput) {
                    const bool groundedNow = player.grounded();
                    const bool inWaterNow = player.inWater();

                    if (groundedNow && !inWaterNow) {
                        stepDistanceAccum += horizDist;
                        stepCooldown = std::max(0.0f, stepCooldown - dt);
                        if (horizSpeed < 0.22f) {
                            stepDistanceAccum = 0.0f;
                        }
                        const float movingThreshold = 0.70f;
                        const float stride = (horizSpeed > 8.5f) ? 0.78f : 0.96f;
                        while (horizSpeed > movingThreshold && stepDistanceAccum >= stride &&
                               stepCooldown <= 0.0f) {
                            const voxel::BlockId groundId = game::blockUnderPlayer(world, camPos);
                            audio.playFootstep(game::footstepProfileForGround(groundId));
                            stepDistanceAccum -= stride;
                            stepCooldown = 0.24f;
                        }
                    } else {
                        stepDistanceAccum = 0.0f;
                        stepCooldown = 0.0f;
                    }

                    if (inWaterNow) {
                        swimCooldown = std::max(0.0f, swimCooldown - dt);
                        bobCooldown = std::max(0.0f, bobCooldown - dt);
                        if (horizSpeed > 0.55f && swimCooldown <= 0.0f) {
                            audio.playSwim();
                            swimCooldown = 0.90f;
                        } else if (horizSpeed <= 0.55f && bobCooldown <= 0.0f) {
                            audio.playWaterBob();
                            bobCooldown = 1.20f;
                        }
                    } else {
                        swimCooldown = std::min(swimCooldown, 0.08f);
                        bobCooldown = 0.0f;
                    }
                } else {
                    stepDistanceAccum = 0.0f;
                    stepCooldown = 0.0f;
                    swimCooldown = 0.0f;
                    bobCooldown = 0.0f;
                }
                prevStepCamPos = camPos;

                int fbw = 1;
                int fbh = 1;
                glfwGetFramebufferSize(window, &fbw, &fbh);
                const float aspect =
                    (fbh > 0) ? static_cast<float>(fbw) / static_cast<float>(fbh) : 16.0f / 9.0f;
                const glm::mat4 proj =
                    glm::perspective(glm::radians(debugCfg.fov), aspect, 0.1f, 500.0f);
                const glm::mat4 view = camera.view();

                const float dayPhase = dayClockSeconds / kDayLengthSeconds;
                const float sunAngle = dayPhase * (glm::pi<float>() * 2.0f);
                const float sunHeight = std::sin(sunAngle);
                const float daylight = glm::smoothstep(-0.10f, 0.20f, sunHeight);
                const float twilight =
                    (1.0f - glm::clamp(std::abs(sunHeight) * 3.0f, 0.0f, 1.0f)) *
                    (0.65f + 0.35f * (1.0f - daylight));

                const glm::vec3 daySky(0.56f, 0.79f, 0.99f);
                const glm::vec3 duskSky(0.96f, 0.56f, 0.30f);
                const glm::vec3 nightSky(0.02f, 0.03f, 0.08f);
                glm::vec3 skyColor = glm::mix(nightSky, daySky, daylight);
                skyColor = glm::mix(skyColor, duskSky, twilight);

                const glm::vec3 sunDir =
                    glm::normalize(glm::vec3(std::cos(sunAngle), sunHeight, 0.0f));
                const glm::vec3 nightTint(0.58f, 0.66f, 0.92f);
                const glm::vec3 dayTint(1.00f, 1.00f, 1.00f);
                const glm::vec3 duskTint(1.14f, 0.88f, 0.70f);
                glm::vec3 skyTint = glm::mix(nightTint, dayTint, daylight);
                skyTint = glm::mix(skyTint, duskTint, twilight * 0.65f);

                glClearColor(skyColor.r, skyColor.g, skyColor.b, 1.0f);
                glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

                const glm::vec3 camForward = camera.forward();
                glm::vec3 camRight = glm::cross(camForward, glm::vec3(0.0f, 1.0f, 0.0f));
                if (glm::dot(camRight, camRight) < 1e-5f) {
                    camRight = glm::vec3(1.0f, 0.0f, 0.0f);
                } else {
                    camRight = glm::normalize(camRight);
                }
                const glm::vec3 camUp = glm::normalize(glm::cross(camRight, camForward));
                const glm::vec3 skyAnchor = camera.position() + glm::vec3(0.0f, 28.0f, 0.0f);
                const glm::vec3 sunCenter = skyAnchor + sunDir * 210.0f;
                const glm::vec3 moonCenter = skyAnchor - sunDir * 210.0f;
                const float sunVis = glm::smoothstep(-0.06f, 0.16f, sunHeight);
                const float moonVis = glm::smoothstep(0.08f, -0.16f, sunHeight);
                const float moonPhase01 = std::clamp(debugCfg.moonPhase01, 0.0f, 1.0f);
                const glm::vec3 sunColor =
                    glm::vec3(1.00f, 0.93f, 0.64f) * (0.28f + 0.72f * sunVis);
                const glm::vec3 moonColor =
                    glm::vec3(0.79f, 0.85f, 0.96f) * (0.28f + 0.72f * moonVis);
                const bool sunDominant = sunHeight >= 0.0f;
                const glm::vec3 celestialDir = sunDominant ? sunDir : -sunDir;
                const float celestialStrength = sunDominant ? (0.92f * sunVis) : (0.36f * moonVis);
                auto skyBillboardAxes = [&](const glm::vec3 &center) {
                    const glm::vec3 toBody = glm::normalize(center - camera.position());
                    const glm::vec3 worldUp(0.0f, 1.0f, 0.0f);
                    glm::vec3 right = glm::cross(worldUp, toBody);
                    if (glm::dot(right, right) < 1e-5f) {
                        right = glm::vec3(1.0f, 0.0f, 0.0f);
                    } else {
                        right = glm::normalize(right);
                    }
                    const glm::vec3 up = glm::normalize(glm::cross(toBody, right));
                    return std::pair<glm::vec3, glm::vec3>{right, up};
                };
                const float starVis =
                    glm::clamp((1.0f - daylight) * (0.65f + 0.35f * moonVis), 0.0f, 1.0f);
                const float cloudVis =
                    glm::clamp(0.20f + 0.60f * daylight + 0.25f * twilight, 0.0f, 0.95f);
                const float cloudLayerY = kCloudLayerY;

                glEnable(GL_BLEND);
                glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
                glDisable(GL_DEPTH_TEST);
                glDepthMask(GL_FALSE);

                if (debugCfg.showStars && starVis > 0.01f) {
                    constexpr int kStarCount = 140;
                    for (int i = 0; i < kStarCount; ++i) {
                        const float az = hash01(i, 13) * (glm::pi<float>() * 2.0f) + dayPhase * 0.35f;
                        const float el = glm::mix(0.10f, 1.12f, hash01(i, 31));
                        const float ce = std::cos(el);
                        glm::vec3 dir(std::cos(az) * ce, std::sin(el), std::sin(az) * ce);
                        dir = glm::normalize(dir);
                        if (dir.y < 0.02f) {
                            continue;
                        }
                        const glm::vec3 center = skyAnchor + dir * 235.0f;
                        const float twinkle =
                            0.72f + 0.28f * std::sin(now * (2.0f + hash01(i, 53) * 3.0f) +
                                                     hash01(i, 71) * (glm::pi<float>() * 2.0f));
                        const float radius = 0.58f + 0.78f * hash01(i, 97);
                        const glm::vec3 starColor =
                            glm::mix(glm::vec3(0.72f, 0.80f, 1.00f), glm::vec3(1.0f), hash01(i, 43));
                        const auto axes = skyBillboardAxes(center);
                        skyBodyRenderer.draw(proj, view, center, axes.first, axes.second, radius,
                                             starColor * (0.35f + 0.75f * starVis), 0.10f * twinkle,
                                             SkyBodyRenderer::BodyType::Star, 0.0f);
                    }
                }

                if (sunCenter.y + 16.0f > camera.position().y) {
                    const auto axes = skyBillboardAxes(sunCenter);
                    skyBodyRenderer.draw(proj, view, sunCenter, axes.first, axes.second, 16.0f,
                                         sunColor,
                                         0.07f + 0.15f * sunVis, SkyBodyRenderer::BodyType::Sun,
                                         0.0f);
                }
                if (moonCenter.y + 14.0f > camera.position().y) {
                    const auto axes = skyBillboardAxes(moonCenter);
                    skyBodyRenderer.draw(proj, view, moonCenter, axes.first, axes.second, 14.0f,
                                         moonColor,
                                         0.03f + 0.05f * moonVis, SkyBodyRenderer::BodyType::Moon,
                                         moonPhase01);
                }

                glDepthMask(GL_TRUE);
                glEnable(GL_DEPTH_TEST);
                glDisable(GL_BLEND);

                const bool wireframe = debugCfg.renderMode == game::RenderMode::Wireframe;
                glPolygonMode(GL_FRONT_AND_BACK, wireframe ? GL_LINE : GL_FILL);

                shader.use();
                shader.setMat4("uProj", proj);
                shader.setMat4("uView", view);
                shader.setFloat("uDaylight", daylight);
                shader.setVec3("uSkyTint", skyTint);
                shader.setVec3("uCelestialDir", celestialDir);
                shader.setFloat("uCelestialStrength", celestialStrength);
                shader.setVec3("uPlayerPos", camera.position());
                const float renderEdge = static_cast<float>(debugCfg.loadRadius * voxel::Chunk::SX);
                const float fogFar = std::max(28.0f, renderEdge - 16.0f);
                const float fogNear =
                    std::max(8.0f, fogFar - std::max(52.0f, renderEdge * 0.72f));
                const glm::vec3 fogColor = glm::mix(skyColor, glm::vec3(0.80f, 0.86f, 0.95f), 0.22f);
                shader.setVec3("uFogColor", fogColor);
                shader.setFloat("uFogNear", debugCfg.showFog ? fogNear : 1000000.0f);
                shader.setFloat("uFogFar", debugCfg.showFog ? fogFar : 1000001.0f);
                shader.setFloat("uCloudShadowEnabled", debugCfg.showClouds ? 1.0f : 0.0f);
                shader.setFloat("uCloudShadowTime", now);
                shader.setFloat("uCloudShadowStrength", 0.32f);
                shader.setFloat("uCloudShadowDay", sunVis);
                shader.setFloat("uCloudLayerY", cloudLayerY);
                shader.setFloat("uCloudShadowRange", kCloudShadowRange);
                const voxel::BlockId heldId = inventory.hotbarSlot(selectedBlockIndex).id;
                const float heldTorchStrength = voxel::isTorch(heldId) ? 0.90f : 0.0f;
                shader.setFloat("uHeldTorchStrength", heldTorchStrength);
                atlas.bind(0);
                shader.setInt("uAtlas", 0);
                shader.setInt("uRenderMode",
                              debugCfg.renderMode == game::RenderMode::Textured ? 0 : 1);
                if (debugCfg.renderMode == game::RenderMode::Textured) {
                    // Pass 1: opaque geometry writes depth.
                    glDisable(GL_BLEND);
                    glDepthMask(GL_TRUE);
                    shader.setInt("uAlphaPass", 0);
                    world.draw();

                    // Pass 2: transparent geometry blends over opaque; no depth writes.
                    glEnable(GL_BLEND);
                    glDepthMask(GL_FALSE);
                    shader.setInt("uAlphaPass", 1);
                    world.drawTransparent(camera.position());
                    glDepthMask(GL_TRUE);
                } else {
                    glDisable(GL_BLEND);
                    glDepthMask(GL_TRUE);
                    shader.setInt("uAlphaPass", 2);
                    world.draw();
                }

                if (debugCfg.showClouds && cloudVis > 0.01f) {
                    constexpr int kRange = kCloudRenderRange;
                    const float cell = kCloudCellSize;
                    const float cloudY = cloudLayerY;
                    const float driftX = now * kCloudDriftXSpeed;
                    const float driftZ = now * kCloudDriftZSpeed;
                    auto cloudHashU32 = [](std::uint32_t x) {
                        x ^= x >> 16u;
                        x *= 0x7feb352du;
                        x ^= x >> 15u;
                        x *= 0x846ca68bu;
                        x ^= x >> 16u;
                        return x;
                    };
                    auto cloudHash = [cloudHashU32](int x, int z, int salt) {
                        const auto u32 = [](int v) { return static_cast<std::uint32_t>(v); };
                        const std::uint32_t h = cloudHashU32(
                            u32(x) ^ (cloudHashU32(u32(z) + 0x9e3779b9u) +
                                      u32(salt) * 0x85ebca6bu));
                        return static_cast<float>(h & 0x00ffffffu) * (1.0f / 16777215.0f);
                    };
                    const int centerGX =
                        static_cast<int>(std::floor((camera.position().x - driftX) / cell));
                    const int centerGZ =
                        static_cast<int>(std::floor((camera.position().z - driftZ) / cell));
                    const glm::vec3 cloudRight(1.0f, 0.0f, 0.0f);
                    const glm::vec3 cloudUp(0.0f, 0.0f, 1.0f);
                    const glm::vec3 cloudColor = glm::mix(
                        glm::vec3(0.80f, 0.86f, 0.92f), glm::vec3(1.00f, 1.00f, 1.00f),
                        0.42f + 0.45f * daylight);

                    glEnable(GL_BLEND);
                    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
                    glEnable(GL_DEPTH_TEST);
                    glDepthFunc(GL_LEQUAL);
                    glDepthMask(GL_FALSE);
                    for (int gz = -kRange; gz <= kRange; ++gz) {
                        for (int gx = -kRange; gx <= kRange; ++gx) {
                            const int gxi = centerGX + gx;
                            const int gzi = centerGZ + gz;
                            const int fx = static_cast<int>(std::floor(static_cast<float>(gxi) * 0.5f));
                            const int fz = static_cast<int>(std::floor(static_cast<float>(gzi) * 0.5f));

                            const float base = cloudHash(fx, fz, 503);
                            const float nE = cloudHash(fx + 1, fz, 503);
                            const float nW = cloudHash(fx - 1, fz, 503);
                            const float nN = cloudHash(fx, fz + 1, 503);
                            const float nS = cloudHash(fx, fz - 1, 503);
                            const bool core = base > 0.77f;
                            const bool fringe =
                                (base > 0.69f) && (std::max(std::max(nE, nW), std::max(nN, nS)) > 0.78f);
                            if (!(core || fringe)) {
                                continue;
                            }

                            const glm::vec3 center((static_cast<float>(gxi) * cell) + driftX, cloudY,
                                                   (static_cast<float>(gzi) * cell) + driftZ);
                            skyBodyRenderer.draw(proj, view, center, cloudRight, cloudUp,
                                                 cell * kCloudQuadRadius,
                                                 cloudColor * (0.48f + 0.52f * cloudVis), 0.0f,
                                                 SkyBodyRenderer::BodyType::Cloud, 0.0f);
                        }
                    }
                    glDepthMask(GL_TRUE);
                    glDepthFunc(GL_LESS);
                    glDisable(GL_BLEND);
                }

                if (debugCfg.showChunkBorders) {
                    std::vector<glm::vec3> borderVerts;
                    borderVerts.reserve((voxel::Chunk::SY + 1) * 8 +
                                        (voxel::Chunk::SX + voxel::Chunk::SZ + 2) * 8);
                    auto pushEdge = [&](const glm::vec3 &a, const glm::vec3 &b) {
                        borderVerts.push_back(a);
                        borderVerts.push_back(b);
                    };
                    const int playerChunkX =
                        static_cast<int>(std::floor(camera.position().x / voxel::Chunk::SX));
                    const int playerChunkZ =
                        static_cast<int>(std::floor(camera.position().z / voxel::Chunk::SZ));
                    const float x0 = static_cast<float>(playerChunkX * voxel::Chunk::SX);
                    const float x1 = x0 + static_cast<float>(voxel::Chunk::SX);
                    const float z0 = static_cast<float>(playerChunkZ * voxel::Chunk::SZ);
                    const float z1 = z0 + static_cast<float>(voxel::Chunk::SZ);
                    const float yTop = static_cast<float>(voxel::Chunk::SY);

                    // Horizontal perimeter lines at every block height to show chunk edge bands.
                    for (int y = 0; y <= voxel::Chunk::SY; ++y) {
                        const float yy = static_cast<float>(y);
                        pushEdge({x0, yy, z0}, {x1, yy, z0});
                        pushEdge({x1, yy, z0}, {x1, yy, z1});
                        pushEdge({x1, yy, z1}, {x0, yy, z1});
                        pushEdge({x0, yy, z1}, {x0, yy, z0});
                    }
                    // Vertical strips on every block boundary along each chunk edge.
                    for (int lx = 0; lx <= voxel::Chunk::SX; ++lx) {
                        const float xx = x0 + static_cast<float>(lx);
                        pushEdge({xx, 0.0f, z0}, {xx, yTop, z0});
                        pushEdge({xx, 0.0f, z1}, {xx, yTop, z1});
                    }
                    for (int lz = 0; lz <= voxel::Chunk::SZ; ++lz) {
                        const float zz = z0 + static_cast<float>(lz);
                        pushEdge({x0, 0.0f, zz}, {x0, yTop, zz});
                        pushEdge({x1, 0.0f, zz}, {x1, yTop, zz});
                    }

                    glEnable(GL_BLEND);
                    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
                    glEnable(GL_DEPTH_TEST);
                    glDepthMask(GL_FALSE);
                    glLineWidth(2.0f);
                    chunkBorderRenderer.draw(proj, view, borderVerts);
                    glLineWidth(1.0f);
                    glDepthMask(GL_TRUE);
                }
                itemDrops.render(proj, view, atlas, hudRegistry);
                std::optional<glm::ivec3> highlightedBlock;
                if (hudVisible && currentHit.has_value()) {
                    highlightedBlock = currentHit->block;
                }
                if (hudVisible) {
                    float breakProgress = 0.0f;
                    voxel::BlockId breakBlockId = voxel::AIR;
                    if (highlightedBlock.has_value() && miningBlock.has_value() &&
                        highlightedBlock.value() == miningBlock.value()) {
                        breakProgress = miningProgress;
                        breakBlockId = world.getBlock(highlightedBlock->x, highlightedBlock->y,
                                                      highlightedBlock->z);
                    }
                    hud.renderBreakOverlay(proj, view, highlightedBlock, breakProgress,
                                           breakBlockId, atlas);
                    hud.renderBlockOutline(proj, view, highlightedBlock, breakProgress);
                    hud.renderWorldWaypoints(
                        proj, view, mapSystem, camera.position(),
                        [&](int wx, int wz) { return world.isChunkLoadedAt(wx, wz); });
                }
                glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
                int winW = 1;
                int winH = 1;
                glfwGetWindowSize(window, &winW, &winH);
                if (hudVisible) {
                    std::vector<game::CraftingSystem::RecipeInfo> visibleRecipes;
                    std::vector<bool> visibleRecipeCraftable;
                    std::vector<game::SmeltingSystem::Recipe> visibleSmeltingRecipes;
                    if (recipeMenuVisible && !usingFurnace) {
                        visibleRecipes.reserve(filteredRecipeIndices.size());
                        visibleRecipeCraftable.reserve(filteredRecipeIndices.size());
                        for (int idx : filteredRecipeIndices) {
                            const auto &recipe = craftingSystem.recipeInfos()[idx];
                            visibleRecipes.push_back(recipe);
                            visibleRecipeCraftable.push_back(isRecipeCraftableNow(idx));
                        }
                    } else if (recipeMenuVisible && usingFurnace) {
                        const auto &smeltRecipes = smeltingSystem.recipes();
                        visibleSmeltingRecipes.reserve(filteredSmeltingIndices.size());
                        visibleRecipeCraftable.reserve(filteredSmeltingIndices.size());
                        int fuelCount = 0;
                        for (int i = 0; i < game::Inventory::kSlotCount; ++i) {
                            const auto &slot = inventory.slot(i);
                            if (slot.id != voxel::AIR && slot.count > 0 &&
                                smeltingSystem.isFuel(slot.id)) {
                                fuelCount += slot.count;
                            }
                        }
                        for (int idx : filteredSmeltingIndices) {
                            const auto &recipe = smeltRecipes[idx];
                            visibleSmeltingRecipes.push_back(recipe);
                            int inputCount = 0;
                            for (int s = 0; s < game::Inventory::kSlotCount; ++s) {
                                const auto &slot = inventory.slot(s);
                                if (slot.id == recipe.input && slot.count > 0) {
                                    inputCount += slot.count;
                                }
                            }
                            visibleRecipeCraftable.push_back(inputCount > 0 && fuelCount > 0);
                        }
                    }
                    std::string lookedAt = "Looking: (none)";
                    if (currentHit.has_value()) {
                        const voxel::BlockId lookedId = world.getBlock(
                            currentHit->block.x, currentHit->block.y, currentHit->block.z);
                        lookedAt = std::string("Looking: ") + game::blockName(lookedId);
                    }
                    const glm::vec3 pos = camera.position();
                    const auto hotbarIds = inventory.hotbarIds();
                    const auto hotbarCounts = inventory.hotbarCounts();
                    const auto allIds = inventory.slotIds();
                    const auto allCounts = inventory.slotCounts();
                    double cursorX = 0.0;
                    double cursorY = 0.0;
                    if (inventoryVisible) {
                        glfwGetCursorPos(window, &cursorX, &cursorY);
                    }
                    const voxel::BlockId selectedPlaceBlock =
                        inventory.hotbarSlot(selectedBlockIndex).id;
                    std::string modeText = "Walking";
                    if (ghostMode) {
                        modeText = "Mode: Ghost";
                    } else if (player.inWater()) {
                        modeText = "Swimming";
                    } else if (player.sprinting()) {
                        modeText = "Sprinting";
                    } else if (player.crouching()) {
                        modeText = "Crouching";
                    }
                    const std::string compassText = compassTextFromForward(camera.forward());
                    const int bx = static_cast<int>(std::floor(pos.x));
                    const int by = static_cast<int>(std::floor(pos.y));
                    const int bz = static_cast<int>(std::floor(pos.z));
                    std::ostringstream coord;
                    coord << "XYZ: " << bx << ", " << by << ", " << bz;
                    hud.render2D(winW, winH, selectedBlockIndex, hotbarIds, hotbarCounts, allIds,
                                 allCounts, inventoryVisible, carriedSlot.id, carriedSlot.count,
                                 static_cast<float>(cursorX), static_cast<float>(cursorY),
                                 hoveredInventorySlot, debugCfg.hudScale, crafting.input,
                                 craftingGridSize, usingCraftingTable, usingFurnace,
                                 smelting.input, smelting.fuel, smelting.output,
                                 smelting.progressSeconds / game::SmeltingSystem::kSmeltSeconds,
                                 (smelting.burnSecondsCapacity > 0.0f)
                                     ? (smelting.burnSecondsRemaining / smelting.burnSecondsCapacity)
                                     : 0.0f,
                                 recipeMenuVisible,
                                 visibleRecipes, visibleSmeltingRecipes, visibleRecipeCraftable,
                                 recipeMenuScroll,
                                 static_cast<float>(glfwGetTime()), recipeSearchText,
                                 creativeMenuVisible, filteredCreativeItems, creativeMenuScroll,
                                 creativeSearchText, recipeCraftableOnly, recipeIngredientFilter,
                                 crafting.output,
                                 (carriedSlot.id == voxel::AIR || carriedSlot.count <= 0)
                                     ? ""
                                     : game::blockName(carriedSlot.id),
                                 (selectedPlaceBlock == voxel::AIR)
                                     ? "Empty Slot"
                                     : game::blockName(selectedPlaceBlock),
                                 lookedAt, modeText, player.health01(), player.sprintStamina01(),
                                 compassText, coord.str(), hudRegistry, atlas);
                }
                if (pauseMenuOpen) {
                    double pmx = 0.0;
                    double pmy = 0.0;
                    glfwGetCursorPos(window, &pmx, &pmy);
                    hud.renderPauseMenu(winW, winH, static_cast<float>(pmx),
                                        static_cast<float>(pmy));
                }
                if (mapOpen) {
                    const glm::vec3 pos = camera.position();
                    const int mapWX = static_cast<int>(std::floor(pos.x));
                    const int mapWZ = static_cast<int>(std::floor(pos.z));
                    double mapCursorX = 0.0;
                    double mapCursorY = 0.0;
                    glfwGetCursorPos(window, &mapCursorX, &mapCursorY);
                    std::string waypointName;
                    std::uint8_t waypointR = 255;
                    std::uint8_t waypointG = 96;
                    std::uint8_t waypointB = 96;
                    int waypointIcon = 0;
                    bool waypointVisible = true;
                    if (selectedWaypointIndex >= 0 &&
                        selectedWaypointIndex < static_cast<int>(mapSystem.waypoints().size())) {
                        const auto &wp = mapSystem.waypoints()[selectedWaypointIndex];
                        waypointName = wp.name;
                        waypointR = wp.r;
                        waypointG = wp.g;
                        waypointB = wp.b;
                        waypointIcon = static_cast<int>(wp.icon);
                        waypointVisible = wp.visible;
                    }
                    hud.renderMapOverlay(
                        winW, winH, mapSystem, static_cast<int>(std::round(mapCenterWX)),
                        static_cast<int>(std::round(mapCenterWZ)), mapWX, mapWZ, mapZoom,
                        static_cast<float>(mapCursorX), static_cast<float>(mapCursorY),
                        selectedWaypointIndex, waypointName, waypointR, waypointG, waypointB,
                        waypointIcon, waypointVisible, std::atan2(camera.forward().x, -camera.forward().z),
                        waypointNameFocused,
                        waypointEditorOpen, hudRegistry, atlas);
                } else if (hudVisible) {
                    const glm::vec3 pos = camera.position();
                    const int mapWX = static_cast<int>(std::floor(pos.x));
                    const int mapWZ = static_cast<int>(std::floor(pos.z));
                    const glm::vec3 fwd = camera.forward();
                    const float miniMapHeadingRad = std::atan2(fwd.x, -fwd.z);
                    hud.renderMiniMap(winW, winH, mapSystem, mapWX, mapWZ, miniMapZoom,
                                      miniMapNorthLocked, miniMapShowCompass, miniMapShowWaypoints,
                                      miniMapHeadingRad,
                                      hudRegistry, atlas);
                }
                debugMenu.render(winW, winH);
                glfwSwapBuffers(window);

                titleAccum += dt;
                if (titleAccum >= 0.2f) {
                    debugMenu.updateWindowTitle(window, debugCfg, stats, fps, dt * 1000.0f);
                    titleAccum = 0.0f;
                }
            }

            gRecipeSearchText = nullptr;
            glfwSetCharCallback(window, nullptr);
            saveCurrentPlayer();
            if (returnToTitle && !glfwWindowShouldClose(window)) {
                glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_NORMAL);
                glfwSetCursor(window, arrowCursor);
                continue;
            }
            appRunning = false;
        }
        glfwDestroyWindow(window);
        if (arrowCursor != nullptr) {
            glfwDestroyCursor(arrowCursor);
        }
        glfwTerminate();
        return 0;
    } catch (const std::exception &e) {
        core::Logger::instance().error(e.what());
        return 1;
    }
}
