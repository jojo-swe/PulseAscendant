#include "modern/caustics.hpp"

#include "gfx/descriptors.hpp"
#include "gfx/paths.hpp"
#include "gfx/sync.hpp"
#include "modern/scene.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>
#include <numbers>
#include <stdexcept>
#include <string>

namespace evo::modern {

namespace {

constexpr float kPi = std::numbers::pi_v<float>;
// Fixed-point units per photon of the nominal flux (the light's estimated power on the casters' sphere over
// the photons): a texel and channel overflows only past 2^32 / 256 = 16.7 million such photons, and one
// deposit is at most 2^20 units (photons.comp).
constexpr float kUnitsPerPhoton = 256.f;
// How far back along its direction a disc photon starts, beyond 4 caster radii.
constexpr float kSourceDistance = 1000.f;

// photons.comp's set and push constants.
const std::array<gfx::Binding, 12> kPhotonBindings = {
    gfx::Binding{0, VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR},
    gfx::Binding{1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER},
    gfx::Binding{2, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER},
    gfx::Binding{3, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER},
    gfx::Binding{4, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER},
    gfx::Binding{5, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER},
    gfx::Binding{6, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER},
    gfx::Binding{7, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER},
    gfx::Binding{8, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER},
    gfx::Binding{9, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER},
    gfx::Binding{10, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER},
    gfx::Binding{11, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER},
};
struct PhotonPush {
    std::uint32_t photonCount, seed, receiverCount, mapSize;
    float unitsPerFlux, sourceDistance;
    std::uint32_t orderMultiplier, orderOffset;
};
static_assert(sizeof(PhotonPush) == 32);

// caustic_resolve.comp's.
const std::array<gfx::Binding, 2> kResolveBindings = {
    gfx::Binding{0, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER},
    gfx::Binding{1, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE},
};
struct ResolvePush {
    std::uint32_t layer, resolution, mapSize;
    float irradianceScale;
};
static_assert(sizeof(ResolvePush) == 16);

constexpr VkDeviceSize kTableBytes = sizeof(rt::CausticReceiver) * (kMaxCausticReceivers + 1);

Vec3 row(const rt::Mat34& m, int r) {
    return {m.m[r][0], m.m[r][1], m.m[r][2]};
}

Vec3 linear(const rt::Mat34& m, Vec3 v) {
    return {m.m[0][0] * v.x + m.m[0][1] * v.y + m.m[0][2] * v.z,
            m.m[1][0] * v.x + m.m[1][1] * v.y + m.m[1][2] * v.z,
            m.m[2][0] * v.x + m.m[2][1] * v.y + m.m[2][2] * v.z};
}

// The longest column of the transform's linear part: how far it stretches a length when it rotates and
// scales (place(): translate * rotate * scale); a shear may stretch further.
float largestScale(const rt::Mat34& m) {
    float most = 0;
    for (int c = 0; c < 3; ++c)
        most = std::max(most, length(Vec3{m.m[0][c], m.m[1][c], m.m[2][c]}));
    return most;
}

// The world area of a receiver's uv square: 1 / |row 0 x row 1|; 0 for a degenerate map.
float uvSquareArea(const CausticReceiver& r) {
    const float c = length(cross(row(r.worldToUv, 0), row(r.worldToUv, 1)));
    return c > 0 && std::isfinite(c) ? 1.f / c : 0.f;
}

[[noreturn]] void refuse(const std::string& what) {
    throw std::invalid_argument("CausticFrame: " + what);
}

const std::vector<rt::Material>& materialTable(const SceneStatic& scene, const SceneFrame& frame) {
    return frame.materials.empty() ? scene.materials : frame.materials;
}

// A mesh's own bounding sphere: its box's centre, out to its farthest vertex.
CasterBounds meshBounds(const rt::Mesh& mesh) {
    if (mesh.vertices.empty())
        return {{0, 0, 0}, 0};
    constexpr float kBig = std::numeric_limits<float>::max();
    Vec3 lo{kBig, kBig, kBig}, hi{-kBig, -kBig, -kBig};
    for (const rt::Vertex& v : mesh.vertices) {
        lo = {std::min(lo.x, v.position[0]), std::min(lo.y, v.position[1]), std::min(lo.z, v.position[2])};
        hi = {std::max(hi.x, v.position[0]), std::max(hi.y, v.position[1]), std::max(hi.z, v.position[2])};
    }
    const Vec3 c = (lo + hi) * 0.5f;
    float r = 0;
    for (const rt::Vertex& v : mesh.vertices)
        r = std::max(r, length(Vec3{v.position[0], v.position[1], v.position[2]} - c));
    return {c, r};
}

} // namespace

CausticReceiver receiverForQuad(const rt::Mat34& transform, float width, float height,
                                std::uint32_t resolution) {
    if (!(width > 0) || !(height > 0) || resolution == 0)
        throw std::invalid_argument("receiverForQuad: the size and the resolution must be positive");
    const Vec3 o = transformPoint(transform, {-0.5f * width, -0.5f * height, 0});
    const Vec3 a = linear(transform, {width, 0, 0}), b = linear(transform, {0, height, 0});
    const Vec3 n = cross(a, b);
    const float n2 = dot(n, n);
    if (!(n2 > 0) || !std::isfinite(n2))
        throw std::invalid_argument("receiverForQuad: a degenerate transform");
    // The dual basis of (a, b) in their plane: dot(a, aDual) = dot(b, bDual) = 1, dot(b, aDual) = dot(a,
    // bDual) = 0.
    const Vec3 rows[3] = {cross(b, n) / n2, cross(n, a) / n2, n / std::sqrt(n2)};
    CausticReceiver r;
    r.resolution = resolution;
    for (int i = 0; i < 3; ++i) {
        r.worldToUv.m[i][0] = rows[i].x;
        r.worldToUv.m[i][1] = rows[i].y;
        r.worldToUv.m[i][2] = rows[i].z;
        r.worldToUv.m[i][3] = -dot(o, rows[i]);
    }
    return r;
}

CasterBounds casterBounds(const SceneStatic& scene, const SceneFrame& frame) {
    const CausticFrame& c = *frame.caustics;
    std::vector<CasterBounds> parts;
    for (std::uint32_t i = c.casterInstanceFirst; i < c.casterInstanceFirst + c.casterInstanceCount; ++i) {
        const SceneInstance& in = frame.instances[i];
        const float stretch = largestScale(in.transform);
        auto place = [&](CasterBounds local) {
            parts.push_back({transformPoint(in.transform, local.center), local.radius * stretch});
        };
        if (in.kind == rt::GeometryKind::Spheres) {
            for (std::uint32_t s = in.firstSphere; s < in.firstSphere + in.sphereCount; ++s) {
                const rt::SphereRecord& r = scene.spheres[s];
                place({{r.center[0], r.center[1], r.center[2]}, r.radius});
            }
        } else {
            place(meshBounds(scene.meshes[in.mesh]));
        }
    }
    constexpr float kBig = std::numeric_limits<float>::max();
    Vec3 lo{kBig, kBig, kBig}, hi{-kBig, -kBig, -kBig};
    for (const CasterBounds& p : parts) {
        lo = {std::min(lo.x, p.center.x - p.radius), std::min(lo.y, p.center.y - p.radius),
              std::min(lo.z, p.center.z - p.radius)};
        hi = {std::max(hi.x, p.center.x + p.radius), std::max(hi.y, p.center.y + p.radius),
              std::max(hi.z, p.center.z + p.radius)};
    }
    CasterBounds all{(lo + hi) * 0.5f, 0};
    for (const CasterBounds& p : parts)
        all.radius = std::max(all.radius, length(p.center - all.center) + p.radius);
    return all;
}

void checkCausticFrame(const SceneStatic& scene, const SceneFrame& frame, std::uint32_t maxResolution) {
    if (!frame.caustics)
        return;
    const CausticFrame& c = *frame.caustics;
    if (c.receivers.size() > kMaxCausticReceivers)
        throw std::length_error("CausticFrame: " + std::to_string(c.receivers.size()) +
                                " receivers, more than " + std::to_string(kMaxCausticReceivers));
    if (c.receivers.empty())
        refuse("no receivers");
    for (const CausticReceiver& r : c.receivers) {
        if (r.resolution == 0)
            refuse("a receiver of resolution 0");
        if (r.resolution > maxResolution)
            throw std::length_error("CausticFrame: a receiver of resolution " + std::to_string(r.resolution) +
                                    ", more than the maps' " + std::to_string(maxResolution));
        for (int i = 0; i < 3; ++i)
            for (int j = 0; j < 4; ++j)
                if (!std::isfinite(r.worldToUv.m[i][j]))
                    refuse("a receiver's uv map is not finite");
        if (!(uvSquareArea(r) > 0))
            refuse("a receiver's uv map is degenerate (rows 0 and 1 parallel or zero)");
    }
    if (c.light < 0) {
        const EnvironmentDesc& e = frame.environment;
        const float most = std::max({e.discRadiance.x, e.discRadiance.y, e.discRadiance.z}) * e.intensity;
        if (!(e.discAngularRadius > 0) || !(most > 0))
            refuse("the light is the environment's disc, but the environment has no disc of light");
    } else {
        const auto index = static_cast<std::size_t>(c.light);
        if (index >= frame.lights.size())
            refuse("light " + std::to_string(c.light) + " is not one of the frame's " +
                   std::to_string(frame.lights.size()) + " lights");
        if (frame.lights[index].kind != static_cast<std::uint32_t>(rt::LightKind::Emitter))
            refuse("the light must be an Emitter");
    }
    if (c.casterInstanceCount == 0)
        refuse("no caster instances");
    if (std::uint64_t(c.casterInstanceFirst) + c.casterInstanceCount > frame.instances.size())
        refuse("the caster instances are not all in the frame");
    for (std::uint32_t i = c.casterInstanceFirst; i < c.casterInstanceFirst + c.casterInstanceCount; ++i)
        if (frame.instances[i].kind == rt::GeometryKind::Spheres && frame.instances[i].dynamicSpheres)
            refuse("the particle field cannot cast (its bounds are on the GPU)");
    for (const rt::Material& m : materialTable(scene, frame)) {
        if ((m.flags & rt::kMaterialCausticReceiver) == 0)
            continue;
        if (m.receiver >= c.receivers.size())
            refuse("a receiver material names receiver " + std::to_string(m.receiver) + " of " +
                   std::to_string(c.receivers.size()));
        const auto cls = static_cast<rt::MaterialClass>(m.materialClass);
        if ((cls != rt::MaterialClass::Pbr && cls != rt::MaterialClass::Checker) || m.transmission > 0.5f ||
            !(m.metallic < 1.f))
            refuse("a receiver material must be opaque Pbr or Checker with a Lambert lobe (metallic < 1)");
    }
}

CausticMaps::CausticMaps(gfx::Context& ctx, gfx::FrameRing& ring, std::uint32_t maxResolution)
    : ctx_(ctx), ring_(ring),
      resolution_(maxResolution > 0 ? maxResolution
                                    : throw std::invalid_argument("CausticMaps: a resolution of 0")),
      photons_(ctx, gfx::shaderDir() / "modern_photons.comp.spv", kPhotonBindings, sizeof(PhotonPush)),
      resolve_(ctx, gfx::shaderDir() / "modern_caustic_resolve.comp.spv", kResolveBindings,
               sizeof(ResolvePush)) {
    for (gfx::Buffer& table : tables_)
        table =
            gfx::Buffer(ctx, {kTableBytes, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, true, "caustic receivers"});
    createMaps();
    ringBegin_ = ring.onBegin([this](std::uint32_t) { prepared_ = false; });
}

CausticMaps::~CausticMaps() {
    ring_.waitAll(); // a frame in flight may still use the maps, the tables and the descriptor sets
}

void CausticMaps::createMaps() {
    const VkDeviceSize texels = VkDeviceSize(resolution_) * resolution_;
    accumulator_ = gfx::Buffer(ctx_, {kMaxCausticReceivers * texels * 3 * sizeof(std::uint32_t),
                                      VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                                      false, "caustic accumulator"});
    gfx::ImageDesc desc{resolution_,
                        resolution_,
                        VK_FORMAT_R16G16B16A16_SFLOAT,
                        VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT |
                            VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
                        1,
                        "caustic maps"};
    desc.viewType = VK_IMAGE_VIEW_TYPE_2D_ARRAY;
    desc.layers = kMaxCausticReceivers;
    maps_ = gfx::Image(ctx_, desc);
}

void CausticMaps::reserve(std::uint32_t maxResolution) {
    if (maxResolution <= resolution_)
        return;
    // The frames in flight may still use the maps: a queue idle, which synchronization validation credits
    // when the memory is reused (as PathTracer::reserve does).
    vkQueueWaitIdle(ctx_.queue()); // setup
    resolution_ = maxResolution;
    createMaps();
}

const gfx::Buffer& CausticMaps::receiverTable() const {
    return tables_[ring_.slot()];
}

VkDeviceSize CausticMaps::bytes() const {
    VkDeviceSize bytes = accumulator_.bytes() + maps_.bytes();
    for (const gfx::Buffer& table : tables_)
        bytes += table.bytes();
    return bytes;
}

void CausticMaps::setPhotonOrder(std::uint32_t multiplier, std::uint32_t offset) {
    orderMultiplier_ = multiplier;
    orderOffset_ = offset;
}

void CausticMaps::prepare(const SceneStatic& scene, const SceneFrame& frame, std::uint32_t photons,
                          std::uint32_t seed) {
    if (!ring_.recording())
        throw std::logic_error("CausticMaps::prepare() outside FrameRing::begin()/submit()");
    if (!frame.caustics)
        throw std::invalid_argument("CausticMaps::prepare(): the frame has no caustics");
    checkCausticFrame(scene, frame, resolution_);
    const CausticFrame& c = *frame.caustics;
    const CasterBounds bounds = casterBounds(scene, frame);

    // The table: the receivers (receiver i on layer i), then the source record (caustic_common.glsl).
    std::array<rt::CausticReceiver, kMaxCausticReceivers + 1> table{};
    for (std::size_t i = 0; i < c.receivers.size(); ++i) {
        std::memcpy(table[i].worldToUv, &c.receivers[i].worldToUv, sizeof table[i].worldToUv);
        table[i].layer = static_cast<std::uint32_t>(i);
        table[i].resolution = c.receivers[i].resolution;
    }
    rt::CausticReceiver& source = table[c.receivers.size()];
    source.worldToUv[0] = bounds.center.x;
    source.worldToUv[1] = bounds.center.y;
    source.worldToUv[2] = bounds.center.z;
    source.worldToUv[3] = bounds.radius;
    source.layer = c.light < 0 ? rt::kNoLight : static_cast<std::uint32_t>(c.light);
    const gfx::Buffer& buffer = tables_[ring_.slot()];
    std::memcpy(buffer.mapped(), table.data(), sizeof table);
    buffer.flush();

    // The nominal photon flux: the light's estimated power towards the casters' sphere over the photons.
    float power = 0;
    if (c.light < 0) {
        const EnvironmentDesc& e = frame.environment;
        const float radiance = std::max({e.discRadiance.x, e.discRadiance.y, e.discRadiance.z}) * e.intensity;
        power =
            radiance * kPi * bounds.radius * bounds.radius * 2 * kPi * (1 - std::cos(e.discAngularRadius));
    } else {
        const rt::RectLight& l = frame.lights[static_cast<std::size_t>(c.light)];
        const Vec3 centre = Vec3{l.corner[0], l.corner[1], l.corner[2]} +
                            0.5f * Vec3{l.edgeU[0], l.edgeU[1], l.edgeU[2]} +
                            0.5f * Vec3{l.edgeV[0], l.edgeV[1], l.edgeV[2]};
        const float d = length(bounds.center - centre);
        const float cosMax =
            d > bounds.radius ? std::sqrt(1 - (bounds.radius / d) * (bounds.radius / d)) : 0.f;
        power = std::max({l.emission[0], l.emission[1], l.emission[2]}) * l.area * 2 * kPi * (1 - cosMax);
    }
    const float fluxUnit = photons > 0 ? power / (float(photons) * kUnitsPerPhoton) : 0.f;
    unitsPerFlux_ = fluxUnit > 0 && std::isfinite(fluxUnit) ? 1.f / fluxUnit : 0.f;
    sourceDistance_ = kSourceDistance + 4 * bounds.radius;
    photonCount_ = unitsPerFlux_ > 0 ? photons : 0;
    seed_ = seed;
    receiverCount_ = static_cast<std::uint32_t>(c.receivers.size());
    for (std::uint32_t i = 0; i < kMaxCausticReceivers; ++i) {
        receiverResolution_[i] = i < receiverCount_ ? c.receivers[i].resolution : 0;
        const float texelArea = i < receiverCount_
                                    ? uvSquareArea(c.receivers[i]) /
                                          (float(receiverResolution_[i]) * float(receiverResolution_[i]))
                                    : 0.f;
        irradianceScale_[i] = texelArea > 0 ? fluxUnit / texelArea : 0.f;
    }
    prepared_ = true;
}

void CausticMaps::record(VkCommandBuffer cmd, const PhotonScene& scene) {
    if (!prepared_)
        throw std::logic_error("CausticMaps::record() without prepare() in this frame");
    constexpr VkAccessFlags2 kReadWrite =
        VK_ACCESS_2_SHADER_STORAGE_READ_BIT | VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT;
    // Clear the sums (after the last frame's photons and resolve), then gather this frame's.
    gfx::bufferBarrier2(cmd, accumulator_.handle(), VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, kReadWrite,
                        VK_PIPELINE_STAGE_2_TRANSFER_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT);
    vkCmdFillBuffer(cmd, accumulator_.handle(), 0, VK_WHOLE_SIZE, 0);
    gfx::bufferBarrier2(cmd, accumulator_.handle(), VK_PIPELINE_STAGE_2_TRANSFER_BIT,
                        VK_ACCESS_2_TRANSFER_WRITE_BIT, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, kReadWrite);
    if (photonCount_ > 0) {
        auto buffer = [](std::uint32_t binding, const gfx::Buffer* b) {
            if (b == nullptr)
                throw std::invalid_argument("CausticMaps::record(): a PhotonScene buffer is missing");
            return gfx::DescriptorWrite::buffer(binding, {b->handle(), 0, VK_WHOLE_SIZE});
        };
        const std::array<gfx::DescriptorWrite, 12> writes = {
            gfx::DescriptorWrite::accelerationStructure(0, scene.tlas),
            buffer(1, scene.instances),
            buffer(2, scene.materials),
            buffer(3, scene.vertices),
            buffer(4, scene.indices),
            buffer(5, scene.meshRecords),
            buffer(6, scene.staticSpheres),
            buffer(7, scene.particleSpheres),
            buffer(8, scene.lights),
            buffer(9, scene.environment),
            buffer(10, &tables_[ring_.slot()]),
            buffer(11, &accumulator_),
        };
        photons_.bind(cmd, ring_, writes);
        const PhotonPush push{photonCount_,  seed_,           receiverCount_,   resolution_,
                              unitsPerFlux_, sourceDistance_, orderMultiplier_, orderOffset_};
        photons_.push(cmd, &push, sizeof push);
        const std::uint32_t rows = (photonCount_ + kPhotonsPerRow - 1) / kPhotonsPerRow;
        photons_.dispatch(cmd, std::min(photonCount_, kPhotonsPerRow), rows, 64, 1);
        gfx::bufferBarrier2(cmd, accumulator_.handle(), VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, kReadWrite,
                            VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_READ_BIT);
    }
    // Resolve every layer (a layer without a receiver to 0).
    maps_.transition(cmd, VK_IMAGE_LAYOUT_GENERAL, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                     VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT);
    for (std::uint32_t layer = 0; layer < kMaxCausticReceivers; ++layer) {
        const std::array<gfx::DescriptorWrite, 2> writes = {
            gfx::DescriptorWrite::buffer(0, {accumulator_.handle(), 0, VK_WHOLE_SIZE}),
            gfx::DescriptorWrite::image(1, {VK_NULL_HANDLE, maps_.view(), VK_IMAGE_LAYOUT_GENERAL}),
        };
        resolve_.bind(cmd, ring_, writes);
        const ResolvePush push{layer, receiverResolution_[layer], resolution_, irradianceScale_[layer]};
        resolve_.push(cmd, &push, sizeof push);
        resolve_.dispatch(cmd, resolution_, resolution_, 8, 8);
    }
}

} // namespace evo::modern
