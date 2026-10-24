#version 460
#extension GL_GOOGLE_include_directive : require

// Hit group 2 (MaterialClass Emissive): an emitting surface over the material's BSDF. With texture
// kRetroScreenTexture the emission is the live 1993 picture as the CRT shows it (screen.glsl: decoded to
// linear light by its sRGB view, barrel-warped, scanlines) times emissionScale, at the mip level of the ray
// cone's footprint; else emission * emissionScale. Never traces.
//
// The camera's own ray on the screen (the primary hit: no cone width yet) takes the picture at four fixed
// points of its pixel (a rotated grid) instead of at the one jittered point, each through the same lens point
// and at the level of a quarter of the footprint: the 1-texel text then no longer changes with the jitter
// from frame to frame (it boiled in the window at 1 spp), and an export averages the same four taps, so the
// window still equals an export. Defocus (the lens point) still blurs it. Primary emission bypasses MIS and
// NEE (which reads the picture at kDiffuseTextureLod), so this changes neither estimator.

#include "hit.glsl"
#include "screen.glsl"

// The four taps in pixels from the pixel's centre (rotated-grid supersampling, x right, y down).
const vec2 kScreenTaps[4] = vec2[4](vec2(0.125, -0.375), vec2(0.375, 0.125), vec2(-0.125, 0.375),
                                    vec2(-0.375, -0.125));

// The point of this pixel at `offset` pixels from its centre, on the camera's focus plane (any plane for a
// pinhole): every lens point's ray to it passes through that point of the picture.
vec3 focusPoint(vec2 offset) {
    float focus = frame.aperture > 0.0 ? frame.focusDistance : 1.0;
    vec2 pixel = vec2(gl_LaunchIDEXT.xy) + 0.5 + offset;
    vec2 ndc = vec2(2.0 * pixel.x / float(frame.width) - 1.0, 1.0 - 2.0 * pixel.y / float(frame.height));
    return frame.cameraPosition +
           focus * (frame.cameraForward + ndc.x * frame.tanHalfFovY * frame.aspect * frame.cameraRight +
                    ndc.y * frame.tanHalfFovY * frame.cameraUp);
}

// The screen's picture over the primary ray's pixel: four taps where the rays from this ray's lens point
// through the pixel's tap points meet the screen's plane.
vec3 primaryScreenPicture(HitPoint h, float lod) {
    vec3 origin = gl_WorldRayOriginEXT;
    float uu = dot(h.dPdu, h.dPdu), uvCross = dot(h.dPdu, h.dPdv), vv = dot(h.dPdv, h.dPdv);
    float gram = uu * vv - uvCross * uvCross;
    if (!(gram > 0.0)) {
        return retroScreenPicture(h.uv, lod);
    }
    float tapLod = max(lod - 1.0, 0.0); // each tap covers half the footprint across
    vec3 sum = vec3(0.0);
    for (int i = 0; i < 4; ++i) {
        vec3 direction = focusPoint(kScreenTaps[i]) - origin;
        float facing = dot(direction, h.geometricNormal);
        vec2 uv = h.uv;
        if (abs(facing) > 1e-12) {
            vec3 q = origin + direction * (dot(h.position - origin, h.geometricNormal) / facing);
            vec3 dp = q - h.position;
            float a = dot(dp, h.dPdu), b = dot(dp, h.dPdv);
            uv += vec2(vv * a - uvCross * b, uu * b - uvCross * a) / gram;
        }
        sum += retroScreenPicture(uv, tapLod);
    }
    return 0.25 * sum;
}

void main() {
    HitPoint h = hitPoint();
    Material m = h.material;
    vec3 emission = materialEmission(m);
    if (m.texture == kRetroScreenTexture) {
        float lod = coneTextureLod(h, vec2(textureSize(retroScreen, 0)));
        bool primary = payload.coneSpread >= 0.0 && payload.coneWidth == 0.0;
        emission = (primary ? primaryScreenPicture(h, lod) : retroScreenPicture(h.uv, lod)) * m.emissionScale;
    }
    writeSurface(h, m.baseColor, m.roughness, m.transmission, emission);
}
