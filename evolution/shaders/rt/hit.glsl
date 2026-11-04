#ifndef EVO_RT_HIT_GLSL
#define EVO_RT_HIT_GLSL

// Closest-hit helpers: the hit triangle's interpolated attributes in world space (or, compiled with
// GEOMETRY_SPHERES, the hit analytic sphere's: the rt_<class>_sphere.rchit.spv variants),
// the procedural patterns and writing the evaluated surface into the payload. Closest-hit shaders never
// trace (recursion depth 1).

#include "common.glsl"
#include "random.glsl"

#ifdef GEOMETRY_SPHERES
#include "sphere.glsl" // sphere.rint reports no attributes: hitPoint() recomputes the point from gl_HitTEXT
#else
hitAttributeEXT vec2 hitBarycentrics;
#endif
// EVO_HIT_POINT_ONLY (pane.rahit, whose ray carries another payload): hitPoint() and the patterns alone.
#ifndef EVO_HIT_POINT_ONLY
layout(location = 0) rayPayloadInEXT HitPayload payload;
#endif

// How far a hit point moved with its instance since the previous frame: the instance's previous transform
// (binding 21, PathTracer: the current one when the frame has no matching previous instances) applied to the
// object-space point it had then (`before`), minus the current transform applied to the point it has now
// (`now`). Exactly 0 when the instance and the point both stayed, so a still object has no motion at all.
vec3 hitMotion(vec3 now, vec3 before) {
    Mat34 m = previousTransforms[gl_InstanceCustomIndexEXT];
    mat4x3 c = gl_ObjectToWorldEXT; // column-major: c[column][row]
    vec4 r0 = vec4(c[0][0], c[1][0], c[2][0], c[3][0]);
    vec4 r1 = vec4(c[0][1], c[1][1], c[2][1], c[3][1]);
    vec4 r2 = vec4(c[0][2], c[1][2], c[2][2], c[3][2]);
    if (now == before && m.rows[0] == r0 && m.rows[1] == r1 && m.rows[2] == r2) {
        return vec3(0.0);
    }
    vec4 a = vec4(before, 1.0);
    vec4 b = vec4(now, 1.0);
    vec3 then = vec3(dot(m.rows[0], a), dot(m.rows[1], a), dot(m.rows[2], a));
    vec3 here = vec3(dot(r0, b), dot(r1, b), dot(r2, b));
    return then - here;
}

struct HitPoint {
    vec3 position;        // world
    vec3 normal;          // world shading normal, outward (on the geometric normal's side of the viewer)
    vec3 geometricNormal; // world, outward
    vec2 uv;
    bool front; // the ray hit the outward (counter-clockwise) side
    InstanceRecord record;
    Material material;
    float uvPerWorldArea; // the triangle's uv area over its world area, for texture LOD
    vec3 dPdu, dPdv;      // world change per unit of u and of v across the triangle (0 for a degenerate uv map)
    vec3 emission;        // the object's own emission on top of its material's (a sphere's colour * emission)
    uint primitive;       // HitPayload.primitive: the triangle of the mesh, or the sphere's index in its buffer
    vec3 prevPosition;    // HitPayload.prevPosition: the hit point where it was in the previous frame (the
                          // previous camera is the guide's business: this is in world space)
};

#ifdef GEOMETRY_SPHERES

// The hit sphere: sphere InstanceRecord.mesh + gl_PrimitiveID of the static spheres, or of the particle field
// for a kInstanceDynamicSpheres instance. Its material is its own SphereRecord.material (every sphere of an
// instance has the instance's class: the hit group), baseColor (and the pattern's baseColor2) multiplied by
// its colour, plus colour * emission emitted. The normal is radial, the uv spherical (sphereUv), and the
// previous position the same point of the sphere around prevCenter (the instance's transform is Task 9's).
HitPoint hitPoint() {
    HitPoint h;
    h.record = instanceRecords[gl_InstanceCustomIndexEXT];
    uint index = h.record.mesh + uint(gl_PrimitiveID);
    SphereRecord s =
        (h.record.flags & kInstanceDynamicSpheres) != 0u ? particleSpheres[index] : staticSpheres[index];
    h.material = materials[s.material];
    h.material.baseColor *= s.color;
    h.material.baseColor2 *= s.color;
    h.emission = s.color * s.emission;
    h.primitive = index;

    vec3 dir = gl_WorldRayDirectionEXT;
    vec3 local = gl_ObjectRayOriginEXT + gl_HitTEXT * gl_ObjectRayDirectionEXT - s.center;
    vec3 nLocal = safeNormalize(local, -normalize(gl_ObjectRayDirectionEXT));
    vec3 surface = s.center + s.radius * nLocal;
    h.position = gl_ObjectToWorldEXT * vec4(surface, 1.0); // on the surface
    h.prevPosition = h.position + hitMotion(surface, s.prevCenter + s.radius * nLocal);
    h.front = gl_HitKindEXT == kSphereHitOutside;
    // Normals go to world space through the inverse transpose: n * WorldToObject.
    h.geometricNormal = safeNormalize((nLocal * gl_WorldToObjectEXT).xyz, h.front ? -dir : dir);
    h.normal = h.geometricNormal;
    h.uv = sphereUv(nLocal);

    vec3 du, dv;
    sphereTangents(nLocal, s.radius, du, dv);
    h.dPdu = gl_ObjectToWorldEXT * vec4(du, 0.0);
    h.dPdv = gl_ObjectToWorldEXT * vec4(dv, 0.0);
    float worldPerUv = length(cross(h.dPdu, h.dPdv));
    h.uvPerWorldArea = worldPerUv > 0.0 ? 1.0 / worldPerUv : 0.0;
    return h;
}

