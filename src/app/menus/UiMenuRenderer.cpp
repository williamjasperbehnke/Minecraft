#include "app/menus/UiMenuRenderer.hpp"
#include "app/util/ShaderProgramUtils.hpp"

#include <glad/glad.h>
#include <stb_easy_font.h>

namespace app::menus {
namespace {

} // namespace

float UiMenuRenderer::textWidthPx(const std::string &text) {
    return static_cast<float>(stb_easy_font_width(const_cast<char *>(text.c_str())));
}

void UiMenuRenderer::init() {
    if (ready_) {
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

    shader_ = app::util::linkInLineProgram(
        app::util::compileInlineShader(GL_VERTEX_SHADER, vs),
        app::util::compileInlineShader(GL_FRAGMENT_SHADER, fs));

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

    ready_ = true;
}

void UiMenuRenderer::begin(int width, int height) {
    init();
    width_ = width;
    height_ = height;
    verts_.clear();
}

void UiMenuRenderer::drawRect(float x, float y, float w, float h, float r, float g, float b,
                              float a) {
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

void UiMenuRenderer::drawText(float x, float y, const std::string &text, unsigned char r,
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

void UiMenuRenderer::end() {
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glDisable(GL_DEPTH_TEST);
    glUseProgram(shader_);
    glUniform2f(glGetUniformLocation(shader_, "uScreen"), static_cast<float>(width_),
                static_cast<float>(height_));
    glBindVertexArray(vao_);
    glBindBuffer(GL_ARRAY_BUFFER, vbo_);
    glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(verts_.size() * sizeof(UiVertex)),
                 verts_.data(), GL_DYNAMIC_DRAW);
    glDrawArrays(GL_TRIANGLES, 0, static_cast<GLsizei>(verts_.size()));
    glEnable(GL_DEPTH_TEST);
}

} // namespace app::menus
