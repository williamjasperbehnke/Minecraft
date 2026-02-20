#include "core/Logger.hpp"
#include "game/AudioSystem.hpp"
#include "game/Camera.hpp"
#include "game/CraftingSystem.hpp"
#include "game/DebugMenu.hpp"
#include "game/GameRules.hpp"
#include "game/Inventory.hpp"
#include "game/ItemDropSystem.hpp"
#include "game/PlayerController.hpp"
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
                    bool &ghostMode, game::Inventory &inventory) {
    std::ifstream in(worldDir / "player.dat");
    if (!in) {
        return false;
    }

    std::string magic;
    in >> magic;
    if (!in || (magic != "VXP1" && magic != "VXP2")) {
        return false;
    }

    glm::vec3 loadedPos{};
    int loadedSelected = 0;
    int loadedMode = 1;
    in >> loadedPos.x >> loadedPos.y >> loadedPos.z;
    in >> loadedSelected;
    if (magic == "VXP2") {
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
    return true;
}

void savePlayerData(const std::filesystem::path &worldDir, const glm::vec3 &cameraPos,
                    int selectedSlot, bool ghostMode, const game::Inventory &inventory) {
    std::filesystem::create_directories(worldDir);
    std::ofstream out(worldDir / "player.dat", std::ios::trunc);
    if (!out) {
        return;
    }

    out << "VXP2\n";
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

int inventorySlotAtCursor(double mx, double my, int width, int height, bool showInventory,
                          float hudScale, int craftingGridSize) {
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
    const float craftY = invY + (invH - craftGridW) * 0.5f;
    for (int r = 0; r < craftGrid; ++r) {
        for (int c = 0; c < craftGrid; ++c) {
            const int idx = game::Inventory::kSlotCount + r * craftGrid + c;
            const float sx = craftX + c * (craftSlot + craftGap);
            const float sy = craftY + r * (craftSlot + craftGap);
            considerSlot(idx, sx, sy, craftSlot);
        }
    }
    const float outX = craftX + craftGridW + 44.0f * uiScale;
    const float outY = craftY + (craftGridW - craftSlot) * 0.5f;
    considerSlot(kCraftOutputSlot, outX, outY, craftSlot);
    const float snapRadius = 9.0f * uiScale;
    if (nearestSlot >= 0 && nearestSlot < kUiSlotCount && nearestDist2 <= snapRadius * snapRadius) {
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
                                         std::size_t recipeCount) {
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

std::string toLowerAscii(std::string s) {
    for (char &c : s) {
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    return s;
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
            bool prevInventoryToggle = false;
            bool prevPauseToggle = false;
            bool hudVisible = true;
            bool inventoryVisible = false;
            bool pauseMenuOpen = false;
            bool ghostMode = true;
            game::PlayerController playerController;
            game::Inventory inventory;
            game::CraftingSystem craftingSystem;
            game::CraftingSystem::State crafting;
            float recipeMenuScroll = 0.0f;
            std::string recipeSearchText;
            std::optional<voxel::BlockId> recipeIngredientFilter;
            bool recipeCraftableOnly = false;
            int craftingGridSize = game::CraftingSystem::kGridSizeInventory;
            bool usingCraftingTable = false;
            game::Inventory::Slot carriedSlot{};
            std::array<bool, game::Inventory::kHotbarSize> prevBlockKeys{};
            int selectedBlockIndex = 0;
            glm::vec3 loadedCameraPos = camera.position();
            bool pendingSpawnResolve = false;
            if (loadPlayerData(worldSelection.path, loadedCameraPos, selectedBlockIndex, ghostMode,
                               inventory)) {
                camera.setPosition(loadedCameraPos);
                if (!ghostMode) {
                    pendingSpawnResolve = true;
                }
            }
            auto saveCurrentPlayer = [&]() {
                savePlayerData(worldSelection.path, camera.position(), selectedBlockIndex, ghostMode,
                               inventory);
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
            bool recipeScrollDragging = false;
            float recipeScrollGrabOffsetY = 0.0f;
            bool recipeSearchFocused = false;
            int lastRecipeFillIndex = -1;

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
                if (pauseToggleDown && !prevPauseToggle && !menuOpen && !inventoryVisible) {
                    pauseMenuOpen = !pauseMenuOpen;
                }
                prevPauseToggle = pauseToggleDown;
                const bool blockInput = menuOpen || inventoryVisible || pauseMenuOpen;
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
                        playerController.setFromCamera(loadedCameraPos, world, false);
                        camera.setPosition(playerController.cameraPosition());
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
                        playerController.update(window, world, camera, dt, !blockInput);
                        camera.setPosition(playerController.cameraPosition());
                    }
                }

                world.updateStream(camera.position());
                world.uploadReadyMeshes();
                stats = world.debugStats();
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
                for (const auto &pickup : itemDrops.consumePickups()) {
                    inventory.add(pickup.first, pickup.second);
                    audio.playPickup();
                }

                const bool hudToggleDown = glfwGetKey(window, GLFW_KEY_F2) == GLFW_PRESS;
                if (hudToggleDown && !prevHudToggle && !menuOpen) {
                    hudVisible = !hudVisible;
                }
                prevHudToggle = hudToggleDown;

                const bool modeToggleDown = glfwGetKey(window, GLFW_KEY_F4) == GLFW_PRESS;
                if (modeToggleDown && !prevModeToggle && !menuOpen && !inventoryVisible &&
                    !pauseMenuOpen) {
                    ghostMode = !ghostMode;
                    if (!ghostMode) {
                        playerController.setFromCamera(camera.position(), world);
                    } else {
                        camera.setPosition(playerController.cameraPosition());
                    }
                }
                prevModeToggle = modeToggleDown;

                auto clearCraftInputs = [&]() {
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
                    recipeMenuVisible && recipeSearchFocused && !menuOpen && !pauseMenuOpen;
                if (invToggleDown && !prevInventoryToggle && !menuOpen && !pauseMenuOpen &&
                    !suppressInventoryToggle) {
                    const bool openingInventory = !inventoryVisible;
                    inventoryVisible = openingInventory;
                    if (openingInventory) {
                        usingCraftingTable =
                            isLookingAtCraftingTable(world, camera, debugCfg.raycastDistance);
                        craftingGridSize = usingCraftingTable ? game::CraftingSystem::kGridSizeTable
                                                              : game::CraftingSystem::kGridSizeInventory;
                        craftingSystem.updateOutput(crafting, craftingGridSize);
                    } else {
                        clearCraftInputs();
                        usingCraftingTable = false;
                        craftingGridSize = game::CraftingSystem::kGridSizeInventory;
                        recipeMenuVisible = false;
                        recipeMenuScroll = 0.0f;
                        recipeScrollDragging = false;
                        recipeSearchFocused = false;
                        recipeSearchText.clear();
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
                    !pauseMenuOpen && !(recipeMenuVisible && recipeSearchFocused)) {
                    recipeMenuVisible = !recipeMenuVisible;
                    if (recipeMenuVisible) {
                        recipeSearchFocused = true;
                        gRecipeSearchText = &recipeSearchText;
                        glfwSetCharCallback(window, onRecipeSearchCharInput);
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

                const bool recipeBackspace = glfwGetKey(window, GLFW_KEY_BACKSPACE) == GLFW_PRESS;
                if (recipeMenuVisible && recipeBackspace && !prevRecipeSearchBackspace &&
                    !recipeSearchText.empty()) {
                    recipeSearchText.pop_back();
                }
                prevRecipeSearchBackspace = recipeBackspace;

                std::vector<int> filteredRecipeIndices;
                std::vector<unsigned char> recipeCraftableCache;
                std::vector<unsigned char> recipeCraftableComputed;
                filteredRecipeIndices.reserve(craftingSystem.recipeInfos().size());
                recipeCraftableCache.assign(craftingSystem.recipeInfos().size(), 0);
                recipeCraftableComputed.assign(craftingSystem.recipeInfos().size(), 0);
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
                    const int pick =
                        filteredPlankRecipeIndices[cycle % static_cast<int>(filteredPlankRecipeIndices.size())];
                    const int insertPos =
                        (plankInsertPos < 0 ||
                         plankInsertPos > static_cast<int>(filteredRecipeIndices.size()))
                            ? static_cast<int>(filteredRecipeIndices.size())
                            : plankInsertPos;
                    filteredRecipeIndices.insert(filteredRecipeIndices.begin() + insertPos, pick);
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
                    const RecipeMenuLayout recipeLayout =
                        computeRecipeMenuLayout(winW, winH, debugCfg.hudScale, craftingGridSize,
                                                filteredRecipeIndices.size());

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
                            const bool onThumb = my >= thumbY && my <= (thumbY + recipeLayout.thumbH);
                            if (onThumb) {
                                recipeScrollDragging = true;
                                recipeScrollGrabOffsetY = static_cast<float>(my) - thumbY;
                            } else if (recipeLayout.maxScroll > 0.0f) {
                                const float t = std::clamp(
                                    static_cast<float>((my - recipeLayout.trackY - recipeLayout.thumbH * 0.5f) /
                                                       std::max(1.0f, thumbTravel)),
                                    0.0f, 1.0f);
                                recipeMenuScroll = t * recipeLayout.maxScroll;
                            }
                        }
                    }
                    if (!leftNow) {
                        recipeScrollDragging = false;
                    }
                    gRecipeSearchText = recipeSearchFocused ? &recipeSearchText : nullptr;
                    glfwSetCharCallback(window,
                                        recipeSearchFocused ? onRecipeSearchCharInput : nullptr);
                    if (recipeScrollDragging && recipeLayout.maxScroll > 0.0f) {
                        const float thumbTravel =
                            std::max(0.0f, recipeLayout.trackH - recipeLayout.thumbH);
                        const float thumbY = std::clamp(
                            static_cast<float>(my) - recipeScrollGrabOffsetY, recipeLayout.trackY,
                            recipeLayout.trackY + thumbTravel);
                        const float t = (thumbTravel > 0.0f) ? (thumbY - recipeLayout.trackY) / thumbTravel
                                                             : 0.0f;
                        recipeMenuScroll = t * recipeLayout.maxScroll;
                    }
                    recipeMenuScroll = std::clamp(recipeMenuScroll, 0.0f, recipeLayout.maxScroll);
                } else {
                    recipeScrollDragging = false;
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
                                              craftingGridSize);
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
                        const auto &s = crafting.input[hoveredInventorySlot - game::Inventory::kSlotCount];
                        if (s.count > 0 && s.id != voxel::AIR) {
                            hoveredId = s.id;
                        }
                    }

                    if (hoveredId != voxel::AIR) {
                        recipeMenuVisible = true;
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
                if (dropDown && !prevDropKey && !menuOpen && !inventoryVisible && !pauseMenuOpen) {
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
                                              craftingGridSize);
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
                        if (idx >= game::Inventory::kSlotCount &&
                            idx < game::Inventory::kSlotCount + kCraftInputCount) {
                            return &crafting.input[idx - game::Inventory::kSlotCount];
                        }
                        return nullptr;
                    };

                    bool handledRecipeClick = false;
                    if (recipeMenuVisible && left && !prevInventoryLeft) {
                        const RecipeMenuLayout recipeLayout =
                            computeRecipeMenuLayout(winW, winH, debugCfg.hudScale, craftingGridSize,
                                                    filteredRecipeIndices.size());
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

                    game::Inventory::Slot *clickedSlot =
                        handledRecipeClick ? nullptr : slotFromUiIndex(slotIndex);

                    if (!handledRecipeClick && slotIndex == kCraftOutputSlot) {
                        if (left && !prevInventoryLeft && crafting.output.id != voxel::AIR &&
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
                    } else if (!handledRecipeClick && clickedSlot != nullptr) {
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
                                    if (slotIndex < game::Inventory::kHotbarSize) {
                                        moveToRange(slotIndex, game::Inventory::kHotbarSize,
                                                    game::Inventory::kSlotCount);
                                    } else {
                                        moveToRange(slotIndex, 0, game::Inventory::kHotbarSize);
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
                            } else if (dst->count == 0) {
                                *dst = carriedSlot;
                                carriedSlot = {};
                            } else if (dst->id == carriedSlot.id &&
                                       dst->count < game::Inventory::kMaxStack) {
                                const int canTake = game::Inventory::kMaxStack - dst->count;
                                const int move =
                                    (carriedSlot.count < canTake) ? carriedSlot.count : canTake;
                                dst->count += move;
                                carriedSlot.count -= move;
                                clearIfEmpty(carriedSlot);
                            } else {
                                std::swap(*dst, carriedSlot);
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
                                if (dst->count == 0 || dst->id == voxel::AIR) {
                                    dst->id = carriedSlot.id;
                                    dst->count = 1;
                                    carriedSlot.count -= 1;
                                    clearIfEmpty(carriedSlot);
                                } else if (dst->id == carriedSlot.id &&
                                           dst->count < game::Inventory::kMaxStack) {
                                    dst->count += 1;
                                    carriedSlot.count -= 1;
                                    clearIfEmpty(carriedSlot);
                                }
                            }
                        }
                        if (slotIndex >= game::Inventory::kSlotCount) {
                            craftingSystem.updateOutput(crafting, craftingGridSize);
                        }
                    } else if (!handledRecipeClick) {
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
                                glm::vec3 dropPos = glm::vec3(target) + glm::vec3(0.5f, 0.2f, 0.5f);
                                if (targetId == voxel::TALL_GRASS || targetId == voxel::FLOWER) {
                                    dropPos.y = static_cast<float>(target.y) + 0.02f;
                                }
                                itemDrops.spawn(voxel::isTorch(targetId) ? voxel::TORCH : targetId,
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
                        if (slot.id == voxel::STICK) {
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
                    const bool groundedNow = playerController.grounded();
                    const bool inWaterNow = playerController.inWater();

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
                }
                glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
                int winW = 1;
                int winH = 1;
                glfwGetWindowSize(window, &winW, &winH);
                if (hudVisible) {
                    std::vector<game::CraftingSystem::RecipeInfo> visibleRecipes;
                    std::vector<bool> visibleRecipeCraftable;
                    visibleRecipes.reserve(filteredRecipeIndices.size());
                    visibleRecipeCraftable.reserve(filteredRecipeIndices.size());
                    for (int idx : filteredRecipeIndices) {
                        const auto &recipe = craftingSystem.recipeInfos()[idx];
                        visibleRecipes.push_back(recipe);
                        visibleRecipeCraftable.push_back(isRecipeCraftableNow(idx));
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
                    std::string modeText = ghostMode ? "Mode: Ghost" : "Mode: Grounded";
                    if (!ghostMode && playerController.inWater()) {
                        modeText = "Mode: Swimming";
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
                                 craftingGridSize, usingCraftingTable, recipeMenuVisible,
                                 visibleRecipes, visibleRecipeCraftable, recipeMenuScroll,
                                 static_cast<float>(glfwGetTime()),
                                 recipeSearchText, recipeCraftableOnly, recipeIngredientFilter,
                                 crafting.output,
                                 (carriedSlot.id == voxel::AIR || carriedSlot.count <= 0)
                                     ? ""
                                     : game::blockName(carriedSlot.id),
                                 (selectedPlaceBlock == voxel::AIR)
                                     ? "Empty Slot"
                                     : game::blockName(selectedPlaceBlock),
                                 lookedAt, modeText, compassText, coord.str(), hudRegistry, atlas);
                }
                if (pauseMenuOpen) {
                    double pmx = 0.0;
                    double pmy = 0.0;
                    glfwGetCursorPos(window, &pmx, &pmy);
                    hud.renderPauseMenu(winW, winH, static_cast<float>(pmx),
                                        static_cast<float>(pmy));
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
