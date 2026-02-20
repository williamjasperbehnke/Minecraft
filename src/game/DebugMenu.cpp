#include "game/DebugMenu.hpp"

#include "core/Logger.hpp"
#include "world/World.hpp"

#include <glad/glad.h>
#include <GLFW/glfw3.h>
#include <stb_easy_font.h>

#include <algorithm>
#include <cstdio>
#include <stdexcept>
#include <string>

namespace game {
namespace {

unsigned int compileShader(unsigned int type, const char *src) {
    unsigned int shader = glCreateShader(type);
    glShaderSource(shader, 1, &src, nullptr);
    glCompileShader(shader);
    int ok = 0;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[1024];
        glGetShaderInfoLog(shader, sizeof(log), nullptr, log);
        throw std::runtime_error(std::string("DebugMenu shader compile failed: ") + log);
    }
    return shader;
}

float textWidthPx(const std::string &text) {
    return static_cast<float>(stb_easy_font_width(const_cast<char *>(text.c_str())));
}

float textHeightPx(const std::string &text) {
    return static_cast<float>(stb_easy_font_height(const_cast<char *>(text.c_str())));
}

const char *moonPhaseName(float t01) {
    const int idx = std::clamp(static_cast<int>(std::floor(std::clamp(t01, 0.0f, 1.0f) * 8.0f)) % 8, 0, 7);
    switch (idx) {
    case 0:
        return "New";
    case 1:
        return "Waxing Crescent";
    case 2:
        return "First Quarter";
    case 3:
        return "Waxing Gibbous";
    case 4:
        return "Full";
    case 5:
        return "Waning Gibbous";
    case 6:
        return "Last Quarter";
    case 7:
        return "Waning Crescent";
    default:
        return "New";
    }
}

bool isStepControl(int id) {
    return id >= 0 && id <= 6;
}

bool isToggleControl(int id) {
    return id >= 7 && id <= 12;
}

bool isSliderControl(int id) {
    return id >= 13 && id <= 14;
}

const char *controlLabel(int id) {
    switch (id) {
    case 0:
        return "Render Mode";
    case 1:
        return "FOV";
    case 2:
        return "Move Speed";
    case 3:
        return "Mouse Sensitivity";
    case 4:
        return "Raycast Distance";
    case 5:
        return "Load Radius";
    case 6:
        return "Unload Radius";
    case 7:
        return "Chunk Borders";
    case 8:
        return "Time Override";
    case 9:
        return "Smooth Lighting";
    case 10:
        return "Clouds";
    case 11:
        return "Stars";
    case 12:
        return "Fog";
    case 13:
        return "Time Of Day";
    case 14:
        return "Moon Phase";
    default:
        return "";
    }
}

} // namespace

bool DebugMenu::keyPressed(GLFWwindow *window, int key) {
    const bool down = glfwGetKey(window, key) == GLFW_PRESS;
    const auto it = std::find(keysDown_.begin(), keysDown_.end(), key);
    const bool wasDown = it != keysDown_.end();
    if (down && !wasDown) {
        keysDown_.push_back(key);
        return true;
    }
    if (!down && wasDown) {
        keysDown_.erase(it);
    }
    return false;
}

const char *DebugMenu::renderModeName(RenderMode mode) {
    switch (mode) {
    case RenderMode::Textured:
        return "Textured";
    case RenderMode::Flat:
        return "Flat";
    case RenderMode::Wireframe:
        return "Wireframe";
    }
    return "Unknown";
}

void DebugMenu::printMenuHint() const {
    core::Logger::instance().info("Debug UI: F1 toggle panel, mouse click +/- "
                                  "buttons, F3 cycle render mode.");
}

void DebugMenu::initRenderer() {
    if (rendererReady_) {
        return;
    }

    const char *vs = R"(
        #version 330 core
        layout(location = 0) in vec2 aPos;
        layout(location = 1) in vec4 aColor;
        uniform vec2 uScreen;
        out vec4 vColor;
        void main() {
            vec2 ndc = vec2((aPos.x / uScreen.x) * 2.0 - 1.0,
                            1.0 - (aPos.y / uScreen.y) * 2.0);
            gl_Position = vec4(ndc, 0.0, 1.0);
            vColor = aColor;
        }
    )";

    const char *fs = R"(
        #version 330 core
        in vec4 vColor;
        out vec4 FragColor;
        void main() {
            FragColor = vColor;
        }
    )";

    const unsigned int vshader = compileShader(GL_VERTEX_SHADER, vs);
    const unsigned int fshader = compileShader(GL_FRAGMENT_SHADER, fs);

    shader_ = glCreateProgram();
    glAttachShader(shader_, vshader);
    glAttachShader(shader_, fshader);
    glLinkProgram(shader_);

    int ok = 0;
    glGetProgramiv(shader_, GL_LINK_STATUS, &ok);
    glDeleteShader(vshader);
    glDeleteShader(fshader);
    if (!ok) {
        char log[1024];
        glGetProgramInfoLog(shader_, sizeof(log), nullptr, log);
        throw std::runtime_error(std::string("DebugMenu shader link failed: ") + log);
    }

    glGenVertexArrays(1, &vao_);
    glGenBuffers(1, &vbo_);

    glBindVertexArray(vao_);
    glBindBuffer(GL_ARRAY_BUFFER, vbo_);
    glBufferData(GL_ARRAY_BUFFER, 0, nullptr, GL_DYNAMIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, sizeof(UiVertex), reinterpret_cast<void *>(0));
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 4, GL_FLOAT, GL_FALSE, sizeof(UiVertex),
                          reinterpret_cast<void *>(2 * sizeof(float)));

    rendererReady_ = true;
}

