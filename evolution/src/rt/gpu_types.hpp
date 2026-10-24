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

} // namespace evo::rt
