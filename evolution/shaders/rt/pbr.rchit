#version 460
#extension GL_GOOGLE_include_directive : require

// Hit group 0 (MaterialClass Pbr): plastic, metal and clear-coated surfaces, with the WoodPlanks,
// BrushedMetal and Concrete patterns. Evaluates the material into the payload; never traces.

#include "hit.glsl"

// Smooth 3D value noise in [0, 1] (integer hashes of the lattice corners).
float valueNoise3(vec3 p, uint seed) {
    vec3 i = floor(p);
    vec3 f = p - i;
    vec3 s = f * f * (3.0 - 2.0 * f);
    uvec3 c = uvec3(ivec3(i));
    uint base = c.x * 0x8DA6B343u + c.y * 0xD8163841u + c.z * 0xCB1AB31Fu + seed;
    float n000 = hash01(base);
    float n100 = hash01(base + 0x8DA6B343u);
    float n010 = hash01(base + 0xD8163841u);
    float n110 = hash01(base + 0x8DA6B343u + 0xD8163841u);
    float n001 = hash01(base + 0xCB1AB31Fu);
    float n101 = hash01(base + 0x8DA6B343u + 0xCB1AB31Fu);
    float n011 = hash01(base + 0xD8163841u + 0xCB1AB31Fu);
    float n111 = hash01(base + 0x8DA6B343u + 0xD8163841u + 0xCB1AB31Fu);
    return mix(mix(mix(n000, n100, s.x), mix(n010, n110, s.x), s.y),
               mix(mix(n001, n101, s.x), mix(n011, n111, s.x), s.y), s.z);
}

// Polished concrete in world space (Task 14 of step 5): cloudy tones between baseColor and baseColor2 at
// three scales, a fine aggregate, roughness varying with the polish and, when patternScale > 0, saw-cut
// joints every patternScale in the surface's plane (the plane across ng's largest axis).
vec3 concrete(Material m, vec3 p, vec3 ng, inout float roughness) {
    float cloud = 0.55 * valueNoise3(p * 1.3, 0x1F2Eu) + 0.3 * valueNoise3(p * 5.1, 0x2E3Du) +
                  0.15 * valueNoise3(p * 19.0, 0x3D4Cu);
    float t = clamp((cloud - 0.5) * 2.2 + 0.5, 0.0, 1.0);
    vec3 color = mix(m.baseColor, m.baseColor2, t);
    color *= 0.93 + 0.14 * valueNoise3(p * 70.0, 0x4C5Bu); // the aggregate
    roughness = clamp(roughness * (0.7 + 0.6 * valueNoise3(p * 2.3, 0x5B6Au)), 0.0, 1.0);
    if (m.patternScale > 0.0) {
        vec3 a = abs(ng);
        vec2 plane = a.y >= a.x && a.y >= a.z ? p.xz : (a.x >= a.z ? p.zy : p.xy);
        vec2 q = plane / m.patternScale + 0.31;
        vec2 edge = min(fract(q), 1.0 - fract(q)) * m.patternScale;
        if (min(edge.x, edge.y) < 0.0025) {
            color *= 0.3;
            roughness = 1.0;
        }
    }
    return color;
}

void main() {
    HitPoint h = hitPoint();
    Material m = h.material;
    vec3 baseColor = m.baseColor;
    float roughness = m.roughness;
    if (m.pattern == kPatternWoodPlanks) {
        baseColor = woodPlanks(m, h.position);
    } else if (m.pattern == kPatternBrushedMetal) {
        baseColor = brushedMetal(m, h.position, roughness);
    } else if (m.pattern == kPatternConcrete) {
        baseColor = concrete(m, h.position, h.geometricNormal, roughness);
    }
    writeSurface(h, baseColor, roughness, m.transmission, materialEmission(m));
}
