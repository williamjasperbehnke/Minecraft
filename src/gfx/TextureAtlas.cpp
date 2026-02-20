#include "gfx/TextureAtlas.hpp"

#include <glad/glad.h>
#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>

#include <stdexcept>
#include <vector>

namespace gfx {

TextureAtlas::TextureAtlas(const std::string &path, int tileW, int tileH)
    : tileW_(tileW), tileH_(tileH) {
    int channels = 0;
    stbi_set_flip_vertically_on_load(0);
    unsigned char *data = stbi_load(path.c_str(), &width_, &height_, &channels, 4);

    glGenTextures(1, &tex_);
    glBindTexture(GL_TEXTURE_2D, tex_);
    // Atlas tiles should stay pixel-stable: mipmaps often bleed adjacent tiles.
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    if (data == nullptr) {
        // Fallback checker so vertical slice still renders without external assets.
        width_ = 32;
        height_ = 32;
        tileW_ = 16;
        tileH_ = 16;
        cols_ = 2;
        std::vector<unsigned char> fallback(width_ * height_ * 4, 255);
        for (int y = 0; y < height_; ++y) {
            for (int x = 0; x < width_; ++x) {
                const bool a = ((x / 16) + (y / 16)) % 2 == 0;
                const int i = (y * width_ + x) * 4;
                fallback[i + 0] = a ? 90 : 150;
                fallback[i + 1] = a ? 180 : 120;
                fallback[i + 2] = a ? 90 : 60;
                fallback[i + 3] = 255;
            }
        }
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, width_, height_, 0, GL_RGBA, GL_UNSIGNED_BYTE,
                     fallback.data());
    } else {
        cols_ = width_ / tileW_;
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, width_, height_, 0, GL_RGBA, GL_UNSIGNED_BYTE,
                     data);
        stbi_image_free(data);
    }

    if (cols_ <= 0) {
        cols_ = 1;
    }
}

TextureAtlas::~TextureAtlas() {
    if (tex_ != 0) {
        glDeleteTextures(1, &tex_);
    }
}

void TextureAtlas::bind(int unit) const {
    glActiveTexture(GL_TEXTURE0 + unit);
    glBindTexture(GL_TEXTURE_2D, tex_);
}

glm::vec4 TextureAtlas::uvRect(unsigned int tileIndex) const {
    const int tx = static_cast<int>(tileIndex % static_cast<unsigned int>(cols_));
    const int ty = static_cast<int>(tileIndex / static_cast<unsigned int>(cols_));
    // Tiny inset prevents edge bleeding without visibly cropping tile borders.
    const float insetU = 0.01f / static_cast<float>(width_);
    const float insetV = 0.01f / static_cast<float>(height_);
    const float u0 = static_cast<float>(tx * tileW_) / static_cast<float>(width_) + insetU;
    const float u1 = static_cast<float>((tx + 1) * tileW_) / static_cast<float>(width_) - insetU;
    // With unflipped image data, OpenGL maps the first image row at v=0.
    // So top-authored atlas row indices map directly via ty.
    const float v0 = static_cast<float>(ty * tileH_) / static_cast<float>(height_) + insetV;
    const float v1 = static_cast<float>((ty + 1) * tileH_) / static_cast<float>(height_) - insetV;
    return {u0, v0, u1, v1};
}

} // namespace gfx
