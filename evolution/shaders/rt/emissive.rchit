#version 460
#extension GL_GOOGLE_include_directive : require

// Hit group 2 (MaterialClass Emissive): an emitting surface over the material's BSDF. With texture
// kRetroScreenTexture the emission is the live 1993 picture as the CRT shows it (screen.glsl: decoded to
// linear light by its sRGB view, barrel-warped, scanlines) times emissionScale, at the mip level of the ray
// cone's footprint; else emission * emissionScale. Never traces.

#include "hit.glsl"
#include "screen.glsl"

void main() {
    HitPoint h = hitPoint();
    Material m = h.material;
    vec3 emission = materialEmission(m);
    if (m.texture == kRetroScreenTexture) {
        float lod = coneTextureLod(h, vec2(textureSize(retroScreen, 0)));
        emission = retroScreenPicture(h.uv, lod) * m.emissionScale;
    }
    writeSurface(h, m.baseColor, m.roughness, m.transmission, emission);
}
