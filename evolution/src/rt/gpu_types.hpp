#pragma once

// The structs the path tracer shares with GLSL. Each is a C++ mirror of a `layout(scalar)` struct in
// shaders/rt/types.glsl: plain 4-byte float and uint fields in declaration order, with sizeof and every
// offsetof pinned below, and every field checked on the GPU by material_test. Change both
// sides together. The geometry structs types.glsl also mirrors (Vertex, MeshRecord, Mat34) live in
// rt/geometry.hpp, and the descriptor bindings in rt/bindings.hpp.
//
// Step 5's ABI (Tasks 1 and 1b froze it so that later tasks fill in features without moving a field):
// Material is 96 bytes (flags, abbe, medium, receiver), RectLight's last word is the portal `gate`,
// InstanceRecord's the sphere `flags`, FrameUniforms is 208 bytes (with the frame's `seconds`), and the
// feature structs below (EnvironmentUniforms, MediumUniforms, SphereRecord, TreeLight, LightTreeNode,
// LensingUniforms, CausticReceiver) have their sizes fixed (Task 11 named MediumUniforms' fields within its
// 128 bytes). The closest-hit -> raygen payload (HitPayload, shaders/rt/common.glsl) is frozen the same way.
// A task that changes a struct updates material_test's layout probe in the same commit.

#include <cstddef>
#include <cstdint>
#include <type_traits>