void DebugMenu::drawRect(float x, float y, float w, float h, float r, float g, float b, float a) {
    const UiVertex v0{x, y, r, g, b, a};
    const UiVertex v1{x + w, y, r, g, b, a};
    const UiVertex v2{x + w, y + h, r, g, b, a};
    const UiVertex v3{x, y + h, r, g, b, a};
    verts_.push_back(v0);
    verts_.push_back(v1);
    verts_.push_back(v2);
    verts_.push_back(v0);
    verts_.push_back(v2);
    verts_.push_back(v3);
}

void DebugMenu::drawText(float x, float y, const std::string &text, unsigned char r,
                         unsigned char g, unsigned char b, unsigned char a) {
    char buffer[99999];
    unsigned char color[4] = {r, g, b, a};
    const int quads =
        stb_easy_font_print(x, y, const_cast<char *>(text.c_str()), color, buffer, sizeof(buffer));

    struct StbVert {
        float x;
        float y;
        float z;
        unsigned char c[4];
    };

    const StbVert *q = reinterpret_cast<const StbVert *>(buffer);
    for (int i = 0; i < quads; ++i) {
        const StbVert &a0 = q[i * 4 + 0];
        const StbVert &a1 = q[i * 4 + 1];
        const StbVert &a2 = q[i * 4 + 2];
        const StbVert &a3 = q[i * 4 + 3];

        auto push = [&](const StbVert &sv) {
            verts_.push_back(UiVertex{sv.x, sv.y, static_cast<float>(sv.c[0]) / 255.0f,
                                      static_cast<float>(sv.c[1]) / 255.0f,
                                      static_cast<float>(sv.c[2]) / 255.0f,
                                      static_cast<float>(sv.c[3]) / 255.0f});
        };

        push(a0);
        push(a1);
        push(a2);
        push(a0);
        push(a2);
        push(a3);
    }
}

void DebugMenu::applyClick(float mx, float my, DebugConfig &cfg) {
    const float contentMy = my + scrollOffset_;
    for (int displayIdx = 0; displayIdx < static_cast<int>(visibleRows_.size()) &&
                             displayIdx < static_cast<int>(rowY_.size());
         ++displayIdx) {
        const int id = visibleRows_[displayIdx];
        const float y = rowY_[displayIdx];
        if (id == 13) {
            const Rect slider{panelX_ + 336.0f, y + 13.0f, 220.0f, 12.0f};
            if (slider.contains(mx, contentMy)) {
                cfg.timeOfDay01 = std::clamp((mx - slider.x) / slider.w, 0.0f, 1.0f);
                cfg.overrideTime = true;
                draggingTimeSlider_ = true;
                return;
            }
        } else if (id == 14) {
            const Rect slider{panelX_ + 336.0f, y + 13.0f, 220.0f, 12.0f};
            if (slider.contains(mx, contentMy)) {
                cfg.moonPhase01 = std::clamp((mx - slider.x) / slider.w, 0.0f, 1.0f);
                draggingMoonSlider_ = true;
                return;
            }
        } else if (isToggleControl(id)) {
            const Rect toggle{panelX_ + panelW_ - 136.0f, y + 6.0f, 100.0f, 28.0f};
            if (!toggle.contains(mx, contentMy)) {
                continue;
            }
            switch (id) {
            case 7:
                cfg.showChunkBorders = !cfg.showChunkBorders;
                return;
            case 8:
                cfg.overrideTime = !cfg.overrideTime;
                return;
            case 9:
                cfg.smoothLighting = !cfg.smoothLighting;
                return;
            case 10:
                cfg.showClouds = !cfg.showClouds;
                return;
            case 11:
                cfg.showStars = !cfg.showStars;
                return;
            case 12:
                cfg.showFog = !cfg.showFog;
                return;
            default:
                break;
            }
        }
    }

    int idx = -1;
    int dir = 0;
    if (hitStepButton(mx, my, idx, dir)) {
        adjustControl(cfg, idx, dir);
        return;
    }
}

