#version 460
#extension GL_GOOGLE_include_directive : require

// Hit group 1 (MaterialClass Glass): a dielectric of the material's IOR (smooth below roughness 0.02),
// tinted by baseColor. The payload's frontFace and outward normal tell bsdf.glsl whether the ray enters
// (wo on the outward side) or leaves. Never traces.

#include "hit.glsl"

void main() {
    HitPoint h = hitPoint();
    Material m = h.material;
    writeSurface(h, m.baseColor, m.roughness, 1.0, materialEmission(m));
}
