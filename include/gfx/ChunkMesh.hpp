#pragma once

#include <cstdint>
#include <vector>

namespace gfx {

struct Vertex {
    float px;
    float py;
    float pz;
    float nx;
    float ny;
    float nz;
    float u;
    float v;
};

struct CpuMesh {
    std::vector<Vertex> vertices;
    std::vector<std::uint32_t> indices;
};

class ChunkMesh {
  public:
    ChunkMesh() = default;
    ~ChunkMesh();

    ChunkMesh(const ChunkMesh &) = delete;
    ChunkMesh &operator=(const ChunkMesh &) = delete;

    void upload(const CpuMesh &mesh);
    void draw() const;

  private:
    unsigned int vao_ = 0;
    unsigned int vbo_ = 0;
    unsigned int ebo_ = 0;
    int indexCount_ = 0;
};

} // namespace gfx