bool DebugMenu::hitStepButton(float mx, float my, int &idx, int &dir) const {
    idx = -1;
    dir = 0;
    const float contentMy = my + scrollOffset_;
    for (int displayIdx = 0; displayIdx < static_cast<int>(visibleRows_.size()) &&
                             displayIdx < static_cast<int>(rowY_.size());
         ++displayIdx) {
        const int id = visibleRows_[displayIdx];
        if (!isStepControl(id)) {
            continue;
        }
        const float y = rowY_[displayIdx];
        const Rect minus{panelX_ + panelW_ - 134.0f, y + 5.0f, 46.0f, 30.0f};
        const Rect plus{panelX_ + panelW_ - 82.0f, y + 5.0f, 46.0f, 30.0f};
        if (minus.contains(mx, contentMy)) {
            idx = id;
            dir = -1;
            return true;
        }
        if (plus.contains(mx, contentMy)) {
            idx = id;
            dir = +1;
            return true;
        }
    }
    return false;
}

void DebugMenu::adjustControl(DebugConfig &cfg, int idx, int dir) const {
    switch (idx) {
    case 0: {
        const int next = (static_cast<int>(cfg.renderMode) + (dir > 0 ? 1 : 2)) % 3;
        cfg.renderMode = static_cast<RenderMode>(next);
        break;
    }
    case 1:
        cfg.fov = std::clamp(cfg.fov + dir * 2.0f, 40.0f, 110.0f);
        break;
    case 2:
        cfg.moveSpeed = std::clamp(cfg.moveSpeed + dir * 1.0f, 2.0f, 60.0f);
        break;
    case 3:
        cfg.mouseSensitivity = std::clamp(cfg.mouseSensitivity + dir * 0.01f, 0.02f, 0.5f);
        break;
    case 4:
        cfg.raycastDistance = std::clamp(cfg.raycastDistance + dir * 1.0f, 2.0f, 24.0f);
        break;
    case 5:
        cfg.loadRadius = std::clamp(cfg.loadRadius + dir, 2, 16);
        cfg.unloadRadius = std::max(cfg.unloadRadius, cfg.loadRadius + 1);
        break;
    case 6:
        cfg.unloadRadius = std::clamp(cfg.unloadRadius + dir, cfg.loadRadius + 1, 20);
        break;
    default:
        break;
    }
}

