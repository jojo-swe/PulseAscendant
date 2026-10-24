#version 460
#extension GL_GOOGLE_include_directive : require

// Hit group 3 (MaterialClass Checker): a procedural checker of patternScale tiles in world space, baseColor
// and baseColor2, over the material's BSDF. Never traces.

#include "hit.glsl"

void main() {
    HitPoint h = hitPoint();
    Material m = h.material;
    writeSurface(h, checkerColor(m, h.position, h.geometricNormal), m.roughness, m.transmission,
                 materialEmission(m));
}
