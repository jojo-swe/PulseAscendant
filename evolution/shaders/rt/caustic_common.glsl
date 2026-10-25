#ifndef EVO_RT_CAUSTIC_COMMON_GLSL
#define EVO_RT_CAUSTIC_COMMON_GLSL

// What the caustic photon maps' three sides share (step 5, Task 13): the photon pass
// (shaders/modern/photons.comp), the receivers' radiance in the closest-hits (caustics.glsl) and the
// raygen's double-count exclusion (caustic_paths.glsl). Plain GLSL over types.glsl and bsdf.glsl: no
// bindings and no ray tracing built-ins.
//
// The receiver table (binding EVO_BINDING_CAUSTIC_RECEIVERS, `receiverTable`; modern::CausticMaps writes
// it) holds the frame's receivers (FrameUniforms.causticReceivers of them; receiver i is layer i of the
// maps), then one source record:
//   worldToUv.rows[0]  the casters' bounding sphere (centre, radius) the photons were aimed at;
//   layer              the RectLight the photons left, or kNoLight for the environment's disc;
//   resolution         0.

#include "bsdf.glsl"

const uint kMaxPhotonDeltaEvents = 10u; // modern::kMaxPhotonDeltaEvents

// A world point's uv on a receiver (rows 0 and 1 of its worldToUv).
vec2 receiverUv(CausticReceiver r, vec3 p) {
    return vec2(dot(r.worldToUv.rows[0], vec4(p, 1.0)), dot(r.worldToUv.rows[1], vec4(p, 1.0)));
}

bool insideUnitSquare(vec2 uv) {
    return uv.x >= 0.0 && uv.y >= 0.0 && uv.x < 1.0 && uv.y < 1.0;
}

// A surface every photon event and every link of an excluded chain must be: delta in all its lobes (smooth
// glass, a smooth metal with no rough coat), judged on the material's own roughness.
bool causticDeltaSurface(Surface s) {
    return !bsdfHasNonDeltaLobe(s);
}

// The share of light at cosTheta to the shading normal that passes the coat and the base specular into the
// Lambert lobe (bsdf.glsl's opaque model).
vec3 lambertThrough(Surface s, float cosTheta) {
    OpaqueLobes l = opaqueLobes(s, cosTheta);
    return (1.0 - coatFresnel(l.coat, cosTheta)) * (1.0 - fresnelSchlick(l.f0, l.f90, cosTheta));
}

// The Lambert lobe's own f cos(wi) (cosines to the shading normal, both > 0): what the photon map stands
// for at a receiver, exactly that term of bsdfEval.
vec3 lambertLobe(Surface s, float cosO, float cosI) {
    return lambertThrough(s, cosO) * lambertThrough(s, cosI) * (1.0 - clamp(s.metallic, 0.0, 1.0)) *
           clamp(s.baseColor, 0.0, 1.0) * (cosI / kBsdfPi);
}

// Whether a light path's last segment, from a point y on the source rect light to the vertex x, lies in the
// photons' beam: the ray from y through x meets the casters' sphere (photons leave each point of the light
// uniformly in the cone it sees the sphere in; a point inside the sphere sends them over its hemisphere).
bool inRectBeam(vec4 sphere, vec3 y, vec3 x) {
    vec3 toCentre = sphere.xyz - y;
    float d2 = dot(toCentre, toCentre);
    float r2 = sphere.w * sphere.w;
    if (d2 <= r2) {
        return true;
    }
    vec3 v = x - y;
    float len2 = dot(v, v);
    if (!(len2 > 0.0)) {
        return false;
    }
    v *= inversesqrt(len2);
    float along = dot(toCentre, v);
    return along > 0.0 && d2 - along * along <= r2;
}

// The same for the environment's disc: the line through x towards the sun (d) crosses the plane through the
// sphere's centre across the disc's axis within the sphere's radius (photons start on that disc of the
// plane, moved back along their direction).
bool inDiscBeam(vec4 sphere, vec3 axis, vec3 x, vec3 d) {
    float cosine = dot(d, axis);
    if (!(cosine > 0.0)) {
        return false;
    }
    vec3 q = x + d * (dot(sphere.xyz - x, axis) / cosine);
    vec3 off = q - sphere.xyz;
    return dot(off, off) <= sphere.w * sphere.w;
}

#endif
