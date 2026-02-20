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
    auto adjust = [&](int idx, int dir) {
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
    };

    for (int i = 0; i < static_cast<int>(rowY_.size()); ++i) {
        const float y = rowY_[i];
        const Rect minus{panelX_ + panelW_ - 134.0f, y + 5.0f, 46.0f, 30.0f};
        const Rect plus{panelX_ + panelW_ - 82.0f, y + 5.0f, 46.0f, 30.0f};
        if (minus.contains(mx, my)) {
            adjust(i, -1);
            return;
        }
        if (plus.contains(mx, my)) {
            adjust(i, +1);
            return;
        }
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
    infoLines_.push_back("Mouse: click +/- buttons to adjust values");

    rowY_.clear();
    const float startY = panelY_ + 66.0f;
    const float rowH = 44.0f;
    for (int i = 0; i < 7; ++i) {
        rowY_.push_back(startY + i * rowH);
    }

    double mx = 0.0;
    double my = 0.0;
    glfwGetCursorPos(window, &mx, &my);
    mouseX_ = static_cast<float>(mx);
    mouseY_ = static_cast<float>(my);
    const bool leftDown = glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_LEFT) == GLFW_PRESS;

    if (open_ && leftDown && !prevMouseLeft_) {
        applyClick(static_cast<float>(mx), static_cast<float>(my), cfg);
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

    const float startY = panelY_ + 66.0f;
    const float rowH = 44.0f;

    const std::string labels[] = {
        "Render Mode",      "FOV",         "Move Speed",    "Mouse Sensitivity",
        "Raycast Distance", "Load Radius", "Unload Radius",
    };

    for (int i = 0; i < 7; ++i) {
        const float y = startY + i * rowH;
        drawRect(panelX_ + 14.0f, y, panelW_ - 28.0f, rowH - 6.0f, 0.10f, 0.12f, 0.16f, 0.86f);
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

        drawText(panelX_ + 24.0f, y + 14.0f, labels[i], 224, 232, 246, 255);
    }

    // Live values.
    char value[64];
    drawText(panelX_ + 336.0f, startY + 14.0f, renderModeName(lastCfg_.renderMode), 160, 200, 255,
             255);
    std::snprintf(value, sizeof(value), "%.1f", lastCfg_.fov);
    drawText(panelX_ + 336.0f, startY + rowH + 14.0f, value, 160, 200, 255, 255);
    std::snprintf(value, sizeof(value), "%.1f", lastCfg_.moveSpeed);
    drawText(panelX_ + 336.0f, startY + rowH * 2.0f + 14.0f, value, 160, 200, 255, 255);
    std::snprintf(value, sizeof(value), "%.2f", lastCfg_.mouseSensitivity);
    drawText(panelX_ + 336.0f, startY + rowH * 3.0f + 14.0f, value, 160, 200, 255, 255);
    std::snprintf(value, sizeof(value), "%.1f", lastCfg_.raycastDistance);
    drawText(panelX_ + 336.0f, startY + rowH * 4.0f + 14.0f, value, 160, 200, 255, 255);
    std::snprintf(value, sizeof(value), "%d", lastCfg_.loadRadius);
    drawText(panelX_ + 336.0f, startY + rowH * 5.0f + 14.0f, value, 160, 200, 255, 255);
    std::snprintf(value, sizeof(value), "%d", lastCfg_.unloadRadius);
    drawText(panelX_ + 336.0f, startY + rowH * 6.0f + 14.0f, value, 160, 200, 255, 255);

    // Info footer.
    float infoY = startY + 7.0f * rowH + 10.0f;
    drawRect(panelX_ + 14.0f, infoY - 6.0f, panelW_ - 28.0f, panelH_ - (infoY - panelY_) - 14.0f,
             0.09f, 0.11f, 0.14f, 0.82f);
    drawText(panelX_ + 24.0f, infoY, "INFO", 230, 240, 255, 255);
    infoY += 20.0f;
    for (const std::string &l : infoLines_) {
        drawText(panelX_ + 24.0f, infoY, l, 190, 210, 235, 255);
        infoY += 16.0f;
    }

    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glDisable(GL_DEPTH_TEST);

    glUseProgram(shader_);
    glUniform2f(glGetUniformLocation(shader_, "uScreen"), static_cast<float>(width),
                static_cast<float>(height));
    glBindVertexArray(vao_);
    glBindBuffer(GL_ARRAY_BUFFER, vbo_);
    glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(verts_.size() * sizeof(UiVertex)),
                 verts_.data(), GL_DYNAMIC_DRAW);
    glDrawArrays(GL_TRIANGLES, 0, static_cast<GLsizei>(verts_.size()));

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
