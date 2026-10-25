// Emissive splats (step 5, Task 10): everything of a splat shader but its points. A scene's splat shader
// includes this file and defines
//
//     SplatPoint splatPoint(uint id, float seconds, vec4 params[4]);
//
// a pure function of (id, seconds, params). main() projects each point through the frame's pinhole camera,
// tests it against the depth guide, spreads it over a renormalised 5x5 Gaussian and adds it into the
// fixed-point accumulators with integer atomics (src/modern/splats.hpp has the rules and the range).

#ifndef EVO_SPLAT_COMMON_GLSL
#define EVO_SPLAT_COMMON_GLSL

#include "../common/finite.glsl"

// A point: world position, flux (radiance summed over its pixels at a 1920x1080 reference) and world radius.
struct SplatPoint {
    vec3 position;
    vec3 flux;
    float radius;
};

SplatPoint splatPoint(uint id, float seconds, vec4 params[4]);

#ifdef SPLAT_SPIKES
// Diffraction spikes (optional: a shader defines SPLAT_SPIKES before including this file, and this function):
// x the share of the point's flux that goes into four spikes along the picture's axes instead of its core, y
// their length in pixels at the 1080-row reference (scaled to the trace's height). Each spike is a row of
// bilinear taps whose weights fall off exponentially from the core; the taps are lens glare, so they are not
// depth tested (the core is), and their weights are renormalised so the spikes carry exactly that share.
vec2 splatSpikes(uint id, SplatPoint s, vec4 params[4]);
#endif

// One id per invocation, rows of kSplatRowWidth ids (src/modern/splats.hpp).
layout(local_size_x = 256, local_size_y = 1) in;
const uint kSplatRowWidth = 65536u;

layout(set = 0, binding = 0, r32ui) uniform uimage2D accumulatorRed;
layout(set = 0, binding = 1, r32ui) uniform uimage2D accumulatorGreen;
layout(set = 0, binding = 2, r32ui) uniform uimage2D accumulatorBlue;
layout(set = 0, binding = 3, r32ui) uniform uimage2D overflowBits; // bit c: channel c wrapped
layout(set = 0, binding = 4, r32f) uniform readonly image2D depthImage;

// Must match SplatPush in src/modern/splats.cpp (128 bytes). The camera's up is right x forward, as
// modern::cameraBasis makes it.
layout(push_constant) uniform SplatPushConstants {
    vec3 cameraPosition;
    float tanHalfFovY;
    vec3 cameraForward;
    float aspect; // width / height of the trace
    vec3 cameraRight;
    float seconds;
    uint count;
    uint width;
    uint height;
    float fixedPointScale;
    vec4 params[4];
} pc;

// The largest float below 2^32: an add of more units than this is an overflow on its own.
const float kSplatMaxUnits = 4294967040.0;

// Adds `value` (reference radiance: radiance at the 1920x1080 reference, before the resolve scales it to the
// trace size) into one channel of a pixel in fixed point; flags the channel on a wrap.
void splatAccumulate(uint channel, ivec2 pixel, float value) {
    float units = value * pc.fixedPointScale;
    if (!(units >= 0.5)) { // rounds to nothing (and NaN)
        return;
    }
    uint bit = 1u << channel;
    if (units > kSplatMaxUnits) {
        imageAtomicOr(overflowBits, pixel, bit);
        return;
    }
    uint add = uint(units + 0.5);
    uint before;
    if (channel == 0u) {
        before = imageAtomicAdd(accumulatorRed, pixel, add);
    } else if (channel == 1u) {
        before = imageAtomicAdd(accumulatorGreen, pixel, add);
    } else {
        before = imageAtomicAdd(accumulatorBlue, pixel, add);
    }
    if (before + add < before) { // wrapped past 2^32
        imageAtomicOr(overflowBits, pixel, bit);
    }
}

#ifdef SPLAT_SPIKES
// Adds `value` bilinearly at a sub-pixel position (pixel centres at +0.5).
void splatBilinear(vec2 at, vec3 value, ivec2 last) {
    vec2 q = at - 0.5;
    ivec2 base = ivec2(floor(q));
    vec2 f = q - vec2(base);
    for (int j = 0; j < 2; ++j) {
        for (int i = 0; i < 2; ++i) {
            ivec2 pixel = base + ivec2(i, j);
            if (any(lessThan(pixel, ivec2(0))) || any(greaterThan(pixel, last))) {
                continue;
            }
            float w = (i == 0 ? 1.0 - f.x : f.x) * (j == 0 ? 1.0 - f.y : f.y);
            vec3 v = value * w;
            splatAccumulate(0u, pixel, v.r);
            splatAccumulate(1u, pixel, v.g);
            splatAccumulate(2u, pixel, v.b);
        }
    }
}

const int kSplatSpikeTaps = 48; // per spike, at most

