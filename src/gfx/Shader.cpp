#include "gfx/Shader.hpp"

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

unsigned int compile(unsigned int type, const std::string &source) {
    const char *src = source.c_str();
    unsigned int shader = glCreateShader(type);
    glShaderSource(shader, 1, &src, nullptr);
    glCompileShader(shader);

    int ok = 0;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char buf[1024];
        glGetShaderInfoLog(shader, sizeof(buf), nullptr, buf);
        throw std::runtime_error(std::string("Shader compile error: ") + buf);
    }
    return shader;
}
} // namespace

namespace gfx {

Shader::Shader(const std::string &vertexPath, const std::string &fragmentPath) {
    const std::string vs = readFile(vertexPath);
    const std::string fs = readFile(fragmentPath);

    const unsigned int vert = compile(GL_VERTEX_SHADER, vs);
    const unsigned int frag = compile(GL_FRAGMENT_SHADER, fs);

    program_ = glCreateProgram();
    glAttachShader(program_, vert);
    glAttachShader(program_, frag);
    glLinkProgram(program_);

    int ok = 0;
    glGetProgramiv(program_, GL_LINK_STATUS, &ok);
    glDeleteShader(vert);
    glDeleteShader(frag);
    if (!ok) {
        char buf[1024];
        glGetProgramInfoLog(program_, sizeof(buf), nullptr, buf);
        throw std::runtime_error(std::string("Shader link error: ") + buf);
    }
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
