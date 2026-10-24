#pragma once

#include "gfx/buffer.hpp"
#include "gfx/context.hpp"

#include <volk.h>

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace evo::rt {

// One vertex as the shaders read it (scalar layout; mirrored in shaders/rt/types.glsl).
struct Vertex {
    float position[3];
    float normal[3];
    float uv[2];
};
static_assert(sizeof(Vertex) == 32);
static_assert(offsetof(Vertex, position) == 0);
static_assert(offsetof(Vertex, normal) == 12);
static_assert(offsetof(Vertex, uv) == 24);

// Where one mesh lives in the store's buffers, in vertices and indices (scalar layout; mirrored in
// shaders/rt/types.glsl). A mesh's indices are local to it: vertex = firstVertex + index.
struct MeshRecord {
    std::uint32_t firstVertex;
    std::uint32_t vertexCount;
    std::uint32_t firstIndex;
    std::uint32_t indexCount;
};
static_assert(sizeof(MeshRecord) == 16);
static_assert(offsetof(MeshRecord, firstVertex) == 0);
static_assert(offsetof(MeshRecord, vertexCount) == 4);
static_assert(offsetof(MeshRecord, firstIndex) == 8);
static_assert(offsetof(MeshRecord, indexCount) == 12);

// A triangle list; indices are local to the mesh and counter-clockwise seen from the outside.
struct Mesh {
    std::vector<Vertex> vertices;
    std::vector<std::uint32_t> indices;
};

// An affine transform, row-major 3x4 (m[row][3] is the translation): the layout of VkTransformMatrixKHR.
struct Mat34 {
    float m[3][4];
};
static_assert(sizeof(Mat34) == sizeof(VkTransformMatrixKHR));

// Every mesh of a scene in one device-local vertex buffer and one index buffer (usable as acceleration
// structure build input, storage buffers and through device addresses), plus a storage buffer of their
// MeshRecords in the order given. Built at setup (staged copies that wait). Throws std::invalid_argument for
// no meshes, an empty mesh, an index out of its mesh's range or an index count not a multiple of 3.
class GeometryStore {
public:
    GeometryStore(gfx::Context& ctx, std::span<const Mesh> meshes);

    std::span<const MeshRecord> records() const { return records_; }
    const gfx::Buffer& vertices() const { return vertices_; }
    const gfx::Buffer& indices() const { return indices_; }
    const gfx::Buffer& meshRecords() const { return meshRecords_; }

private:
    std::vector<MeshRecord> records_;
    gfx::Buffer vertices_;
    gfx::Buffer indices_;
    gfx::Buffer meshRecords_;
};

} // namespace evo::rt