// The four spikes of `flux` (already the spikes' share) around `position`, `lengthPx` long.
void splatSpikeTaps(vec2 position, vec3 flux, float lengthPx, ivec2 last) {
    int taps = clamp(int(ceil(lengthPx)), 1, kSplatSpikeTaps);
    float step = max(lengthPx / float(taps), 1.0);
    float total = 0.0;
    for (int k = 1; k <= taps; ++k) {
        total += exp(-3.5 * float(k) / float(taps));
    }
    vec3 perTap = flux / (4.0 * total);
    for (int k = 1; k <= taps; ++k) {
        vec3 value = perTap * exp(-3.5 * float(k) / float(taps));
        float d = float(k) * step;
        splatBilinear(position + vec2(d, 0.0), value, last);
        splatBilinear(position - vec2(d, 0.0), value, last);
        splatBilinear(position + vec2(0.0, d), value, last);
        splatBilinear(position - vec2(0.0, d), value, last);
    }
}
#endif

// Projects, depth-tests and spreads one point.
#ifdef SPLAT_SPIKES
void splat(SplatPoint s, vec2 spikes) {
#else
void splat(SplatPoint s) {
#endif
    if (!allFinite(s.position) || !allFinite(s.flux) || !isFinite(s.radius)) {
        return;
    }
    vec3 flux = max(s.flux, vec3(0.0));
    if (!(max(flux.r, max(flux.g, flux.b)) > 0.0)) {
        return;
    }
    vec3 cameraUp = cross(pc.cameraRight, pc.cameraForward);
    vec3 v = s.position - pc.cameraPosition;
    float z = dot(v, pc.cameraForward);
    if (!(z > 1e-6)) { // behind the camera
        return;
    }
    float ndcX = dot(v, pc.cameraRight) / (z * pc.tanHalfFovY * pc.aspect);
    float ndcY = dot(v, cameraUp) / (z * pc.tanHalfFovY);
    vec2 size = vec2(float(pc.width), float(pc.height));
    vec2 position = vec2((ndcX + 1.0) * 0.5 * size.x, (1.0 - ndcY) * 0.5 * size.y);
    // Off the picture by more than the footprint (and far off, where ivec2 would not hold it).
#ifdef SPLAT_SPIKES
    float spikeShare = clamp(spikes.x, 0.0, 1.0);
    float spikeLength = max(spikes.y, 0.0) * size.y / 1080.0;
    float margin = 3.0 + (spikeShare > 0.0 ? spikeLength : 0.0);
#else
    float margin = 3.0;
#endif
    if (!(position.x > -margin && position.y > -margin && position.x < size.x + margin &&
          position.y < size.y + margin)) {
        return;
    }
    ivec2 centre = ivec2(floor(position));
    ivec2 last = ivec2(int(pc.width) - 1, int(pc.height) - 1);
    float guide = imageLoad(depthImage, clamp(centre, ivec2(0), last)).r;
    if (guide >= 0.0 && z > guide) { // behind the traced surface; -1 (a miss) is infinitely far
        return;
    }

#ifdef SPLAT_SPIKES
    if (spikeShare > 0.0) {
        splatSpikeTaps(position, flux * spikeShare, spikeLength, last);
        flux *= 1.0 - spikeShare;
    }
#endif
    // sigma = max(projected radius, 0.5 px); the 5x5 taps around the centre pixel, renormalised to sum 1.
    float projected = max(s.radius, 0.0) / (z * pc.tanHalfFovY) * 0.5 * size.y;
    float sigma = max(projected, 0.5);
    float falloff = -0.5 / (sigma * sigma);
    float weights[25];
    float total = 0.0;
    for (int j = 0; j < 5; ++j) {
        for (int i = 0; i < 5; ++i) {
            vec2 d = vec2(centre + ivec2(i - 2, j - 2)) + 0.5 - position;
            float w = exp(falloff * dot(d, d));
            weights[j * 5 + i] = w;
            total += w;
        }
    }
    // In reference units: the resolve scales by (W * H) / (1920 * 1080), so the point's energy is the same at
    // every resolution, and so is the fixed-point step an add rounds to.
    vec3 scaled = flux / total;
    for (int j = 0; j < 5; ++j) {
        for (int i = 0; i < 5; ++i) {
            ivec2 pixel = centre + ivec2(i - 2, j - 2);
            if (any(lessThan(pixel, ivec2(0))) || any(greaterThan(pixel, last))) {
                continue;
            }
            vec3 value = scaled * weights[j * 5 + i];
            splatAccumulate(0u, pixel, value.r);
            splatAccumulate(1u, pixel, value.g);
            splatAccumulate(2u, pixel, value.b);
        }
    }
}

void main() {
    uint id = gl_GlobalInvocationID.y * kSplatRowWidth + gl_GlobalInvocationID.x;
    if (gl_GlobalInvocationID.x >= kSplatRowWidth || id >= pc.count) {
        return;
    }
    vec4 params[4] = pc.params;
#ifdef SPLAT_SPIKES
    SplatPoint s = splatPoint(id, pc.seconds, params);
    splat(s, splatSpikes(id, s, params));
#else
    splat(splatPoint(id, pc.seconds, params));
#endif
}

#endif
