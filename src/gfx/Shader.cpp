#include "gfx/Shader.hpp"

#include "app/util/ShaderProgramUtils.hpp"
#include "core/Logger.hpp"

#include <glad/glad.h>

#include <fstream>
#include <sstream>
#include <stdexcept>

namespace {
std::string readFile(const std::string &path) {
    std::ifstream file(path);
    if (!file) {
        throw std::runtime_error("Failed to open shader file: " + path);
    }
    std::ostringstream ss;
    ss << file.rdbuf();
    return ss.str();
}

} // namespace

namespace gfx {

Shader::Shader(const std::string &vertexPath, const std::string &fragmentPath) {
    const std::string vs = readFile(vertexPath);
    const std::string fs = readFile(fragmentPath);

    program_ = app::util::linkInLineProgram(
        app::util::compileInlineShader(GL_VERTEX_SHADER, vs.c_str()),
        app::util::compileInlineShader(GL_FRAGMENT_SHADER, fs.c_str()));
}

Shader::~Shader() {
    if (program_ != 0) {
        glDeleteProgram(program_);
    }
}

void Shader::use() const {
    glUseProgram(program_);
}

void Shader::setInt(const char *name, int value) const {
    glUniform1i(glGetUniformLocation(program_, name), value);
}

void Shader::setFloat(const char *name, float value) const {
    glUniform1f(glGetUniformLocation(program_, name), value);
}

void Shader::setVec3(const char *name, const glm::vec3 &v) const {
    glUniform3f(glGetUniformLocation(program_, name), v.x, v.y, v.z);
}

void Shader::setMat4(const char *name, const glm::mat4 &m) const {
    glUniformMatrix4fv(glGetUniformLocation(program_, name), 1, GL_FALSE, &m[0][0]);
}

} // namespace gfx