namespace evo::rt {

// --- Materials and lights ----------------------------------------------------------------------------------

enum class MaterialClass : std::uint32_t { Pbr = 0, Glass = 1, Emissive = 2, Checker = 3 }; // = hit group
// Tesserae and TesseraeSphere are the Checker class's (shaders/rt/checker.rchit, shaders/rt/tesserae.glsl):
// the 1993 rotozoom's 128x128 texture laid as a polished stone mosaic, one tessera of patternScale per texel
// in the pixel's palette colour (marble, basalt or gold leaf), in the world's xz plane (Tesserae) or over a
// sphere about its instance's origin along great circles from the point above its centre (TesseraeSphere);
// baseColor2's x and y move the mosaic by that many tesserae (baseColor is unused).
// Hull and Planet are the Ship's (step 5, Task 16; shaders/rt/pbr.rchit documents what they read).
// The City's patterns (Task 23 of step 5, shaders/rt/city_patterns.glsl) start at 16: the facades of its
// towers (punched windows, a curtain wall, ribbon bands, a round curtain wall), its streets, and on Emissive
// materials the light trails of its traffic and the lettering of its signs.
enum class Pattern : std::uint32_t {
    None = 0,
    WoodPlanks = 1,
    BrushedMetal = 2,
    Concrete = 3,
    Tesserae = 4,
    TesseraeSphere = 5,
    Hull = 6,
    Planet = 7,
    CityPunched = 16,
    CityCurtain = 17,
    CityBands = 18,
    CityRound = 19,
    CityStreet = 20,
    CityTrail = 21,
    CitySign = 22
};
inline constexpr std::uint32_t kNoTexture = 0, kRetroScreenTexture = 1, kNoLight = ~0u, kNoMedium = ~0u,
                               kNoReceiver = ~0u;

// Material::flags.
//   CameraOnlyEmission  the emission counts only as seen from the camera or along a chain of delta events
//                       from it, never after a rough vertex, and the surface is never a light (Task 6)
//   CausticReceiver     the surface receives the caustic photon map (Task 13)
//   ThinWalled          a dielectric of no thickness: transmission without refraction (stained glass), always
//                       smooth (bsdf.glsl's thinDielectricSample; the raygen and the photons, Task 20)
inline constexpr std::uint32_t kMaterialCameraOnlyEmission = 1u, kMaterialCausticReceiver = 2u,
                               kMaterialThinWalled = 4u;

// The BSDF (shaders/rt/bsdf.glsl): an opaque surface is a clear coat (F0 0.04, coatRoughness, weight `coat`)
// over GGX specular with F0 = mix(F0(ior), baseColor, metallic) plus (1 - metallic) Lambert, where
// F0(ior) = ((ior - 1) / (ior + 1))^2 is 0.04 at the default IOR 1.5 and 0 at IOR 1 (pure Lambert). With
// transmission > 0.5 it is a dielectric of that IOR, tinted by baseColor. A specular lobe with roughness
// below 0.02 is a smooth (delta) lobe. The defaults are a mid-grey plastic.
struct Material { // 96 bytes, scalar
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
    // The pattern's second colour; on a dielectric (transmission > 0.5, not thin-walled) its absorption
    // coefficient per unit length instead (Beer-Lambert: exp(-baseColor2 * d) after d inside; Task 20 of step
    // 5), 0 for clear glass.
    float baseColor2[3] = {0.f, 0.f, 0.f};
    std::uint32_t materialClass = static_cast<std::uint32_t>(MaterialClass::Pbr);
    std::uint32_t flags = 0;          // kMaterial*
    float abbe = 0.f;                 // dispersion: the Abbe number of a dielectric, 0 for none (Task 6)
    std::uint32_t medium = kNoMedium; // the participating medium inside a closed surface (Task 11)
    // The caustic receiver this surface is: an index into the receiver table (CausticReceiver[], binding
    // EVO_BINDING_CAUSTIC_RECEIVERS) on a kMaterialCausticReceiver surface, else kNoReceiver (Task 13).
    std::uint32_t receiver = kNoReceiver;
};

// Portal: radiance = the environment along the direction. Tree: the whole light tree of a scene's sphere
// lights (Task 12), one top-level entry.
enum class LightKind : std::uint32_t { Emitter = 0, RetroScreen = 1, Portal = 2, Tree = 3 };

// A one-sided rectangular area light: point = corner + u * edgeU + v * edgeV for u, v in [0, 1], emitting
// along normalize(cross(edgeU, edgeV)). `gate` is the index of the Portal light through which this light is
// seen (a moon outside a window: light selection gives it no weight at a point the portal does not show it
// to, Task 3), or kNoLight.
struct RectLight { // 64 bytes, scalar
    float corner[3];
    float edgeU[3];
    float edgeV[3];
    float emission[3];
    std::uint32_t kind;
    float selectWeight;
    float area;
    std::uint32_t gate;
};

// InstanceRecord::flags: the instance is a set of analytic spheres (`mesh` then holds its first sphere), and
// those are the frame's particle field rather than the scene's static spheres (Task 8).
inline constexpr std::uint32_t kInstanceSpheres = 1u, kInstanceDynamicSpheres = 2u;

struct InstanceRecord { // 16 bytes; index = gl_InstanceCustomIndexEXT
    std::uint32_t mesh, material, light, flags;
};

// What an instance's geometry is: triangles of a mesh, or analytic spheres (Task 8: an intersection shader,
// a second set of kHitGroupsPerKind hit groups).
enum class GeometryKind : std::uint32_t { Triangles = 0, Spheres = 1 };
inline constexpr std::uint32_t kHitGroupsPerKind = 4; // = MaterialClass
constexpr std::uint32_t hitGroupFor(MaterialClass c, GeometryKind k) {
    return static_cast<std::uint32_t>(c) + kHitGroupsPerKind * static_cast<std::uint32_t>(k);
}

// --- The path tracer's frame -------------------------------------------------------------------------------

// FrameUniforms::flags. FixedLightSelection: choose lights by their fixed selectWeight alone (step 4), not by
// their importance at the shading point (Task 3 of step 5); for comparisons and tests.
inline constexpr std::uint32_t kTraceFlagNee = 1u, kTraceFlagRussianRoulette = 2u,
                               kTraceFlagFixedLightSelection = 4u, kTraceFlagThinPanes = 8u;
// TLAS instance masks (Task 20): a thin-walled glass pane is a filter, not an occluder; shadow rays trace the
// solid instances, then the panes in their way for their tint.
inline constexpr std::uint8_t kInstanceMaskSolid = 1u, kInstanceMaskPane = 2u;

// What the path tracer's raygen reads once per frame (one buffer per ring slot). The camera is an orthonormal
// basis: right, up and forward unit vectors (forward looks into the picture, up is the picture's up); a pixel
// (x, y) with y down maps to forward + ((2 (x + 0.5) / width - 1) aspect right + (1 - 2 (y + 0.5) / height)
// up) tanHalfFovY. `aperture` is the thin lens's diameter (0: a pinhole), focused at focusDistance along
// forward. The prev* fields are the previous frame's camera for the motion guide (prevValid 0: none).
// pixelSpread is the ray cone's spread angle, atan(2 tanHalfFovY / height). The fields from
// regularizeRoughness on are step 5's, each filled by the task that reads it (0 until then; sliceCount 1).
struct FrameUniforms { // 208 bytes, scalar
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
    float clampIndirect;            // luminance limit of one contribution after a rough vertex
    float lightWeightSum;           // the sum of every light's selectWeight (no shader reads it since Task 3)
    float regularizeRoughness;      // the least roughness of every lobe after the first rough vertex (Task 6)
    std::uint32_t treeLightCount;   // the lights of the light tree (Task 12)
    std::uint32_t mediumFlags;      // which media the frame has (Task 11)
    std::uint32_t causticReceivers; // caustic maps bound (Task 13)
    std::uint32_t lensing;          // 1: bend rays (Task 25)
    float seconds;           // the frame's chapter-local show time (TraceSettings::seconds), every frame
    float shutterOffset;     // this slice's time offset from `seconds` (Task 9)
    float environmentSelect; // the probability that NEE samples the environment instead of a rect light
    std::uint32_t sliceIndex, sliceCount; // the export shutter's time slices (Task 9)
};

// --- Step 5 feature structs (Task 1 fixed their sizes; the code that owns each fills them)
// -------------------------------

// The scene's environment (binding EVO_BINDING_ENVIRONMENT; Task 2): kind (EnvironmentKind), an overall
// intensity, the sun or moon disc (cosine of its angular radius, the share of environment samples it gets,
// its direction and radiance), the dome's share, the star field, the gradient (zenith, horizon, ground), haze
// and a glow.
struct EnvironmentUniforms { // 112 bytes, scalar
    std::uint32_t kind;
    float intensity;
    float discCosRadius;
    float discShare;
    float discDirection[3];
    float domeShare;
    float discRadiance[3];
    float starDensity;
    float zenith[3];
    float starBrightness;
    float horizon[3];
    float townDensity;
    float ground[3];
    float haze;
    float glowDirection[3];
    float glowStrength;
};

// The frame's participating media (binding EVO_BINDING_MEDIUM; Task 11, modern::packMedium; which of them the
// frame has is FrameUniforms::mediumFlags). The grid: worldToMedium (row-major 3x4, the box to [0, 1]^3), its
// voxels per axis, sigma_t = densityScale * the baked density, the single-scattering albedo and HG g, the
// majorant grid's cells per axis and the emission's scale. The fog: albedo, HG g, and sigma_t(y) =
// fogDensity * exp(-fogFalloff * (y - fogBaseHeight)).
struct MediumUniforms { // 128 bytes, scalar
    float worldToMedium[12];
    std::uint32_t gridSize[3];
    float densityScale;
    float gridAlbedo[3];
    float gridPhaseG;
    std::uint32_t majorantSize[3];
    float emissionScale;
    float fogAlbedo[3];
    float fogPhaseG;
    float fogDensity;
    float fogBaseHeight;
    float fogFalloff;
    std::uint32_t pad;
};

// An analytic sphere (static spheres, binding EVO_BINDING_STATIC_SPHERES, and the particle field,
// EVO_BINDING_PARTICLE_SPHERES; Task 8): colour multiplies the material's baseColor, emission scales
// colour into emitted radiance, prevCenter is where it was in the previous frame (motion vectors).
struct SphereRecord { // 48 bytes, scalar
    float center[3];
    float radius;
    float color[3];
    std::uint32_t material;
    float prevCenter[3];
    float emission;
};

// A light of the light tree (binding EVO_BINDING_TREE_LIGHTS; Task 12): an emissive sphere. trail holds the
// child bits from the root to its leaf, depth their count.
struct TreeLight { // 48 bytes, scalar
    float center[3];
    float radius;
    float emission[3];
    std::uint32_t sphere;
    std::uint32_t trail, depth, pad0, pad1;
};

// A node of the light tree (binding EVO_BINDING_TREE_NODES; Task 12). A leaf has child = light | 0x80000000;
// an inner node's children are child and child + 1.
struct LightTreeNode { // 32 bytes, scalar
    float boundsMin[3];
    float power;
    float boundsMax[3];
    std::uint32_t child;
};

// Gravitational lensing (binding EVO_BINDING_LENSING; Task 25, modern::packLensing): the black hole's centre
// and Schwarzschild radius r_s, the radius of the sphere about it inside which main rays follow the null
// geodesic, the most integration steps a ray takes there, flags (kLensingDisc: the frame's grid medium is the
// lens's accretion disc, which the lens marches along the bent segments) and each step's length as a fraction
// of the ray's distance from the centre.
struct LensingUniforms { // 32 bytes, scalar
    float center[3];
    float massRadius;
    float influenceRadius;
    std::uint32_t steps;
    std::uint32_t flags;
    float stepScale;
};
inline constexpr std::uint32_t kLensingDisc = 1u;

// A caustic receiver (binding EVO_BINDING_CAUSTIC_RECEIVERS, the table Material::receiver indexes; Task 13
// fills it and may name the pads): worldToUv maps a world point to the receiver's map, row-major 3x4 like
// Mat34 (uv in [0, 1]^2 on the receiver, the third row its distance off the surface), `layer` is its layer of
// the caustic maps (EVO_BINDING_CAUSTIC_MAPS) and `resolution` that layer's texels per side.
struct CausticReceiver { // 64 bytes, scalar
    float worldToUv[12];
    std::uint32_t layer, resolution, pad0, pad1;
};

// --- Layout pins -------------------------------------------------------------------------------------------

template <typename T>
inline constexpr bool kGpuMirror = std::is_standard_layout_v<T> && std::is_trivially_copyable_v<T>;

static_assert(kGpuMirror<Material> && sizeof(Material) == 96);
static_assert(offsetof(Material, baseColor) == 0 && offsetof(Material, roughness) == 12 &&
              offsetof(Material, metallic) == 16 && offsetof(Material, ior) == 20 &&
              offsetof(Material, transmission) == 24 && offsetof(Material, emission) == 28 &&
              offsetof(Material, emissionScale) == 40 && offsetof(Material, coat) == 44 &&
              offsetof(Material, coatRoughness) == 48 && offsetof(Material, pattern) == 52 &&
              offsetof(Material, texture) == 56 && offsetof(Material, patternScale) == 60 &&
              offsetof(Material, baseColor2) == 64 && offsetof(Material, materialClass) == 76 &&
              offsetof(Material, flags) == 80 && offsetof(Material, abbe) == 84 &&
              offsetof(Material, medium) == 88 && offsetof(Material, receiver) == 92);

static_assert(kGpuMirror<RectLight> && sizeof(RectLight) == 64);
static_assert(offsetof(RectLight, corner) == 0 && offsetof(RectLight, edgeU) == 12 &&
              offsetof(RectLight, edgeV) == 24 && offsetof(RectLight, emission) == 36 &&
              offsetof(RectLight, kind) == 48 && offsetof(RectLight, selectWeight) == 52 &&
              offsetof(RectLight, area) == 56 && offsetof(RectLight, gate) == 60);

static_assert(kGpuMirror<InstanceRecord> && sizeof(InstanceRecord) == 16);
static_assert(offsetof(InstanceRecord, mesh) == 0 && offsetof(InstanceRecord, material) == 4 &&
              offsetof(InstanceRecord, light) == 8 && offsetof(InstanceRecord, flags) == 12);

static_assert(kGpuMirror<FrameUniforms> && sizeof(FrameUniforms) == 208);
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
              offsetof(FrameUniforms, lightWeightSum) == 164 &&
              offsetof(FrameUniforms, regularizeRoughness) == 168 &&
              offsetof(FrameUniforms, treeLightCount) == 172 && offsetof(FrameUniforms, mediumFlags) == 176 &&
              offsetof(FrameUniforms, causticReceivers) == 180 && offsetof(FrameUniforms, lensing) == 184 &&
              offsetof(FrameUniforms, seconds) == 188 && offsetof(FrameUniforms, shutterOffset) == 192 &&
              offsetof(FrameUniforms, environmentSelect) == 196 &&
              offsetof(FrameUniforms, sliceIndex) == 200 && offsetof(FrameUniforms, sliceCount) == 204);

