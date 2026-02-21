#include "gfx/HudRenderer.hpp"

#include "game/GameRules.hpp"
#include "game/SmeltingSystem.hpp"

#include <glad/glad.h>
#include <stb_easy_font.h>

#include <glm/common.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <stdexcept>
#include <vector>

namespace gfx {
namespace {

unsigned int compile(unsigned int type, const char *src) {
    const unsigned int s = glCreateShader(type);
    glShaderSource(s, 1, &src, nullptr);
    glCompileShader(s);
    int ok = 0;
    glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[1024];
        glGetShaderInfoLog(s, sizeof(log), nullptr, log);
        throw std::runtime_error(std::string("HUD shader compile failed: ") + log);
    }
    return s;
}

unsigned int link(unsigned int vs, unsigned int fs) {
    const unsigned int p = glCreateProgram();
    glAttachShader(p, vs);
    glAttachShader(p, fs);
    glLinkProgram(p);
    int ok = 0;
    glGetProgramiv(p, GL_LINK_STATUS, &ok);
    if (!ok) {
        char log[1024];
        glGetProgramInfoLog(p, sizeof(log), nullptr, log);
        throw std::runtime_error(std::string("HUD shader link failed: ") + log);
    }
    glDeleteShader(vs);
    glDeleteShader(fs);
    return p;
}

float textWidthPx(const std::string &text) {
    return static_cast<float>(stb_easy_font_width(const_cast<char *>(text.c_str())));
}

} // namespace

void HudRenderer::init2D() {
    if (uiReady_) {
        return;
    }

    const char *vs = R"(
#version 330 core
layout(location=0) in vec2 aPos;
layout(location=1) in vec4 aColor;
uniform vec2 uScreen;
out vec4 vColor;
void main() {
  vec2 ndc = vec2((aPos.x / uScreen.x) * 2.0 - 1.0, 1.0 - (aPos.y / uScreen.y) * 2.0);
  gl_Position = vec4(ndc, 0.0, 1.0);
  vColor = aColor;
}
)";

    const char *fs = R"(
#version 330 core
in vec4 vColor;
out vec4 FragColor;
void main() { FragColor = vColor; }
)";

    uiShader_ = link(compile(GL_VERTEX_SHADER, vs), compile(GL_FRAGMENT_SHADER, fs));

    glGenVertexArrays(1, &uiVao_);
    glGenBuffers(1, &uiVbo_);
    glBindVertexArray(uiVao_);
    glBindBuffer(GL_ARRAY_BUFFER, uiVbo_);
    glBufferData(GL_ARRAY_BUFFER, 0, nullptr, GL_DYNAMIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, sizeof(UiVertex), reinterpret_cast<void *>(0));
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 4, GL_FLOAT, GL_FALSE, sizeof(UiVertex),
                          reinterpret_cast<void *>(2 * sizeof(float)));

    uiReady_ = true;
}

void HudRenderer::initLine() {
    if (lineReady_) {
        return;
    }

    const char *vs = R"(
#version 330 core
layout(location=0) in vec3 aPos;
uniform mat4 uProj;
uniform mat4 uView;
uniform mat4 uModel;
void main() { gl_Position = uProj * uView * uModel * vec4(aPos, 1.0); }
)";

    const char *fs = R"(
#version 330 core
uniform vec4 uColor;
out vec4 FragColor;
void main() { FragColor = uColor; }
)";

    lineShader_ = link(compile(GL_VERTEX_SHADER, vs), compile(GL_FRAGMENT_SHADER, fs));

    constexpr float edge[24 * 3] = {0, 0, 0, 1, 0, 0, 1, 0, 0, 1, 1, 0, 1, 1, 0, 0, 1, 0,
                                    0, 1, 0, 0, 0, 0, 0, 0, 1, 1, 0, 1, 1, 0, 1, 1, 1, 1,
                                    1, 1, 1, 0, 1, 1, 0, 1, 1, 0, 0, 1, 0, 0, 0, 0, 0, 1,
                                    1, 0, 0, 1, 0, 1, 1, 1, 0, 1, 1, 1, 0, 1, 0, 0, 1, 1};

    glGenVertexArrays(1, &lineVao_);
    glGenBuffers(1, &lineVbo_);
    glBindVertexArray(lineVao_);
    glBindBuffer(GL_ARRAY_BUFFER, lineVbo_);
    glBufferData(GL_ARRAY_BUFFER, sizeof(edge), edge, GL_STATIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), nullptr);

    lineReady_ = true;
}

void HudRenderer::initCrack() {
    if (crackReady_) {
        return;
    }

    const char *vs = R"(
#version 330 core
layout(location=0) in vec3 aPos;
layout(location=1) in vec2 aUV;
uniform mat4 uProj;
uniform mat4 uView;
uniform mat4 uModel;
uniform vec4 uUvRect;
out vec2 vUV;
void main() {
  vUV = vec2(mix(uUvRect.x, uUvRect.z, aUV.x), mix(uUvRect.y, uUvRect.w, aUV.y));
  gl_Position = uProj * uView * uModel * vec4(aPos, 1.0);
}
)";

    const char *fs = R"(
#version 330 core
in vec2 vUV;
uniform sampler2D uAtlas;
uniform float uAlpha;
uniform vec3 uTint;
out vec4 FragColor;
void main() {
  vec4 c = texture(uAtlas, vUV);
  if (c.a < 0.05) discard;
  // Stable crack mask: use atlas alpha directly for a cleaner, less noisy look.
  float crack = c.a;
  vec3 col = mix(uTint, vec3(0.0, 0.0, 0.0), 0.88);
  FragColor = vec4(col, crack * uAlpha);
}
)";

    crackShader_ = link(compile(GL_VERTEX_SHADER, vs), compile(GL_FRAGMENT_SHADER, fs));

    constexpr float cube[36 * 5] = {
        // +Z
        0, 0, 1, 0, 0, 1, 0, 1, 1, 0, 1, 1, 1, 1, 1, 0, 0, 1, 0, 0, 1, 1, 1, 1, 1, 0, 1, 1, 0, 1,
        // -Z
        1, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 1, 0, 1, 1, 1, 0, 0, 0, 0, 0, 1, 0, 1, 1, 1, 1, 0, 0, 1,
        // -X
        0, 0, 0, 0, 0, 0, 0, 1, 1, 0, 0, 1, 1, 1, 1, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 0, 1, 0, 0, 1,
        // +X
        1, 0, 1, 0, 0, 1, 0, 0, 1, 0, 1, 1, 0, 1, 1, 1, 0, 1, 0, 0, 1, 1, 0, 1, 1, 1, 1, 1, 0, 1,
        // +Y
        0, 1, 0, 0, 0, 0, 1, 1, 0, 1, 1, 1, 1, 1, 1, 0, 1, 0, 0, 0, 1, 1, 1, 1, 1, 1, 1, 0, 1, 0,
        // -Y
        0, 0, 0, 0, 0, 1, 0, 0, 1, 0, 1, 0, 1, 1, 1, 0, 0, 0, 0, 0, 1, 0, 1, 1, 1, 0, 0, 1, 0, 1};

    glGenVertexArrays(1, &crackVao_);
    glGenBuffers(1, &crackVbo_);
    glBindVertexArray(crackVao_);
    glBindBuffer(GL_ARRAY_BUFFER, crackVbo_);
    glBufferData(GL_ARRAY_BUFFER, sizeof(cube), cube, GL_STATIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 5 * sizeof(float), reinterpret_cast<void *>(0));
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 5 * sizeof(float),
                          reinterpret_cast<void *>(3 * sizeof(float)));
    crackReady_ = true;
}

void HudRenderer::initIcons() {
    if (iconReady_) {
        return;
    }

    const char *vs = R"(
#version 330 core
layout(location=0) in vec2 aPos;
layout(location=1) in vec2 aUV;
layout(location=2) in float aLight;
uniform vec2 uScreen;
out vec2 vUV;
out float vLight;
void main() {
  vec2 ndc = vec2((aPos.x / uScreen.x) * 2.0 - 1.0, 1.0 - (aPos.y / uScreen.y) * 2.0);
  gl_Position = vec4(ndc, 0.0, 1.0);
  vUV = aUV;
  vLight = aLight;
}
)";

    const char *fs = R"(
#version 330 core
in vec2 vUV;
in float vLight;
uniform sampler2D uAtlas;
out vec4 FragColor;
void main() {
  vec4 c = texture(uAtlas, vUV);
  if (c.a <= 0.01) discard;
  FragColor = vec4(c.rgb * vLight, c.a);
}
)";

    iconShader_ = link(compile(GL_VERTEX_SHADER, vs), compile(GL_FRAGMENT_SHADER, fs));
    glGenVertexArrays(1, &iconVao_);
    glGenBuffers(1, &iconVbo_);
    glBindVertexArray(iconVao_);
    glBindBuffer(GL_ARRAY_BUFFER, iconVbo_);
    glBufferData(GL_ARRAY_BUFFER, 0, nullptr, GL_DYNAMIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, sizeof(IconVertex),
                          reinterpret_cast<void *>(0));
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, sizeof(IconVertex),
                          reinterpret_cast<void *>(2 * sizeof(float)));
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(2, 1, GL_FLOAT, GL_FALSE, sizeof(IconVertex),
                          reinterpret_cast<void *>(4 * sizeof(float)));
    iconReady_ = true;
}

void HudRenderer::drawRect(float x, float y, float w, float h, float r, float g, float b, float a) {
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

void HudRenderer::drawText(float x, float y, const std::string &text, unsigned char r,
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
        auto push = [&](const StbVert &v) {
            verts_.push_back(UiVertex{v.x, v.y, v.c[0] / 255.0f, v.c[1] / 255.0f, v.c[2] / 255.0f,
                                      v.c[3] / 255.0f});
        };
        push(a0);
        push(a1);
        push(a2);
        push(a0);
        push(a2);
        push(a3);
    }
}

