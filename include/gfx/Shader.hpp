#pragma once

#include <glm/mat4x4.hpp>
#include <glm/vec3.hpp>
#include <string>

namespace gfx {

class Shader {
  public:
    Shader(const std::string &vertexPath, const std::string &fragmentPath);
    ~Shader();

    Shader(const Shader &) = delete;
    Shader &operator=(const Shader &) = delete;

    void use() const;
    void setInt(const char *name, int value) const;
    void setFloat(const char *name, float value) const;
    void setVec3(const char *name, const glm::vec3 &v) const;
    void setMat4(const char *name, const glm::mat4 &m) const;

  private:
    unsigned int program_ = 0;
};

} // namespace gfx