void DebugMenu::update(GLFWwindow *window, DebugConfig &cfg, const world::WorldDebugStats &stats,
                       float fps, float frameMs) {
    if (keyPressed(window, GLFW_KEY_F1)) {
        open_ = !open_;
        if (open_) {
            printMenuHint();
        }
    }

    if (keyPressed(window, GLFW_KEY_F3)) {
        const int next = (static_cast<int>(cfg.renderMode) + 1) % 3;
        cfg.renderMode = static_cast<RenderMode>(next);
    }

    int winW = 1;
    int winH = 1;
    glfwGetWindowSize(window, &winW, &winH);
    panelW_ = std::min(760.0f, static_cast<float>(winW) - 80.0f);
    panelH_ = std::min(620.0f, static_cast<float>(winH) - 80.0f);
    panelX_ = (static_cast<float>(winW) - panelW_) * 0.5f;
    panelY_ = (static_cast<float>(winH) - panelH_) * 0.5f;

    lastCfg_ = cfg;

    infoLines_.clear();
    char line[256];
    std::snprintf(line, sizeof(line), "FPS: %.1f  Frame: %.2f ms", fps, frameMs);
    infoLines_.push_back(line);
    std::snprintf(line, sizeof(line),
                  "Chunks Loaded: %d  Meshed: %d  Pending Load: %d  Pending Mesh: %d",
                  stats.loadedChunks, stats.meshedChunks, stats.pendingLoad, stats.pendingRemesh);
    infoLines_.push_back(line);
    std::snprintf(line, sizeof(line), "Triangles: %d", stats.totalTriangles);
    infoLines_.push_back(line);
    infoLines_.push_back("Mouse: use tabs, click +/- and switches, drag Time/Moon sliders");

    rowY_.clear();
    visibleRows_.clear();
    switch (selectedTab_) {
    case 0:
        visibleRows_ = {0, 1, 2, 3, 4};
        break;
    case 1:
        visibleRows_ = {5, 6, 7, 9};
        break;
    case 2:
    default:
        visibleRows_ = {8, 10, 11, 12, 13, 14};
        break;
    }

    const float startY = panelY_ + 102.0f;
    const float rowH = 44.0f;
    for (int i = 0; i < static_cast<int>(visibleRows_.size()); ++i) {
        rowY_.push_back(startY + i * rowH);
    }
    const float contentTop = panelY_ + 102.0f;
    const float contentBottom = panelY_ + panelH_ - 14.0f;
    viewHeight_ = std::max(1.0f, contentBottom - contentTop);
    const float infoStartY = startY + static_cast<float>(visibleRows_.size()) * rowH + 10.0f;
    const float infoHeight = 30.0f + static_cast<float>(infoLines_.size()) * 16.0f;
    contentHeight_ = (infoStartY + infoHeight) - contentTop;
    const float maxScroll = std::max(0.0f, contentHeight_ - viewHeight_);
    scrollOffset_ = std::clamp(scrollOffset_, 0.0f, maxScroll);

    double mx = 0.0;
    double my = 0.0;
    glfwGetCursorPos(window, &mx, &my);
    mouseX_ = static_cast<float>(mx);
    mouseY_ = static_cast<float>(my);
    const bool leftDown = glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_LEFT) == GLFW_PRESS;

    if (open_ && leftDown && !prevMouseLeft_) {
        const float tabsY = panelY_ + 52.0f;
        const float tabsH = 34.0f;
        const float tabsGap = 8.0f;
        const float tabW = (panelW_ - 28.0f - tabsGap * 2.0f) / 3.0f;
        for (int ti = 0; ti < 3; ++ti) {
            const Rect tab{panelX_ + 14.0f + ti * (tabW + tabsGap), tabsY, tabW, tabsH};
            if (tab.contains(mouseX_, mouseY_)) {
                if (selectedTab_ != ti) {
                    selectedTab_ = ti;
                    scrollOffset_ = 0.0f;
                }
                prevMouseLeft_ = leftDown;
                return;
            }
        }

        const Rect contentRect{panelX_ + 14.0f, contentTop, panelW_ - 52.0f, viewHeight_};
        const Rect scrollTrack{panelX_ + panelW_ - 24.0f, contentTop, 10.0f, viewHeight_};
        if (maxScroll > 0.0f && scrollTrack.contains(mouseX_, mouseY_)) {
            const float thumbH = std::clamp((viewHeight_ / contentHeight_) * viewHeight_, 32.0f, viewHeight_);
            const float thumbTravel = std::max(1.0f, viewHeight_ - thumbH);
            const float thumbY = contentTop + (scrollOffset_ / maxScroll) * thumbTravel;
            const Rect thumb{scrollTrack.x - 2.0f, thumbY, scrollTrack.w + 4.0f, thumbH};
            if (thumb.contains(mouseX_, mouseY_)) {
                draggingScrollThumb_ = true;
                scrollGrabOffsetY_ = mouseY_ - thumbY;
            } else {
                const float t = std::clamp((mouseY_ - contentTop - thumbH * 0.5f) / thumbTravel, 0.0f, 1.0f);
                scrollOffset_ = t * maxScroll;
            }
        } else if (contentRect.contains(mouseX_, mouseY_)) {
            int idx = -1;
            int dir = 0;
            if (hitStepButton(static_cast<float>(mx), static_cast<float>(my), idx, dir)) {
                adjustControl(cfg, idx, dir);
                heldButtonRow_ = idx;
                heldButtonDir_ = dir;
                nextButtonRepeatTime_ = glfwGetTime() + 0.33;
            } else {
                heldButtonRow_ = -1;
                heldButtonDir_ = 0;
                applyClick(static_cast<float>(mx), static_cast<float>(my), cfg);
            }
        }
    }
    if (open_ && leftDown && draggingScrollThumb_ && maxScroll > 0.0f) {
        const float thumbH = std::clamp((viewHeight_ / contentHeight_) * viewHeight_, 32.0f, viewHeight_);
        const float thumbTravel = std::max(1.0f, viewHeight_ - thumbH);
        const float thumbY = std::clamp(mouseY_ - scrollGrabOffsetY_, contentTop, contentTop + thumbTravel);
        const float t = (thumbY - contentTop) / thumbTravel;
        scrollOffset_ = t * maxScroll;
    }

    if (open_ && leftDown && draggingTimeSlider_ && !rowY_.empty()) {
        int displayIdx = -1;
        for (int i = 0; i < static_cast<int>(visibleRows_.size()); ++i) {
            if (visibleRows_[i] == 13) {
                displayIdx = i;
                break;
            }
        }
        if (displayIdx >= 0 && displayIdx < static_cast<int>(rowY_.size())) {
            const float y = rowY_[displayIdx];
            const Rect slider{panelX_ + 336.0f, y + 13.0f, 220.0f, 12.0f};
            const float t = std::clamp((mouseX_ - slider.x) / slider.w, 0.0f, 1.0f);
            cfg.timeOfDay01 = t;
            cfg.overrideTime = true;
        }
    }
    if (open_ && leftDown && draggingMoonSlider_ && !rowY_.empty()) {
        int displayIdx = -1;
        for (int i = 0; i < static_cast<int>(visibleRows_.size()); ++i) {
            if (visibleRows_[i] == 14) {
                displayIdx = i;
                break;
            }
        }
        if (displayIdx >= 0 && displayIdx < static_cast<int>(rowY_.size())) {
            const float y = rowY_[displayIdx];
            const Rect slider{panelX_ + 336.0f, y + 13.0f, 220.0f, 12.0f};
            cfg.moonPhase01 = std::clamp((mouseX_ - slider.x) / slider.w, 0.0f, 1.0f);
        }
    }
    if (open_ && leftDown && heldButtonRow_ >= 0 && heldButtonDir_ != 0 && !draggingScrollThumb_ &&
        !draggingTimeSlider_ && !draggingMoonSlider_) {
        int idx = -1;
        int dir = 0;
        if (hitStepButton(mouseX_, mouseY_, idx, dir) && idx == heldButtonRow_ &&
            dir == heldButtonDir_) {
            double now = glfwGetTime();
            while (now >= nextButtonRepeatTime_) {
                adjustControl(cfg, heldButtonRow_, heldButtonDir_);
                nextButtonRepeatTime_ += 0.075;
            }
        } else {
            heldButtonRow_ = -1;
            heldButtonDir_ = 0;
        }
    }
    if (!leftDown) {
        draggingTimeSlider_ = false;
        draggingMoonSlider_ = false;
        draggingScrollThumb_ = false;
        heldButtonRow_ = -1;
        heldButtonDir_ = 0;
    }
    prevMouseLeft_ = leftDown;
}