void HudRenderer::renderTitleScreen(int width, int height, const std::vector<std::string> &worlds,
                                    int selectedWorld, bool createMode, bool editSeed,
                                    const std::string &createName, const std::string &createSeed,
                                    float cursorX, float cursorY) {
    init2D();
    verts_.clear();

    const float w = static_cast<float>(width);
    const float h = static_cast<float>(height);
    const float cx = w * 0.5f;

    // Layered sky/ground background.
    drawRect(0.0f, 0.0f, w, h, 0.08f, 0.13f, 0.20f, 1.0f);
    drawRect(0.0f, h * 0.40f, w, h * 0.60f, 0.17f, 0.28f, 0.38f, 0.72f);
    drawRect(0.0f, h * 0.80f, w, h * 0.20f, 0.24f, 0.19f, 0.13f, 0.78f);
    for (int i = 0; i < 8; ++i) {
        const float px = 80.0f + static_cast<float>(i) * (w / 8.5f);
        const float py = 86.0f + std::sin(static_cast<float>(i) * 1.2f) * 14.0f;
        drawRect(px, py, 42.0f, 18.0f, 0.90f, 0.93f, 0.97f, 0.15f);
    }

    const std::string title = "VOXELCRAFT";
    const float titleW = textWidthPx(title);
    drawText(cx - titleW * 0.5f + 3.0f, 56.0f, title, 18, 14, 10, 200);
    drawText(cx - titleW * 0.5f, 53.0f, title, 255, 226, 128, 255);
    const std::string subtitle = "Singleplayer World Select";
    drawText(cx - textWidthPx(subtitle) * 0.5f, 82.0f, subtitle, 214, 224, 240, 255);

    const float panelW = std::min(760.0f, w - 60.0f);
    const float panelX = cx - panelW * 0.5f;
    const float panelY = 118.0f;
    const float panelH = h - 170.0f;
    drawRect(panelX, panelY, panelW, panelH, 0.06f, 0.07f, 0.09f, 0.88f);
    drawRect(panelX + 2.0f, panelY + 2.0f, panelW - 4.0f, panelH - 4.0f, 0.14f, 0.15f, 0.18f,
             0.90f);
    drawRect(panelX + 2.0f, panelY + 2.0f, panelW - 4.0f, 34.0f, 0.18f, 0.21f, 0.26f, 0.92f);
    const std::string panelTitle = "Select World";
    drawText(panelX + panelW * 0.5f - textWidthPx(panelTitle) * 0.5f, panelY + 14.0f, panelTitle,
             244, 246, 252, 255);

    const float rowX = panelX + 18.0f;
    const float rowW = panelW - 36.0f;
    const float rowY0 = panelY + 48.0f;
    const float rowH = 30.0f;
    const float listH = panelH - 130.0f;
    drawRect(rowX, rowY0 - 2.0f, rowW, listH, 0.09f, 0.10f, 0.12f, 0.78f);
    const int maxRows = static_cast<int>(std::max(1.0f, (listH - 4.0f) / rowH));
    const int total = static_cast<int>(worlds.size());
    int scroll = 0;
    if (selectedWorld >= maxRows) {
        scroll = selectedWorld - maxRows + 1;
    }

    if (worlds.empty()) {
        const std::string emptyText = "No worlds yet. Click New World to begin.";
        drawText(rowX + rowW * 0.5f - textWidthPx(emptyText) * 0.5f, rowY0 + 12.0f, emptyText, 222,
                 226, 236, 255);
    } else {
        const int visible = std::min(maxRows, total - scroll);
        for (int i = 0; i < visible; ++i) {
            const int idx = scroll + i;
            const float y = rowY0 + static_cast<float>(i) * rowH;
            const bool selected = idx == selectedWorld;
            const bool hover = cursorX >= rowX && cursorX <= (rowX + rowW) && cursorY >= y &&
                               cursorY <= (y + rowH - 4.0f);
            if (selected) {
                drawRect(rowX + 2.0f, y + 1.0f, rowW - 4.0f, rowH - 6.0f, 0.70f, 0.56f, 0.24f,
                         0.96f);
                drawRect(rowX + 4.0f, y + 3.0f, rowW - 8.0f, rowH - 10.0f, 0.88f, 0.70f, 0.33f,
                         0.95f);
            } else if (hover) {
                drawRect(rowX + 2.0f, y + 1.0f, rowW - 4.0f, rowH - 6.0f, 0.24f, 0.29f, 0.36f,
                         0.94f);
                drawRect(rowX + 3.0f, y + 2.0f, rowW - 6.0f, rowH - 8.0f, 0.32f, 0.38f, 0.46f,
                         0.94f);
            } else {
                drawRect(rowX + 2.0f, y + 1.0f, rowW - 4.0f, rowH - 6.0f, 0.13f, 0.14f, 0.17f,
                         0.90f);
            }
            drawText(rowX + rowW * 0.5f - textWidthPx(worlds[idx]) * 0.5f, y + 9.0f, worlds[idx],
                     245, 246, 250, 255);
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
            drawRect(btn.x, btn.y, btn.w, btn.h, 0.52f, 0.39f, 0.16f, 0.98f);
            drawRect(btn.x + 2.0f, btn.y + 2.0f, btn.w - 4.0f, btn.h - 4.0f, 0.82f, 0.64f, 0.30f,
                     0.95f);
        } else {
            drawRect(btn.x, btn.y, btn.w, btn.h, 0.11f, 0.11f, 0.13f, 0.95f);
            drawRect(btn.x + 1.0f, btn.y + 1.0f, btn.w - 2.0f, btn.h - 2.0f, 0.21f, 0.22f, 0.25f,
                     0.95f);
        }
        const std::string label(btn.label);
        const float labelX = btn.x + btn.w * 0.5f - textWidthPx(label) * 0.5f;
        drawText(labelX, btn.y + 13.0f, label, 245, 246, 250, 255);
    }

    if (createMode) {
        // Modal for world creation.
        drawRect(0.0f, 0.0f, w, h, 0.02f, 0.03f, 0.04f, 0.52f);
        const float mw = std::min(560.0f, w - 80.0f);
        const float mh = 238.0f;
        const float mx = cx - mw * 0.5f;
        const float my = h * 0.5f - mh * 0.5f;
        drawRect(mx, my, mw, mh, 0.08f, 0.09f, 0.11f, 0.98f);
        drawRect(mx + 2.0f, my + 2.0f, mw - 4.0f, mh - 4.0f, 0.16f, 0.17f, 0.21f, 0.98f);
        drawRect(mx + 2.0f, my + 2.0f, mw - 4.0f, 34.0f, 0.22f, 0.25f, 0.31f, 0.98f);
        const std::string createTitle = "Create New World";
        drawText(mx + mw * 0.5f - textWidthPx(createTitle) * 0.5f, my + 14.0f, createTitle, 246,
                 248, 252, 255);

        const float fieldX = mx + 16.0f;
        const float fieldW = mw - 32.0f;
        const float nameY = my + 58.0f;
        const float seedY = my + 104.0f;
        const std::string nameLabel = "Name";
        const std::string seedLabel = "Seed";
        drawText(fieldX + fieldW * 0.5f - textWidthPx(nameLabel) * 0.5f, nameY - 12.0f, nameLabel,
                 214, 220, 234, 255);
        drawText(fieldX + fieldW * 0.5f - textWidthPx(seedLabel) * 0.5f, seedY - 12.0f, seedLabel,
                 214, 220, 234, 255);

        const bool nameHover = cursorX >= fieldX && cursorX <= (fieldX + fieldW) &&
                               cursorY >= nameY && cursorY <= (nameY + 30.0f);
        const bool seedHover = cursorX >= fieldX && cursorX <= (fieldX + fieldW) &&
                               cursorY >= seedY && cursorY <= (seedY + 30.0f);
        drawRect(fieldX, nameY, fieldW, 30.0f, (editSeed ? 0.18f : 0.45f),
                 (editSeed ? 0.20f : 0.34f), 0.24f, 0.95f);
        drawRect(fieldX + 1.0f, nameY + 1.0f, fieldW - 2.0f, 28.0f, nameHover ? 0.30f : 0.23f,
                 nameHover ? 0.34f : 0.26f, 0.39f, 0.95f);
        drawRect(fieldX, seedY, fieldW, 30.0f, (editSeed ? 0.45f : 0.18f),
                 (editSeed ? 0.34f : 0.20f), 0.24f, 0.95f);
        drawRect(fieldX + 1.0f, seedY + 1.0f, fieldW - 2.0f, 28.0f, seedHover ? 0.30f : 0.23f,
                 seedHover ? 0.34f : 0.26f, 0.39f, 0.95f);

        const std::string nameLine = createName + (!editSeed ? "_" : "");
        const std::string seedLine = createSeed + (editSeed ? "_" : "");
        drawText(fieldX + fieldW * 0.5f - textWidthPx(nameLine) * 0.5f, nameY + 10.0f, nameLine,
                 238, 242, 250, 255);
        drawText(fieldX + fieldW * 0.5f - textWidthPx(seedLine) * 0.5f, seedY + 10.0f, seedLine,
                 238, 242, 250, 255);

        const float cby = my + mh - 48.0f;
        const float cbw = (fieldW - 8.0f) * 0.5f;
        const Btn createBtn{fieldX, cby, cbw, 34.0f, "Create & Play"};
        const Btn cancelBtn{fieldX + cbw + 8.0f, cby, cbw, 34.0f, "Cancel"};
        const Btn modalButtons[2] = {createBtn, cancelBtn};
        for (int i = 0; i < 2; ++i) {
            const Btn &btn = modalButtons[i];
            const bool hover = cursorX >= btn.x && cursorX <= (btn.x + btn.w) && cursorY >= btn.y &&
                               cursorY <= (btn.y + btn.h);
            if (i == 0) {
                drawRect(btn.x, btn.y, btn.w, btn.h, hover ? 0.54f : 0.40f, hover ? 0.41f : 0.30f,
                         0.15f, 0.98f);
                drawRect(btn.x + 2.0f, btn.y + 2.0f, btn.w - 4.0f, btn.h - 4.0f,
                         hover ? 0.83f : 0.68f, hover ? 0.65f : 0.52f, 0.28f, 0.96f);
            } else {
                drawRect(btn.x, btn.y, btn.w, btn.h, hover ? 0.26f : 0.15f, hover ? 0.18f : 0.13f,
                         hover ? 0.18f : 0.14f, 0.98f);
                drawRect(btn.x + 2.0f, btn.y + 2.0f, btn.w - 4.0f, btn.h - 4.0f,
                         hover ? 0.36f : 0.24f, hover ? 0.25f : 0.20f, hover ? 0.26f : 0.22f,
                         0.95f);
            }
            const std::string label(btn.label);
            const float tx = btn.x + btn.w * 0.5f - textWidthPx(label) * 0.5f;
            drawText(tx, btn.y + 12.0f, label, 245, 246, 250, 255);
        }
    }

    const std::string flowText =
        "Flow: Select world -> Load. New World opens a modal with Create/Cancel.";
    drawText(panelX + panelW * 0.5f - textWidthPx(flowText) * 0.5f, panelY + panelH - 14.0f,
             flowText, 198, 206, 222, 255);

    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glDisable(GL_DEPTH_TEST);
    glUseProgram(uiShader_);
    glUniform2f(glGetUniformLocation(uiShader_, "uScreen"), static_cast<float>(width),
                static_cast<float>(height));
    glBindVertexArray(uiVao_);
    glBindBuffer(GL_ARRAY_BUFFER, uiVbo_);
    glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(verts_.size() * sizeof(UiVertex)),
                 verts_.data(), GL_DYNAMIC_DRAW);
    glDrawArrays(GL_TRIANGLES, 0, static_cast<GLsizei>(verts_.size()));
    glEnable(GL_DEPTH_TEST);
}

void HudRenderer::renderPauseMenu(int width, int height, float cursorX, float cursorY) {
    init2D();
    verts_.clear();

    const float w = static_cast<float>(width);
    const float h = static_cast<float>(height);
    const float cx = w * 0.5f;
    const float cy = h * 0.5f;

    drawRect(0.0f, 0.0f, w, h, 0.02f, 0.03f, 0.04f, 0.55f);
    drawRect(cx - 202.0f, cy - 142.0f, 404.0f, 284.0f, 0.08f, 0.08f, 0.11f, 0.96f);
    drawRect(cx - 198.0f, cy - 138.0f, 396.0f, 276.0f, 0.16f, 0.17f, 0.21f, 0.96f);
    drawRect(cx - 198.0f, cy - 138.0f, 396.0f, 38.0f, 0.23f, 0.26f, 0.33f, 0.96f);
    const std::string pausedTitle = "PAUSED";
    const std::string pausedSubtitle = "Game is paused. Choose what to do next.";
    drawText(cx - textWidthPx(pausedTitle) * 0.5f, cy - 122.0f, pausedTitle, 246, 248, 252, 255);
    drawText(cx - textWidthPx(pausedSubtitle) * 0.5f, cy - 96.0f, pausedSubtitle, 208, 216, 232,
             255);

    struct Btn {
        float x;
        float y;
        float w;
        float h;
        const char *label;
    };
    const Btn buttons[4] = {
        {cx - 140.0f, cy - 70.0f, 280.0f, 36.0f, "Resume"},
        {cx - 140.0f, cy - 24.0f, 280.0f, 36.0f, "Save World"},
        {cx - 140.0f, cy + 22.0f, 280.0f, 36.0f, "Save & Title"},
        {cx - 140.0f, cy + 68.0f, 280.0f, 36.0f, "Save & Quit"},
    };
    for (int i = 0; i < 4; ++i) {
        const Btn &btn = buttons[i];
        const bool hover = cursorX >= btn.x && cursorX <= (btn.x + btn.w) && cursorY >= btn.y &&
                           cursorY <= (btn.y + btn.h);
        if (i == 3) {
            drawRect(btn.x, btn.y, btn.w, btn.h, hover ? 0.45f : 0.30f, hover ? 0.16f : 0.12f,
                     hover ? 0.12f : 0.10f, 0.98f);
            drawRect(btn.x + 2.0f, btn.y + 2.0f, btn.w - 4.0f, btn.h - 4.0f, hover ? 0.68f : 0.48f,
                     hover ? 0.24f : 0.18f, hover ? 0.18f : 0.14f, 0.95f);
        } else if (i == 2) {
            drawRect(btn.x, btn.y, btn.w, btn.h, hover ? 0.21f : 0.15f, hover ? 0.33f : 0.24f,
                     hover ? 0.52f : 0.40f, 0.98f);
            drawRect(btn.x + 2.0f, btn.y + 2.0f, btn.w - 4.0f, btn.h - 4.0f, hover ? 0.28f : 0.22f,
                     hover ? 0.46f : 0.36f, hover ? 0.72f : 0.58f, 0.95f);
        } else if (hover) {
            drawRect(btn.x, btn.y, btn.w, btn.h, 0.52f, 0.39f, 0.16f, 0.98f);
            drawRect(btn.x + 2.0f, btn.y + 2.0f, btn.w - 4.0f, btn.h - 4.0f, 0.80f, 0.62f, 0.30f,
                     0.95f);
        } else {
            drawRect(btn.x, btn.y, btn.w, btn.h, 0.11f, 0.11f, 0.13f, 0.95f);
            drawRect(btn.x + 1.0f, btn.y + 1.0f, btn.w - 2.0f, btn.h - 2.0f, 0.20f, 0.21f, 0.24f,
                     0.95f);
        }
        const std::string label(btn.label);
        const float lx = btn.x + btn.w * 0.5f - textWidthPx(label) * 0.5f;
        drawText(lx, btn.y + 13.0f, label, 245, 246, 250, 255);
    }

    const std::string pauseHint = "Esc closes this menu";
    drawText(cx - textWidthPx(pauseHint) * 0.5f, cy + 120.0f, pauseHint, 210, 216, 230, 255);

    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glDisable(GL_DEPTH_TEST);
    glUseProgram(uiShader_);
    glUniform2f(glGetUniformLocation(uiShader_, "uScreen"), static_cast<float>(width),
                static_cast<float>(height));
    glBindVertexArray(uiVao_);
    glBindBuffer(GL_ARRAY_BUFFER, uiVbo_);
    glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(verts_.size() * sizeof(UiVertex)),
                 verts_.data(), GL_DYNAMIC_DRAW);
    glDrawArrays(GL_TRIANGLES, 0, static_cast<GLsizei>(verts_.size()));
    glEnable(GL_DEPTH_TEST);
}

