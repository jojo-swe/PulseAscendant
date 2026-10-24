// The geometry store and the acceleration structures: every mesh in one vertex and one index buffer, one
// compacted BLAS per mesh built at setup, a TLAS rebuilt every frame from per-slot instance buffers. A
// ray-query probe casts a grid of rays along -z and each hit (distance, custom index, primitive, facing,
// instance index, SBT record offset) must equal a CPU intersection of the same instances, also with a cull
// mask that leaves instances out and with the cull-back-facing ray flag (which every instance's
// FACING_CULL_DISABLE overrides). A TLAS refuses a second build in one frame. Review Focus 3: 64 frames with
// two in flight while one instance moves, each frame's probe equal to its own transform. Runs with the
// validation layer and its synchronization validation on and fails on any message.

#include "gfx/buffer.hpp"
#include "gfx/compute_pipeline.hpp"
#include "gfx/context.hpp"
#include "gfx/descriptors.hpp"
#include "gfx/frame_ring.hpp"
#include "gfx/sync.hpp"
#include "rt/accel.hpp"
#include "rt/geometry.hpp"

#include <array>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

using namespace evo;

int checksPassed = 0;

#define CHECK(x)                                                                                             \
    do {                                                                                                     \
        if (!(x))                                                                                            \
            throw std::runtime_error(std::string(#x) + " (line " + std::to_string(__LINE__) + ")");          \
        ++checksPassed;                                                                                      \
    } while (false)

template <typename Exception, typename F> bool throws(F&& f) {
    try {
        f();
    } catch (const Exception& error) {
        std::printf("  refused: %s\n", error.what());
        return true;
    }
    return false;
}

const std::filesystem::path kProbe = EVO_TEST_SPIRV_DIR "/test_ray_query_probe.comp.spv";

// --- Meshes and transforms ---------------------------------------------------------------------------------

// A unit quad in the xy plane facing +z, counter-clockwise seen from +z.
rt::Mesh unitQuad() {
    rt::Mesh mesh;
    const float corners[4][2] = {{-0.5f, -0.5f}, {0.5f, -0.5f}, {0.5f, 0.5f}, {-0.5f, 0.5f}};
    for (const auto& c : corners)
        mesh.vertices.push_back({{c[0], c[1], 0.0f}, {0.0f, 0.0f, 1.0f}, {c[0] + 0.5f, c[1] + 0.5f}});
    mesh.indices = {0, 1, 2, 0, 2, 3};
    return mesh;
}

// A unit cube centred on the origin, flat normals, every face counter-clockwise seen from outside.
rt::Mesh unitCube() {
    struct Face {
        float n[3], u[3], v[3]; // u x v = n
    };
    const Face faces[6] = {
        {{1, 0, 0}, {0, 1, 0}, {0, 0, 1}}, {{-1, 0, 0}, {0, 0, 1}, {0, 1, 0}},
        {{0, 1, 0}, {0, 0, 1}, {1, 0, 0}}, {{0, -1, 0}, {1, 0, 0}, {0, 0, 1}},
        {{0, 0, 1}, {1, 0, 0}, {0, 1, 0}}, {{0, 0, -1}, {0, 1, 0}, {1, 0, 0}},
    };
    const float signs[4][2] = {{-1, -1}, {1, -1}, {1, 1}, {-1, 1}};
    rt::Mesh mesh;
    for (const Face& face : faces) {
        const auto base = static_cast<std::uint32_t>(mesh.vertices.size());
        for (const auto& s : signs) {
            rt::Vertex vertex{};
            for (int k = 0; k < 3; ++k) {
                vertex.position[k] = 0.5f * (face.n[k] + s[0] * face.u[k] + s[1] * face.v[k]);
                vertex.normal[k] = face.n[k];
            }
            vertex.uv[0] = 0.5f * (s[0] + 1.0f);
            vertex.uv[1] = 0.5f * (s[1] + 1.0f);
            mesh.vertices.push_back(vertex);
        }
        for (std::uint32_t i : {0u, 1u, 2u, 0u, 2u, 3u})
            mesh.indices.push_back(base + i);
    }
    return mesh;
}

struct Vec3d {
    double x, y, z;
};
Vec3d operator-(Vec3d a, Vec3d b) {
    return {a.x - b.x, a.y - b.y, a.z - b.z};
}
Vec3d cross(Vec3d a, Vec3d b) {
    return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}
double dot(Vec3d a, Vec3d b) {
    return a.x * b.x + a.y * b.y + a.z * b.z;
}

// translate(t) * rotate(axis, degrees) * scale(s), row-major 3x4.
rt::Mat34 makeTransform(Vec3d t, Vec3d axis, double degrees, double s) {
    const double length = std::sqrt(dot(axis, axis));
    const Vec3d a{axis.x / length, axis.y / length, axis.z / length};
    const double angle = degrees * 3.14159265358979323846 / 180.0;
    const double c = std::cos(angle), sn = std::sin(angle), k = 1.0 - c;
    const double r[3][3] = {
        {c + a.x * a.x * k, a.x * a.y * k - a.z * sn, a.x * a.z * k + a.y * sn},
        {a.y * a.x * k + a.z * sn, c + a.y * a.y * k, a.y * a.z * k - a.x * sn},
        {a.z * a.x * k - a.y * sn, a.z * a.y * k + a.x * sn, c + a.z * a.z * k},
    };
    rt::Mat34 m{};
    const double translation[3] = {t.x, t.y, t.z};
    for (int row = 0; row < 3; ++row) {
        for (int col = 0; col < 3; ++col)
            m.m[row][col] = static_cast<float>(r[row][col] * s);
        m.m[row][3] = static_cast<float>(translation[row]);
    }
    return m;
}

Vec3d apply(const rt::Mat34& m, const float p[3]) {
    double out[3];
    for (int row = 0; row < 3; ++row)
        out[row] = double(m.m[row][0]) * p[0] + double(m.m[row][1]) * p[1] + double(m.m[row][2]) * p[2] +
                   double(m.m[row][3]);
    return {out[0], out[1], out[2]};
}

constexpr std::uint32_t kQuad = 0, kCube = 1;

// The scene at `frame`: a quad in front of everything at x = 0.1 * frame, a quad turned away from the rays
// (they hit its back) and two turned cubes. Hit groups 2, 1, 0, 1 and masks 0xFF, 0x01, 0x02, 0x05, so a
// cull mask of 0x01 leaves out the turned quad and 0x02 both cubes.
std::vector<rt::Instance> instancesAt(int frame) {
    const auto x = static_cast<double>(static_cast<float>(0.1 * frame));
    return {
        {kQuad, makeTransform({x, -1.9, 3.0}, {0, 0, 1}, 0.0, 1.0), 10, 2, 0xFF},
        {kCube, makeTransform({1.9, -2.1, 0.0}, {1, 1, 0}, 35.0, 1.2), 11, 1, 0x01},
        {kQuad, makeTransform({-1.9, 2.05, -1.0}, {0, 1, 0}, 140.0, 2.0), 12, 0, 0x02},
        {kCube, makeTransform({2.0, 1.95, 0.5}, {1, 2, 3}, 50.0, 1.5), 13, 1, 0x05},
    };
}

// --- The probe and its CPU reference -----------------------------------------------------------------------

struct ProbeHit {
    float t;
    std::int32_t customIndex;
    std::int32_t primitiveId;
    std::int32_t frontFace;
    std::int32_t instanceId; // the instance's index in the build
    std::int32_t sbtOffset;  // its hit group
};
static_assert(sizeof(ProbeHit) == 24);

constexpr std::uint32_t kGrid = 16;
constexpr std::uint32_t kRays = kGrid * kGrid;
using ProbeHits = std::array<ProbeHit, kRays>;

constexpr std::uint32_t kCullBackFacing = 0x10; // gl_RayFlagsCullBackFacingTrianglesEXT

struct ProbePush {
    float originX, originY, originZ, spacing;
    std::uint32_t gridWidth, gridHeight;
    std::uint32_t cullMask = 0xFF;
    std::uint32_t rayFlags = 0; // gl_RayFlagsNoneEXT
};
// Ray x and y sit 0.03 and 0.08 off the 0.1 lattice that every instance's edges and the moving quad lie on,
// so no ray grazes an edge (the reference checks that).
constexpr ProbePush kStaticGrid{-3.97f, -3.92f, 10.0f, 0.5f, kGrid, kGrid};
constexpr ProbePush kMovingGrid{-0.97f, -3.92f, 10.0f, 0.5f, kGrid, kGrid};

// The closest hit of every ray of `grid` against the world-space triangles of the instances its cull mask
// keeps (Moller-Trumbore in double, no face culling: every instance has FACING_CULL_DISABLE, whatever the
// ray flags). A triangle is front-facing when cross(v1 - v0, v2 - v0) points back at the ray origin:
// Vulkan's default ("clockwise from the ray in a left-handed system", FLIP_FACING not set) is
// counter-clockwise seen from the origin in our right-handed space, so an outward face hit from outside is
// front-facing. The spec defines facing in object space (the ray transformed into the instance); the
// world-space winding used here is the same only while the transform keeps orientation, so the reference
// refuses a transform whose determinant is not positive.
// Throws if any ray comes within 1e-3 (barycentric) of a triangle's edge, where GPU and CPU may differ.
ProbeHits reference(const rt::Mesh* meshes, const std::vector<rt::Instance>& instances, ProbePush grid) {
    for (const rt::Instance& instance : instances) {
        const auto& m = instance.transform.m;
        const double det = double(m[0][0]) * (double(m[1][1]) * m[2][2] - double(m[1][2]) * m[2][1]) -
                           double(m[0][1]) * (double(m[1][0]) * m[2][2] - double(m[1][2]) * m[2][0]) +
                           double(m[0][2]) * (double(m[1][0]) * m[2][1] - double(m[1][1]) * m[2][0]);
        if (!(det > 0.0))
            throw std::runtime_error("reference: a mirroring transform; facing would need object space");
    }
    ProbeHits hits{};
    for (std::uint32_t j = 0; j < grid.gridHeight; ++j)
        for (std::uint32_t i = 0; i < grid.gridWidth; ++i) {
            const Vec3d origin{double(grid.originX + grid.spacing * float(i)),
                               double(grid.originY + grid.spacing * float(j)), double(grid.originZ)};
            const Vec3d dir{0.0, 0.0, -1.0};
            ProbeHit best{-1.0f, -1, -1, -1, -1, -1};
            double bestT = std::numeric_limits<double>::infinity();
            for (std::size_t index = 0; index < instances.size(); ++index) {
                const rt::Instance& instance = instances[index];
                if ((instance.mask & grid.cullMask) == 0)
                    continue;
                const rt::Mesh& mesh = meshes[instance.mesh];
                for (std::size_t tri = 0; tri < mesh.indices.size() / 3; ++tri) {
                    const Vec3d v0 = apply(instance.transform, mesh.vertices[mesh.indices[3 * tri]].position);
                    const Vec3d v1 =
                        apply(instance.transform, mesh.vertices[mesh.indices[3 * tri + 1]].position);
                    const Vec3d v2 =
                        apply(instance.transform, mesh.vertices[mesh.indices[3 * tri + 2]].position);
                    const Vec3d e1 = v1 - v0, e2 = v2 - v0;
                    const Vec3d p = cross(dir, e2);
                    const double det = dot(e1, p);
                    if (std::fabs(det) < 1e-12)
                        continue; // edge-on: no ray here is parallel to a face
                    const Vec3d s = origin - v0;
                    const double u = dot(s, p) / det;
                    const Vec3d q = cross(s, e1);
                    const double v = dot(dir, q) / det;
                    const double t = dot(e2, q) / det;
                    if (t <= 0.0 || t >= 1000.0)
                        continue;
                    const double margin = std::fmin(std::fmin(u, v), 1.0 - u - v);
                    if (std::fabs(margin) < 1e-3)
                        throw std::runtime_error("reference: ray " + std::to_string(i) + "," +
                                                 std::to_string(j) + " grazes an edge; move the grid");
                    if (margin < 0.0)
                        continue;
                    if (std::fabs(t - bestT) < 1e-3)
                        throw std::runtime_error("reference: two hits at the same distance");
                    if (t < bestT) {
                        bestT = t;
                        best.t = static_cast<float>(t);
                        best.customIndex = static_cast<std::int32_t>(instance.customIndex);
                        best.primitiveId = static_cast<std::int32_t>(tri);
                        best.frontFace = dot(cross(e1, e2), dir) < 0.0 ? 1 : 0;
                        best.instanceId = static_cast<std::int32_t>(index);
                        best.sbtOffset = static_cast<std::int32_t>(instance.hitGroup);
                    }
                }
            }
            hits[j * grid.gridWidth + i] = best;
        }
    return hits;
}

// Equal within 1e-4 in t, exactly in the ids and facing. Prints the first difference.
bool matches(const ProbeHits& got, const ProbeHits& want, const char* what) {
    for (std::uint32_t r = 0; r < kRays; ++r) {
        const ProbeHit& g = got[r];
        const ProbeHit& w = want[r];
        if (std::fabs(g.t - w.t) > 1e-4f || g.customIndex != w.customIndex ||
            g.primitiveId != w.primitiveId || g.frontFace != w.frontFace || g.instanceId != w.instanceId ||
            g.sbtOffset != w.sbtOffset) {
            std::printf(
                "%s: ray %u got (t %.6f, custom %d, primitive %d, front %d, instance %d, sbt %d), want "
                "(t %.6f, custom %d, primitive %d, front %d, instance %d, sbt %d)\n",
                what, r, g.t, g.customIndex, g.primitiveId, g.frontFace, g.instanceId, g.sbtOffset, w.t,
                w.customIndex, w.primitiveId, w.frontFace, w.instanceId, w.sbtOffset);
            return false;
        }
    }
    return true;
}

// Every kind of probe result occurs: misses, both meshes, both facings.
void checkCoverage(const ProbeHits& hits) {
    std::array<int, 4> perInstance{};
    int misses = 0, front = 0, back = 0;
    for (const ProbeHit& hit : hits) {
        if (hit.customIndex < 0) {
            ++misses;
            continue;
        }
        ++perInstance[static_cast<std::size_t>(hit.customIndex - 10)];
        (hit.frontFace ? front : back)++;
    }
    std::printf("probe: %d misses, hits per instance %d %d %d %d, %d front-facing, %d back-facing\n", misses,
                perInstance[0], perInstance[1], perInstance[2], perInstance[3], front, back);
    CHECK(misses > 0);
    for (int count : perInstance)
        CHECK(count > 0);
    CHECK(front > 0 && back > 0);
}

class Probe {
public:
    explicit Probe(gfx::Context& ctx) : pipeline_(ctx, kProbe, kBindings, sizeof(ProbePush)) {}

    // Traces `grid` through `tlas` (built earlier in this command buffer) into `out`, readable by the host
    // once the frame completes.
    void record(VkCommandBuffer cmd, gfx::FrameRing& ring, const rt::Tlas& tlas, const gfx::Buffer& out,
                const ProbePush& grid) {
        // The previous write of `out` (two frames ago) finished before the host read it (the ring waited on
        // its timeline), but the installed layer (1.3.275) does not credit timeline host waits and reports a
        // WRITE_AFTER_WRITE between the two dispatches without this barrier. It covers this buffer only: no
        // frame-wide barrier that could hide a TLAS hazard.
        gfx::bufferBarrier2(cmd, out.handle(), VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                            VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                            VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT);
        const std::array<gfx::DescriptorWrite, 2> writes = {
            gfx::DescriptorWrite::accelerationStructure(0, tlas.handle()),
            gfx::DescriptorWrite::buffer(1, {out.handle(), 0, VK_WHOLE_SIZE})};
        pipeline_.bind(cmd, ring, writes);
        pipeline_.push(cmd, &grid, sizeof grid);
        pipeline_.dispatch(cmd, grid.gridWidth, grid.gridHeight);
        gfx::bufferBarrier2(cmd, out.handle(), VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                            VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT, VK_PIPELINE_STAGE_2_HOST_BIT,
                            VK_ACCESS_2_HOST_READ_BIT);
    }

private:
    static constexpr std::array<gfx::Binding, 2> kBindings = {
        gfx::Binding{0, VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR},
        gfx::Binding{1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER},
    };
    gfx::ComputePipeline pipeline_;
};

gfx::Buffer hitBuffer(gfx::Context& ctx, const char* name) {
    return gfx::Buffer(ctx, {sizeof(ProbeHits), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, true, name});
}

ProbeHits readHits(const gfx::Buffer& buffer) {
    buffer.invalidate();
    ProbeHits hits{};
    std::memcpy(hits.data(), buffer.mapped(), sizeof hits);
    return hits;
}

// Bytes of a device buffer, copied back through a staging buffer (setup-style, waits).
std::vector<std::byte> download(gfx::Context& ctx, const gfx::Buffer& buffer) {
    gfx::Buffer staging(ctx, {buffer.size(), VK_BUFFER_USAGE_TRANSFER_DST_BIT, true, "download"});
    ctx.immediate([&](VkCommandBuffer cmd) { // setup
        const VkBufferCopy region{0, 0, buffer.size()};
        vkCmdCopyBuffer(cmd, buffer.handle(), staging.handle(), 1, &region);
        gfx::bufferBarrier2(cmd, staging.handle(), VK_PIPELINE_STAGE_2_COPY_BIT,
                            VK_ACCESS_2_TRANSFER_WRITE_BIT, VK_PIPELINE_STAGE_2_HOST_BIT,
                            VK_ACCESS_2_HOST_READ_BIT);
    });
    staging.invalidate();
    std::vector<std::byte> bytes(buffer.size());
    std::memcpy(bytes.data(), staging.mapped(), bytes.size());
    return bytes;
}

// --- Tests -------------------------------------------------------------------------------------------------

// (a) The store packs both meshes into one vertex and one index buffer with their records, and refuses
// meshes it cannot build.
void testGeometryStore(gfx::Context& ctx, const std::array<rt::Mesh, 2>& meshes) {
    static_assert(sizeof(rt::Vertex) == 32);
    static_assert(sizeof(rt::MeshRecord) == 16);
    static_assert(sizeof(rt::Mat34) == sizeof(VkTransformMatrixKHR));

    rt::GeometryStore store(ctx, meshes);
    const auto records = store.records();
    CHECK(records.size() == 2);
    CHECK(records[0].firstVertex == 0 && records[0].vertexCount == 4);
    CHECK(records[0].firstIndex == 0 && records[0].indexCount == 6);
    CHECK(records[1].firstVertex == 4 && records[1].vertexCount == 24);
    CHECK(records[1].firstIndex == 6 && records[1].indexCount == 36);

    // The device buffers hold the meshes back to back, indices still local to their mesh, and the records.
    std::vector<rt::Vertex> vertices = meshes[0].vertices;
    vertices.insert(vertices.end(), meshes[1].vertices.begin(), meshes[1].vertices.end());
    std::vector<std::uint32_t> indices = meshes[0].indices;
    indices.insert(indices.end(), meshes[1].indices.begin(), meshes[1].indices.end());
    const auto vertexBytes = download(ctx, store.vertices());
    const auto indexBytes = download(ctx, store.indices());
    const auto recordBytes = download(ctx, store.meshRecords());
    CHECK(vertexBytes.size() >= vertices.size() * sizeof(rt::Vertex));
    CHECK(std::memcmp(vertexBytes.data(), vertices.data(), vertices.size() * sizeof(rt::Vertex)) == 0);
    CHECK(indexBytes.size() >= indices.size() * sizeof(std::uint32_t));
    CHECK(std::memcmp(indexBytes.data(), indices.data(), indices.size() * sizeof(std::uint32_t)) == 0);
    CHECK(recordBytes.size() >= records.size_bytes());
    CHECK(std::memcmp(recordBytes.data(), records.data(), records.size_bytes()) == 0);

    std::printf("expecting four refused geometry stores:\n");
    rt::Mesh outOfRange = unitQuad(); // 4 vertices
    outOfRange.indices[4] = 9;
    CHECK(throws<std::invalid_argument>([&] { rt::GeometryStore(ctx, std::array{outOfRange}); }));
    rt::Mesh notTriangles = unitQuad();
    notTriangles.indices.resize(4);
    CHECK(throws<std::invalid_argument>([&] { rt::GeometryStore(ctx, std::array{notTriangles}); }));
    rt::Mesh empty;
    CHECK(throws<std::invalid_argument>([&] { rt::GeometryStore(ctx, std::array{unitQuad(), empty}); }));
    CHECK(throws<std::invalid_argument>([&] { rt::GeometryStore(ctx, std::span<const rt::Mesh>{}); }));
}

// (b) Compacted and uncompacted BLASes give the same probe, equal to the CPU intersection, and compaction
// does not grow them. The probe also matches with cull masks that leave instances out, and with the
// cull-back-facing ray flag: FACING_CULL_DISABLE keeps the back-facing hits. A TLAS refuses more instances
// than it was made for, and a second build in the frame (it would overwrite the instances and the TLAS the
// first build's traces read).
void testProbeMatchesReference(gfx::Context& ctx, const std::array<rt::Mesh, 2>& meshes) {
    rt::GeometryStore store(ctx, meshes);
    rt::BlasSet compacted(ctx, store);
    rt::BlasSet uncompacted(ctx, store, false);
    std::printf("BLAS bytes: built %llu, compacted %llu; without compaction %llu / %llu\n",
                static_cast<unsigned long long>(compacted.builtBytes()),
                static_cast<unsigned long long>(compacted.compactedBytes()),
                static_cast<unsigned long long>(uncompacted.builtBytes()),
                static_cast<unsigned long long>(uncompacted.compactedBytes()));
    CHECK(compacted.compactedBytes() > 0);
    CHECK(compacted.compactedBytes() <= compacted.builtBytes());
    CHECK(uncompacted.compactedBytes() == uncompacted.builtBytes());
    CHECK(uncompacted.builtBytes() == compacted.builtBytes());
    for (std::uint32_t mesh = 0; mesh < 2; ++mesh) {
        CHECK(compacted.address(mesh) != 0);
        CHECK(compacted.address(mesh) != uncompacted.address(mesh));
    }
    CHECK(throws<std::out_of_range>([&] { (void)compacted.address(2); }));

    Probe probe(ctx);
    gfx::Buffer outCompacted = hitBuffer(ctx, "hits compacted");
    gfx::Buffer outUncompacted = hitBuffer(ctx, "hits uncompacted");
    gfx::Buffer outMaskTurned = hitBuffer(ctx, "hits mask 0x01");
    gfx::Buffer outMaskCubes = hitBuffer(ctx, "hits mask 0x02");
    gfx::Buffer outCullBack = hitBuffer(ctx, "hits cull back-facing");
    gfx::FrameRing ring(ctx);
    rt::Tlas tlasCompacted(ctx, ring, 4);
    rt::Tlas tlasUncompacted(ctx, ring, 4);

    const std::vector<rt::Instance> instances = instancesAt(-20); // the moving quad at x = -2
    ProbePush maskTurned = kStaticGrid, maskCubes = kStaticGrid, cullBack = kStaticGrid;
    maskTurned.cullMask = 0x01;
    maskCubes.cullMask = 0x02;
    cullBack.rayFlags = kCullBackFacing;
    VkCommandBuffer cmd = ring.begin();
    std::printf("expecting two refused TLAS builds:\n");
    std::vector<rt::Instance> five = instances;
    five.push_back(instances[0]);
    CHECK(throws<std::length_error>([&] { tlasCompacted.build(cmd, compacted, five); }));
    tlasCompacted.build(cmd, compacted, instances); // the refused build did not count as the frame's build
    tlasUncompacted.build(cmd, uncompacted, instances);
    CHECK(throws<std::logic_error>([&] { tlasCompacted.build(cmd, compacted, instances); }));
    probe.record(cmd, ring, tlasCompacted, outCompacted, kStaticGrid);
    probe.record(cmd, ring, tlasUncompacted, outUncompacted, kStaticGrid);
    probe.record(cmd, ring, tlasCompacted, outMaskTurned, maskTurned);
    probe.record(cmd, ring, tlasCompacted, outMaskCubes, maskCubes);
    probe.record(cmd, ring, tlasCompacted, outCullBack, cullBack);
    ring.wait(ring.submit());

    const ProbeHits want = reference(meshes.data(), instances, kStaticGrid);
    const ProbeHits got = readHits(outCompacted);
    checkCoverage(want);
    CHECK(matches(got, want, "compacted"));
    // Exact: the same build inputs, so the same hits on lavapipe. Another GPU may order or round a compacted
    // BLAS differently; relax this to matches(gotUncompacted, want) (1e-4 in t) if it fails there.
    const ProbeHits gotUncompacted = readHits(outUncompacted);
    CHECK(std::memcmp(gotUncompacted.data(), got.data(), sizeof got) == 0);

    // The masks: 0x01 loses the turned quad (instance 2) and finds what lies behind it; 0x02 keeps only the
    // two quads.
    const auto hitsInstance = [](const ProbeHits& hits, std::int32_t id) {
        for (const ProbeHit& hit : hits)
            if (hit.instanceId == id)
                return true;
        return false;
    };
    const ProbeHits wantTurned = reference(meshes.data(), instances, maskTurned);
    CHECK(hitsInstance(want, 2) && !hitsInstance(wantTurned, 2));
    CHECK(matches(readHits(outMaskTurned), wantTurned, "cull mask 0x01"));
    const ProbeHits wantCubes = reference(meshes.data(), instances, maskCubes);
    CHECK(hitsInstance(want, 1) && hitsInstance(want, 3));
    CHECK(!hitsInstance(wantCubes, 1) && !hitsInstance(wantCubes, 3) && hitsInstance(wantCubes, 2));
    CHECK(matches(readHits(outMaskCubes), wantCubes, "cull mask 0x02"));
    // The cull-back-facing flag changes nothing: the back-facing hits checkCoverage found stay.
    CHECK(matches(readHits(outCullBack), want, "cull back-facing flag"));

    // The next frame may build again.
    cmd = ring.begin();
    tlasCompacted.build(cmd, compacted, instances);
    ring.wait(ring.submit());
}

// (c) Review Focus 3: 64 frames, two in flight, the quad moving 0.1 to the right each frame; the TLAS is
// rebuilt every frame (while the previous frame may still trace it) and each frame's probe, read after its
// own wait, equals the CPU intersection at that frame's transform.
void testMovingInstanceTwoFramesInFlight(gfx::Context& ctx, const std::array<rt::Mesh, 2>& meshes) {
    rt::GeometryStore store(ctx, meshes);
    rt::BlasSet blases(ctx, store);
    Probe probe(ctx);
    // One output per ring slot, alive for the whole run.
    std::array<gfx::Buffer, gfx::FrameRing::kFramesInFlight> outputs = {hitBuffer(ctx, "hits slot 0"),
                                                                        hitBuffer(ctx, "hits slot 1")};
    gfx::FrameRing ring(ctx);
    rt::Tlas tlas(ctx, ring, 4);

    constexpr int kFrames = 64;
    std::array<std::uint64_t, kFrames> done{};
    std::array<std::uint32_t, kFrames> slotOf{};
    int verified = 0;
    auto verify = [&](int frame) {
        ring.wait(done[frame]);
        const ProbeHits want = reference(meshes.data(), instancesAt(frame), kMovingGrid);
        const std::string what = "frame " + std::to_string(frame);
        const ProbeHits got = readHits(outputs[slotOf[frame]]);
        CHECK(matches(got, want, what.c_str()));
        bool sawMovingQuad = false; // every frame's grid sees the quad, wherever it is
        for (const ProbeHit& hit : got)
            sawMovingQuad = sawMovingQuad || hit.customIndex == 10;
        CHECK(sawMovingQuad);
        ++verified;
    };
    for (int frame = 0; frame < kFrames; ++frame) {
        slotOf[frame] = ring.slot();
        VkCommandBuffer cmd = ring.begin();
        tlas.build(cmd, blases, instancesAt(frame));
        probe.record(cmd, ring, tlas, outputs[slotOf[frame]], kMovingGrid);
        done[frame] = ring.submit();
        if (frame > 0)
            verify(frame - 1); // while this frame may still run
    }
    verify(kFrames - 1);
    CHECK(verified == kFrames);
}

} // namespace

int main() {
    const auto start = std::chrono::steady_clock::now();
    try {
        const std::array<rt::Mesh, 2> meshes = {unitQuad(), unitCube()};
        {
            auto ctx = gfx::Context::create({.validation = true, .syncValidation = true});
            if (!ctx.validationEnabled())
                std::printf("warning: validation layer unavailable; only the results are checked\n");
            else if (!ctx.syncValidationEnabled())
                std::printf("warning: synchronization validation unavailable\n");
            // Between tests the queue goes idle. Each test's ring already waited for its frames on the
            // timeline semaphore, but the installed layer's synchronization validation (1.3.275) does not
            // credit host waits on a timeline semaphore (vkWaitSemaphores), only vkQueueWaitIdle and the
            // like. When VMA gives a freed buffer's memory to the next test's buffers (test (b)'s probe
            // output became test (c)'s staging buffer), it then reports a READ_AFTER_WRITE against a dispatch
            // that completed long before. Within a test every buffer a frame uses stays alive for the whole
            // run.
            const auto settle = [&] { vkQueueWaitIdle(ctx.queue()); };
            testGeometryStore(ctx, meshes);
            settle();
            testProbeMatchesReference(ctx, meshes);
            settle();
            testMovingInstanceTwoFramesInFlight(ctx, meshes);
            CHECK(ctx.validationMessageCount() == 0);
        }
        // Also counts what the layer reported while the objects and the Context were destroyed.
        CHECK(gfx::validationMessagesReported() == 0);
    } catch (const std::exception& error) {
        std::fprintf(stderr, "FAILED: %s\n", error.what());
        return 1;
    }
    const double seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
    std::printf("accel_test: %d checks passed in %.1f s\n", checksPassed, seconds);
    return 0;
}
