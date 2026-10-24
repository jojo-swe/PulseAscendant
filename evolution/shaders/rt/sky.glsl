#ifndef EVO_RT_SKY_GLSL
#define EVO_RT_SKY_GLSL

// The procedural night sky seen through the Studio's window (radiance in linear light, +y up): a dark blue
// gradient from the horizon to the zenith, hashed stars, a moon disc with a halo and the faint warm glow of
// a city along the horizon. A pure function of the direction, so the sky miss and NEE through a portal agree.

#include "random.glsl"

const vec3 kMoonDirection = vec3(-0.4537, 0.3226, -0.8308); // normalised
const float kMoonCos = 0.99975;                             // angular radius about 1.3 degrees
const vec3 kMoonRadiance = vec3(3.2, 3.1, 2.8);
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
    float brightness = 0.6 + 4.0 * float((h >> 16u) & 0xFFu) / 255.0;
    float warmth = float((h >> 24u) & 0xFFu) / 255.0;
    return brightness * mix(vec3(0.75, 0.85, 1.0), vec3(1.0, 0.9, 0.75), warmth);
}

vec3 skyRadiance(vec3 direction) {
    vec3 d = normalize(direction);
    float y = d.y;
    vec3 cityGlow = vec3(0.10, 0.055, 0.025) * exp(-abs(y) * 12.0);
    if (y < 0.0) {
        return vec3(0.004, 0.004, 0.005) + cityGlow; // the dark ground and the city below the horizon
    }
    vec3 zenith = vec3(0.003, 0.005, 0.016);
    vec3 horizon = vec3(0.016, 0.022, 0.045);
    vec3 sky = mix(horizon, zenith, sqrt(y)) + cityGlow;
    sky += skyStars(d) * smoothstep(0.03, 0.2, y);
    float c = dot(d, kMoonDirection);
    if (c > kMoonCos) {
        return kMoonRadiance; // the disc hides the stars and the glow behind it
    }
    float angle = acos(clamp(c, -1.0, 1.0));
    sky += vec3(0.05, 0.055, 0.07) * exp(-angle * 9.0) + vec3(0.25, 0.25, 0.27) * exp(-angle * 60.0);
    return sky;
}

#endif