void HudRenderer::render2D(int width, int height, int selectedIndex,
                           const std::array<voxel::BlockId, game::Inventory::kHotbarSize> &hotbar,
                           const std::array<int, game::Inventory::kHotbarSize> &hotbarCounts,
                           const std::array<voxel::BlockId, game::Inventory::kSlotCount> &allIds,
                           const std::array<int, game::Inventory::kSlotCount> &allCounts,
                           bool showInventory, voxel::BlockId carryingId, int carryingCount,
                           float cursorX, float cursorY, int hoveredSlotIndex, float hudScale,
                           const std::array<game::Inventory::Slot, game::CraftingSystem::kInputCount>
                               &craftInput,
                           int craftingGridSize, bool usingCraftingTable, bool usingFurnace,
                           const game::Inventory::Slot &smeltInput,
                           const game::Inventory::Slot &smeltFuel,
                           const game::Inventory::Slot &smeltOutput, float smeltProgress01,
                           float smeltFuel01,
                           bool showRecipeMenu,
                           const std::vector<game::CraftingSystem::RecipeInfo> &recipes,
                           const std::vector<game::SmeltingSystem::Recipe> &smeltingRecipes,
                           const std::vector<bool> &recipeCraftable,
                           float recipeScroll, float uiTimeSeconds, const std::string &recipeSearch,
                           bool showCreativeMenu,
                           const std::vector<voxel::BlockId> &creativeItems, float creativeScroll,
                           const std::string &creativeSearch,
                           bool recipeCraftableOnly,
                           const std::optional<voxel::BlockId> &recipeIngredientFilter,
                           const game::Inventory::Slot &craftOutput,
                           const std::string &carryingName, const std::string &selectedName,
                           const std::string &lookedAtText, const std::string &modeText,
                           const std::string &compassText, const std::string &coordText,
                           const voxel::BlockRegistry &registry, const TextureAtlas &atlas) {
    init2D();
    initIcons();
    verts_.clear();
    iconVerts_.clear();
    struct IconClipBatch {
        std::size_t start = 0;
        std::size_t count = 0;
        float x = 0.0f;
        float y = 0.0f;
        float w = 0.0f;
        float h = 0.0f;
    };
    std::vector<IconClipBatch> iconClipBatches;
    auto beginIconClipBatch = [&](float x, float y, float w, float h) {
        iconClipBatches.push_back({iconVerts_.size(), 0, x, y, w, h});
        return iconClipBatches.size() - 1;
    };
    auto endIconClipBatch = [&](std::size_t batchIndex) {
        if (batchIndex < iconClipBatches.size()) {
            iconClipBatches[batchIndex].count = iconVerts_.size() - iconClipBatches[batchIndex].start;
        }
    };
    std::string recipeHoverTipText;
    float recipeHoverTipX = 0.0f;
    float recipeHoverTipY = 0.0f;
    struct RecipeNameLabel {
        float x = 0.0f;
        float y = 0.0f;
        float w = 0.0f;
        float h = 0.0f;
        std::string text;
    };
    std::vector<RecipeNameLabel> recipeNameLabels;
    bool dragIconActive = false;
    voxel::BlockId dragIconId = voxel::AIR;
    int dragIconCount = 0;
    float dragIconX = 0.0f;
    float dragIconY = 0.0f;
    float dragIconSize = 0.0f;
    std::string dragIconName;
    auto appendItemIcon = [&](float x0, float y0, float x1, float y1, const glm::vec4 &uv,
                              float light) {
        iconVerts_.push_back({x0, y0, uv.x, uv.y, light});
        iconVerts_.push_back({x1, y0, uv.z, uv.y, light});
        iconVerts_.push_back({x1, y1, uv.z, uv.w, light});
        iconVerts_.push_back({x0, y0, uv.x, uv.y, light});
        iconVerts_.push_back({x1, y1, uv.z, uv.w, light});
        iconVerts_.push_back({x0, y1, uv.x, uv.w, light});
    };
    auto appendSkewQuad = [&](const glm::vec2 &a, const glm::vec2 &b, const glm::vec2 &c,
                              const glm::vec2 &d, const glm::vec4 &uv, float light) {
        iconVerts_.push_back({a.x, a.y, uv.x, uv.y, light});
        iconVerts_.push_back({b.x, b.y, uv.z, uv.y, light});
        iconVerts_.push_back({c.x, c.y, uv.z, uv.w, light});
        iconVerts_.push_back({a.x, a.y, uv.x, uv.y, light});
        iconVerts_.push_back({c.x, c.y, uv.z, uv.w, light});
        iconVerts_.push_back({d.x, d.y, uv.x, uv.w, light});
    };
    auto appendCubeIcon = [&](float x, float y, float w, float h, const voxel::BlockDef &def) {
        const glm::vec4 uvTop = atlas.uvRect(def.topTile);
        const glm::vec4 uvSide = atlas.uvRect(def.sideTile);
        const bool furnaceLike = voxel::isFurnaceDef(def);
        const glm::vec4 uvFront =
            furnaceLike ? atlas.uvRect(voxel::TILE_FURNACE_FRONT) : uvSide;
        const glm::vec2 t0{x + w * 0.50f, y + h * 0.10f};
        const glm::vec2 t1{x + w * 0.84f, y + h * 0.29f};
        const glm::vec2 t2{x + w * 0.50f, y + h * 0.49f};
        const glm::vec2 t3{x + w * 0.16f, y + h * 0.29f};
        const glm::vec2 bL{x + w * 0.16f, y + h * 0.71f};
        const glm::vec2 bR{x + w * 0.84f, y + h * 0.71f};
        const glm::vec2 bC{x + w * 0.50f, y + h * 0.90f};

        // Draw order keeps the top crisp where faces meet.
        appendSkewQuad(t3, t2, bC, bL, uvFront, 0.74f); // left/front (flipped for furnace)
        appendSkewQuad(t2, t1, bR, bC, uvSide, 0.90f);  // right
        appendSkewQuad(t0, t1, t2, t3, uvTop, 1.00f);  // top
    };
    auto isFlatItemId = [](voxel::BlockId id) {
        return id == voxel::TALL_GRASS || id == voxel::FLOWER || id == voxel::STICK ||
               id == voxel::IRON_INGOT || id == voxel::COPPER_INGOT ||
               id == voxel::GOLD_INGOT ||
               voxel::isTorch(id);
    };
    auto drawSelectedOutline = [&](float x, float y, float size) {
        drawRect(x - 3.0f, y - 3.0f, size + 6.0f, 4.0f, 1.0f, 0.90f, 0.30f, 1.0f);
        drawRect(x - 3.0f, y + size - 1.0f, size + 6.0f, 4.0f, 1.0f, 0.90f, 0.30f, 1.0f);
        drawRect(x - 3.0f, y + 1.0f, 4.0f, size - 2.0f, 1.0f, 0.90f, 0.30f, 1.0f);
        drawRect(x + size - 1.0f, y + 1.0f, 4.0f, size - 2.0f, 1.0f, 0.90f, 0.30f, 1.0f);
    };
    auto drawHoverOutline = [&](float x, float y, float size) {
        drawRect(x - 2.0f, y - 2.0f, size + 4.0f, 3.0f, 0.36f, 0.78f, 1.0f, 0.95f);
        drawRect(x - 2.0f, y + size - 1.0f, size + 4.0f, 3.0f, 0.36f, 0.78f, 1.0f, 0.95f);
        drawRect(x - 2.0f, y + 1.0f, 3.0f, size - 2.0f, 0.36f, 0.78f, 1.0f, 0.95f);
        drawRect(x + size - 1.0f, y + 1.0f, 3.0f, size - 2.0f, 0.36f, 0.78f, 1.0f, 0.95f);
    };
    auto drawValidHintOutline = [&](float x, float y, float size) {
        drawRect(x - 1.0f, y - 1.0f, size + 2.0f, 2.0f, 0.38f, 0.92f, 0.48f, 0.88f);
        drawRect(x - 1.0f, y + size - 1.0f, size + 2.0f, 2.0f, 0.38f, 0.92f, 0.48f, 0.88f);
        drawRect(x - 1.0f, y + 1.0f, 2.0f, size - 2.0f, 0.38f, 0.92f, 0.48f, 0.88f);
        drawRect(x + size - 1.0f, y + 1.0f, 2.0f, size - 2.0f, 0.38f, 0.92f, 0.48f, 0.88f);
    };
    static const game::SmeltingSystem smeltRules;
    const bool isDraggingItem = carryingCount > 0 && carryingId != voxel::AIR;
    const int furnaceInputIndex = game::Inventory::kSlotCount;
    const int furnaceFuelIndex = game::Inventory::kSlotCount + 1;
    const bool hintFurnaceInput = usingFurnace && !isDraggingItem &&
                                  hoveredSlotIndex == furnaceInputIndex &&
                                  (smeltInput.id == voxel::AIR || smeltInput.count <= 0);
    const bool hintFurnaceFuel = usingFurnace && !isDraggingItem &&
                                 hoveredSlotIndex == furnaceFuelIndex &&
                                 (smeltFuel.id == voxel::AIR || smeltFuel.count <= 0);

    const float cx = width * 0.5f;
    const float cy = height * 0.5f;
    // Crosshair: crisp 4-arm reticle with center dot and soft outline.
    const float uiScale = glm::clamp(hudScale, 0.8f, 1.8f);
    auto setHoverTip = [&](voxel::BlockId id, int count) {
        if (isDraggingItem) {
            return;
        }
        if (id == voxel::AIR || count <= 0) {
            return;
        }
        recipeHoverTipText = game::blockName(id);
        recipeHoverTipX = cursorX + 12.0f * uiScale;
        recipeHoverTipY = cursorY - 8.0f * uiScale;
    };
    auto drawTooltipPanel = [&](const std::string &text, float rawX, float rawY) {
        if (text.empty()) {
            return;
        }
        const float padX = 8.0f * uiScale;
        const float tipH = 16.0f * uiScale;
        const float tipW = textWidthPx(text) + padX * 2.0f;
        const float tipX =
            std::clamp(rawX, 4.0f * uiScale, static_cast<float>(width) - tipW - 4.0f * uiScale);
        const float tipY =
            std::clamp(rawY, 4.0f * uiScale, static_cast<float>(height) - tipH - 4.0f * uiScale);
        drawRect(tipX, tipY, tipW, tipH, 0.04f, 0.06f, 0.08f, 0.90f);
        drawRect(tipX + 1.0f, tipY + 1.0f, tipW - 2.0f, tipH - 2.0f, 0.11f, 0.14f, 0.19f, 0.94f);
        const float tx = tipX + (tipW - textWidthPx(text)) * 0.5f;
        const float ty = tipY + (tipH - 8.0f) * 0.5f;
        drawText(tx + 1.0f, ty + 1.0f, text, 14, 16, 20, 220);
        drawText(tx, ty, text, 228, 234, 246, 255);
    };
    const float kArmGap = 3.0f * uiScale;
    const float kArmLen = 6.0f * uiScale;
    const float kArmThick = 2.0f * uiScale;

    if (!showInventory) {
        // Outline/shadow pass.
        drawRect(cx - 1.5f, cy - (kArmGap + kArmLen + 0.5f), 3.0f, kArmLen + 1.0f, 0.02f, 0.02f,
                 0.02f, 0.72f);
        drawRect(cx - 1.5f, cy + kArmGap - 0.5f, 3.0f, kArmLen + 1.0f, 0.02f, 0.02f, 0.02f, 0.72f);
        drawRect(cx - (kArmGap + kArmLen + 0.5f), cy - 1.5f, kArmLen + 1.0f, 3.0f, 0.02f, 0.02f,
                 0.02f, 0.72f);
        drawRect(cx + kArmGap - 0.5f, cy - 1.5f, kArmLen + 1.0f, 3.0f, 0.02f, 0.02f, 0.02f, 0.72f);
        drawRect(cx - 2.0f, cy - 2.0f, 4.0f, 4.0f, 0.02f, 0.02f, 0.02f, 0.72f);

        // Main white pass.
        drawRect(cx - (kArmThick * 0.5f), cy - (kArmGap + kArmLen), kArmThick, kArmLen, 0.95f,
                 0.95f, 0.95f, 0.96f);
        drawRect(cx - (kArmThick * 0.5f), cy + kArmGap, kArmThick, kArmLen, 0.95f, 0.95f, 0.95f,
                 0.96f);
        drawRect(cx - (kArmGap + kArmLen), cy - (kArmThick * 0.5f), kArmLen, kArmThick, 0.95f,
                 0.95f, 0.95f, 0.96f);
        drawRect(cx + kArmGap, cy - (kArmThick * 0.5f), kArmLen, kArmThick, 0.95f, 0.95f, 0.95f,
                 0.96f);
        drawRect(cx - 1.0f, cy - 1.0f, 2.0f, 2.0f, 1.0f, 1.0f, 1.0f, 0.98f);
    }

    const float slot = 48.0f * uiScale;
    const float gap = 10.0f * uiScale;
    const float totalW =
        static_cast<float>(hotbar.size()) * slot + static_cast<float>(hotbar.size() - 1) * gap;
    const float x0 = cx - totalW * 0.5f;
    const float y0 = height - 90.0f * uiScale;
    drawRect(x0 - 8.0f * uiScale, y0 - 6.0f * uiScale, totalW + 16.0f * uiScale,
             slot + 12.0f * uiScale, 0.03f, 0.04f, 0.05f, 0.58f);

    struct SlotLabel {
        float x = 0.0f;
        float y = 0.0f;
        std::string text;
    };
    std::vector<SlotLabel> slotLabels;
    slotLabels.reserve(hotbar.size());

    for (int i = 0; i < static_cast<int>(hotbar.size()); ++i) {
        const float x = x0 + i * (slot + gap);
        drawRect(x + 2.0f, y0 + 2.0f, slot, slot, 0.0f, 0.0f, 0.0f, 0.35f);
        drawRect(x, y0, slot, slot, 0.09f, 0.10f, 0.12f, 0.88f);
        drawRect(x + 2.0f, y0 + 2.0f, slot - 4.0f, slot - 4.0f, 0.16f, 0.17f, 0.20f, 0.76f);
        const voxel::BlockDef &def = registry.get(hotbar[i]);
        const float ix = x + 8.0f * uiScale;
        const float iy = y0 + 8.0f * uiScale;
        const float iw = slot - 16.0f * uiScale;
        const float ih = slot - 16.0f * uiScale;
        if (isFlatItemId(hotbar[i])) {
            const glm::vec4 uv = atlas.uvRect(def.sideTile);
            appendItemIcon(ix, iy, ix + iw, iy + ih, uv, 1.0f);
        } else {
            const float cubeInset = 5.0f * uiScale;
            appendCubeIcon(x + cubeInset, y0 + cubeInset, slot - 2.0f * cubeInset,
                           slot - 2.0f * cubeInset, def);
        }
        if (!showInventory && i == selectedIndex) {
            drawSelectedOutline(x, y0, slot);
        }
        if (showInventory && hoveredSlotIndex == i) {
            drawHoverOutline(x, y0, slot);
            setHoverTip(hotbar[i], hotbarCounts[i]);
        }
        if (showInventory && hotbarCounts[i] > 0 &&
            ((hintFurnaceInput && smeltRules.canSmelt(hotbar[i])) ||
             (hintFurnaceFuel && smeltRules.isFuel(hotbar[i])))) {
            drawValidHintOutline(x, y0, slot);
        }
        const int count = hotbarCounts[i];
        if (count > 0) {
            slotLabels.push_back(SlotLabel{x + 6.0f, y0 + slot - 13.0f, std::to_string(count)});
        }
        slotLabels.push_back(SlotLabel{x + slot - 11.0f, y0 + 4.0f, std::to_string(i + 1)});
    }

    drawText(cx - 176.0f * uiScale, y0 + slot + 14.0f * uiScale,
             "1-9 Select  |  LMB Mine  |  RMB Place  |  E Inventory  |  F Creative  |  F2 HUD",
             200, 206, 222, 255);

    if (showInventory) {
        const int cols = game::Inventory::kColumns;
        const int rows =
            game::Inventory::kRows - 1; // Top inventory rows, hotbar rendered separately.
        const float invSlot = 48.0f * uiScale;
        const float invGap = 10.0f * uiScale;
        const float invW = cols * invSlot + (cols - 1) * invGap;
        const float invH = rows * invSlot + (rows - 1) * invGap;
        const float invY = y0 - 44.0f * uiScale - invH;

        const int craftGrid = std::clamp(craftingGridSize, game::CraftingSystem::kGridSizeInventory,
                                         game::CraftingSystem::kGridSizeTable);
        // Crafting panel (2x2 inventory or 3x3 table).
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
        const float menuInvX = cx - totalPanelW * 0.5f;
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
        const float craftOutX = usingFurnace ? furnaceOutXBase : (craftX + craftGridW + 44.0f * uiScale);
        const float craftOutY =
            usingFurnace ? furnaceInYBase : (craftY + (craftGridW - craftSlot) * 0.5f);

        drawRect(invX - 10.0f * uiScale, invY - 26.0f * uiScale, invW + 20.0f * uiScale,
                 invH + 36.0f * uiScale, 0.03f, 0.04f, 0.05f, 0.72f);
        drawText(invX, invY - 18.0f * uiScale, "Inventory", 244, 246, 252, 255);
        const float craftBorderTopPad = usingFurnace
                                            ? (26.0f * uiScale)
                                            : ((craftGrid == game::CraftingSystem::kGridSizeTable)
                                                   ? 26.0f * uiScale
                                                   : 28.0f * uiScale);
        const float craftBorderBottomPad = 8.0f * uiScale;
        const float craftContentH =
            usingFurnace ? (furnaceFuelYBase + craftSlot - craftY + 8.0f * uiScale) : craftGridW;
        drawRect(craftX - 10.0f * uiScale, craftY - craftBorderTopPad,
                 (craftOutX + craftSlot) - craftX + 20.0f * uiScale,
                 craftContentH + craftBorderTopPad + craftBorderBottomPad, 0.03f, 0.04f, 0.05f,
                 0.72f);
        drawText(craftX, craftY - 20.0f * uiScale,
                 usingFurnace ? "Furnace" : (usingCraftingTable ? "Crafting Table (3x3)"
                                                                  : "Crafting (2x2)"),
                 244, 246, 252, 255);
        if (!usingFurnace) {
            drawText(craftX + craftGridW + 16.0f * uiScale, craftOutY + craftSlot * 0.45f, "->", 230,
                     236, 248, 255);
        }

        auto drawSlotFrame = [&](float sx, float sy, float size) {
            drawRect(sx + 2.0f, sy + 2.0f, size, size, 0.0f, 0.0f, 0.0f, 0.32f);
            drawRect(sx, sy, size, size, 0.09f, 0.10f, 0.12f, 0.88f);
            drawRect(sx + 2.0f, sy + 2.0f, size - 4.0f, size - 4.0f, 0.16f, 0.17f, 0.20f, 0.76f);
        };

        if (usingFurnace) {
            const float furnaceInX = furnaceInXBase;
            const float furnaceInY = furnaceInYBase;
            const float furnaceFuelX = furnaceInX;
            const float furnaceFuelY = furnaceFuelYBase;
            const float furnaceOutX = craftOutX;
            drawText(furnaceInX, furnaceInY - 11.0f * uiScale, "Input", 212, 222, 236, 255);
            drawText(furnaceFuelX, furnaceFuelY - 11.0f * uiScale, "Fuel", 212, 222, 236, 255);
            drawText(furnaceOutX, craftOutY - 11.0f * uiScale, "Output", 212, 222, 236, 255);
            drawSlotFrame(furnaceInX, furnaceInY, craftSlot);
            if (smeltInput.id != voxel::AIR && smeltInput.count > 0) {
                const voxel::BlockDef &def = registry.get(smeltInput.id);
                const float ix = furnaceInX + 8.0f * uiScale;
                const float iy = furnaceInY + 8.0f * uiScale;
                const float iw = craftSlot - 16.0f * uiScale;
                const float ih = craftSlot - 16.0f * uiScale;
                if (isFlatItemId(smeltInput.id)) {
                    const glm::vec4 uv = atlas.uvRect(def.sideTile);
                    appendItemIcon(ix, iy, ix + iw, iy + ih, uv, 1.0f);
                } else {
                    const float cubeInset = 5.0f * uiScale;
                    appendCubeIcon(furnaceInX + cubeInset, furnaceInY + cubeInset,
                                   craftSlot - 2.0f * cubeInset, craftSlot - 2.0f * cubeInset, def);
                }
                slotLabels.push_back(SlotLabel{furnaceInX + 5.0f, furnaceInY + craftSlot - 13.0f,
                                               std::to_string(smeltInput.count)});
            }
            if (hoveredSlotIndex == furnaceInputIndex) {
                drawHoverOutline(furnaceInX, furnaceInY, craftSlot);
                setHoverTip(smeltInput.id, smeltInput.count);
            }

            drawSlotFrame(furnaceFuelX, furnaceFuelY, craftSlot);
            if (smeltFuel.id != voxel::AIR && smeltFuel.count > 0) {
                const voxel::BlockDef &def = registry.get(smeltFuel.id);
                const float ix = furnaceFuelX + 8.0f * uiScale;
                const float iy = furnaceFuelY + 8.0f * uiScale;
                const float iw = craftSlot - 16.0f * uiScale;
                const float ih = craftSlot - 16.0f * uiScale;
                if (isFlatItemId(smeltFuel.id)) {
                    const glm::vec4 uv = atlas.uvRect(def.sideTile);
                    appendItemIcon(ix, iy, ix + iw, iy + ih, uv, 1.0f);
                } else {
                    const float cubeInset = 5.0f * uiScale;
                    appendCubeIcon(furnaceFuelX + cubeInset, furnaceFuelY + cubeInset,
                                   craftSlot - 2.0f * cubeInset, craftSlot - 2.0f * cubeInset, def);
                }
                slotLabels.push_back(SlotLabel{furnaceFuelX + 5.0f, furnaceFuelY + craftSlot - 13.0f,
                                               std::to_string(smeltFuel.count)});
            }
            if (hoveredSlotIndex == furnaceFuelIndex) {
                drawHoverOutline(furnaceFuelX, furnaceFuelY, craftSlot);
                setHoverTip(smeltFuel.id, smeltFuel.count);
            }

            const float inputCenterX = furnaceInX + craftSlot * 0.5f;
            const float outputCenterX = furnaceOutX + craftSlot * 0.5f;
            const float flowCenterX = (inputCenterX + outputCenterX) * 0.5f;
            const float progW = 38.0f * uiScale;
            const float progX = flowCenterX - progW * 0.5f;
            const float progY = furnaceInY + craftSlot * 0.44f;
            const float progH = 8.0f * uiScale;
            drawText(progX + progW * 0.5f - textWidthPx("Smelt") * 0.5f, progY - 10.0f * uiScale,
                     "Smelt", 186, 200, 220, 255);
            drawRect(progX, progY, progW, progH, 0.10f, 0.11f, 0.13f, 0.95f);
            drawRect(progX + 1.0f, progY + 1.0f,
                     (progW - 2.0f) * std::clamp(smeltProgress01, 0.0f, 1.0f), progH - 2.0f, 0.95f,
                     0.62f, 0.22f, 0.95f);

            const float burnX = furnaceMidX;
            const float burnY = furnaceFuelY + craftSlot * 0.50f;
            const float burnW = 30.0f * uiScale;
            const float burnH = 7.0f * uiScale;
            drawText(burnX + burnW * 0.5f - textWidthPx("Burn") * 0.5f, burnY - 10.0f * uiScale,
                     "Burn", 186, 200, 220, 255);
            drawRect(burnX, burnY, burnW, burnH, 0.10f, 0.11f, 0.13f, 0.95f);
            drawRect(burnX + 1.0f, burnY + 1.0f,
                     (burnW - 2.0f) * std::clamp(smeltFuel01, 0.0f, 1.0f), burnH - 2.0f, 0.98f,
                     0.46f, 0.20f, 0.95f);
        } else {
            for (int r = 0; r < craftGrid; ++r) {
                for (int c = 0; c < craftGrid; ++c) {
                    const int craftIdx = r * craftGrid + c;
                    const int uiIdx = game::Inventory::kSlotCount + craftIdx;
                    const float sx = craftX + c * (craftSlot + craftGap);
                    const float sy = craftY + r * (craftSlot + craftGap);
                    drawSlotFrame(sx, sy, craftSlot);
                    const auto &slotData = craftInput[craftIdx];
                    if (slotData.id != voxel::AIR && slotData.count > 0) {
                        const voxel::BlockDef &def = registry.get(slotData.id);
                        const float ix = sx + 8.0f * uiScale;
                        const float iy = sy + 8.0f * uiScale;
                        const float iw = craftSlot - 16.0f * uiScale;
                        const float ih = craftSlot - 16.0f * uiScale;
                        if (isFlatItemId(slotData.id)) {
                            const glm::vec4 uv = atlas.uvRect(def.sideTile);
                            appendItemIcon(ix, iy, ix + iw, iy + ih, uv, 1.0f);
                        } else {
                            const float cubeInset = 5.0f * uiScale;
                            appendCubeIcon(sx + cubeInset, sy + cubeInset,
                                           craftSlot - 2.0f * cubeInset,
                                           craftSlot - 2.0f * cubeInset, def);
                        }
                        slotLabels.push_back(SlotLabel{sx + 5.0f, sy + craftSlot - 13.0f,
                                                       std::to_string(slotData.count)});
                    }
                    if (hoveredSlotIndex == uiIdx) {
                        drawHoverOutline(sx, sy, craftSlot);
                        setHoverTip(slotData.id, slotData.count);
                    }
                }
            }
        }

        drawSlotFrame(craftOutX, craftOutY, craftSlot);
        const int craftOutputIndex = game::Inventory::kSlotCount +
                                     static_cast<int>(craftInput.size());
        const int trashIndex = craftOutputIndex + 1;
        const game::Inventory::Slot &displayOutput = usingFurnace ? smeltOutput : craftOutput;
        if (displayOutput.id != voxel::AIR && displayOutput.count > 0) {
            const voxel::BlockDef &def = registry.get(displayOutput.id);
            const float ix = craftOutX + 8.0f * uiScale;
            const float iy = craftOutY + 8.0f * uiScale;
            const float iw = craftSlot - 16.0f * uiScale;
            const float ih = craftSlot - 16.0f * uiScale;
            if (isFlatItemId(displayOutput.id)) {
                const glm::vec4 uv = atlas.uvRect(def.sideTile);
                appendItemIcon(ix, iy, ix + iw, iy + ih, uv, 1.0f);
            } else {
                const float cubeInset = 5.0f * uiScale;
                appendCubeIcon(craftOutX + cubeInset, craftOutY + cubeInset,
                               craftSlot - 2.0f * cubeInset, craftSlot - 2.0f * cubeInset, def);
            }
            slotLabels.push_back(SlotLabel{craftOutX + 5.0f, craftOutY + craftSlot - 13.0f,
                                           std::to_string(displayOutput.count)});
        }
        if (hoveredSlotIndex == craftOutputIndex) {
            drawHoverOutline(craftOutX, craftOutY, craftSlot);
            setHoverTip(displayOutput.id, displayOutput.count);
        }

        const float trashX = craftX - 10.0f * uiScale;
        const float invBorderBottomY = invY + invH + 10.0f * uiScale;
        const float trashY = invBorderBottomY - craftSlot - 2.0f;
        drawRect(trashX - 2.0f, trashY - 2.0f, craftSlot + 4.0f, craftSlot + 4.0f, 0.60f, 0.22f,
                 0.22f, 0.78f);
        drawSlotFrame(trashX, trashY, craftSlot);
        drawRect(trashX + 8.0f * uiScale, trashY + 10.0f * uiScale, craftSlot - 16.0f * uiScale,
                 5.0f * uiScale, 0.55f, 0.18f, 0.18f, 0.95f);
        drawRect(trashX + 14.0f * uiScale, trashY + 15.0f * uiScale, craftSlot - 28.0f * uiScale,
                 19.0f * uiScale, 0.78f, 0.24f, 0.24f, 0.95f);
        drawRect(trashX + 18.0f * uiScale, trashY + 18.0f * uiScale, 2.0f * uiScale,
                 12.0f * uiScale, 0.92f, 0.82f, 0.82f, 0.85f);
        drawRect(trashX + 23.0f * uiScale, trashY + 18.0f * uiScale, 2.0f * uiScale,
                 12.0f * uiScale, 0.92f, 0.82f, 0.82f, 0.85f);
        drawRect(trashX + 28.0f * uiScale, trashY + 18.0f * uiScale, 2.0f * uiScale,
                 12.0f * uiScale, 0.92f, 0.82f, 0.82f, 0.85f);
        if (hoveredSlotIndex == trashIndex) {
            drawHoverOutline(trashX, trashY, craftSlot);
        }

        for (int row = 0; row < rows; ++row) {
            for (int col = 0; col < cols; ++col) {
                const int slotIndex = game::Inventory::kHotbarSize + row * cols + col;
                const float sx = invX + col * (invSlot + invGap);
                const float sy = invY + row * (invSlot + invGap);
                drawRect(sx + 2.0f, sy + 2.0f, invSlot, invSlot, 0.0f, 0.0f, 0.0f, 0.32f);
                drawRect(sx, sy, invSlot, invSlot, 0.09f, 0.10f, 0.12f, 0.88f);
                drawRect(sx + 2.0f, sy + 2.0f, invSlot - 4.0f, invSlot - 4.0f, 0.16f, 0.17f, 0.20f,
                         0.76f);

                const voxel::BlockDef &def = registry.get(allIds[slotIndex]);
                const float ix = sx + 8.0f * uiScale;
                const float iy = sy + 8.0f * uiScale;
                const float iw = invSlot - 16.0f * uiScale;
                const float ih = invSlot - 16.0f * uiScale;
                if (isFlatItemId(allIds[slotIndex])) {
                    const glm::vec4 uv = atlas.uvRect(def.sideTile);
                    appendItemIcon(ix, iy, ix + iw, iy + ih, uv, 1.0f);
                } else {
                    const float cubeInset = 5.0f * uiScale;
                    appendCubeIcon(sx + cubeInset, sy + cubeInset, invSlot - 2.0f * cubeInset,
                                   invSlot - 2.0f * cubeInset, def);
                }

                const int count = allCounts[slotIndex];
                if (count > 0) {
                    slotLabels.push_back(
                        SlotLabel{sx + 5.0f, sy + invSlot - 13.0f, std::to_string(count)});
                }
                if (hoveredSlotIndex == slotIndex) {
                    drawHoverOutline(sx, sy, invSlot);
                    setHoverTip(allIds[slotIndex], allCounts[slotIndex]);
                }
                if (showInventory && allCounts[slotIndex] > 0 &&
                    ((hintFurnaceInput && smeltRules.canSmelt(allIds[slotIndex])) ||
                     (hintFurnaceFuel && smeltRules.isFuel(allIds[slotIndex])))) {
                    drawValidHintOutline(sx, sy, invSlot);
                }
            }
        }

        if (carryingCount > 0 && carryingId != voxel::AIR) {
            const float dragSize = 34.0f * uiScale;
            const float dx = cursorX + 10.0f * uiScale;
            const float dy = cursorY + 8.0f * uiScale;
            dragIconActive = true;
            dragIconId = carryingId;
            dragIconCount = carryingCount;
            dragIconX = dx;
            dragIconY = dy;
            dragIconSize = dragSize;
            dragIconName = carryingName;
        }

        if (showCreativeMenu) {
            auto drawRectClippedCreative = [&](float x, float y, float w, float h, float cx0,
                                               float cy0, float cx1, float cy1, float r, float g,
                                               float b, float a) {
                const float x0 = std::max(x, cx0);
                const float y0 = std::max(y, cy0);
                const float x1 = std::min(x + w, cx1);
                const float y1 = std::min(y + h, cy1);
                if (x1 <= x0 || y1 <= y0) {
                    return;
                }
                drawRect(x0, y0, x1 - x0, y1 - y0, r, g, b, a);
            };
            const float panelHeaderH = 34.0f * uiScale;
            const float panelBodyH = 220.0f * uiScale;
            const float panelH = panelHeaderH + panelBodyH;
            const float panelX = menuInvX;
            const float panelY = invY - panelH - 44.0f * uiScale;
            const float panelW = totalPanelW;
            drawRect(panelX, panelY, panelW, panelH, 0.03f, 0.04f, 0.05f, 0.78f);
            drawText(panelX + 10.0f * uiScale, panelY + 10.0f * uiScale, "Creative", 244, 246, 252,
                     255);
            drawText(panelX + panelW - 36.0f * uiScale, panelY + 9.0f * uiScale, "F: Close", 182,
                     190, 206, 255);

            const float searchX = panelX + 88.0f * uiScale;
            const float searchY = panelY + 6.0f * uiScale;
            const float searchW = panelW - 188.0f * uiScale;
            const float searchH = 22.0f * uiScale;
            drawRect(searchX, searchY, searchW, searchH, 0.06f, 0.08f, 0.10f, 0.95f);
            drawRect(searchX + 1.0f, searchY + 1.0f, searchW - 2.0f, searchH - 2.0f, 0.12f, 0.14f,
                     0.18f, 0.95f);
            const bool blinkOn = (static_cast<int>(std::floor(uiTimeSeconds * 2.0f)) % 2) == 0;
            const std::string searchText =
                creativeSearch.empty() ? "Search items..." : creativeSearch;
            drawText(searchX + 6.0f * uiScale, searchY + 7.0f * uiScale,
                     searchText + (blinkOn ? "_" : ""), creativeSearch.empty() ? 168 : 232,
                     creativeSearch.empty() ? 176 : 238, creativeSearch.empty() ? 190 : 248, 255);

            const float contentX = panelX + 10.0f * uiScale;
            const float contentY = panelY + panelHeaderH;
            const float contentW = panelW - 28.0f * uiScale;
            const float contentH = panelBodyH - 10.0f * uiScale;
            const std::size_t creativeIconClipBatch =
                beginIconClipBatch(contentX, contentY, contentW, contentH);
            drawRect(contentX, contentY, contentW, contentH, 0.09f, 0.10f, 0.12f, 0.70f);

            const float cell = 40.0f * uiScale;
            const float cellGap = 8.0f * uiScale;
            const int columns =
                std::max(1, std::min(9, static_cast<int>((contentW + cellGap) / (cell + cellGap))));
            const float usedW =
                static_cast<float>(columns) * cell + static_cast<float>(columns - 1) * cellGap;
            const float gridInset = std::max(0.0f, (contentW - usedW) * 0.5f);
            const float rowStride = cell + cellGap;
            const int rowsCount =
                (static_cast<int>(creativeItems.size()) + columns - 1) / columns;
            const float totalContentH =
                static_cast<float>(rowsCount) * cell +
                static_cast<float>(std::max(0, rowsCount - 1)) * cellGap;
            const float maxScroll = std::max(0.0f, totalContentH - contentH);
            const float scroll = std::clamp(creativeScroll, 0.0f, maxScroll);

            for (std::size_t i = 0; i < creativeItems.size(); ++i) {
                const int row = static_cast<int>(i) / columns;
                const int col = static_cast<int>(i) % columns;
                const float sx = contentX + gridInset + static_cast<float>(col) * (cell + cellGap);
                const float sy = contentY + static_cast<float>(row) * rowStride - scroll;
                if (sy + cell < contentY || sy > (contentY + contentH)) {
                    continue;
                }

                drawRectClippedCreative(sx + 2.0f, sy + 2.0f, cell, cell, contentX, contentY,
                                        contentX + contentW, contentY + contentH, 0.0f, 0.0f, 0.0f,
                                        0.32f);
                drawRectClippedCreative(sx, sy, cell, cell, contentX, contentY, contentX + contentW,
                                        contentY + contentH, 0.09f, 0.10f, 0.12f, 0.88f);
                drawRectClippedCreative(sx + 2.0f, sy + 2.0f, cell - 4.0f, cell - 4.0f, contentX,
                                        contentY, contentX + contentW, contentY + contentH, 0.16f,
                                        0.17f, 0.20f, 0.76f);

                const voxel::BlockId id = creativeItems[i];
                const voxel::BlockDef &def = registry.get(id);
                if (isFlatItemId(id)) {
                    const float ix = sx + 7.0f * uiScale;
                    const float iy = sy + 7.0f * uiScale;
                    const float is = cell - 14.0f * uiScale;
                    const glm::vec4 uv = atlas.uvRect(def.sideTile);
                    appendItemIcon(ix, iy, ix + is, iy + is, uv, 1.0f);
                } else {
                    const float cubeInset = 4.0f * uiScale;
                    appendCubeIcon(sx + cubeInset, sy + cubeInset, cell - 2.0f * cubeInset,
                                   cell - 2.0f * cubeInset, def);
                }
                if (cursorX >= sx && cursorX <= (sx + cell) && cursorY >= sy && cursorY <= (sy + cell)) {
                    recipeHoverTipText = game::blockName(id);
                    recipeHoverTipX = cursorX + 12.0f * uiScale;
                    recipeHoverTipY = cursorY - 8.0f * uiScale;
                }
            }

            const float trackX = panelX + panelW - 12.0f * uiScale;
            const float trackY = contentY;
            const float trackH = contentH;
            drawRect(trackX, trackY, 4.0f * uiScale, trackH, 0.14f, 0.16f, 0.18f, 0.95f);
            if (maxScroll > 0.0f) {
                const float thumbH = std::max(22.0f * uiScale, trackH * (contentH / totalContentH));
                const float thumbTravel = std::max(0.0f, trackH - thumbH);
                const float t = scroll / maxScroll;
                const float thumbY = trackY + t * thumbTravel;
                drawRect(trackX - 2.0f * uiScale, thumbY, 8.0f * uiScale, thumbH, 0.30f, 0.72f, 0.95f,
                         0.95f);
            } else {
                drawRect(trackX - 2.0f * uiScale, trackY, 8.0f * uiScale, trackH, 0.24f, 0.28f, 0.33f,
                         0.85f);
            }
            endIconClipBatch(creativeIconClipBatch);
        }

        if (showRecipeMenu) {
            auto drawRectClipped = [&](float x, float y, float w, float h, float cx0, float cy0,
                                       float cx1, float cy1, float r, float g, float b, float a) {
                const float x0 = std::max(x, cx0);
                const float y0 = std::max(y, cy0);
                const float x1 = std::min(x + w, cx1);
                const float y1 = std::min(y + h, cy1);
                if (x1 <= x0 || y1 <= y0) {
                    return;
                }
                drawRect(x0, y0, x1 - x0, y1 - y0, r, g, b, a);
            };
            auto drawTextClipped = [&](float x, float y, const std::string &text, float cx0, float cy0,
                                       float cx1, float cy1, unsigned char r, unsigned char g,
                                       unsigned char b, unsigned char a) {
                char buffer[99999];
                unsigned char color[4] = {r, g, b, a};
                const int quads = stb_easy_font_print(x, y, const_cast<char *>(text.c_str()), color,
                                                      buffer, sizeof(buffer));
                struct StbVert {
                    float x;
                    float y;
                    float z;
                    unsigned char c[4];
                };
                const StbVert *q = reinterpret_cast<const StbVert *>(buffer);
                for (int i = 0; i < quads; ++i) {
                    const StbVert &a0 = q[i * 4 + 0];
                    const StbVert &a2 = q[i * 4 + 2];
                    const float x0 = std::max(a0.x, cx0);
                    const float y0 = std::max(a0.y, cy0);
                    const float x1 = std::min(a2.x, cx1);
                    const float y1 = std::min(a2.y, cy1);
                    if (x1 <= x0 || y1 <= y0) {
                        continue;
                    }
                    const float cr = r / 255.0f;
                    const float cg = g / 255.0f;
                    const float cb = b / 255.0f;
                    const float ca = a / 255.0f;
                    verts_.push_back(UiVertex{x0, y0, cr, cg, cb, ca});
                    verts_.push_back(UiVertex{x1, y0, cr, cg, cb, ca});
                    verts_.push_back(UiVertex{x1, y1, cr, cg, cb, ca});
                    verts_.push_back(UiVertex{x0, y0, cr, cg, cb, ca});
                    verts_.push_back(UiVertex{x1, y1, cr, cg, cb, ca});
                    verts_.push_back(UiVertex{x0, y1, cr, cg, cb, ca});
                }
            };
            if (usingFurnace) {
                const float recipeHeaderH = 34.0f * uiScale;
                const float recipeBodyH = 220.0f * uiScale;
                const float recipeH = recipeHeaderH + recipeBodyH;
                const float recipeX = menuInvX;
                const float recipeY = invY - recipeH - 44.0f * uiScale;
                const float recipeW = totalPanelW;
                drawRect(recipeX, recipeY, recipeW, recipeH, 0.03f, 0.04f, 0.05f, 0.78f);
                drawText(recipeX + 10.0f * uiScale, recipeY + 10.0f * uiScale, "Smelting", 244, 246,
                         252, 255);
                drawText(recipeX + recipeW - 36.0f * uiScale, recipeY + 9.0f * uiScale, "R: Close",
                         182, 190, 206, 255);

                const float searchX = recipeX + 88.0f * uiScale;
                const float searchY = recipeY + 6.0f * uiScale;
                const float searchW = recipeW - 372.0f * uiScale;
                const float searchH = 22.0f * uiScale;
                drawRect(searchX, searchY, searchW, searchH, 0.06f, 0.08f, 0.10f, 0.95f);
                drawRect(searchX + 1.0f, searchY + 1.0f, searchW - 2.0f, searchH - 2.0f, 0.12f, 0.14f,
                         0.18f, 0.95f);
                const std::string searchText =
                    recipeSearch.empty() ? "Search smelting..." : recipeSearch;
                const bool blinkOn = (static_cast<int>(std::floor(uiTimeSeconds * 2.0f)) % 2) == 0;
                const std::string cursorSuffix = blinkOn ? "_" : "";
                drawText(searchX + 6.0f * uiScale, searchY + 7.0f * uiScale, searchText + cursorSuffix,
                         recipeSearch.empty() ? 168 : 232, recipeSearch.empty() ? 176 : 238,
                         recipeSearch.empty() ? 190 : 248, 255);
                const float craftableFilterX = recipeX + recipeW - 124.0f * uiScale;
                const float craftableFilterY = recipeY + 10.0f * uiScale;
                const float craftableFilterS = 14.0f * uiScale;
                drawRect(craftableFilterX, craftableFilterY, craftableFilterS, craftableFilterS, 0.09f,
                         0.10f, 0.12f, 0.95f);
                drawRect(craftableFilterX + 1.0f, craftableFilterY + 1.0f, craftableFilterS - 2.0f,
                         craftableFilterS - 2.0f, recipeCraftableOnly ? 0.28f : 0.15f,
                         recipeCraftableOnly ? 0.74f : 0.18f, recipeCraftableOnly ? 0.42f : 0.21f,
                         0.95f);
                if (recipeCraftableOnly) {
                    const std::string checkText = "X";
                    const float checkX =
                        craftableFilterX + (craftableFilterS - textWidthPx(checkText)) * 0.5f;
                    const float checkY = craftableFilterY + (craftableFilterS - 8.0f) * 0.5f;
                    drawText(checkX, checkY, checkText, 242, 248, 255, 255);
                }
                drawText(craftableFilterX + craftableFilterS + 6.0f * uiScale,
                         craftableFilterY + 4.0f * uiScale, "Smeltable", 198, 214, 236, 255);
                if (recipeIngredientFilter.has_value()) {
                    const std::string ingredientTag =
                        std::string("Ingredient: ") + game::blockName(recipeIngredientFilter.value());
                    const float tagX = craftableFilterX - 154.0f * uiScale;
                    const float tagY = craftableFilterY - 1.0f * uiScale;
                    const float tagW = 146.0f * uiScale;
                    const float tagH = 16.0f * uiScale;
                    const float closeS = 12.0f * uiScale;
                    const float closeX = tagX + tagW - closeS - 2.0f * uiScale;
                    const float closeY = tagY + 2.0f * uiScale;
                    drawRect(tagX, tagY, tagW, tagH, 0.10f, 0.22f, 0.16f, 0.92f);
                    drawRect(tagX + 1.0f, tagY + 1.0f, tagW - 2.0f, tagH - 2.0f, 0.14f, 0.30f, 0.22f,
                             0.95f);
                    drawRect(tagX + 1.0f, tagY + 1.0f, tagW - 2.0f, 2.0f, 0.22f, 0.44f, 0.32f, 0.88f);
                    drawRect(closeX, closeY, closeS, closeS, 0.20f, 0.36f, 0.27f, 0.95f);
                    drawRect(closeX + 1.0f, closeY + 1.0f, closeS - 2.0f, closeS - 2.0f, 0.16f, 0.26f,
                             0.20f, 0.95f);
                    const std::string clearText = "x";
                    const float clearX = closeX + (closeS - textWidthPx(clearText)) * 0.5f;
                    const float clearY = closeY + (closeS - 8.0f) * 0.5f;
                    drawText(clearX, clearY, clearText, 228, 244, 234, 255);
                    drawTextClipped(tagX + 6.0f * uiScale, tagY + 4.0f * uiScale, ingredientTag, tagX,
                                    tagY, closeX - 2.0f * uiScale, tagY + tagH, 208, 236, 218, 255);
                }

                const float contentX = recipeX + 10.0f * uiScale;
                const float contentY = recipeY + recipeHeaderH;
                const float contentW = recipeW - 28.0f * uiScale;
                const float contentH = recipeBodyH - 10.0f * uiScale;
                const std::size_t recipeIconClipBatch =
                    beginIconClipBatch(contentX, contentY, contentW, contentH);
                drawRect(contentX, contentY, contentW, contentH, 0.09f, 0.10f, 0.12f, 0.70f);

                const int columns = 2;
                const float cellGapX = 10.0f * uiScale;
                const float cellGapY = 10.0f * uiScale;
                const float gridInsetRight = 30.0f * uiScale;
                const float gridInsetLeft = gridInsetRight;
                const float cellW = (contentW - gridInsetLeft - gridInsetRight - cellGapX) * 0.5f;
                const float cellH = 74.0f * uiScale;
                const float rowStride = cellH + cellGapY;
                const int rowsCount =
                    (static_cast<int>(smeltingRecipes.size()) + columns - 1) / columns;
                const float totalContentH = static_cast<float>(rowsCount) * cellH +
                                            static_cast<float>(std::max(0, rowsCount - 1)) * cellGapY;
                const float maxScroll = std::max(0.0f, totalContentH - contentH);
                const float scroll = std::clamp(recipeScroll, 0.0f, maxScroll);
                std::string hoveredIngredientName;

                for (std::size_t i = 0; i < smeltingRecipes.size(); ++i) {
                    const auto &recipe = smeltingRecipes[i];
                    const int row = static_cast<int>(i) / columns;
                    const int col = static_cast<int>(i) % columns;
                    const float rx =
                        contentX + gridInsetLeft + static_cast<float>(col) * (cellW + cellGapX);
                    const float ry = contentY + static_cast<float>(row) * rowStride - scroll;
                    if (ry + cellH < contentY || ry > (contentY + contentH)) {
                        continue;
                    }
                    const bool hovered = (cursorX >= rx && cursorX <= (rx + cellW) && cursorY >= ry &&
                                          cursorY <= (ry + cellH));
                    const bool craftable = i < recipeCraftable.size() && recipeCraftable[i];
                    const float baseR = craftable ? 0.12f : 0.10f;
                    const float baseG = craftable ? 0.20f : 0.12f;
                    const float baseB = craftable ? 0.13f : 0.15f;
                    const float glow = hovered ? 0.08f : 0.0f;
                    drawRectClipped(rx, ry, cellW, cellH, contentX, contentY, contentX + contentW,
                                    contentY + contentH, baseR + glow, baseG + glow, baseB + glow,
                                    craftable ? 0.92f : 0.86f);
                    drawRectClipped(rx + 2.0f * uiScale, ry + 2.0f * uiScale, cellW - 4.0f * uiScale,
                                    cellH - 4.0f * uiScale, contentX, contentY, contentX + contentW,
                                    contentY + contentH, craftable ? 0.18f : 0.14f,
                                    craftable ? 0.22f : 0.16f, craftable ? 0.18f : 0.20f, 0.82f);

                    const float iconS = 36.0f * uiScale;
                    const float iconY = ry + 8.0f * uiScale;
                    float iconX = rx + 10.0f * uiScale;
                    const voxel::BlockDef &inDef = registry.get(recipe.input);
                    drawRectClipped(iconX, iconY, iconS, iconS, contentX, contentY, contentX + contentW,
                                    contentY + contentH, 0.08f, 0.10f, 0.12f, 0.95f);
                    if (isFlatItemId(recipe.input)) {
                        const glm::vec4 inUv = atlas.uvRect(inDef.sideTile);
                        appendItemIcon(iconX + 4.0f * uiScale, iconY + 4.0f * uiScale,
                                       iconX + iconS - 4.0f * uiScale, iconY + iconS - 4.0f * uiScale,
                                       inUv, 1.0f);
                    } else {
                        const float inset = 2.0f * uiScale;
                        appendCubeIcon(iconX + inset, iconY + inset, iconS - 2.0f * inset,
                                       iconS - 2.0f * inset, inDef);
                    }
                    if (cursorX >= iconX && cursorX <= (iconX + iconS) && cursorY >= iconY &&
                        cursorY <= (iconY + iconS)) {
                        hoveredIngredientName = game::blockName(recipe.input);
                    }
                    iconX += 46.0f * uiScale;
                    drawTextClipped(iconX, ry + 21.0f * uiScale, "->", contentX, contentY,
                                    contentX + contentW, contentY + contentH, 230, 236, 248, 255);
                    iconX += 18.0f * uiScale;
                    const voxel::BlockDef &outDef = registry.get(recipe.output);
                    drawRectClipped(iconX, iconY, iconS, iconS, contentX, contentY, contentX + contentW,
                                    contentY + contentH, 0.08f, 0.10f, 0.12f, 0.95f);
                    if (isFlatItemId(recipe.output)) {
                        const glm::vec4 outUv = atlas.uvRect(outDef.sideTile);
                        appendItemIcon(iconX + 4.0f * uiScale, iconY + 4.0f * uiScale,
                                       iconX + iconS - 4.0f * uiScale, iconY + iconS - 4.0f * uiScale,
                                       outUv, 1.0f);
                    } else {
                        const float inset = 2.0f * uiScale;
                        appendCubeIcon(iconX + inset, iconY + inset, iconS - 2.0f * inset,
                                       iconS - 2.0f * inset, outDef);
                    }
                    if (cursorX >= iconX && cursorX <= (iconX + iconS) && cursorY >= iconY &&
                        cursorY <= (iconY + iconS)) {
                        hoveredIngredientName = game::blockName(recipe.output);
                    }
                    drawTextClipped(iconX + 2.0f * uiScale, iconY + iconS - 10.0f * uiScale,
                                    std::to_string(std::max(1, recipe.outputCount)), contentX,
                                    contentY, contentX + contentW, contentY + contentH, 232, 236, 246,
                                    255);
                    const std::string label = game::blockName(recipe.output);
                    const float labelW = std::clamp(textWidthPx(label) + 14.0f * uiScale,
                                                    46.0f * uiScale, cellW - 14.0f * uiScale);
                    const float labelX = rx + (cellW - labelW) * 0.5f;
                    const float labelY = ry + cellH - 18.0f * uiScale;
                    const bool labelFullyVisible =
                        labelY >= contentY && (labelY + 14.0f * uiScale) <= (contentY + contentH);
                    if (labelFullyVisible) {
                        recipeNameLabels.push_back({labelX, labelY, labelW, 14.0f * uiScale, label});
                    }
                }
                endIconClipBatch(recipeIconClipBatch);

                const float trackX = recipeX + recipeW - 12.0f * uiScale;
                const float trackY = contentY;
                const float trackH = contentH;
                drawRect(trackX, trackY, 4.0f * uiScale, trackH, 0.14f, 0.16f, 0.18f, 0.95f);
                if (maxScroll > 0.0f) {
                    const float thumbH =
                        std::max(22.0f * uiScale, trackH * (contentH / totalContentH));
                    const float thumbTravel = std::max(0.0f, trackH - thumbH);
                    const float t = scroll / maxScroll;
                    const float thumbY = trackY + t * thumbTravel;
                    drawRect(trackX - 2.0f * uiScale, thumbY, 8.0f * uiScale, thumbH, 0.30f, 0.72f,
                             0.95f, 0.95f);
                } else {
                    drawRect(trackX - 2.0f * uiScale, trackY, 8.0f * uiScale, trackH, 0.24f, 0.28f,
                             0.33f, 0.85f);
                }
                if (!hoveredIngredientName.empty()) {
                    recipeHoverTipText = hoveredIngredientName;
                    recipeHoverTipX = cursorX + 12.0f * uiScale;
                    recipeHoverTipY = cursorY - 8.0f * uiScale;
                }
            } else {
            const float recipeHeaderH = 34.0f * uiScale;
            const float recipeBodyH = 220.0f * uiScale;
            const float recipeH = recipeHeaderH + recipeBodyH;
            const float recipeX = menuInvX;
            const float recipeY = invY - recipeH - 44.0f * uiScale;
            const float recipeW = totalPanelW;
            drawRect(recipeX, recipeY, recipeW, recipeH, 0.03f, 0.04f, 0.05f, 0.78f);
            drawText(recipeX + 10.0f * uiScale, recipeY + 10.0f * uiScale, "Recipes", 244, 246, 252,
                     255);
            drawText(recipeX + recipeW - 36.0f * uiScale, recipeY + 9.0f * uiScale,
                     "R: Close", 182, 190, 206, 255);

            const float searchX = recipeX + 88.0f * uiScale;
            const float searchY = recipeY + 6.0f * uiScale;
            const float searchW = recipeW - 372.0f * uiScale;
            const float searchH = 22.0f * uiScale;
            drawRect(searchX, searchY, searchW, searchH, 0.06f, 0.08f, 0.10f, 0.95f);
            drawRect(searchX + 1.0f, searchY + 1.0f, searchW - 2.0f, searchH - 2.0f, 0.12f, 0.14f,
                     0.18f, 0.95f);
            const std::string searchText =
                recipeSearch.empty() ? "Search recipes..." : recipeSearch;
            const bool blinkOn = (static_cast<int>(std::floor(uiTimeSeconds * 2.0f)) % 2) == 0;
            const std::string cursorSuffix = blinkOn ? "_" : "";
            drawText(searchX + 6.0f * uiScale, searchY + 7.0f * uiScale, searchText + cursorSuffix,
                     recipeSearch.empty() ? 168 : 232, recipeSearch.empty() ? 176 : 238,
                     recipeSearch.empty() ? 190 : 248, 255);
            const float craftableFilterX = recipeX + recipeW - 124.0f * uiScale;
            const float craftableFilterY = recipeY + 10.0f * uiScale;
            const float craftableFilterS = 14.0f * uiScale;
            drawRect(craftableFilterX, craftableFilterY, craftableFilterS, craftableFilterS, 0.09f,
                     0.10f, 0.12f, 0.95f);
            drawRect(craftableFilterX + 1.0f, craftableFilterY + 1.0f, craftableFilterS - 2.0f,
                     craftableFilterS - 2.0f, recipeCraftableOnly ? 0.28f : 0.15f,
                     recipeCraftableOnly ? 0.74f : 0.18f, recipeCraftableOnly ? 0.42f : 0.21f,
                     0.95f);
            if (recipeCraftableOnly) {
                const std::string checkText = "X";
                const float checkX =
                    craftableFilterX + (craftableFilterS - textWidthPx(checkText)) * 0.5f;
                const float checkY = craftableFilterY + (craftableFilterS - 8.0f) * 0.5f;
                drawText(checkX, checkY, checkText, 242, 248, 255, 255);
            }
            drawText(craftableFilterX + craftableFilterS + 6.0f * uiScale,
                     craftableFilterY + 4.0f * uiScale, "Craftable", 198, 214, 236, 255);
            if (recipeIngredientFilter.has_value()) {
                const std::string ingredientTag =
                    std::string("Ingredient: ") + game::blockName(recipeIngredientFilter.value());
                const float tagX = craftableFilterX - 154.0f * uiScale;
                const float tagY = craftableFilterY - 1.0f * uiScale;
                const float tagW = 146.0f * uiScale;
                const float tagH = 16.0f * uiScale;
                const float closeS = 12.0f * uiScale;
                const float closeX = tagX + tagW - closeS - 2.0f * uiScale;
                const float closeY = tagY + 2.0f * uiScale;
                drawRect(tagX, tagY, tagW, tagH, 0.10f, 0.22f, 0.16f, 0.92f);
                drawRect(tagX + 1.0f, tagY + 1.0f, tagW - 2.0f, tagH - 2.0f, 0.14f, 0.30f, 0.22f,
                         0.95f);
                drawRect(tagX + 1.0f, tagY + 1.0f, tagW - 2.0f, 2.0f, 0.22f, 0.44f, 0.32f, 0.88f);
                drawRect(closeX, closeY, closeS, closeS, 0.20f, 0.36f, 0.27f, 0.95f);
                drawRect(closeX + 1.0f, closeY + 1.0f, closeS - 2.0f, closeS - 2.0f, 0.16f, 0.26f,
                         0.20f, 0.95f);
                const std::string clearText = "x";
                const float clearX = closeX + (closeS - textWidthPx(clearText)) * 0.5f;
                const float clearY = closeY + (closeS - 8.0f) * 0.5f;
                drawText(clearX, clearY, clearText, 228, 244, 234, 255);
                drawTextClipped(tagX + 6.0f * uiScale, tagY + 4.0f * uiScale, ingredientTag, tagX,
                                tagY, closeX - 2.0f * uiScale, tagY + tagH, 208, 236, 218, 255);
            }

            const float contentX = recipeX + 10.0f * uiScale;
            const float contentY = recipeY + recipeHeaderH;
            const float contentW = recipeW - 28.0f * uiScale;
            const float contentH = recipeBodyH - 10.0f * uiScale;
            const std::size_t recipeIconClipBatch =
                beginIconClipBatch(contentX, contentY, contentW, contentH);
            const int columns = 2;
            const float cellGapX = 10.0f * uiScale;
            const float cellGapY = 10.0f * uiScale;
            const float gridInsetRight = 30.0f * uiScale;
            const float gridInsetLeft = gridInsetRight;
            const float cellW = (contentW - gridInsetLeft - gridInsetRight - cellGapX) * 0.5f;
            const float cellH = 78.0f * uiScale;
            const float rowStride = cellH + cellGapY;
            const int rowsCount =
                (static_cast<int>(recipes.size()) + columns - 1) / columns;
            const float totalContentH = static_cast<float>(rowsCount) * cellH +
                                        static_cast<float>(std::max(0, rowsCount - 1)) * cellGapY;
            const float maxScroll = std::max(0.0f, totalContentH - contentH);
            const float scroll = std::clamp(recipeScroll, 0.0f, maxScroll);
            std::string hoveredIngredientName;

            const std::array<voxel::BlockId, 3> woodCycle = {
                voxel::WOOD,
                voxel::SPRUCE_WOOD,
                voxel::BIRCH_WOOD,
            };
            const std::array<voxel::BlockId, 3> plankCycle = {
                voxel::OAK_PLANKS,
                voxel::SPRUCE_PLANKS,
                voxel::BIRCH_PLANKS,
            };
            const int cycleIndex =
                static_cast<int>(std::floor(std::max(0.0f, uiTimeSeconds) * 0.6f)) %
                static_cast<int>(woodCycle.size());

            drawRect(contentX, contentY, contentW, contentH, 0.09f, 0.10f, 0.12f, 0.70f);
            for (std::size_t i = 0; i < recipes.size(); ++i) {
                const auto &recipe = recipes[i];
                const int row = static_cast<int>(i) / columns;
                const int col = static_cast<int>(i) % columns;
                const float rx = contentX + gridInsetLeft + static_cast<float>(col) * (cellW + cellGapX);
                const float ry = contentY + static_cast<float>(row) * rowStride - scroll;
                if (ry + cellH < contentY || ry > (contentY + contentH)) {
                    continue;
                }
                const bool hovered = (cursorX >= rx && cursorX <= (rx + cellW) && cursorY >= ry &&
                                      cursorY <= (ry + cellH));
                const bool craftable = i < recipeCraftable.size() && recipeCraftable[i];
                const float baseR = craftable ? 0.12f : 0.10f;
                const float baseG = craftable ? 0.20f : 0.12f;
                const float baseB = craftable ? 0.13f : 0.15f;
                const float glow = hovered ? 0.08f : 0.0f;
                drawRectClipped(rx, ry, cellW, cellH, contentX, contentY, contentX + contentW,
                                contentY + contentH, baseR + glow, baseG + glow, baseB + glow,
                                craftable ? 0.92f : 0.86f);
                drawRectClipped(rx + 2.0f * uiScale, ry + 2.0f * uiScale, cellW - 4.0f * uiScale,
                                cellH - 4.0f * uiScale, contentX, contentY, contentX + contentW,
                                contentY + contentH, craftable ? 0.18f : 0.14f,
                                craftable ? 0.22f : 0.16f, craftable ? 0.18f : 0.20f, 0.82f);
                if (hovered) {
                    drawRectClipped(rx - 1.0f, ry - 1.0f, cellW + 2.0f, 2.0f, contentX, contentY,
                                    contentX + contentW, contentY + contentH, 0.38f, 0.80f, 1.0f,
                                    0.95f);
                    drawRectClipped(rx - 1.0f, ry + cellH - 1.0f, cellW + 2.0f, 2.0f, contentX,
                                    contentY, contentX + contentW, contentY + contentH, 0.38f, 0.80f,
                                    1.0f, 0.95f);
                    drawRectClipped(rx - 1.0f, ry, 2.0f, cellH, contentX, contentY, contentX + contentW,
                                    contentY + contentH, 0.38f, 0.80f, 1.0f, 0.95f);
                    drawRectClipped(rx + cellW - 1.0f, ry, 2.0f, cellH, contentX, contentY,
                                    contentX + contentW, contentY + contentH, 0.38f, 0.80f, 1.0f,
                                    0.95f);
                }

                float iconX = rx + 8.0f * uiScale;
                const float iconY = ry + 7.0f * uiScale;
                const float iconS = 36.0f * uiScale;

                for (const auto &in : recipe.ingredients) {
                    voxel::BlockId renderId = in.id;
                    if (in.allowAnyWood) {
                        renderId = woodCycle[cycleIndex];
                    } else if (in.allowAnyPlanks) {
                        renderId = plankCycle[cycleIndex];
                    }
                    const voxel::BlockDef &def = registry.get(renderId);
                    drawRectClipped(iconX, iconY, iconS, iconS, contentX, contentY, contentX + contentW,
                                    contentY + contentH, 0.08f, 0.10f, 0.12f, 0.95f);
                    const float drawX = iconX + 4.0f * uiScale;
                    const float drawY = iconY + 4.0f * uiScale;
                    const float drawS = iconS - 8.0f * uiScale;
                    if (isFlatItemId(renderId)) {
                        const glm::vec4 uv = atlas.uvRect(def.sideTile);
                        appendItemIcon(drawX, drawY, drawX + drawS, drawY + drawS, uv, 1.0f);
                    } else {
                        const float cubeInset = 2.0f * uiScale;
                        appendCubeIcon(iconX + cubeInset, iconY + cubeInset, iconS - 2.0f * cubeInset,
                                       iconS - 2.0f * cubeInset, def);
                    }
                    drawTextClipped(iconX + 2.0f * uiScale, iconY + iconS - 10.0f * uiScale,
                                    std::to_string(in.count), contentX, contentY, contentX + contentW,
                                    contentY + contentH, 232, 236, 246, 255);
                    if (cursorX >= iconX && cursorX <= (iconX + iconS) && cursorY >= iconY &&
                        cursorY <= (iconY + iconS)) {
                        hoveredIngredientName =
                            (in.allowAnyWood || in.allowAnyPlanks) ? game::blockName(renderId)
                                                                    : game::blockName(in.id);
                    }
                    iconX += 48.0f * uiScale;
                }

                drawTextClipped(iconX, ry + 19.0f * uiScale, "->", contentX, contentY,
                                contentX + contentW, contentY + contentH, 230, 236, 248, 255);
                iconX += 20.0f * uiScale;

                const voxel::BlockDef &outDef = registry.get(recipe.outputId);
                drawRectClipped(iconX, iconY, iconS, iconS, contentX, contentY, contentX + contentW,
                                contentY + contentH, 0.08f, 0.10f, 0.12f, 0.95f);
                const float outDrawX = iconX + 4.0f * uiScale;
                const float outDrawY = iconY + 4.0f * uiScale;
                const float outDrawS = iconS - 8.0f * uiScale;
                if (isFlatItemId(recipe.outputId)) {
                    const glm::vec4 outUv = atlas.uvRect(outDef.sideTile);
                    appendItemIcon(outDrawX, outDrawY, outDrawX + outDrawS, outDrawY + outDrawS,
                                   outUv, 1.0f);
                } else {
                    const float outCubeInset = 2.0f * uiScale;
                    appendCubeIcon(iconX + outCubeInset, iconY + outCubeInset,
                                   iconS - 2.0f * outCubeInset, iconS - 2.0f * outCubeInset, outDef);
                }
                if (cursorX >= iconX && cursorX <= (iconX + iconS) && cursorY >= iconY &&
                    cursorY <= (iconY + iconS)) {
                    hoveredIngredientName = game::blockName(recipe.outputId);
                }
                drawTextClipped(iconX + 2.0f * uiScale, iconY + iconS - 10.0f * uiScale,
                                std::to_string(recipe.outputCount), contentX, contentY,
                                contentX + contentW, contentY + contentH, 232, 236, 246, 255);
                const float labelPad = 7.0f * uiScale;
                const float labelH = 14.0f * uiScale;
                const float labelTextW = textWidthPx(recipe.label);
                const float labelW = std::clamp(labelTextW + labelPad * 2.0f, 42.0f * uiScale,
                                                cellW - 18.0f * uiScale);
                const float labelX = rx + (cellW - labelW) * 0.5f;
                const float labelY = ry + cellH - labelH - 4.0f * uiScale;
                const bool labelFullyVisible = labelY >= contentY &&
                                               (labelY + labelH) <= (contentY + contentH);
                if (labelFullyVisible) {
                    recipeNameLabels.push_back({labelX, labelY, labelW, labelH, recipe.label});
                }
            }
            endIconClipBatch(recipeIconClipBatch);

            const float trackX = recipeX + recipeW - 12.0f * uiScale;
            const float trackY = contentY;
            const float trackH = contentH;
            drawRect(trackX, trackY, 4.0f * uiScale, trackH, 0.14f, 0.16f, 0.18f, 0.95f);
            if (maxScroll > 0.0f) {
                const float thumbH = std::max(22.0f * uiScale, trackH * (contentH / totalContentH));
                const float thumbTravel = std::max(0.0f, trackH - thumbH);
                const float t = scroll / maxScroll;
                const float thumbY = trackY + t * thumbTravel;
                drawRect(trackX - 2.0f * uiScale, thumbY, 8.0f * uiScale, thumbH, 0.30f, 0.72f, 0.95f,
                         0.95f);
            } else {
                drawRect(trackX - 2.0f * uiScale, trackY, 8.0f * uiScale, trackH, 0.24f, 0.28f, 0.33f,
                         0.85f);
            }
            if (!hoveredIngredientName.empty()) {
                recipeHoverTipText = hoveredIngredientName;
                recipeHoverTipX = cursorX + 12.0f * uiScale;
                recipeHoverTipY = cursorY - 8.0f * uiScale;
            }
            }
        }
    }

    // Context info panel (top-left): looked block, mode, compass, and player coordinates.
    const float panelX = 14.0f * uiScale;
    const float panelY = 14.0f * uiScale;
    const float panelW = 280.0f * uiScale;
    const float panelH = 66.0f * uiScale;
    drawRect(panelX, panelY, panelW, panelH, 0.03f, 0.04f, 0.05f, 0.58f);
    drawText(panelX + 8.0f, panelY + 8.0f, lookedAtText, 235, 239, 247, 255);
    drawText(panelX + 8.0f, panelY + 22.0f, modeText, 216, 222, 236, 255);
    drawText(panelX + 8.0f, panelY + 36.0f, compassText, 216, 222, 236, 255);
    drawText(panelX + 8.0f, panelY + 50.0f, coordText, 216, 222, 236, 255);

    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glDisable(GL_DEPTH_TEST);

    glUseProgram(uiShader_);
    glUniform2f(glGetUniformLocation(uiShader_, "uScreen"), static_cast<float>(width),
                static_cast<float>(height));
    glBindVertexArray(uiVao_);
    glBindBuffer(GL_ARRAY_BUFFER, uiVbo_);
    glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(verts_.size() * sizeof(UiVertex)),
                 verts_.data(), GL_DYNAMIC_DRAW);
    glDrawArrays(GL_TRIANGLES, 0, static_cast<GLsizei>(verts_.size()));

    glUseProgram(iconShader_);
    glUniform2f(glGetUniformLocation(iconShader_, "uScreen"), static_cast<float>(width),
                static_cast<float>(height));
    glUniform1i(glGetUniformLocation(iconShader_, "uAtlas"), 0);
    atlas.bind(0);
    glBindVertexArray(iconVao_);
    glBindBuffer(GL_ARRAY_BUFFER, iconVbo_);
    glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(iconVerts_.size() * sizeof(IconVertex)),
                 iconVerts_.data(), GL_DYNAMIC_DRAW);
    if (!iconClipBatches.empty()) {
        int viewport[4] = {0, 0, width, height};
        glGetIntegerv(GL_VIEWPORT, viewport);
        const float sx =
            (width > 0) ? (static_cast<float>(viewport[2]) / static_cast<float>(width)) : 1.0f;
        const float sy =
            (height > 0) ? (static_cast<float>(viewport[3]) / static_cast<float>(height)) : 1.0f;
        std::size_t cursor = 0;
        for (const IconClipBatch &batch : iconClipBatches) {
            if (batch.start > cursor) {
                glDrawArrays(GL_TRIANGLES, static_cast<GLint>(cursor),
                             static_cast<GLsizei>(batch.start - cursor));
            }
            if (batch.count > 0) {
                const int scX =
                    viewport[0] + std::max(0, static_cast<int>(std::floor(batch.x * sx)));
                const int scY = viewport[1] +
                                std::max(0, static_cast<int>(std::floor(
                                                  (static_cast<float>(height) - (batch.y + batch.h)) *
                                                  sy)));
                const int scW = std::max(0, static_cast<int>(std::ceil(batch.w * sx)));
                const int scH = std::max(0, static_cast<int>(std::ceil(batch.h * sy)));
                glEnable(GL_SCISSOR_TEST);
                glScissor(scX, scY, scW, scH);
                glDrawArrays(GL_TRIANGLES, static_cast<GLint>(batch.start),
                             static_cast<GLsizei>(batch.count));
                glDisable(GL_SCISSOR_TEST);
            }
            cursor = std::max(cursor, batch.start + batch.count);
        }
        if (cursor < iconVerts_.size()) {
            glDrawArrays(GL_TRIANGLES, static_cast<GLint>(cursor),
                         static_cast<GLsizei>(iconVerts_.size() - cursor));
        }
    } else {
        glDrawArrays(GL_TRIANGLES, 0, static_cast<GLsizei>(iconVerts_.size()));
    }

    // Draw recipe labels and slot numbers after icons so they overlay block textures.
    verts_.clear();
    for (const RecipeNameLabel &label : recipeNameLabels) {
        drawRect(label.x, label.y, label.w, label.h, 0.04f, 0.06f, 0.08f, 0.86f);
        drawRect(label.x + 1.0f, label.y + 1.0f, label.w - 2.0f, label.h - 2.0f, 0.11f, 0.14f,
                 0.19f, 0.92f);
        const float textX = label.x + (label.w - textWidthPx(label.text)) * 0.5f;
        const float textY = label.y + (label.h - 8.0f) * 0.5f;
        drawText(textX + 1.0f, textY + 1.0f, label.text, 14, 16, 20, 220);
        drawText(textX, textY, label.text, 228, 234, 246, 255);
    }
    for (const SlotLabel &label : slotLabels) {
        drawText(label.x + 1.0f, label.y + 1.0f, label.text, 20, 20, 24, 220);
        drawText(label.x, label.y, label.text, 246, 247, 250, 255);
    }
    if (!recipeHoverTipText.empty() && !dragIconActive) {
        drawTooltipPanel(recipeHoverTipText, recipeHoverTipX, recipeHoverTipY);
    }
    glUseProgram(uiShader_);
    glUniform2f(glGetUniformLocation(uiShader_, "uScreen"), static_cast<float>(width),
                static_cast<float>(height));
    glBindVertexArray(uiVao_);
    glBindBuffer(GL_ARRAY_BUFFER, uiVbo_);
    glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(verts_.size() * sizeof(UiVertex)),
                 verts_.data(), GL_DYNAMIC_DRAW);
    glDrawArrays(GL_TRIANGLES, 0, static_cast<GLsizei>(verts_.size()));

    if (dragIconActive && dragIconId != voxel::AIR && dragIconCount > 0) {
        verts_.clear();
        drawRect(dragIconX - 4.0f * uiScale, dragIconY - 3.0f * uiScale, dragIconSize + 8.0f * uiScale,
                 dragIconSize + 8.0f * uiScale, 0.0f, 0.0f, 0.0f, 0.38f);
        drawRect(dragIconX - 1.0f * uiScale, dragIconY - 1.0f * uiScale, dragIconSize + 2.0f * uiScale,
                 dragIconSize + 2.0f * uiScale, 0.08f, 0.10f, 0.12f, 0.92f);
        drawRect(dragIconX, dragIconY, dragIconSize, dragIconSize, 0.16f, 0.18f, 0.22f, 0.78f);

        const std::string dragCountText = std::to_string(dragIconCount);
        const float chipW = textWidthPx(dragCountText) + 8.0f * uiScale;
        const float chipH = 12.0f * uiScale;
        const float chipX = dragIconX + dragIconSize - chipW + 2.0f * uiScale;
        const float chipY = dragIconY + dragIconSize - chipH + 1.0f * uiScale;
        drawRect(chipX, chipY, chipW, chipH, 0.04f, 0.06f, 0.08f, 0.92f);
        drawRect(chipX + 1.0f, chipY + 1.0f, chipW - 2.0f, chipH - 2.0f, 0.12f, 0.15f, 0.20f, 0.95f);
        const float countTx = chipX + (chipW - textWidthPx(dragCountText)) * 0.5f;
        const float countTy = chipY + (chipH - 8.0f) * 0.5f;
        drawText(countTx + 1.0f, countTy + 1.0f, dragCountText, 16, 18, 22, 220);
        drawText(countTx, countTy, dragCountText, 238, 244, 252, 255);

        if (!dragIconName.empty()) {
            drawTooltipPanel(dragIconName, dragIconX, dragIconY - 18.0f * uiScale);
        }
        glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(verts_.size() * sizeof(UiVertex)),
                     verts_.data(), GL_DYNAMIC_DRAW);
        glDrawArrays(GL_TRIANGLES, 0, static_cast<GLsizei>(verts_.size()));

        iconVerts_.clear();
        const voxel::BlockDef &dragDef = registry.get(dragIconId);
        if (isFlatItemId(dragIconId)) {
            const glm::vec4 uv = atlas.uvRect(dragDef.sideTile);
            appendItemIcon(dragIconX, dragIconY, dragIconX + dragIconSize, dragIconY + dragIconSize, uv,
                           1.0f);
        } else {
            appendCubeIcon(dragIconX, dragIconY, dragIconSize, dragIconSize, dragDef);
        }
        glUseProgram(iconShader_);
        glUniform2f(glGetUniformLocation(iconShader_, "uScreen"), static_cast<float>(width),
                    static_cast<float>(height));
        glUniform1i(glGetUniformLocation(iconShader_, "uAtlas"), 0);
        atlas.bind(0);
        glBindVertexArray(iconVao_);
        glBindBuffer(GL_ARRAY_BUFFER, iconVbo_);
        glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(iconVerts_.size() * sizeof(IconVertex)),
                     iconVerts_.data(), GL_DYNAMIC_DRAW);
        glDrawArrays(GL_TRIANGLES, 0, static_cast<GLsizei>(iconVerts_.size()));
    }

    glEnable(GL_DEPTH_TEST);
}

