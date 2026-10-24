#ifndef EVO_RT_TYPES_GLSL
#define EVO_RT_TYPES_GLSL

// The structs the path tracer shares with the CPU: mirrors of src/rt/geometry.hpp (Vertex, MeshRecord) and
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

const uint kNoTexture = 0u;
const uint kRetroScreenTexture = 1u;
const uint kNoLight = 0xFFFFFFFFu;

const uint kLightEmitter = 0u; // LightKind
const uint kLightRetroScreen = 1u;
const uint kLightPortal = 2u;

const uint kTraceFlagNee = 1u; // FrameUniforms.flags
const uint kTraceFlagRussianRoulette = 2u;

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

struct Material { // 80 bytes
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
    vec3 baseColor2;
    uint materialClass;
};

struct RectLight { // 64 bytes: point = corner + u * edgeU + v * edgeV, emits along normalize(cross(edgeU, edgeV))
    vec3 corner;
    vec3 edgeU;
    vec3 edgeV;
    vec3 emission;
    uint kind;
    float selectWeight;
    float area;
    uint pad;
};

struct InstanceRecord { // 16 bytes; index = gl_InstanceCustomIndexEXT
    uint mesh;
    uint material;
    uint light;
    uint pad;
};

// The path tracer's per-frame constants (see the C++ mirror for the camera convention).
struct FrameUniforms { // 176 bytes
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
    float lightWeightSum;
    uint pad0;
    uint pad1;
};

#endif
