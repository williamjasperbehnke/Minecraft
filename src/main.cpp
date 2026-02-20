#include "core/Logger.hpp"
#include "game/AudioSystem.hpp"
#include "game/Camera.hpp"
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
void onFramebufferResize(GLFWwindow * /*window*/, int width, int height) {
    glViewport(0, 0, width, height);
}

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
    out << std::fixed << std::setprecision(3) << cameraPos.x << ' ' << cameraPos.y << ' '
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

int inventorySlotAtCursor(double mx, double my, int width, int height, bool showInventory) {
    if (!showInventory) {
        return -1;
    }
    const float cx = width * 0.5f;
    const float slot = 48.0f;
    const float gap = 10.0f;
    const float totalW = static_cast<float>(game::Inventory::kHotbarSize) * slot +
                         static_cast<float>(game::Inventory::kHotbarSize - 1) * gap;
    const float x0 = cx - totalW * 0.5f;
    const float y0 = height - 90.0f;

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
    const float invSlot = 48.0f;
    const float invGap = 10.0f;
    const float invW = cols * invSlot + (cols - 1) * invGap;
    const float invH = rows * invSlot + (rows - 1) * invGap;
    const float invX = cx - invW * 0.5f;
    const float invY = y0 - 44.0f - invH;

    for (int row = 0; row < rows; ++row) {
        for (int col = 0; col < cols; ++col) {
            const float sx = invX + col * (invSlot + invGap);
            const float sy = invY + row * (invSlot + invGap);
            considerSlot(game::Inventory::kHotbarSize + row * cols + col, sx, sy, invSlot);
        }
    }
    if (nearestSlot >= 0 && nearestDist2 <= 81.0f) {
        return nearestSlot;
    }
    return -1;
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
            game::Inventory::Slot carriedSlot{};
            std::array<bool, game::Inventory::kHotbarSize> prevBlockKeys{};
            int selectedBlockIndex = 0;
            glm::vec3 loadedCameraPos = camera.position();
            bool pendingSpawnResolve = false;
            if (loadPlayerData(worldSelection.path, loadedCameraPos, selectedBlockIndex, ghostMode,
                               inventory)) {
                camera.setPosition(loadedCameraPos);
                pendingSpawnResolve = !ghostMode;
            }
            camera.resetMouseLook(window);
            bool prevInventoryLeft = false;
            bool prevInventoryRight = false;
            bool prevDropKey = false;
            float lastInventoryLeftClickTime = -1.0f;
            voxel::BlockId lastInventoryLeftClickId = voxel::AIR;
            bool recaptureMouseAfterInventoryClose = false;

            float lastTime = static_cast<float>(glfwGetTime());
            float titleAccum = 0.0f;
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
                    const glm::vec3 spawnPos = camera.position();
                    const int swx = static_cast<int>(std::floor(spawnPos.x));
                    const int swz = static_cast<int>(std::floor(spawnPos.z));
                    if (world.isChunkLoadedAt(swx, swz)) {
                        playerController.setFromCamera(spawnPos, world);
                        camera.setPosition(playerController.cameraPosition());
                        pendingSpawnResolve = false;
                    }
                }

                if (ghostMode) {
                    if (!blockInput) {
                        camera.handleKeyboard(window, dt);
                    }
                } else {
                    playerController.update(window, world, camera, dt, !blockInput);
                    camera.setPosition(playerController.cameraPosition());
                }

                world.updateStream(camera.position());
                world.uploadReadyMeshes();
                stats = world.debugStats();
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

                const bool invToggleDown = glfwGetKey(window, GLFW_KEY_E) == GLFW_PRESS;
                if (invToggleDown && !prevInventoryToggle && !menuOpen && !pauseMenuOpen) {
                    inventoryVisible = !inventoryVisible;
                    glfwSetInputMode(window, GLFW_CURSOR,
                                     inventoryVisible ? GLFW_CURSOR_NORMAL : GLFW_CURSOR_DISABLED);
                    glfwSetCursor(window, inventoryVisible ? arrowCursor : nullptr);
                    if (!inventoryVisible) {
                        recaptureMouseAfterInventoryClose = true;
                    }
                }
                prevInventoryToggle = invToggleDown;

                // Placement block selection (keys 1..9).
                if (!pauseMenuOpen) {
                    for (int i = 0; i < game::Inventory::kHotbarSize; ++i) {
                        const int key = GLFW_KEY_1 + i;
                        const bool down = glfwGetKey(window, key) == GLFW_PRESS;
                        if (down && !prevBlockKeys[i]) {
                            selectedBlockIndex = i;
                            const voxel::BlockId selected =
                                inventory.hotbarSlot(selectedBlockIndex).id;
                            core::Logger::instance().info(std::string("Selected slot block: ") +
                                                          game::blockName(selected));
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
                            savePlayerData(worldSelection.path, camera.position(),
                                           selectedBlockIndex, ghostMode, inventory);
                        } else if (button == 3) {
                            savePlayerData(worldSelection.path, camera.position(),
                                           selectedBlockIndex, ghostMode, inventory);
                            returnToTitle = true;
                            break;
                        } else if (button == 4) {
                            savePlayerData(worldSelection.path, camera.position(),
                                           selectedBlockIndex, ghostMode, inventory);
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
                    const int slotIndex = inventorySlotAtCursor(mx, my, winW, winH, true);
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

                    if (slotIndex >= 0 && slotIndex < game::Inventory::kSlotCount) {
                        auto &dst = inventory.slot(slotIndex);
                        if (left && !prevInventoryLeft) {
                            voxel::BlockId clickId = voxel::AIR;
                            if (carriedSlot.count > 0 && carriedSlot.id != voxel::AIR) {
                                clickId = carriedSlot.id;
                            } else if (dst.count > 0 && dst.id != voxel::AIR) {
                                clickId = dst.id;
                            }

                            const bool doubleClickCollect =
                                !shiftDown && clickId != voxel::AIR &&
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
                                if (slotIndex < game::Inventory::kHotbarSize) {
                                    moveToRange(slotIndex, game::Inventory::kHotbarSize,
                                                game::Inventory::kSlotCount);
                                } else {
                                    moveToRange(slotIndex, 0, game::Inventory::kHotbarSize);
                                }
                            } else if (carriedSlot.count == 0) {
                                carriedSlot = dst;
                                dst = {};
                            } else if (dst.count == 0) {
                                dst = carriedSlot;
                                carriedSlot = {};
                            } else if (dst.id == carriedSlot.id &&
                                       dst.count < game::Inventory::kMaxStack) {
                                const int canTake = game::Inventory::kMaxStack - dst.count;
                                const int move =
                                    (carriedSlot.count < canTake) ? carriedSlot.count : canTake;
                                dst.count += move;
                                carriedSlot.count -= move;
                                clearIfEmpty(carriedSlot);
                            } else {
                                std::swap(dst, carriedSlot);
                            }
                            lastInventoryLeftClickTime = now;
                            lastInventoryLeftClickId = clickId;
                        } else if (right && !prevInventoryRight) {
                            if (carriedSlot.count == 0) {
                                if (dst.count > 0 && dst.id != voxel::AIR) {
                                    const int take = (dst.count + 1) / 2;
                                    carriedSlot.id = dst.id;
                                    carriedSlot.count = take;
                                    dst.count -= take;
                                    clearIfEmpty(dst);
                                }
                            } else {
                                if (dst.count == 0 || dst.id == voxel::AIR) {
                                    dst.id = carriedSlot.id;
                                    dst.count = 1;
                                    carriedSlot.count -= 1;
                                    clearIfEmpty(carriedSlot);
                                } else if (dst.id == carriedSlot.id &&
                                           dst.count < game::Inventory::kMaxStack) {
                                    dst.count += 1;
                                    carriedSlot.count -= 1;
                                    clearIfEmpty(carriedSlot);
                                }
                            }
                        }
                    } else {
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
                                itemDrops.spawn(targetId, dropPos, 1);
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
                    const bool intersectsPlayer =
                        !ghostMode && placeCellIntersectsPlayer(place, camPos);
                    if (place != camCell && !intersectsPlayer) {
                        const auto slot = inventory.hotbarSlot(selectedBlockIndex);
                        if (slot.id != voxel::AIR && slot.count > 0) {
                            if (world.setBlock(place.x, place.y, place.z, slot.id)) {
                                inventory.consumeHotbar(selectedBlockIndex, 1);
                                audio.playPlace(game::soundProfileForBlock(slot.id));
                            }
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

                glClearColor(0.52f, 0.75f, 0.98f, 1.0f);
                glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

                const bool wireframe = debugCfg.renderMode == game::RenderMode::Wireframe;
                glPolygonMode(GL_FRONT_AND_BACK, wireframe ? GL_LINE : GL_FILL);

                shader.use();
                shader.setMat4("uProj", proj);
                shader.setMat4("uView", view);
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
                    std::ostringstream coord;
                    coord << std::fixed << std::setprecision(1) << "XYZ: " << pos.x << ", " << pos.y
                          << ", " << pos.z;
                    hud.render2D(winW, winH, selectedBlockIndex, hotbarIds, hotbarCounts, allIds,
                                 allCounts, inventoryVisible, carriedSlot.id, carriedSlot.count,
                                 static_cast<float>(cursorX), static_cast<float>(cursorY),
                                 (carriedSlot.id == voxel::AIR || carriedSlot.count <= 0)
                                     ? ""
                                     : game::blockName(carriedSlot.id),
                                 (selectedPlaceBlock == voxel::AIR)
                                     ? "Empty Slot"
                                     : game::blockName(selectedPlaceBlock),
                                 lookedAt, modeText, game::biomeHintFromSurface(world, pos),
                                 coord.str(), hudRegistry, atlas);
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

            savePlayerData(worldSelection.path, camera.position(), selectedBlockIndex, ghostMode,
                           inventory);
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
