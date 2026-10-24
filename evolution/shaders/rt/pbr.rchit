#version 460
#extension GL_GOOGLE_include_directive : require

// Hit group 0 (MaterialClass Pbr): plastic, metal and clear-coated surfaces, with the WoodPlanks and
// BrushedMetal patterns. Evaluates the material into the payload; never traces.

#include "hit.glsl"

void main() {
    HitPoint h = hitPoint();
    Material m = h.material;
    vec3 baseColor = m.baseColor;
    float roughness = m.roughness;
    if (m.pattern == kPatternWoodPlanks) {
        baseColor = woodPlanks(m, h.position);
    } else if (m.pattern == kPatternBrushedMetal) {
        baseColor = brushedMetal(m, h.position, roughness);
    }
    writeSurface(h, baseColor, roughness, m.transmission, materialEmission(m));
}
