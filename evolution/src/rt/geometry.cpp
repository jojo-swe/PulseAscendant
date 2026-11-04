#include "rt/geometry.hpp"

#include <limits>
#include <stdexcept>
#include <string>

namespace evo::rt {

namespace {

// Vertices and indices: read by acceleration structure builds and by the shaders, through descriptors or
// device addresses; TRANSFER_SRC so tests can read them back.
constexpr VkBufferUsageFlags kGeometryUsage =
    VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR |
    VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT |
    VK_BUFFER_USAGE_TRANSFER_SRC_BIT;

template <typename T> std::span<const std::byte> bytesOf(const std::vector<T>& values) {
    return std::as_bytes(std::span(values));
}

void validate(const Mesh& mesh, std::size_t index) {
    const std::string name = "GeometryStore: mesh " + std::to_string(index);
    if (mesh.vertices.empty() || mesh.indices.empty())
        throw std::invalid_argument(name + " is empty");
    if (mesh.indices.size() % 3 != 0)
        throw std::invalid_argument(name + " has " + std::to_string(mesh.indices.size()) +
                                    " indices, not a multiple of 3");
    for (std::uint32_t i : mesh.indices)
        if (i >= mesh.vertices.size())
            throw std::invalid_argument(name + " has index " + std::to_string(i) + " for " +
                                        std::to_string(mesh.vertices.size()) + " vertices");
}

} // namespace

GeometryStore::GeometryStore(gfx::Context& ctx, std::span<const Mesh> meshes) {
    if (meshes.empty())
        throw std::invalid_argument("GeometryStore: no meshes");

    std::vector<Vertex> vertices;
    std::vector<std::uint32_t> indices;
    for (std::size_t i = 0; i < meshes.size(); ++i) {
        const Mesh& mesh = meshes[i];
        validate(mesh, i);
        if (vertices.size() + mesh.vertices.size() > std::numeric_limits<std::uint32_t>::max() ||
            indices.size() + mesh.indices.size() > std::numeric_limits<std::uint32_t>::max())
            throw std::invalid_argument("GeometryStore: more than 2^32 vertices or indices");
        records_.push_back(
            {static_cast<std::uint32_t>(vertices.size()), static_cast<std::uint32_t>(mesh.vertices.size()),
             static_cast<std::uint32_t>(indices.size()), static_cast<std::uint32_t>(mesh.indices.size())});
        vertices.insert(vertices.end(), mesh.vertices.begin(), mesh.vertices.end());
        indices.insert(indices.end(), mesh.indices.begin(), mesh.indices.end());
    }

    const auto vertexBytes = bytesOf(vertices);
    const auto indexBytes = bytesOf(indices);
    const auto recordBytes = bytesOf(records_);
    vertices_ = gfx::createDeviceBuffer(ctx, {vertexBytes.size(), kGeometryUsage, false, "geometry vertices"},
                                        vertexBytes);
    indices_ = gfx::createDeviceBuffer(ctx, {indexBytes.size(), kGeometryUsage, false, "geometry indices"},
                                       indexBytes);
    meshRecords_ = gfx::createDeviceBuffer(
        ctx,
        {recordBytes.size(), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT, false,
         "geometry mesh records"},
        recordBytes);
}

} // namespace evo::rt