#else

HitPoint hitPoint() {
    HitPoint h;
    h.record = instanceRecords[gl_InstanceCustomIndexEXT];
    h.material = materials[h.record.material];
    MeshRecord mesh = meshRecords[h.record.mesh];
    uint first = mesh.firstIndex + 3u * uint(gl_PrimitiveID);
    Vertex v0 = vertices[mesh.firstVertex + indices[first]];
    Vertex v1 = vertices[mesh.firstVertex + indices[first + 1u]];
    Vertex v2 = vertices[mesh.firstVertex + indices[first + 2u]];
    vec3 b = vec3(1.0 - hitBarycentrics.x - hitBarycentrics.y, hitBarycentrics.x, hitBarycentrics.y);

    vec3 p = v0.position * b.x + v1.position * b.y + v2.position * b.z;
    h.position = gl_ObjectToWorldEXT * vec4(p, 1.0);
    h.uv = v0.uv * b.x + v1.uv * b.y + v2.uv * b.z;
    h.front = gl_HitKindEXT == gl_HitKindFrontFacingTriangleEXT;

    vec3 e1 = v1.position - v0.position;
    vec3 e2 = v2.position - v0.position;
    // Normals go to world space through the inverse transpose: n * WorldToObject.
    vec3 dir = gl_WorldRayDirectionEXT;
    vec3 ng = (cross(e1, e2) * gl_WorldToObjectEXT).xyz;
    // A zero-area triangle has no normal: face the ray.
    h.geometricNormal = safeNormalize(ng, h.front ? -dir : dir);
    vec3 n = ((v0.normal * b.x + v1.normal * b.y + v2.normal * b.z) * gl_WorldToObjectEXT).xyz;
    h.normal = safeNormalize(n, h.geometricNormal);
    // An interpolated normal that puts the viewer on the other side than the geometry does would let rays
    // scatter into the surface: use the geometric normal there.
    if ((dot(h.normal, dir) < 0.0) != (dot(h.geometricNormal, dir) < 0.0)) {
        h.normal = h.geometricNormal;
    }

    vec3 w1 = gl_ObjectToWorldEXT * vec4(e1, 0.0);
    vec3 w2 = gl_ObjectToWorldEXT * vec4(e2, 0.0);
    float worldArea = length(cross(w1, w2));
    vec2 d1 = v1.uv - v0.uv;
    vec2 d2 = v2.uv - v0.uv;
    float uvArea = abs(d1.x * d2.y - d1.y * d2.x);
    h.uvPerWorldArea = worldArea > 0.0 ? uvArea / worldArea : 0.0;
    float uvDet = d1.x * d2.y - d1.y * d2.x;
    h.dPdu = uvDet != 0.0 ? (d2.y * w1 - d1.y * w2) / uvDet : vec3(0.0);
    h.dPdv = uvDet != 0.0 ? (d1.x * w2 - d2.x * w1) / uvDet : vec3(0.0);
    h.emission = vec3(0.0);
    h.primitive = uint(gl_PrimitiveID);
    h.prevPosition = h.position + hitMotion(p, p); // the same point under the previous transform
    return h;
}

#endif

#ifndef EVO_HIT_POINT_ONLY
// The mip level of a texture of texSize texels for this hit, from the ray cone (Akenine-Moller et al. 2019);
// kDiffuseTextureLod once the path has scattered off a rough surface.
float coneTextureLod(HitPoint h, vec2 texSize) {
    if (payload.coneSpread < 0.0) {
        return kDiffuseTextureLod;
    }
    float width = abs(payload.coneWidth + payload.coneSpread * gl_HitTEXT);
    float texelsPerArea = h.uvPerWorldArea * texSize.x * texSize.y;
    if (!(texelsPerArea > 0.0) || !(width > 0.0)) {
        return 0.0;
    }
    float cosine = max(abs(dot(gl_WorldRayDirectionEXT, h.geometricNormal)), 1e-3);
    return max(0.5 * log2(texelsPerArea) + log2(width / cosine), 0.0);
}
#endif

// --- Procedural patterns (integer hashes only) -------------------------------------------------------------

