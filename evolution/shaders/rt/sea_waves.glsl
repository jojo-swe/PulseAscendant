#ifndef EVO_RT_SEA_WAVES_GLSL
#define EVO_RT_SEA_WAVES_GLSL

// PARTICLE OCEAN's swell (the Particles scene): eight Gerstner waves, shared by the particle shader
// (shaders/modern/particles_particles.comp, which floats the beads on them) and the beads' surface
// (detail_patterns.glsl, which turns each bead's mirror towards the swell's normal). src/modern/scenes/
// particles.cpp mirrors the tables.
//
// Wave k: direction (unit), wave number = 2 pi / wavelength, angular speed sqrt(9.81 k), amplitude (times the
// frame's wave scale) and phase. A point of the calm sea at `base` (xz) moves by Q A cos(f) along the
// direction and by A sin(f) up, f = k (direction . base) - speed t + phase, Q = kSeaSteepness.

const vec2 kSeaDirection[8] = vec2[8](vec2(0.94, 0.34), vec2(0.77, 0.64), vec2(0.99, -0.14), vec2(0.6, -0.8),
                                      vec2(0.87, 0.5), vec2(0.34, 0.94), vec2(0.97, 0.24), vec2(0.71, -0.71));
const float kSeaNumber[8] = float[8](0.4488, 0.6614, 0.8976, 1.1855, 1.6111, 2.244, 2.992, 4.1888);
const float kSeaSpeed[8] = float[8](2.0983, 2.5472, 2.9674, 3.4102, 3.9755, 4.692, 5.4177, 6.4103);
const float kSeaAmplitude[8] = float[8](0.308, 0.209, 0.154, 0.1166, 0.0858, 0.0616, 0.0462, 0.033);
const float kSeaPhase[8] = float[8](0.0, 1.3, 2.9, 4.1, 0.7, 5.3, 2.2, 3.7);
const float kSeaSteepness = 0.5;

// The horizontal displacement of the calm point `base` at time t.
vec2 seaShift(vec2 base, float t, float scale) {
    vec2 d = vec2(0.0);
    for (int k = 0; k < 8; ++k) {
        float f = kSeaNumber[k] * dot(kSeaDirection[k], base) - kSeaSpeed[k] * t + kSeaPhase[k];
        d += kSeaSteepness * kSeaAmplitude[k] * scale * kSeaDirection[k] * cos(f);
    }
    return d;
}

// The swell's unit normal under a displaced point `p` (xz) at time t: the calm point found by two
// fixed-point steps (the shift is a few centimetres against wavelengths of metres), then the Gerstner
// surface's normal there.
vec3 seaNormal(vec2 p, float t, float scale) {
    vec2 base = p - seaShift(p, t, scale);
    base = p - seaShift(base, t, scale);
    vec3 n = vec3(0.0, 1.0, 0.0);
    for (int k = 0; k < 8; ++k) {
        float a = kSeaAmplitude[k] * scale, w = kSeaNumber[k];
        float f = w * dot(kSeaDirection[k], base) - kSeaSpeed[k] * t + kSeaPhase[k];
        n.xz -= kSeaDirection[k] * (w * a * cos(f));
        n.y -= kSeaSteepness * w * a * sin(f);
    }
    return normalize(n);
}

#endif
