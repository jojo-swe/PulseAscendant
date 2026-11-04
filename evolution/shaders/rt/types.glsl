#ifndef EVO_RT_TYPES_GLSL
#define EVO_RT_TYPES_GLSL

// The structs the path tracer shares with the CPU: mirrors of src/rt/geometry.hpp (Vertex, MeshRecord, Mat34) and
// src/rt/gpu_types.hpp (the rest, FrameUniforms included), read from buffers declared layout(scalar)
// (GL_EXT_scalar_block_layout).
// Field order and types must match the C++ structs, whose sizeof and offsetof are pinned there;
// material_test checks every field on the GPU.

const uint kMaterialPbr = 0u; // MaterialClass, = hit group
const uint kMaterialGlass = 1u;
const uint kMaterialEmissive = 2u;
const uint kMaterialChecker = 3u;

const uint kPatternNone = 0u;
const uint kPatternWoodPlanks = 1u;
const uint kPatternBrushedMetal = 2u;
const uint kPatternConcrete = 3u;
const uint kPatternTesserae = 4u;       // Checker class: the 1993 rotozoom texture as stone, in the xz plane
const uint kPatternTesseraeSphere = 5u; // the same over a sphere about its instance's origin
const uint kPatternHull = 6u;           // Pbr class: the ship's panelled skin
const uint kPatternPlanet = 7u;         // Pbr class: an ocean planet under clouds
const uint kPatternCityPunched = 16u; // the City's patterns (city_patterns.glsl)
const uint kPatternCityCurtain = 17u;
const uint kPatternCityBands = 18u;
const uint kPatternCityRound = 19u;
const uint kPatternCityStreet = 20u;
const uint kPatternCityTrail = 21u;
const uint kPatternCitySign = 22u;
const uint kPatternSeaBead = 32u;   // detail_patterns.glsl: Pbr spheres, the sea's glitter
const uint kPatternLightRing = 33u; // Pbr: DOTS' floor and its landing ring
const uint kPatternCrtGlass = 34u;  // Emissive (retro texture): the Studio's CRT glass, dust and a fingerprint
const uint kPatternKeyCap = 35u;    // Pbr: the Studio's yellowed key caps
const uint kPatternWormholeWall = 36u; // the Ship's WORMHOLE (pbr.rchit): the tunnel's wall, a ring's basalt
const uint kPatternWormholeRing = 37u;
const uint kPatternPlasmaSea = 44u; // Pbr class: PLASMA PLANET's sea (plasma_sea.glsl)

const uint kNoTexture = 0u;
const uint kRetroScreenTexture = 1u;
const uint kNoLight = 0xFFFFFFFFu;
const uint kNoMedium = 0xFFFFFFFFu;
const uint kNoReceiver = 0xFFFFFFFFu;

const uint kMediumFog = 1u; // FrameUniforms.mediumFlags (modern::kMediumFog, kMediumGrid)
const uint kMediumGrid = 2u;

const uint kMaterialCameraOnlyEmission = 1u; // Material.flags
const uint kMaterialCausticReceiver = 2u;
const uint kMaterialThinWalled = 4u;

const uint kLightEmitter = 0u; // LightKind
const uint kLightRetroScreen = 1u;
const uint kLightPortal = 2u;
const uint kLightTree = 3u;

const uint kInstanceSpheres = 1u; // InstanceRecord.flags
const uint kInstanceDynamicSpheres = 2u;

const uint kGeometryTriangles = 0u; // GeometryKind
const uint kGeometrySpheres = 1u;
const uint kHitGroupsPerKind = 4u;

const uint kTraceFlagNee = 1u; // FrameUniforms.flags
const uint kTraceFlagRussianRoulette = 2u;
const uint kTraceFlagFixedLightSelection = 4u; // the original fixed selectWeights (comparisons)
const uint kTraceFlagThinPanes = 8u; // the frame has thin-walled glass (shadow rays then pass through it)
const uint kTraceFlagFilteredGrid = 16u; // the grid's density through the sampler's filter (not lavapipe)
const uint kTraceFlagPaneAnyHit = 32u;   // fast mode: shadow rays pass the panes through pane.rahit
// Fast mode: the volume layer at half rate (medium_path.glsl's mediumLayerSkipped): only pixels whose x + y
// (+ 1 with kTraceFlagHalfVolumeOdd) is even trace the primary segment's in-scattering and continuation.
const uint kTraceFlagHalfVolume = 64u;
const uint kTraceFlagHalfVolumeOdd = 128u;
// The features a path tracer pipeline is built for (common.glsl's kFeatures, specialization constant 0;
// rt::kFeature* in src/rt/gpu_types.hpp): every one in the generic pipeline; a scene's own pipeline (the
// window's fast mode) only those its frames use, so the compiler drops the rest and their registers.
const uint kFeatureLensing = 1u;      // bent main rays
const uint kFeatureFog = 2u;          // height fog
const uint kFeatureGrid = 4u;         // a grid medium
const uint kFeatureCaustics = 8u;     // caustic photon maps (and the paths they exclude)
const uint kFeaturePanes = 16u;       // thin-walled glass panes on shadow rays
const uint kFeatureTree = 32u;        // the light tree
const uint kFeatureDispersion = 64u;  // glass with an Abbe number
const uint kFeatureRetro = 128u;      // the retro screen
const uint kFeatureStudioPatterns = 256u; // wood, brushed metal, concrete, the CRT's glass, key caps
const uint kFeatureCityPatterns = 512u;   // facades, streets, trails, signs
const uint kFeatureShipPatterns = 1024u;  // hull, planet, the wormhole's wall and rings
const uint kFeatureSeaPatterns = 2048u;   // sea beads, DOTS' light ring
const uint kFeaturePlasmaSea = 4096u;     // PLASMA PLANET's sea
const uint kFeatureTesserae = 8192u;      // the 1993 rotozoom's stones
// TLAS instance masks: thin-walled glass panes, and everything else (shadow rays see only the latter).
const uint kInstanceMaskSolid = 1u;
const uint kInstanceMaskPane = 2u;

