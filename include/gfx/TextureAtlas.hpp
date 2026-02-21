#pragma once

#include <glm/vec3.hpp>
#include <glm/vec4.hpp>
#include <string>
#include <vector>

namespace gfx {

class TextureAtlas {
  public:
    TextureAtlas(const std::string &path, int tileW, int tileH);
    ~TextureAtlas();

    TextureAtlas(const TextureAtlas &) = delete;
    TextureAtlas &operator=(const TextureAtlas &) = delete;

    void bind(int unit) const;
    glm::vec4 uvRect(unsigned int tileIndex) const;
    glm::vec3 tileAverageColor(unsigned int tileIndex) const;
    bool reload(const std::string &path);

  private:
    unsigned int tex_ = 0;
    int width_ = 0;
    int height_ = 0;
    int tileW_ = 16;
    int tileH_ = 16;
    int cols_ = 1;
    std::vector<glm::vec3> tileAverageColors_;
};

} // namespace gfx