static_assert(kGpuMirror<EnvironmentUniforms> && sizeof(EnvironmentUniforms) == 112);
static_assert(
    offsetof(EnvironmentUniforms, kind) == 0 && offsetof(EnvironmentUniforms, intensity) == 4 &&
    offsetof(EnvironmentUniforms, discCosRadius) == 8 && offsetof(EnvironmentUniforms, discShare) == 12 &&
    offsetof(EnvironmentUniforms, discDirection) == 16 && offsetof(EnvironmentUniforms, domeShare) == 28 &&
    offsetof(EnvironmentUniforms, discRadiance) == 32 && offsetof(EnvironmentUniforms, starDensity) == 44 &&
    offsetof(EnvironmentUniforms, zenith) == 48 && offsetof(EnvironmentUniforms, starBrightness) == 60 &&
    offsetof(EnvironmentUniforms, horizon) == 64 && offsetof(EnvironmentUniforms, townDensity) == 76 &&
    offsetof(EnvironmentUniforms, ground) == 80 && offsetof(EnvironmentUniforms, haze) == 92 &&
    offsetof(EnvironmentUniforms, glowDirection) == 96 && offsetof(EnvironmentUniforms, glowStrength) == 108);

static_assert(kGpuMirror<MediumUniforms> && sizeof(MediumUniforms) == 128);
static_assert(offsetof(MediumUniforms, worldToMedium) == 0 && offsetof(MediumUniforms, gridSize) == 48 &&
              offsetof(MediumUniforms, densityScale) == 60 && offsetof(MediumUniforms, gridAlbedo) == 64 &&
              offsetof(MediumUniforms, gridPhaseG) == 76 && offsetof(MediumUniforms, majorantSize) == 80 &&
              offsetof(MediumUniforms, emissionScale) == 92 && offsetof(MediumUniforms, fogAlbedo) == 96 &&
              offsetof(MediumUniforms, fogPhaseG) == 108 && offsetof(MediumUniforms, fogDensity) == 112 &&
              offsetof(MediumUniforms, fogBaseHeight) == 116 && offsetof(MediumUniforms, fogFalloff) == 120 &&
              offsetof(MediumUniforms, pad) == 124);

