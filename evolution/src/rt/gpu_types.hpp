#pragma once

// The structs the path tracer shares with GLSL. Each is a C++ mirror of a `layout(scalar)` struct in
// shaders/rt/types.glsl: plain 4-byte float and uint fields in declaration order, with sizeof and every
// offsetof pinned below, and every field checked on the GPU by material_test (Review Focus 10). Change both
// sides together. The geometry structs types.glsl also mirrors (Vertex, MeshRecord) live in rt/geometry.hpp.

#include <cstddef>
#include <cstdint>
#include <type_traits>

namespace evo::rt {

// --- Materials and lights ----------------------------------------------------------------------------------

enum class MaterialClass : std::uint32_t { Pbr = 0, Glass = 1, Emissive = 2, Checker = 3 }; // = hit group
enum class Pattern : std::uint32_t { None = 0, WoodPlanks = 1, BrushedMetal = 2 };
inline constexpr std::uint32_t kNoTexture = 0, kRetroScreenTexture = 1, kNoLight = ~0u;

// The BSDF (shaders/rt/bsdf.glsl): an opaque surface is a clear coat (F0 0.04, coatRoughness, weight `coat`)
// over GGX specular with F0 = mix(F0(ior), baseColor, metallic) plus (1 - metallic) Lambert, where
// F0(ior) = ((ior - 1) / (ior + 1))^2 is 0.04 at the default IOR 1.5 and 0 at IOR 1 (pure Lambert). With
// transmission > 0.5 it is a dielectric of that IOR, tinted by baseColor. A specular lobe with roughness
// below 0.02 is a smooth (delta) lobe. The defaults are a mid-grey plastic.
struct Material { // 80 bytes, scalar
    float baseColor[3] = {0.8f, 0.8f, 0.8f};
    float roughness = 0.5f;
    float metallic = 0.f;
    // Schlick's F90 is saturate(50 * mean F0), so below IOR ~1.33 (mean F0 < 0.02) grazing Fresnel is capped
    // under 1. Deliberate: IOR 1 then has no specular lobe at all and is pure Lambert. The Lambert lobe is
    // weighted by (1 - F(wo)) (1 - F(wi)) without energy compensation: at IOR 1.5 the (1 - F(wi)) factor
    // alone takes about 8 % (its cosine-weighted mean is 0.914), about 12 % in all at normal incidence.
    float ior = 1.5f;
    float transmission = 0.f;
    float emission[3] = {0.f, 0.f, 0.f};
    float emissionScale = 1.f;
    float coat = 0.f;
    float coatRoughness = 0.03f;
    std::uint32_t pattern = static_cast<std::uint32_t>(Pattern::None);
    std::uint32_t texture = kNoTexture;
    float patternScale = 1.f;
    float baseColor2[3] = {0.f, 0.f, 0.f}; // the pattern's second colour
    std::uint32_t materialClass = static_cast<std::uint32_t>(MaterialClass::Pbr);
};

enum class LightKind : std::uint32_t {
    Emitter = 0,
    RetroScreen = 1,
    Portal = 2
}; // Portal: radiance = sky(dir)

// A one-sided rectangular area light: point = corner + u * edgeU + v * edgeV for u, v in [0, 1], emitting
// along normalize(cross(edgeU, edgeV)).
struct RectLight { // 64 bytes, scalar
    float corner[3];
    float edgeU[3];
    float edgeV[3];
    float emission[3];
    std::uint32_t kind;
    float selectWeight;
    float area;
    std::uint32_t pad;
};

struct InstanceRecord { // 16 bytes; index = gl_InstanceCustomIndexEXT
    std::uint32_t mesh, material, light, pad;
};

// --- The path tracer's frame -------------------------------------------------------------------------------

// FrameUniforms::flags.
inline constexpr std::uint32_t kTraceFlagNee = 1u, kTraceFlagRussianRoulette = 2u;

// What the path tracer's raygen reads once per frame (one buffer per ring slot). The camera is an orthonormal
// basis: right, up and forward unit vectors (forward looks into the picture, up is the picture's up); a pixel
// (x, y) with y down maps to forward + ((2 (x + 0.5) / width - 1) aspect right + (1 - 2 (y + 0.5) / height)
// up) tanHalfFovY. `aperture` is the thin lens's diameter (0: a pinhole), focused at focusDistance along
// forward. The prev* fields are the previous frame's camera for the motion guide (prevValid 0: none).
// pixelSpread is the ray cone's spread angle, atan(2 tanHalfFovY / height).
struct FrameUniforms { // 176 bytes, scalar
    float cameraPosition[3];
    float tanHalfFovY;
    float cameraRight[3];
    float aspect;
    float cameraUp[3];
    float aperture;
    float cameraForward[3];
    float focusDistance;
    float prevPosition[3];
    float prevTanHalfFovY;
    float prevRight[3];
    float prevAspect;
    float prevUp[3];
    std::uint32_t prevValid;
    float prevForward[3];
    float pixelSpread;
    std::uint32_t width, height;
    std::uint32_t frameSeed;
    std::uint32_t spp; // samples of the whole frame, over every dispatch
    std::uint32_t maxBounces;
    std::uint32_t flags; // kTraceFlag*
    std::uint32_t lightCount;
    std::uint32_t instanceCount;
    float clampIndirect;  // luminance limit of one contribution after the first bounce
    float lightWeightSum; // the sum of every light's selectWeight
    std::uint32_t pad0, pad1;
};

// --- Layout pins -------------------------------------------------------------------------------------------

template <typename T>
inline constexpr bool kGpuMirror = std::is_standard_layout_v<T> && std::is_trivially_copyable_v<T>;

static_assert(kGpuMirror<Material> && sizeof(Material) == 80);
static_assert(offsetof(Material, baseColor) == 0 && offsetof(Material, roughness) == 12 &&
              offsetof(Material, metallic) == 16 && offsetof(Material, ior) == 20 &&
              offsetof(Material, transmission) == 24 && offsetof(Material, emission) == 28 &&
              offsetof(Material, emissionScale) == 40 && offsetof(Material, coat) == 44 &&
              offsetof(Material, coatRoughness) == 48 && offsetof(Material, pattern) == 52 &&
              offsetof(Material, texture) == 56 && offsetof(Material, patternScale) == 60 &&
              offsetof(Material, baseColor2) == 64 && offsetof(Material, materialClass) == 76);

static_assert(kGpuMirror<RectLight> && sizeof(RectLight) == 64);
static_assert(offsetof(RectLight, corner) == 0 && offsetof(RectLight, edgeU) == 12 &&
              offsetof(RectLight, edgeV) == 24 && offsetof(RectLight, emission) == 36 &&
              offsetof(RectLight, kind) == 48 && offsetof(RectLight, selectWeight) == 52 &&
              offsetof(RectLight, area) == 56 && offsetof(RectLight, pad) == 60);

static_assert(kGpuMirror<InstanceRecord> && sizeof(InstanceRecord) == 16);
static_assert(offsetof(InstanceRecord, mesh) == 0 && offsetof(InstanceRecord, material) == 4 &&
              offsetof(InstanceRecord, light) == 8 && offsetof(InstanceRecord, pad) == 12);

static_assert(kGpuMirror<FrameUniforms> && sizeof(FrameUniforms) == 176);
static_assert(offsetof(FrameUniforms, cameraPosition) == 0 && offsetof(FrameUniforms, tanHalfFovY) == 12 &&
              offsetof(FrameUniforms, cameraRight) == 16 && offsetof(FrameUniforms, aspect) == 28 &&
              offsetof(FrameUniforms, cameraUp) == 32 && offsetof(FrameUniforms, aperture) == 44 &&
              offsetof(FrameUniforms, cameraForward) == 48 && offsetof(FrameUniforms, focusDistance) == 60 &&
              offsetof(FrameUniforms, prevPosition) == 64 && offsetof(FrameUniforms, prevTanHalfFovY) == 76 &&
              offsetof(FrameUniforms, prevRight) == 80 && offsetof(FrameUniforms, prevAspect) == 92 &&
              offsetof(FrameUniforms, prevUp) == 96 && offsetof(FrameUniforms, prevValid) == 108 &&
              offsetof(FrameUniforms, prevForward) == 112 && offsetof(FrameUniforms, pixelSpread) == 124 &&
              offsetof(FrameUniforms, width) == 128 && offsetof(FrameUniforms, height) == 132 &&
              offsetof(FrameUniforms, frameSeed) == 136 && offsetof(FrameUniforms, spp) == 140 &&
              offsetof(FrameUniforms, maxBounces) == 144 && offsetof(FrameUniforms, flags) == 148 &&
              offsetof(FrameUniforms, lightCount) == 152 && offsetof(FrameUniforms, instanceCount) == 156 &&
              offsetof(FrameUniforms, clampIndirect) == 160 &&
              offsetof(FrameUniforms, lightWeightSum) == 164 && offsetof(FrameUniforms, pad0) == 168 &&
              offsetof(FrameUniforms, pad1) == 172);

} // namespace evo::rt
