#ifndef EVO_RT_CAUSTICS_GLSL
#define EVO_RT_CAUSTICS_GLSL

// Caustics from the photon map (caustic photon maps): a kMaterialCausticReceiver surface reflects the
// irradiance its receiver's map holds (binding EVO_BINDING_CAUSTIC_MAPS, layer = the receiver; filled by
// shaders/modern/photons.comp and caustic_resolve.comp) through its Lambert lobe. writeSurface() adds it to
// the payload's emission, so the primary hit's caustic goes to the emission image, past the denoiser, and a
// later hit's adds to the path's lighting. The raygen excludes the paths the map holds
// (caustic_paths.glsl). Included by hit.glsl (needs HitPoint and the closest-hit's ray built-ins).

#include "caustic_common.glsl"

// The radiance the caustic photon map reflects off a hit towards the ray's origin: albedo / pi * E with the
// Lambert lobe's Fresnel transmission towards the viewer (the map's E already holds the incoming side's) on
// a receiver of this frame's caustics (h.material.baseColor after the material's pattern); 0 elsewhere.
vec3 causticRadiance(HitPoint h) {
    Material m = h.material;
    if ((m.flags & kMaterialCausticReceiver) == 0u || m.receiver >= frame.causticReceivers) {
        return vec3(0.0);
    }
    CausticReceiver r = receiverTable[m.receiver];
    vec2 uv = receiverUv(r, h.position);
    if (!insideUnitSquare(uv)) {
        return vec3(0.0);
    }
    float size = float(textureSize(causticMaps, 0).x);
    vec3 e = textureLod(causticMaps, vec3(uv * (float(r.resolution) / size), float(r.layer)), 0.0).rgb;
    Surface s;
    s.baseColor = m.baseColor;
    s.roughness = m.roughness;
    s.metallic = m.metallic;
    s.ior = m.ior;
    s.transmission = m.transmission;
    s.coat = m.coat;
    s.coatRoughness = m.coatRoughness;
    s.emission = vec3(0.0);
    float cosO = abs(dot(h.normal, gl_WorldRayDirectionEXT));
    return lambertThrough(s, cosO) * (1.0 - clamp(m.metallic, 0.0, 1.0)) * clamp(m.baseColor, 0.0, 1.0) *
           (1.0 / kBsdfPi) * max(e, vec3(0.0));
}

#endif
