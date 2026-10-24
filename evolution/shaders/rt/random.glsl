#ifndef EVO_RT_RANDOM_GLSL
#define EVO_RT_RANDOM_GLSL

// Every random number of the path tracer is an integer hash of (frameSeed, pixel, sample index, dimension):
// no state carried between invocations, no atomics, so a frame is a pure function of its seed (Global
// Constraints, determinism).

// PCG-RXS-M-XS 32-bit hash (Jarzynski and Olano, "Hash Functions for GPU Rendering", 2020).
uint pcgHash(uint v) {
    uint state = v * 747796405u + 2891336453u;
    uint word = ((state >> ((state >> 28u) + 4u)) ^ state) * 277803737u;
    return (word >> 22u) ^ word;
}

uint randomHash(uint frameSeed, uvec2 pixel, uint sampleIndex, uint dimension) {
    uint h = pcgHash(dimension);
    h = pcgHash(sampleIndex ^ h);
    h = pcgHash(pixel.y ^ h);
    h = pcgHash(pixel.x ^ h);
    return pcgHash(frameSeed ^ h);
}

// Uniform in [0, 1): the hash's top 24 bits, exactly representable as a float.
float randomFloat(uint frameSeed, uvec2 pixel, uint sampleIndex, uint dimension) {
    return float(randomHash(frameSeed, pixel, sampleIndex, dimension) >> 8u) * (1.0 / 16777216.0);
}

// Shirley-Chiu concentric map of the unit square onto the unit disc (area preserving).
vec2 sampleConcentricDisk(vec2 u) {
    vec2 p = 2.0 * u - 1.0;
    if (p.x == 0.0 && p.y == 0.0) {
        return vec2(0.0);
    }
    const float quarterPi = 0.78539816339744831;
    float r;
    float phi;
    if (abs(p.x) > abs(p.y)) {
        r = p.x;
        phi = quarterPi * (p.y / p.x);
    } else {
        r = p.y;
        phi = 2.0 * quarterPi - quarterPi * (p.x / p.y);
    }
    return r * vec2(cos(phi), sin(phi));
}

// Cosine-weighted direction about +z (pdf = z / pi).
vec3 sampleCosineHemisphere(vec2 u) {
    vec2 d = sampleConcentricDisk(u);
    return vec3(d, sqrt(max(0.0, 1.0 - dot(d, d))));
}

#endif
