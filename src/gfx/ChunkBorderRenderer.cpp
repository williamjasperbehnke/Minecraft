#include "gfx/ChunkBorderRenderer.hpp"
#include "app/util/ShaderProgramUtils.hpp"

#include <glad/glad.h>
#include <GLFW/glfw3.h>

namespace gfx {

ChunkBorderRenderer::~ChunkBorderRenderer() {
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

void ChunkBorderRenderer::draw(const glm::mat4 &proj, const glm::mat4 &view,
                               const std::vector<glm::vec3> &verts) {
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

void ChunkBorderRenderer::init() {
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
    program_ = app::util::linkInLineProgram(
        app::util::compileInlineShader(GL_VERTEX_SHADER, vs),
        app::util::compileInlineShader(GL_FRAGMENT_SHADER, fs));

    glGenVertexArrays(1, &vao_);
    glGenBuffers(1, &vbo_);
    glBindVertexArray(vao_);
    glBindBuffer(GL_ARRAY_BUFFER, vbo_);
    glBufferData(GL_ARRAY_BUFFER, 0, nullptr, GL_DYNAMIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(glm::vec3), nullptr);
    ready_ = true;
}

} // namespace gfx