void DebugMenu::render(int width, int height) {
    if (!open_) {
        return;
    }

    initRenderer();
    verts_.clear();

    drawRect(panelX_ - 2.0f, panelY_ - 2.0f, panelW_ + 4.0f, panelH_ + 4.0f, 0.01f, 0.01f, 0.02f,
             0.70f);
    drawRect(panelX_, panelY_, panelW_, panelH_, 0.06f, 0.07f, 0.09f, 0.92f);
    drawRect(panelX_ + 2.0f, panelY_ + 2.0f, panelW_ - 4.0f, panelH_ - 4.0f, 0.14f, 0.15f, 0.18f,
             0.94f);
    drawRect(panelX_ + 2.0f, panelY_ + 2.0f, panelW_ - 4.0f, 42.0f, 0.20f, 0.24f, 0.30f, 0.96f);
    const std::string heading = "DEBUG MENU";
    drawText(panelX_ + panelW_ * 0.5f - textWidthPx(heading) * 0.5f, panelY_ + 14.0f, heading, 236,
             242, 255, 255);

    const float startY = panelY_ + 102.0f;
    const float rowH = 44.0f;
    const float contentTop = panelY_ + 102.0f;
    const float contentBottom = panelY_ + panelH_ - 14.0f;

    const char *tabs[] = {"Player", "World", "Sky"};
    const float tabsY = panelY_ + 52.0f;
    const float tabsH = 34.0f;
    const float tabsGap = 8.0f;
    const float tabW = (panelW_ - 28.0f - tabsGap * 2.0f) / 3.0f;
    for (int ti = 0; ti < 3; ++ti) {
        const Rect tab{panelX_ + 14.0f + ti * (tabW + tabsGap), tabsY, tabW, tabsH};
        const bool selected = selectedTab_ == ti;
        const bool hover = tab.contains(mouseX_, mouseY_);
        drawRect(tab.x, tab.y, tab.w, tab.h, selected ? 0.32f : 0.12f, selected ? 0.26f : 0.15f,
                 selected ? 0.16f : 0.20f, hover ? 0.98f : 0.93f);
        drawRect(tab.x + 1.0f, tab.y + 1.0f, tab.w - 2.0f, tab.h - 2.0f,
                 selected ? 0.60f : 0.20f, selected ? 0.45f : 0.24f, selected ? 0.24f : 0.30f,
                 0.95f);
        const std::string t(tabs[ti]);
        const float tx = tab.x + (tab.w - textWidthPx(t)) * 0.5f;
        const float ty = tab.y + (tab.h - textHeightPx(t)) * 0.5f;
        drawText(tx, ty, t, selected ? 255 : 224, selected ? 246 : 230, selected ? 212 : 244, 255);
    }

    auto rowY = [&](int i) { return startY + static_cast<float>(i) * rowH - scrollOffset_; };

    for (int displayIdx = 0; displayIdx < static_cast<int>(visibleRows_.size()); ++displayIdx) {
        const int i = visibleRows_[displayIdx];
        const float y = rowY(displayIdx);
        drawRect(panelX_ + 14.0f, y, panelW_ - 28.0f, rowH - 6.0f, 0.10f, 0.12f, 0.16f, 0.86f);
        if (isStepControl(i)) {
            const Rect minus{panelX_ + panelW_ - 134.0f, y + 5.0f, 46.0f, 30.0f};
            const Rect plus{panelX_ + panelW_ - 82.0f, y + 5.0f, 46.0f, 30.0f};
            const bool minusHover = minus.contains(mouseX_, mouseY_);
            const bool plusHover = plus.contains(mouseX_, mouseY_);

            auto drawButton = [&](const Rect &r, bool hover, const char *symbol) {
                if (hover) {
                    drawRect(r.x, r.y, r.w, r.h, 0.55f, 0.41f, 0.17f, 0.98f);
                    drawRect(r.x + 2.0f, r.y + 2.0f, r.w - 4.0f, r.h - 4.0f, 0.84f, 0.66f, 0.32f,
                             0.96f);
                } else {
                    drawRect(r.x, r.y, r.w, r.h, 0.16f, 0.18f, 0.22f, 0.98f);
                    drawRect(r.x + 1.0f, r.y + 1.0f, r.w - 2.0f, r.h - 2.0f, 0.23f, 0.26f, 0.32f,
                             0.95f);
                    drawRect(r.x + 3.0f, r.y + 3.0f, r.w - 6.0f, r.h - 6.0f, 0.18f, 0.21f, 0.27f,
                             0.94f);
                }
                const std::string s(symbol);
                const float tx = r.x + (r.w - textWidthPx(s)) * 0.5f;
                const float ty = r.y + (r.h - textHeightPx(s)) * 0.5f;
                drawText(tx, ty, s, 246, 248, 252, 255);
            };

            drawButton(minus, minusHover, "-");
            drawButton(plus, plusHover, "+");
        } else if (isToggleControl(i)) {
            const bool on = (i == 7) ? lastCfg_.showChunkBorders
                            : (i == 8)  ? lastCfg_.overrideTime
                            : (i == 9)  ? lastCfg_.smoothLighting
                            : (i == 10) ? lastCfg_.showClouds
                            : (i == 11) ? lastCfg_.showStars
                                        : lastCfg_.showFog;
            const Rect toggle{panelX_ + panelW_ - 136.0f, y + 6.0f, 100.0f, 28.0f};
            const bool hover = toggle.contains(mouseX_, mouseY_);
            drawRect(toggle.x, toggle.y, toggle.w, toggle.h, 0.10f, 0.12f, 0.16f, hover ? 0.99f : 0.95f);
            drawRect(toggle.x + 1.0f, toggle.y + 1.0f, toggle.w - 2.0f, toggle.h - 2.0f, 0.18f, 0.21f,
                     0.27f, 0.98f);
            drawRect(toggle.x + 2.0f, toggle.y + 2.0f, toggle.w - 4.0f, toggle.h - 4.0f,
                     on ? 0.20f : 0.14f, on ? 0.43f : 0.16f, on ? 0.28f : 0.20f, 0.95f);

            const float knobW = 40.0f;
            const float knobX = on ? (toggle.x + toggle.w - knobW - 4.0f) : (toggle.x + 4.0f);
            drawRect(knobX, toggle.y + 4.0f, knobW, toggle.h - 8.0f, 0.80f, 0.84f, 0.90f, 0.98f);
            drawRect(knobX + 1.0f, toggle.y + 5.0f, knobW - 2.0f, toggle.h - 10.0f, 0.93f, 0.95f,
                     0.98f, 0.98f);

            const std::string offLabel = "OFF";
            const std::string onLabel = "ON";
            const float offX = toggle.x + 12.0f;
            const float onX = toggle.x + toggle.w - textWidthPx(onLabel) - 12.0f;
            const float ty = toggle.y + (toggle.h - textHeightPx(onLabel)) * 0.5f;
            drawText(offX, ty, offLabel, on ? 120 : 235, on ? 134 : 242, on ? 154 : 250, 255);
            drawText(onX, ty, onLabel, on ? 235 : 120, on ? 246 : 134, on ? 208 : 154, 255);
        } else {
            const Rect slider{panelX_ + 336.0f, y + 13.0f, 220.0f, 12.0f};
            drawRect(slider.x, slider.y, slider.w, slider.h, 0.15f, 0.18f, 0.22f, 0.95f);
            drawRect(slider.x + 1.0f, slider.y + 1.0f, slider.w - 2.0f, slider.h - 2.0f, 0.22f,
                     0.26f, 0.32f, 0.95f);
            const float t = (i == 13) ? std::clamp(lastCfg_.timeOfDay01, 0.0f, 1.0f)
                                      : std::clamp(lastCfg_.moonPhase01, 0.0f, 1.0f);
            const float knobX = slider.x + t * slider.w;
            drawRect(slider.x, slider.y, t * slider.w, slider.h, 0.46f, 0.58f, 0.84f, 0.85f);
            drawRect(knobX - 4.0f, slider.y - 3.0f, 8.0f, slider.h + 6.0f, 0.95f, 0.86f, 0.42f,
                     0.96f);
        }

        drawText(panelX_ + 24.0f, y + 14.0f, controlLabel(i), 224, 232, 246, 255);
    }

    // Live values.
    char value[64];
    auto drawValue = [&](int controlId, float x, const std::string &text) {
        int displayIdx = -1;
        for (int i = 0; i < static_cast<int>(visibleRows_.size()); ++i) {
            if (visibleRows_[i] == controlId) {
                displayIdx = i;
                break;
            }
        }
        if (displayIdx < 0) {
            return;
        }
        drawText(x, rowY(displayIdx) + 14.0f, text, 160, 200, 255, 255);
    };
    drawValue(0, panelX_ + 336.0f, renderModeName(lastCfg_.renderMode));
    std::snprintf(value, sizeof(value), "%.1f", lastCfg_.fov);
    drawValue(1, panelX_ + 336.0f, value);
    std::snprintf(value, sizeof(value), "%.1f", lastCfg_.moveSpeed);
    drawValue(2, panelX_ + 336.0f, value);
    std::snprintf(value, sizeof(value), "%.2f", lastCfg_.mouseSensitivity);
    drawValue(3, panelX_ + 336.0f, value);
    std::snprintf(value, sizeof(value), "%.1f", lastCfg_.raycastDistance);
    drawValue(4, panelX_ + 336.0f, value);
    std::snprintf(value, sizeof(value), "%d", lastCfg_.loadRadius);
    drawValue(5, panelX_ + 336.0f, value);
    std::snprintf(value, sizeof(value), "%d", lastCfg_.unloadRadius);
    drawValue(6, panelX_ + 336.0f, value);
    std::snprintf(value, sizeof(value), "%.2f", std::clamp(lastCfg_.timeOfDay01, 0.0f, 1.0f));
    drawValue(13, panelX_ + 562.0f, value);
    std::snprintf(value, sizeof(value), "%.2f (%s)", std::clamp(lastCfg_.moonPhase01, 0.0f, 1.0f),
                  moonPhaseName(lastCfg_.moonPhase01));
    drawValue(14, panelX_ + 562.0f, value);

    // Info footer.
    float infoY = startY - scrollOffset_ + static_cast<float>(visibleRows_.size()) * rowH + 10.0f;
    drawRect(panelX_ + 14.0f, infoY - 6.0f, panelW_ - 28.0f, panelH_ - (infoY - panelY_) - 14.0f,
             0.09f, 0.11f, 0.14f, 0.82f);
    drawText(panelX_ + 24.0f, infoY, "INFO", 230, 240, 255, 255);
    infoY += 20.0f;
    for (const std::string &l : infoLines_) {
        drawText(panelX_ + 24.0f, infoY, l, 190, 210, 235, 255);
        infoY += 16.0f;
    }

    // Occlusion strips: keep scrolling content continuous and hide overflow above/below content area.
    const float innerLeft = panelX_ + 2.0f;
    const float innerTop = panelY_ + 2.0f;
    const float innerRight = panelX_ + panelW_ - 2.0f;
    const float innerBottom = panelY_ + panelH_ - 2.0f;
    const float topMaskBottom = contentTop;
    const float bottomMaskTop = contentBottom;
    if (topMaskBottom > innerTop) {
        drawRect(innerLeft, innerTop, innerRight - innerLeft, topMaskBottom - innerTop, 0.14f,
                 0.15f, 0.18f, 0.94f);
        drawRect(panelX_ + 2.0f, panelY_ + 2.0f, panelW_ - 4.0f, 42.0f, 0.20f, 0.24f, 0.30f, 0.96f);
        drawText(panelX_ + panelW_ * 0.5f - textWidthPx(heading) * 0.5f, panelY_ + 14.0f, heading,
                 236, 242, 255, 255);
        const char *tabs[] = {"Player", "World", "Sky"};
        const float tabsY = panelY_ + 52.0f;
        const float tabsH = 34.0f;
        const float tabsGap = 8.0f;
        const float tabW = (panelW_ - 28.0f - tabsGap * 2.0f) / 3.0f;
        for (int ti = 0; ti < 3; ++ti) {
            const Rect tab{panelX_ + 14.0f + ti * (tabW + tabsGap), tabsY, tabW, tabsH};
            const bool selected = selectedTab_ == ti;
            drawRect(tab.x, tab.y, tab.w, tab.h, selected ? 0.32f : 0.12f, selected ? 0.26f : 0.15f,
                     selected ? 0.16f : 0.20f, 0.95f);
            drawRect(tab.x + 1.0f, tab.y + 1.0f, tab.w - 2.0f, tab.h - 2.0f,
                     selected ? 0.60f : 0.20f, selected ? 0.45f : 0.24f,
                     selected ? 0.24f : 0.30f, 0.95f);
            const std::string t(tabs[ti]);
            const float tx = tab.x + (tab.w - textWidthPx(t)) * 0.5f;
            const float ty = tab.y + (tab.h - textHeightPx(t)) * 0.5f;
            drawText(tx, ty, t, selected ? 255 : 224, selected ? 246 : 230, selected ? 212 : 244,
                     255);
        }
    }
    if (bottomMaskTop < innerBottom) {
        drawRect(innerLeft, bottomMaskTop, innerRight - innerLeft, innerBottom - bottomMaskTop,
                 0.14f, 0.15f, 0.18f, 0.94f);
    }

    const float maxScroll = std::max(0.0f, contentHeight_ - viewHeight_);
    if (maxScroll > 0.0f) {
        const Rect track{panelX_ + panelW_ - 24.0f, contentTop, 10.0f, viewHeight_};
        const float thumbH = std::clamp((viewHeight_ / contentHeight_) * viewHeight_, 32.0f, viewHeight_);
        const float thumbTravel = std::max(1.0f, viewHeight_ - thumbH);
        const float thumbY = contentTop + (scrollOffset_ / maxScroll) * thumbTravel;
        const Rect thumb{track.x - 2.0f, thumbY, track.w + 4.0f, thumbH};
        drawRect(track.x, track.y, track.w, track.h, 0.16f, 0.18f, 0.22f, 0.80f);
        drawRect(track.x + 1.0f, track.y + 1.0f, track.w - 2.0f, track.h - 2.0f, 0.12f, 0.14f,
                 0.18f, 0.86f);
        drawRect(thumb.x, thumb.y, thumb.w, thumb.h, 0.56f, 0.64f, 0.78f, 0.92f);
    }

    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glDisable(GL_DEPTH_TEST);
    glEnable(GL_SCISSOR_TEST);
    GLint vp[4] = {0, 0, width, height};
    glGetIntegerv(GL_VIEWPORT, vp);
    const float sxScale = (width > 0) ? (static_cast<float>(vp[2]) / static_cast<float>(width)) : 1.0f;
    const float syScale =
        (height > 0) ? (static_cast<float>(vp[3]) / static_cast<float>(height)) : 1.0f;
    const int sx =
        std::max(0, static_cast<int>((panelX_ + 2.0f) * sxScale));
    const int sy = std::max(
        0, static_cast<int>((static_cast<float>(height) - (panelY_ + panelH_ - 2.0f)) * syScale));
    const int sw = std::max(1, static_cast<int>((panelW_ - 4.0f) * sxScale));
    const int sh = std::max(1, static_cast<int>((panelH_ - 4.0f) * syScale));
    glScissor(sx, sy, sw, sh);

    glUseProgram(shader_);
    glUniform2f(glGetUniformLocation(shader_, "uScreen"), static_cast<float>(width),
                static_cast<float>(height));
    glBindVertexArray(vao_);
    glBindBuffer(GL_ARRAY_BUFFER, vbo_);
    glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(verts_.size() * sizeof(UiVertex)),
                 verts_.data(), GL_DYNAMIC_DRAW);
    glDrawArrays(GL_TRIANGLES, 0, static_cast<GLsizei>(verts_.size()));

    glDisable(GL_SCISSOR_TEST);
    glEnable(GL_DEPTH_TEST);
    glDisable(GL_BLEND);
}

void DebugMenu::updateWindowTitle(GLFWwindow *window, const DebugConfig &cfg,
                                  const world::WorldDebugStats &stats, float fps,
                                  float frameMs) const {
    char buf[512];
    std::snprintf(buf, sizeof(buf),
                  "Voxel Clone | FPS %.1f (%.2f ms) | Chunks L:%d M:%d PendingLd:%d "
                  "PendingMesh:%d | Tris:%d | Mode:%s | FOV:%.0f | R: %d/%d",
                  fps, frameMs, stats.loadedChunks, stats.meshedChunks, stats.pendingLoad,
                  stats.pendingRemesh, stats.totalTriangles, renderModeName(cfg.renderMode),
                  cfg.fov, cfg.loadRadius, cfg.unloadRadius);
    glfwSetWindowTitle(window, buf);
}

} // namespace game