static_assert(kGpuMirror<SphereRecord> && sizeof(SphereRecord) == 48);
static_assert(offsetof(SphereRecord, center) == 0 && offsetof(SphereRecord, radius) == 12 &&
              offsetof(SphereRecord, color) == 16 && offsetof(SphereRecord, material) == 28 &&
              offsetof(SphereRecord, prevCenter) == 32 && offsetof(SphereRecord, emission) == 44);

static_assert(kGpuMirror<TreeLight> && sizeof(TreeLight) == 48);
static_assert(offsetof(TreeLight, center) == 0 && offsetof(TreeLight, radius) == 12 &&
              offsetof(TreeLight, emission) == 16 && offsetof(TreeLight, sphere) == 28 &&
              offsetof(TreeLight, trail) == 32 && offsetof(TreeLight, depth) == 36 &&
              offsetof(TreeLight, pad0) == 40 && offsetof(TreeLight, pad1) == 44);

static_assert(kGpuMirror<LightTreeNode> && sizeof(LightTreeNode) == 32);
static_assert(offsetof(LightTreeNode, boundsMin) == 0 && offsetof(LightTreeNode, power) == 12 &&
              offsetof(LightTreeNode, boundsMax) == 16 && offsetof(LightTreeNode, child) == 28);

static_assert(kGpuMirror<LensingUniforms> && sizeof(LensingUniforms) == 32);
static_assert(offsetof(LensingUniforms, center) == 0 && offsetof(LensingUniforms, massRadius) == 12 &&
              offsetof(LensingUniforms, influenceRadius) == 16 && offsetof(LensingUniforms, steps) == 20 &&
              offsetof(LensingUniforms, flags) == 24 && offsetof(LensingUniforms, stepScale) == 28);

static_assert(kGpuMirror<CausticReceiver> && sizeof(CausticReceiver) == 64);
static_assert(offsetof(CausticReceiver, worldToUv) == 0 && offsetof(CausticReceiver, layer) == 48 &&
              offsetof(CausticReceiver, resolution) == 52 && offsetof(CausticReceiver, pad0) == 56 &&
              offsetof(CausticReceiver, pad1) == 60);

} // namespace evo::rt