struct Vertex { // 32 bytes
    vec3 position;
    vec3 normal;
    vec2 uv;
};

struct MeshRecord { // 16 bytes
    uint firstVertex;
    uint vertexCount;
    uint firstIndex;
    uint indexCount;
};

struct Mat34 { // 48 bytes: rt::Mat34, row-major 3x4 (rows[r][3] is the translation)
    vec4 rows[3];
};

struct Material { // 96 bytes
    vec3 baseColor;
    float roughness;
    float metallic;
    float ior;
    float transmission;
    vec3 emission;
    float emissionScale;
    float coat;
    float coatRoughness;
    uint pattern;
    uint texture; // a member name, so the built-in texture() stays callable
    float patternScale;
    vec3 baseColor2; // the pattern's second colour; a solid dielectric's absorption per unit length
    uint materialClass;
    uint flags; // kMaterial*
    float abbe;
    uint medium;
    uint receiver; // a CausticReceiver index, or kNoReceiver
};

struct RectLight { // 64 bytes: point = corner + u * edgeU + v * edgeV, emits along normalize(cross(edgeU, edgeV))
    vec3 corner;
    vec3 edgeU;
    vec3 edgeV;
    vec3 emission;
    uint kind;
    float selectWeight;
    float area;
    uint gate; // the Portal light this light is seen through, or kNoLight
};

struct InstanceRecord { // 16 bytes; index = gl_InstanceCustomIndexEXT
    uint mesh; // the first sphere for a sphere instance
    uint material;
    uint light;
    uint flags; // kInstance*
};

// The path tracer's per-frame constants (see the C++ mirror for the camera convention).
struct FrameUniforms { // 208 bytes
    vec3 cameraPosition;
    float tanHalfFovY;
    vec3 cameraRight;
    float aspect;
    vec3 cameraUp;
    float aperture;
    vec3 cameraForward;
    float focusDistance;
    vec3 prevPosition;
    float prevTanHalfFovY;
    vec3 prevRight;
    float prevAspect;
    vec3 prevUp;
    uint prevValid;
    vec3 prevForward;
    float pixelSpread;
    uint width;
    uint height;
    uint frameSeed;
    uint spp; // spp and instanceCount are filled in but not read by any shader
    uint maxBounces;
    uint flags;
    uint lightCount;
    uint instanceCount;
    float clampIndirect;
    float lightWeightSum; // not read: light selection sums its weights per shading point (lights.glsl)
    float regularizeRoughness;
    uint treeLightCount;
    uint mediumFlags;
    uint causticReceivers;
    uint lensing;
    float seconds; // chapter-local show time
    float shutterOffset;
    float environmentSelect;
    uint sliceIndex;
    uint sliceCount;
};

// The feature structs (see the C++ mirrors for their meaning).
struct EnvironmentUniforms { // 112 bytes
    uint kind;
    float intensity;
    float discCosRadius;
    float discShare;
    vec3 discDirection;
    float domeShare;
    vec3 discRadiance;
    float starDensity;
    vec3 zenith;
    float starBrightness;
    vec3 horizon;
    float townDensity;
    vec3 ground;
    float haze;
    vec3 glowDirection;
    float glowStrength;
};

struct MediumUniforms { // 128 bytes
    Mat34 worldToMedium;
    uvec3 gridSize;
    float densityScale;
    vec3 gridAlbedo;
    float gridPhaseG;
    uvec3 majorantSize;
    float emissionScale;
    vec3 fogAlbedo;
    float fogPhaseG;
    float fogDensity;
    float fogBaseHeight;
    float fogFalloff;
    uint pad;
};

struct SphereRecord { // 48 bytes
    vec3 center;
    float radius;
    vec3 color;
    uint material;
    vec3 prevCenter;
    float emission;
};

struct TreeLight { // 48 bytes
    vec3 center;
    float radius;
    vec3 emission;
    uint sphere;
    uint trail;
    uint depth;
    uint pad0;
    uint pad1;
};

struct LightTreeNode { // 32 bytes
    vec3 boundsMin;
    float power;
    vec3 boundsMax;
    uint child;
};

struct LensingUniforms { // 32 bytes (rt::LensingUniforms documents the fields)
    vec3 center;
    float massRadius;
    float influenceRadius;
    uint steps;
    uint flags;
    float stepScale;
};
const uint kLensingDisc = 1u;
struct CausticReceiver { // 64 bytes
    Mat34 worldToUv;
    uint layer;
    uint resolution;
    uint pad0;
    uint pad1;
};

#endif
