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
const uint kTraceFlagFixedLightSelection = 4u; // step 4's fixed selectWeights (comparisons)
const uint kTraceFlagThinPanes = 8u; // the frame has thin-walled glass (shadow rays then pass through it)
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

// The step 5 feature structs (see the C++ mirrors for their meaning).
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

struct LensingUniforms { // 32 bytes (Task 25 names the fields)
    uint words[8];
};
struct CausticReceiver { // 64 bytes
    Mat34 worldToUv;
    uint layer;
    uint resolution;
    uint pad0;
    uint pad1;
};

#endif
