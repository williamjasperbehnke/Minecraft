#include "app/menus/WorldSelectionMenu.hpp"

#include "gfx/HudRenderer.hpp"

#include <glad/glad.h>
#include <GLFW/glfw3.h>

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace app::menus {
namespace {

struct WorldEntry {
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
    if (gTitleInputState == nullptr || !gTitleInputState->active || codepoint > 127u) {
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

} // namespace

void WorldSelectionMenu::render(gfx::HudRenderer &hud, int width, int height,
                                const std::vector<std::string> &worlds, int selectedWorld,
                                bool createMode, bool editSeed,
                                const std::string &createName, const std::string &createSeed,
                                float cursorX, float cursorY) const {
    (void)hud;
    ui_.begin(width, height);

    const float w = static_cast<float>(width);
    const float h = static_cast<float>(height);
    const float cx = w * 0.5f;

    ui_.drawRect(0.0f, 0.0f, w, h, 0.08f, 0.13f, 0.20f, 1.0f);
    ui_.drawRect(0.0f, h * 0.40f, w, h * 0.60f, 0.17f, 0.28f, 0.38f, 0.72f);
    ui_.drawRect(0.0f, h * 0.80f, w, h * 0.20f, 0.24f, 0.19f, 0.13f, 0.78f);
    for (int i = 0; i < 8; ++i) {
        const float px = 80.0f + static_cast<float>(i) * (w / 8.5f);
        const float py = 86.0f + std::sin(static_cast<float>(i) * 1.2f) * 14.0f;
        ui_.drawRect(px, py, 42.0f, 18.0f, 0.90f, 0.93f, 0.97f, 0.15f);
    }

    const std::string title = "VOXELCRAFT";
    const float titleW = UiMenuRenderer::textWidthPx(title);
    ui_.drawText(cx - titleW * 0.5f + 3.0f, 56.0f, title, 18, 14, 10, 200);
    ui_.drawText(cx - titleW * 0.5f, 53.0f, title, 255, 226, 128, 255);
    const std::string subtitle = "Singleplayer World Select";
    ui_.drawText(cx - UiMenuRenderer::textWidthPx(subtitle) * 0.5f, 82.0f, subtitle, 214, 224,
                 240, 255);

    const float panelW = std::min(760.0f, w - 60.0f);
    const float panelX = cx - panelW * 0.5f;
    const float panelY = 118.0f;
    const float panelH = h - 170.0f;
    ui_.drawRect(panelX, panelY, panelW, panelH, 0.06f, 0.07f, 0.09f, 0.88f);
    ui_.drawRect(panelX + 2.0f, panelY + 2.0f, panelW - 4.0f, panelH - 4.0f, 0.14f, 0.15f, 0.18f,
                 0.90f);
    ui_.drawRect(panelX + 2.0f, panelY + 2.0f, panelW - 4.0f, 34.0f, 0.18f, 0.21f, 0.26f, 0.92f);
    const std::string panelTitle = "Select World";
    ui_.drawText(panelX + panelW * 0.5f - UiMenuRenderer::textWidthPx(panelTitle) * 0.5f,
                 panelY + 14.0f, panelTitle, 244, 246, 252, 255);

    const float rowX = panelX + 18.0f;
    const float rowW = panelW - 36.0f;
    const float rowY0 = panelY + 48.0f;
    const float rowH = 30.0f;
    const float listH = panelH - 130.0f;
    ui_.drawRect(rowX, rowY0 - 2.0f, rowW, listH, 0.09f, 0.10f, 0.12f, 0.78f);
    const int maxRows = static_cast<int>(std::max(1.0f, (listH - 4.0f) / rowH));
    const int total = static_cast<int>(worlds.size());
    int scroll = 0;
    if (selectedWorld >= maxRows) {
        scroll = selectedWorld - maxRows + 1;
    }

    if (worlds.empty()) {
        const std::string emptyText = "No worlds yet. Click New World to begin.";
        ui_.drawText(rowX + rowW * 0.5f - UiMenuRenderer::textWidthPx(emptyText) * 0.5f,
                     rowY0 + 12.0f, emptyText, 222, 226, 236, 255);
    } else {
        const int visible = std::min(maxRows, total - scroll);
        for (int i = 0; i < visible; ++i) {
            const int idx = scroll + i;
            const float y = rowY0 + static_cast<float>(i) * rowH;
            const bool selected = idx == selectedWorld;
            const bool hover = cursorX >= rowX && cursorX <= (rowX + rowW) && cursorY >= y &&
                               cursorY <= (y + rowH - 4.0f);
            if (selected) {
                ui_.drawRect(rowX + 2.0f, y + 1.0f, rowW - 4.0f, rowH - 6.0f, 0.70f, 0.56f, 0.24f,
                             0.96f);
                ui_.drawRect(rowX + 4.0f, y + 3.0f, rowW - 8.0f, rowH - 10.0f, 0.88f, 0.70f, 0.33f,
                             0.95f);
            } else if (hover) {
                ui_.drawRect(rowX + 2.0f, y + 1.0f, rowW - 4.0f, rowH - 6.0f, 0.24f, 0.29f, 0.36f,
                             0.94f);
                ui_.drawRect(rowX + 3.0f, y + 2.0f, rowW - 6.0f, rowH - 8.0f, 0.32f, 0.38f, 0.46f,
                             0.94f);
            } else {
                ui_.drawRect(rowX + 2.0f, y + 1.0f, rowW - 4.0f, rowH - 6.0f, 0.13f, 0.14f, 0.17f,
                             0.90f);
            }
            ui_.drawText(rowX + rowW * 0.5f - UiMenuRenderer::textWidthPx(worlds[idx]) * 0.5f,
                         y + 9.0f, worlds[idx], 245, 246, 250, 255);
        }
    }

    struct Btn {
        float x;
        float y;
        float w;
        float h;
        const char *label;
    };
    const float btnY = panelY + panelH - 68.0f;
    const float btnW = (rowW - 12.0f) * 0.25f;
    const float btnH = 36.0f;
    const Btn buttons[4] = {
        {rowX, btnY, btnW, btnH, "Load Selected"},
        {rowX + (btnW + 4.0f), btnY, btnW, btnH, "New World"},
        {rowX + 2.0f * (btnW + 4.0f), btnY, btnW, btnH, "Refresh"},
        {rowX + 3.0f * (btnW + 4.0f), btnY, btnW, btnH, "Quit"},
    };
    for (int i = 0; i < 4; ++i) {
        const Btn &btn = buttons[i];
        const bool hover = cursorX >= btn.x && cursorX <= (btn.x + btn.w) && cursorY >= btn.y &&
                           cursorY <= (btn.y + btn.h);
        const bool active = (i == 1 && createMode);
        if (active || hover) {
            ui_.drawRect(btn.x, btn.y, btn.w, btn.h, 0.52f, 0.39f, 0.16f, 0.98f);
            ui_.drawRect(btn.x + 2.0f, btn.y + 2.0f, btn.w - 4.0f, btn.h - 4.0f, 0.82f, 0.64f,
                         0.30f, 0.95f);
        } else {
            ui_.drawRect(btn.x, btn.y, btn.w, btn.h, 0.11f, 0.11f, 0.13f, 0.95f);
            ui_.drawRect(btn.x + 1.0f, btn.y + 1.0f, btn.w - 2.0f, btn.h - 2.0f, 0.21f, 0.22f,
                         0.25f, 0.95f);
        }
        const std::string label(btn.label);
        const float labelX = btn.x + btn.w * 0.5f - UiMenuRenderer::textWidthPx(label) * 0.5f;
        ui_.drawText(labelX, btn.y + 13.0f, label, 245, 246, 250, 255);
    }

    if (createMode) {
        ui_.drawRect(0.0f, 0.0f, w, h, 0.02f, 0.03f, 0.04f, 0.52f);
        const float mw = std::min(560.0f, w - 80.0f);
        const float mh = 238.0f;
        const float mx = cx - mw * 0.5f;
        const float my = h * 0.5f - mh * 0.5f;
        ui_.drawRect(mx, my, mw, mh, 0.08f, 0.09f, 0.11f, 0.98f);
        ui_.drawRect(mx + 2.0f, my + 2.0f, mw - 4.0f, mh - 4.0f, 0.16f, 0.17f, 0.21f, 0.98f);
        ui_.drawRect(mx + 2.0f, my + 2.0f, mw - 4.0f, 34.0f, 0.22f, 0.25f, 0.31f, 0.98f);
        const std::string createTitle = "Create New World";
        ui_.drawText(mx + mw * 0.5f - UiMenuRenderer::textWidthPx(createTitle) * 0.5f, my + 14.0f,
                     createTitle, 246, 248, 252, 255);

        const float fieldX = mx + 16.0f;
        const float fieldW = mw - 32.0f;
        const float nameY = my + 58.0f;
        const float seedY = my + 104.0f;
        const std::string nameLabel = "Name";
        const std::string seedLabel = "Seed";
        ui_.drawText(fieldX + fieldW * 0.5f - UiMenuRenderer::textWidthPx(nameLabel) * 0.5f,
                     nameY - 12.0f, nameLabel, 214, 220, 234, 255);
        ui_.drawText(fieldX + fieldW * 0.5f - UiMenuRenderer::textWidthPx(seedLabel) * 0.5f,
                     seedY - 12.0f, seedLabel, 214, 220, 234, 255);

        const bool nameHover = cursorX >= fieldX && cursorX <= (fieldX + fieldW) &&
                               cursorY >= nameY && cursorY <= (nameY + 30.0f);
        const bool seedHover = cursorX >= fieldX && cursorX <= (fieldX + fieldW) &&
                               cursorY >= seedY && cursorY <= (seedY + 30.0f);
        ui_.drawRect(fieldX, nameY, fieldW, 30.0f, (editSeed ? 0.18f : 0.45f),
                     (editSeed ? 0.20f : 0.34f), 0.24f, 0.95f);
        ui_.drawRect(fieldX + 1.0f, nameY + 1.0f, fieldW - 2.0f, 28.0f, nameHover ? 0.30f : 0.23f,
                     nameHover ? 0.34f : 0.26f, 0.39f, 0.95f);
        ui_.drawRect(fieldX, seedY, fieldW, 30.0f, (editSeed ? 0.45f : 0.18f),
                     (editSeed ? 0.34f : 0.20f), 0.24f, 0.95f);
        ui_.drawRect(fieldX + 1.0f, seedY + 1.0f, fieldW - 2.0f, 28.0f, seedHover ? 0.30f : 0.23f,
                     seedHover ? 0.34f : 0.26f, 0.39f, 0.95f);

        const std::string nameLine = createName + (!editSeed ? "_" : "");
        const std::string seedLine = createSeed + (editSeed ? "_" : "");
        ui_.drawText(fieldX + fieldW * 0.5f - UiMenuRenderer::textWidthPx(nameLine) * 0.5f,
                     nameY + 10.0f, nameLine, 238, 242, 250, 255);
        ui_.drawText(fieldX + fieldW * 0.5f - UiMenuRenderer::textWidthPx(seedLine) * 0.5f,
                     seedY + 10.0f, seedLine, 238, 242, 250, 255);

        const float cby = my + mh - 48.0f;
        const float cbw = (fieldW - 8.0f) * 0.5f;
        const Btn createBtn{fieldX, cby, cbw, 34.0f, "Create & Play"};
        const Btn cancelBtn{fieldX + cbw + 8.0f, cby, cbw, 34.0f, "Cancel"};
        const Btn modalButtons[2] = {createBtn, cancelBtn};
        for (int i = 0; i < 2; ++i) {
            const Btn &btn = modalButtons[i];
            const bool hover = cursorX >= btn.x && cursorX <= (btn.x + btn.w) &&
                               cursorY >= btn.y && cursorY <= (btn.y + btn.h);
            if (i == 0) {
                ui_.drawRect(btn.x, btn.y, btn.w, btn.h, hover ? 0.54f : 0.40f,
                             hover ? 0.41f : 0.30f, 0.15f, 0.98f);
                ui_.drawRect(btn.x + 2.0f, btn.y + 2.0f, btn.w - 4.0f, btn.h - 4.0f,
                             hover ? 0.83f : 0.68f, hover ? 0.65f : 0.52f, 0.28f, 0.96f);
            } else {
                ui_.drawRect(btn.x, btn.y, btn.w, btn.h, hover ? 0.26f : 0.15f,
                             hover ? 0.18f : 0.13f, hover ? 0.18f : 0.14f, 0.98f);
                ui_.drawRect(btn.x + 2.0f, btn.y + 2.0f, btn.w - 4.0f, btn.h - 4.0f,
                             hover ? 0.36f : 0.24f, hover ? 0.25f : 0.20f,
                             hover ? 0.26f : 0.22f, 0.95f);
            }
            const std::string label(btn.label);
            const float tx = btn.x + btn.w * 0.5f - UiMenuRenderer::textWidthPx(label) * 0.5f;
            ui_.drawText(tx, btn.y + 12.0f, label, 245, 246, 250, 255);
        }
    }

    const std::string flowText =
        "Flow: Select world -> Load. New World opens a modal with Create/Cancel.";
    ui_.drawText(panelX + panelW * 0.5f - UiMenuRenderer::textWidthPx(flowText) * 0.5f,
                 panelY + panelH - 14.0f, flowText, 198, 206, 222, 255);
    ui_.end();
}

WorldSelection WorldSelectionMenu::run(GLFWwindow *window, gfx::HudRenderer &hud) {
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
        render(hud, winW, winH, worldLines, selectedWorld, createMode, editSeed, createName,
               createSeed, static_cast<float>(mx), static_cast<float>(my));
        glfwSwapBuffers(window);
        prevLeftMouse = leftMouse;
    }

    glfwSetCharCallback(window, nullptr);
    gTitleInputState = nullptr;
    return {};
}

} // namespace app::menus