float hash01(uint v) {
    return float(pcgHash(v) >> 8u) * (1.0 / 16777216.0);
}

// Smooth 1D value noise in [0, 1].
float valueNoise(float x, uint seed) {
    float i = floor(x);
    float f = x - i;
    uint k = uint(int(i)) * 0x9E3779B1u + seed;
    float a = hash01(k);
    float c = hash01(k + 0x9E3779B1u);
    return mix(a, c, f * f * (3.0 - 2.0 * f));
}

// Planks along world x, patternScale wide across z, each with its own shade and a wavy grain; thin dark
// seams between them.
vec3 woodPlanks(Material m, vec3 p) {
    float width = max(m.patternScale, 1e-3);
    float across = p.z / width;
    float plank = floor(across);
    uint id = uint(int(plank)) * 0x632BE5ABu;
    float shade = hash01(id);
    float along = p.x + hash01(id + 7u) * 13.0;
    float grain = valueNoise(along * 9.0 + 4.0 * valueNoise(across * 3.0, id + 3u), id + 11u);
    float rings = 0.5 + 0.5 * sin((across * 7.0 + 2.5 * valueNoise(along * 1.7, id + 5u)) * 6.2831853);
    float t = clamp(0.25 * shade + 0.45 * grain + 0.3 * rings, 0.0, 1.0);
    vec3 color = mix(m.baseColor, m.baseColor2, t);
    float seam = min(across - plank, plank + 1.0 - across) * width;
    return seam < 0.0015 ? m.baseColor2 * 0.45 : color;
}

// Brushed along world x: fine streaks of brightness and roughness across y and z.
vec3 brushedMetal(Material m, vec3 p, inout float roughness) {
    float scale = max(m.patternScale, 1e-3);
    float streak = valueNoise((p.y * 0.7 + p.z) * 900.0 / scale, 0x1234u);
    float coarse = valueNoise((p.y + p.z * 0.7) * 60.0 / scale, 0x4321u);
    roughness = clamp(roughness * (0.75 + 0.5 * streak), 0.0, 1.0);
    return m.baseColor * (0.85 + 0.1 * streak + 0.05 * coarse);
}

// A 3D checker of patternScale tiles, sampled half a tile inside the surface so that a face lying on a tile
// boundary (a floor at y = 0) does not flicker between two tiles.
vec3 checkerColor(Material m, vec3 p, vec3 ng) {
    float tile = max(m.patternScale, 1e-4);
    vec3 q = floor((p - ng * (0.5 * tile)) / tile);
    bool odd = (int(q.x + q.y + q.z) & 1) != 0;
    return odd ? m.baseColor2 : m.baseColor;
}

#ifndef EVO_HIT_POINT_ONLY
#include "caustics.glsl" // causticRadiance(HitPoint), the caustics' hook

// --- The payload -------------------------------------------------------------------------------------------

// Fills the payload with the hit and the surface (baseColor and roughness after any pattern; emission is
// the radiance towards the ray's origin: a light's surface emits on its outward side only; the object's own
// h.emission adds to the material's).
void writeSurface(HitPoint h, vec3 baseColor, float roughness, float transmission, vec3 emission) {
#ifdef GEOMETRY_SPHERES
    emission += h.emission; // (triangles have none: their bytes stay the original)
#endif
    payload.position = h.position;
    payload.t = gl_HitTEXT;
    payload.normal = h.normal;
    payload.light = h.record.light;
    payload.geometricNormal = h.geometricNormal;
    payload.frontFace = h.front ? 1u : 0u;
    payload.baseColor = baseColor;
    payload.roughness = roughness;
    payload.metallic = h.material.metallic;
    payload.ior = h.material.ior;
    payload.transmission = transmission;
    payload.coat = h.material.coat;
    payload.coatRoughness = h.material.coatRoughness;
    payload.materialClass = h.material.materialClass;
    payload.emission = (h.record.light == kNoLight || h.front) ? emission : vec3(0.0);
    // Caustics: a caustic receiver reflects its photon map (0 without caustics in the frame).
    if ((h.material.flags & kMaterialCausticReceiver) != 0u) {
        h.material.baseColor = baseColor; // after the pattern
        payload.emission += causticRadiance(h);
    }
    payload.materialFlags = h.material.flags;
    payload.abbe = h.material.abbe;
    payload.medium = h.material.medium;
    payload.primitive = h.primitive;
    payload.prevPosition = h.prevPosition;
    // Beer-Lambert inside glass: a dielectric's baseColor2 is its absorption per unit length.
    bool solidGlass = transmission > 0.5 && (h.material.flags & kMaterialThinWalled) == 0u;
    payload.absorption = solidGlass ? max(h.material.baseColor2, vec3(0.0)) : vec3(0.0);
}

#endif // EVO_HIT_POINT_ONLY

vec3 materialEmission(Material m) {
    return m.emission * m.emissionScale;
}

#endif