void HudRenderer::renderBreakOverlay(const glm::mat4 &proj, const glm::mat4 &view,
                                     const std::optional<glm::ivec3> &block, float breakProgress,
                                     voxel::BlockId blockId, const TextureAtlas &atlas) {
    if (!block.has_value()) {
        return;
    }
    breakProgress = glm::clamp(breakProgress, 0.0f, 1.0f);
    if (breakProgress <= 0.01f) {
        return;
    }
    initCrack();

    constexpr int kCrackTileBase = 32;
    const bool isPlant =
        (blockId == voxel::TALL_GRASS || blockId == voxel::FLOWER || voxel::isTorch(blockId));
    // Plants look better with a lighter/faster crack progression.
    const float eased = std::pow(breakProgress, isPlant ? 1.10f : 1.35f);
    const int stage0 = std::min(9, static_cast<int>(std::floor(eased * (isPlant ? 4.0f : 7.0f))));

    glm::mat4 model(1.0f);
    model = glm::translate(model, glm::vec3(block->x, block->y, block->z));
    const float grow =
        isPlant ? (0.0015f + 0.0035f * breakProgress) : (0.0045f + 0.0100f * breakProgress);
    model = glm::translate(model, glm::vec3(-grow, -grow, -grow));
    model = glm::scale(model, glm::vec3(1.0f + 2.0f * grow));

    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glDepthMask(GL_FALSE);
    glUseProgram(crackShader_);
    glUniformMatrix4fv(glGetUniformLocation(crackShader_, "uProj"), 1, GL_FALSE,
                       glm::value_ptr(proj));
    glUniformMatrix4fv(glGetUniformLocation(crackShader_, "uView"), 1, GL_FALSE,
                       glm::value_ptr(view));
    glUniformMatrix4fv(glGetUniformLocation(crackShader_, "uModel"), 1, GL_FALSE,
                       glm::value_ptr(model));
    const float baseAlpha =
        isPlant ? (0.18f + 0.34f * breakProgress) : (0.30f + 0.62f * breakProgress);
    glUniform1i(glGetUniformLocation(crackShader_, "uAtlas"), 0);
    glUniform3f(glGetUniformLocation(crackShader_, "uTint"), 0.16f, 0.15f, 0.14f);
    atlas.bind(0);
    glBindVertexArray(crackVao_);

    const glm::vec4 uv0 = atlas.uvRect(static_cast<unsigned int>(kCrackTileBase + stage0));
    glUniform4f(glGetUniformLocation(crackShader_, "uUvRect"), uv0.x, uv0.y, uv0.z, uv0.w);
    glUniform1f(glGetUniformLocation(crackShader_, "uAlpha"), baseAlpha);
    glDrawArrays(GL_TRIANGLES, 0, 36);
    glDepthMask(GL_TRUE);
}

