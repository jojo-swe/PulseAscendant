#ifndef EVO_RT_SKY_GLSL
#define EVO_RT_SKY_GLSL

// The procedural night sky seen through the Studio's window (radiance in linear light, +y up): a dark blue
// gradient from the horizon to the zenith, hashed stars, the moon's halo, the faint warm glow of a town along
// the horizon and its lights below it. A pure function of the direction, so the sky miss and NEE through a portal agree.

#include "random.glsl"

// kMoonDirection, towards the moon (normalised), shared with the C++ side. The moon itself is not part of the
// sky: the Studio places it as a small emitter outside its window along the same direction, an area light
// that NEE finds (a small bright disc in the sky could only be reached through the window portal's samples:
// fireflies). The sky keeps its glow.
#include "../../src/rt/sky_constants.hpp"
const uint kStarCells = 384u; // per cube face side

// Stars: the direction's cell on a cube around the viewer; a cell holds a star with a small probability.
vec3 skyStars(vec3 d) {
    vec3 a = abs(d);
    uint face;
    vec2 uv;
    if (a.x >= a.y && a.x >= a.z) {
        face = d.x > 0.0 ? 0u : 1u;
        uv = d.yz / a.x;
    } else if (a.y >= a.z) {
        face = d.y > 0.0 ? 2u : 3u;
        uv = d.xz / a.y;
    } else {
        face = d.z > 0.0 ? 4u : 5u;
        uv = d.xy / a.z;
    }
    uvec2 cell = uvec2(clamp((uv * 0.5 + 0.5) * float(kStarCells), vec2(0.0), vec2(float(kStarCells - 1u))));
    uint h = pcgHash(cell.x + kStarCells * (cell.y + kStarCells * face) + 0x51A7u);
    if ((h & 0xFFFFu) >= 180u) { // about 0.27 % of the cells
        return vec3(0.0);
    }
    float brightness = 0.12 + 0.8 * float((h >> 16u) & 0xFFu) / 255.0;
    float warmth = float((h >> 24u) & 0xFFu) / 255.0;
    return brightness * mix(vec3(0.75, 0.85, 1.0), vec3(1.0, 0.9, 0.75), warmth);
}

// The distant town: lit windows and street lights in hashed cells of azimuth and depression in a thin band
// under the horizon (down to kTownDepth), densest at the horizon, warm with a few cold ones.
const float kTownDepth = 0.06;

vec3 cityLights(vec3 d) {
    if (!(d.y > -kTownDepth)) {
        return vec3(0.0);
    }
    float azimuth = atan(d.x, d.z);
    vec2 cell = floor(vec2(azimuth * 260.0, d.y * 700.0));
    uint h = pcgHash(uint(int(cell.x) + 4096) * 0x9E3779B1u + uint(int(-cell.y)) * 0x85EBCA6Bu + 0xC17Eu);
    float density = 0.05 * (1.0 + d.y / kTownDepth);
    if (float(h & 0xFFFFu) >= density * 65536.0) {
        return vec3(0.0);
    }
    float brightness = 0.08 + 0.3 * float((h >> 16u) & 0xFFu) / 255.0;
    bool cold = ((h >> 24u) & 7u) == 0u;
    return brightness * (cold ? vec3(0.7, 0.8, 1.0) : vec3(1.0, 0.62, 0.3));
}

vec3 skyRadiance(vec3 direction) {
    vec3 d = normalize(direction);
    float y = d.y;
    vec3 cityGlow = vec3(0.045, 0.028, 0.016) * exp(-abs(y) * 18.0);
    if (y < 0.0) {
        return vec3(0.0015, 0.0018, 0.003) + 0.5 * cityGlow + cityLights(d); // the dark town
    }
    vec3 zenith = vec3(0.006, 0.011, 0.034);
    vec3 horizon = vec3(0.03, 0.045, 0.1);
    vec3 sky = mix(horizon, zenith, sqrt(y)) + cityGlow;
    sky += skyStars(d) * smoothstep(0.03, 0.2, y);
    float c = dot(d, kMoonDirection);
    float angle = acos(clamp(c, -1.0, 1.0));
    sky += vec3(0.03, 0.04, 0.06) * exp(-angle * 4.0) + vec3(0.12, 0.13, 0.15) * exp(-angle * 30.0);
    return sky;
}

#endif
