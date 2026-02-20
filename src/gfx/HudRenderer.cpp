#include "gfx/HudRenderer.hpp"

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
uniform vec2 uScreen;
out vec2 vUV;
void main() {
  vec2 ndc = vec2((aPos.x / uScreen.x) * 2.0 - 1.0, 1.0 - (aPos.y / uScreen.y) * 2.0);
  gl_Position = vec4(ndc, 0.0, 1.0);
  vUV = aUV;
}
)";

    const char *fs = R"(
#version 330 core
in vec2 vUV;
uniform sampler2D uAtlas;
out vec4 FragColor;
void main() {
  vec4 c = texture(uAtlas, vUV);
  if (c.a <= 0.01) discard;
  FragColor = c;
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
                           float cursorX, float cursorY, const std::string &carryingName,
                           const std::string &selectedName, const std::string &lookedAtText,
                           const std::string &modeText, const std::string &biomeText,
                           const std::string &coordText, const voxel::BlockRegistry &registry,
                           const TextureAtlas &atlas) {
    init2D();
    initIcons();
    verts_.clear();
    iconVerts_.clear();

    const float cx = width * 0.5f;
    const float cy = height * 0.5f;
    // Crosshair: crisp 4-arm reticle with center dot and soft outline.
    constexpr float kArmGap = 3.0f;
    constexpr float kArmLen = 6.0f;
    constexpr float kArmThick = 2.0f;

    // Outline/shadow pass.
    drawRect(cx - 1.5f, cy - (kArmGap + kArmLen + 0.5f), 3.0f, kArmLen + 1.0f, 0.02f, 0.02f, 0.02f,
             0.72f);
    drawRect(cx - 1.5f, cy + kArmGap - 0.5f, 3.0f, kArmLen + 1.0f, 0.02f, 0.02f, 0.02f, 0.72f);
    drawRect(cx - (kArmGap + kArmLen + 0.5f), cy - 1.5f, kArmLen + 1.0f, 3.0f, 0.02f, 0.02f, 0.02f,
             0.72f);
    drawRect(cx + kArmGap - 0.5f, cy - 1.5f, kArmLen + 1.0f, 3.0f, 0.02f, 0.02f, 0.02f, 0.72f);
    drawRect(cx - 2.0f, cy - 2.0f, 4.0f, 4.0f, 0.02f, 0.02f, 0.02f, 0.72f);

    // Main white pass.
    drawRect(cx - (kArmThick * 0.5f), cy - (kArmGap + kArmLen), kArmThick, kArmLen, 0.95f, 0.95f,
             0.95f, 0.96f);
    drawRect(cx - (kArmThick * 0.5f), cy + kArmGap, kArmThick, kArmLen, 0.95f, 0.95f, 0.95f, 0.96f);
    drawRect(cx - (kArmGap + kArmLen), cy - (kArmThick * 0.5f), kArmLen, kArmThick, 0.95f, 0.95f,
             0.95f, 0.96f);
    drawRect(cx + kArmGap, cy - (kArmThick * 0.5f), kArmLen, kArmThick, 0.95f, 0.95f, 0.95f, 0.96f);
    drawRect(cx - 1.0f, cy - 1.0f, 2.0f, 2.0f, 1.0f, 1.0f, 1.0f, 0.98f);

    const float slot = 48.0f;
    const float gap = 10.0f;
    const float totalW =
        static_cast<float>(hotbar.size()) * slot + static_cast<float>(hotbar.size() - 1) * gap;
    const float x0 = cx - totalW * 0.5f;
    const float y0 = height - 90.0f;
    drawRect(x0 - 8.0f, y0 - 6.0f, totalW + 16.0f, slot + 12.0f, 0.03f, 0.04f, 0.05f, 0.58f);

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
        const auto uv = atlas.uvRect(def.topTile);
        const float ix = x + 8.0f;
        const float iy = y0 + 8.0f;
        const float iw = slot - 16.0f;
        const float ih = slot - 16.0f;
        iconVerts_.push_back({ix, iy, uv.x, uv.y});
        iconVerts_.push_back({ix + iw, iy, uv.z, uv.y});
        iconVerts_.push_back({ix + iw, iy + ih, uv.z, uv.w});
        iconVerts_.push_back({ix, iy, uv.x, uv.y});
        iconVerts_.push_back({ix + iw, iy + ih, uv.z, uv.w});
        iconVerts_.push_back({ix, iy + ih, uv.x, uv.w});
        if (i == selectedIndex) {
            drawRect(x - 3.0f, y0 - 3.0f, slot + 6.0f, 4.0f, 1.0f, 0.90f, 0.30f, 1.0f);
            drawRect(x - 3.0f, y0 + slot - 1.0f, slot + 6.0f, 4.0f, 1.0f, 0.90f, 0.30f, 1.0f);
            drawRect(x - 3.0f, y0 + 1.0f, 4.0f, slot - 2.0f, 1.0f, 0.90f, 0.30f, 1.0f);
            drawRect(x + slot - 1.0f, y0 + 1.0f, 4.0f, slot - 2.0f, 1.0f, 0.90f, 0.30f, 1.0f);
        }
        const int count = hotbarCounts[i];
        if (count > 0) {
            slotLabels.push_back(SlotLabel{x + 6.0f, y0 + slot - 13.0f, std::to_string(count)});
        }
        slotLabels.push_back(SlotLabel{x + slot - 11.0f, y0 + 4.0f, std::to_string(i + 1)});
    }

    const int clampedSelected =
        glm::clamp(selectedIndex, 0, static_cast<int>(game::Inventory::kHotbarSize) - 1);
    const float selectedSlotCenterX =
        x0 + static_cast<float>(clampedSelected) * (slot + gap) + slot * 0.5f;
    const float nameWidth =
        static_cast<float>(stb_easy_font_width(const_cast<char *>(selectedName.c_str())));
    const float labelY = y0 - 24.0f;
    const float nameX = selectedSlotCenterX - nameWidth * 0.5f;
    drawText(nameX, labelY + 3.0f, selectedName, 255, 245, 200, 255);
    drawText(cx - 176.0f, y0 + slot + 14.0f,
             "1-9 Select  |  LMB Mine  |  RMB Place  |  E Inventory  |  F2 HUD", 200, 206, 222,
             255);

    if (showInventory) {
        const int cols = game::Inventory::kColumns;
        const int rows =
            game::Inventory::kRows - 1; // Top inventory rows, hotbar rendered separately.
        const float invSlot = 48.0f;
        const float invGap = 10.0f;
        const float invW = cols * invSlot + (cols - 1) * invGap;
        const float invH = rows * invSlot + (rows - 1) * invGap;
        const float invX = cx - invW * 0.5f;
        const float invY = y0 - 44.0f - invH;
        drawRect(invX - 10.0f, invY - 26.0f, invW + 20.0f, invH + 36.0f, 0.03f, 0.04f, 0.05f,
                 0.72f);
        drawText(invX, invY - 18.0f, "Inventory", 244, 246, 252, 255);

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
                const auto uv = atlas.uvRect(def.topTile);
                const float ix = sx + 7.0f;
                const float iy = sy + 7.0f;
                const float iw = invSlot - 14.0f;
                const float ih = invSlot - 14.0f;
                iconVerts_.push_back({ix, iy, uv.x, uv.y});
                iconVerts_.push_back({ix + iw, iy, uv.z, uv.y});
                iconVerts_.push_back({ix + iw, iy + ih, uv.z, uv.w});
                iconVerts_.push_back({ix, iy, uv.x, uv.y});
                iconVerts_.push_back({ix + iw, iy + ih, uv.z, uv.w});
                iconVerts_.push_back({ix, iy + ih, uv.x, uv.w});

                const int count = allCounts[slotIndex];
                if (count > 0) {
                    slotLabels.push_back(
                        SlotLabel{sx + 5.0f, sy + invSlot - 13.0f, std::to_string(count)});
                }
            }
        }

        if (carryingCount > 0 && carryingId != voxel::AIR) {
            const voxel::BlockDef &def = registry.get(carryingId);
            const auto uv = atlas.uvRect(def.topTile);
            const float dragSize = 34.0f;
            const float dx = cursorX + 10.0f;
            const float dy = cursorY + 8.0f;
            drawRect(dx - 2.0f, dy - 2.0f, dragSize + 4.0f, dragSize + 4.0f, 0.0f, 0.0f, 0.0f,
                     0.45f);
            iconVerts_.push_back({dx, dy, uv.x, uv.y});
            iconVerts_.push_back({dx + dragSize, dy, uv.z, uv.y});
            iconVerts_.push_back({dx + dragSize, dy + dragSize, uv.z, uv.w});
            iconVerts_.push_back({dx, dy, uv.x, uv.y});
            iconVerts_.push_back({dx + dragSize, dy + dragSize, uv.z, uv.w});
            iconVerts_.push_back({dx, dy + dragSize, uv.x, uv.w});
            slotLabels.push_back(
                SlotLabel{dx + 2.0f, dy + dragSize - 8.0f, std::to_string(carryingCount)});
            if (!carryingName.empty()) {
                const float nameY = dy - 13.0f;
                drawText(dx + 1.0f, nameY + 1.0f, carryingName, 16, 18, 22, 220);
                drawText(dx, nameY, carryingName, 238, 242, 250, 255);
            }
        }
    }

    // Context info panel (top-left): looked block, mode, biome, and player
    // coordinates.
    const float panelX = 14.0f;
    const float panelY = 14.0f;
    const float panelW = 280.0f;
    const float panelH = 66.0f;
    drawRect(panelX, panelY, panelW, panelH, 0.03f, 0.04f, 0.05f, 0.58f);
    drawText(panelX + 8.0f, panelY + 8.0f, lookedAtText, 235, 239, 247, 255);
    drawText(panelX + 8.0f, panelY + 22.0f, modeText, 216, 222, 236, 255);
    drawText(panelX + 8.0f, panelY + 36.0f, biomeText, 216, 222, 236, 255);
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
    glDrawArrays(GL_TRIANGLES, 0, static_cast<GLsizei>(iconVerts_.size()));

    // Draw slot numbers after icons so they overlay the block textures.
    verts_.clear();
    for (const SlotLabel &label : slotLabels) {
        drawText(label.x + 1.0f, label.y + 1.0f, label.text, 20, 20, 24, 220);
        drawText(label.x, label.y, label.text, 246, 247, 250, 255);
    }
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
    const bool isPlant = (blockId == voxel::TALL_GRASS || blockId == voxel::FLOWER);
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
