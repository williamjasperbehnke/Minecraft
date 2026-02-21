#include "gfx/TextureAtlas.hpp"

#include <glad/glad.h>
#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>

#include <glm/vec3.hpp>

#include <cstdint>
#include <stdexcept>
#include <vector>

namespace gfx {

namespace {

bool loadImageRgba(const std::string &path, int &width, int &height, unsigned char **data) {
    int channels = 0;
    stbi_set_flip_vertically_on_load(0);
    *data = stbi_load(path.c_str(), &width, &height, &channels, 4);
    return *data != nullptr;
}

std::vector<glm::vec3> computeTileAverageColors(const unsigned char *pixels, int width, int height,
                                                int tileW, int tileH, int cols) {
    std::vector<glm::vec3> out;
    if (pixels == nullptr || width <= 0 || height <= 0 || tileW <= 0 || tileH <= 0 || cols <= 0) {
        return out;
    }
    const int rows = height / tileH;
    const int count = cols * rows;
    out.resize(static_cast<std::size_t>(count), glm::vec3(0.5f, 0.5f, 0.5f));
    for (int t = 0; t < count; ++t) {
        const int tx = t % cols;
        const int ty = t / cols;
        const int x0 = tx * tileW;
        const int y0 = ty * tileH;
        std::uint64_t sumR = 0;
        std::uint64_t sumG = 0;
        std::uint64_t sumB = 0;
        std::uint64_t sumA = 0;
        for (int y = 0; y < tileH; ++y) {
            for (int x = 0; x < tileW; ++x) {
                const int ix = x0 + x;
                const int iy = y0 + y;
                if (ix < 0 || ix >= width || iy < 0 || iy >= height) {
                    continue;
                }
                const int idx = (iy * width + ix) * 4;
                const std::uint32_t a = pixels[idx + 3];
                sumR += static_cast<std::uint64_t>(pixels[idx + 0]) * a;
                sumG += static_cast<std::uint64_t>(pixels[idx + 1]) * a;
                sumB += static_cast<std::uint64_t>(pixels[idx + 2]) * a;
                sumA += a;
            }
        }
        if (sumA > 0) {
            out[static_cast<std::size_t>(t)] = glm::vec3(
                static_cast<float>(sumR) / static_cast<float>(sumA) / 255.0f,
                static_cast<float>(sumG) / static_cast<float>(sumA) / 255.0f,
                static_cast<float>(sumB) / static_cast<float>(sumA) / 255.0f);
        }
    }
    return out;
}

} // namespace

TextureAtlas::TextureAtlas(const std::string &path, int tileW, int tileH)
    : tileW_(tileW), tileH_(tileH) {
    unsigned char *data = nullptr;
    const bool loaded = loadImageRgba(path, width_, height_, &data);

    glGenTextures(1, &tex_);
    glBindTexture(GL_TEXTURE_2D, tex_);
    // Atlas tiles should stay pixel-stable: mipmaps often bleed adjacent tiles.
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    if (!loaded) {
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
        tileAverageColors_ =
            computeTileAverageColors(fallback.data(), width_, height_, tileW_, tileH_, cols_);
    } else {
        cols_ = width_ / tileW_;
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, width_, height_, 0, GL_RGBA, GL_UNSIGNED_BYTE,
                     data);
        tileAverageColors_ = computeTileAverageColors(data, width_, height_, tileW_, tileH_, cols_);
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

glm::vec3 TextureAtlas::tileAverageColor(unsigned int tileIndex) const {
    if (tileAverageColors_.empty()) {
        return {0.5f, 0.5f, 0.5f};
    }
    const std::size_t idx =
        static_cast<std::size_t>(tileIndex % static_cast<unsigned int>(tileAverageColors_.size()));
    return tileAverageColors_[idx];
}

bool TextureAtlas::reload(const std::string &path) {
    unsigned char *data = nullptr;
    int newW = 0;
    int newH = 0;
    if (!loadImageRgba(path, newW, newH, &data)) {
        return false;
    }
    if (newW <= 0 || newH <= 0) {
        stbi_image_free(data);
        return false;
    }

    if (tex_ == 0) {
        glGenTextures(1, &tex_);
    }
    glBindTexture(GL_TEXTURE_2D, tex_);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, newW, newH, 0, GL_RGBA, GL_UNSIGNED_BYTE, data);
    tileAverageColors_ = computeTileAverageColors(data, newW, newH, tileW_, tileH_, newW / tileW_);
    stbi_image_free(data);

    width_ = newW;
    height_ = newH;
    cols_ = width_ / tileW_;
    if (cols_ <= 0) {
        cols_ = 1;
    }
    return true;
}

} // namespace gfx