void HudRenderer::renderBlockOutline(const glm::mat4 &proj, const glm::mat4 &view,
                                     const std::optional<glm::ivec3> &block, float breakProgress) {
    if (!block.has_value()) {
        return;
    }
    initLine();

    breakProgress = glm::clamp(breakProgress, 0.0f, 1.0f);
    const float pulse = 0.0025f + 0.005f * breakProgress;

    glUseProgram(lineShader_);
    glUniformMatrix4fv(glGetUniformLocation(lineShader_, "uProj"), 1, GL_FALSE,
                       glm::value_ptr(proj));
    glUniformMatrix4fv(glGetUniformLocation(lineShader_, "uView"), 1, GL_FALSE,
                       glm::value_ptr(view));

    auto drawFrame = [&](float expand, float r, float g, float b, float a) {
        glm::mat4 model(1.0f);
        model = glm::translate(model, glm::vec3(block->x, block->y, block->z));
        model = glm::translate(model, glm::vec3(-expand, -expand, -expand));
        model = glm::scale(model, glm::vec3(1.0f + 2.0f * expand));
        glUniformMatrix4fv(glGetUniformLocation(lineShader_, "uModel"), 1, GL_FALSE,
                           glm::value_ptr(model));
        glUniform4f(glGetUniformLocation(lineShader_, "uColor"), r, g, b, a);
        glBindVertexArray(lineVao_);
        glDrawArrays(GL_LINES, 0, 24);
    };

    // Base outline transitions from warm yellow to red as mining progresses.
    const float r = 1.0f;
    const float g = 0.92f - 0.85f * breakProgress;
    const float b = 0.30f - 0.26f * breakProgress;
    const float a = 1.0f;

    glLineWidth(2.0f + 2.0f * breakProgress);
    drawFrame(pulse, r, g, b, a);

    if (breakProgress > 0.02f) {
        // Animated "collapse" frame that moves toward the center while turning red.
        const float eased = breakProgress * breakProgress;
        const float shrink = -0.46f * eased;
        const float collapseR = 1.0f;
        const float collapseG = 0.16f + 0.14f * (1.0f - eased);
        const float collapseB = 0.08f;
        const float collapseA = 0.45f + 0.45f * breakProgress;
        drawFrame(shrink, collapseR, collapseG, collapseB, collapseA);
    }

    glLineWidth(1.0f);
}

} // namespace gfx
