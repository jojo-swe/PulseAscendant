#ifndef EVO_RT_MEDIUM_GLSL
#define EVO_RT_MEDIUM_GLSL

// Participating media between surface hits: height fog and a baked density grid, as
// src/modern/medium.hpp describes. This file holds what the integrator is built from: the frame's media
// (MediumUniforms `mediumData`, binding EVO_BINDING_MEDIUM; FrameUniforms.mediumFlags), their own random
// numbers, the fog's closed forms, the grid's DDA over its majorant cells (EVO_BINDING_MAJORANT) and its density
// (EVO_BINDING_DENSITY, trilinear by hand; both read with texelFetch), residual ratio tracking, delta tracking and the
// Henyey-Greenstein phase function. The integrator itself (the primary segment, medium vertices, the
// multiple-scattering continuation) is medium_path.glsl, which pathtrace.rgen includes after its helpers.
// Without a medium (mediumFlags 0) mediumActive() is false and mediumTransmittance() is exactly 1, so the
// tracer's picture is the original picture byte for byte. Needs common.glsl and random.glsl.

const float kMediumInfinity = 1e30; // = pathtrace.rgen's kRayMax: a ray that leaves the scene
// Tentative collisions per tracking walk at most (a safety net: a walk this long is in a medium dense enough
// that its transmittance has long been cut to 0).
const uint kMediumMaxSteps = 4096u;
// A transmittance estimate below this ends a ratio-tracking walk with 0 (its bias is below this).
const float kMediumTransmittanceCutoff = 1e-6;
const float kMajorantCell = 8.0; // modern::kMajorantCell
const float kMediumPi = 3.14159265358979;

// Whether this frame has any medium, fog or a grid.
bool mediumActive() {
    return frame.mediumFlags != 0u;
}
bool fogActive() {
    return (frame.mediumFlags & kMediumFog) != 0u;
}
bool gridActive() {
    return (frame.mediumFlags & kMediumGrid) != 0u;
}

// --- Random numbers ----------------------------------------------------------------------------------------
// The media draw a variable count of numbers per path, so they have their own counter-based stream, seeded
// per sample by the raygen (mediumSeed): number n is pcgHash(seed ^ pcgHash(n)).

uint gMediumSeed = 0u;
uint gMediumCount = 0u;

void mediumSeed(uint seed) {
    gMediumSeed = seed;
    gMediumCount = 0u;
}

// Uniform in [0, 1).
float mediumRand() {
    uint h = pcgHash(gMediumSeed ^ pcgHash(gMediumCount));
    gMediumCount += 1u;
    return float(h >> 8u) * (1.0 / 16777216.0);
}

// An exponential distance of rate 1: -ln(1 - u), finite for u in [0, 1).
float mediumExponential() {
    return -log(1.0 - mediumRand());
}

// --- Henyey-Greenstein -------------------------------------------------------------------------------------

// The density over directions at cosTheta to the direction of travel (modern::henyeyGreenstein).
float hgPhase(float g, float cosTheta) {
    float denom = 1.0 + g * g - 2.0 * g * cosTheta;
    return (1.0 - g * g) / (4.0 * kMediumPi * denom * sqrt(max(denom, 1e-12)));
}

// A direction sampled with density hgPhase about the direction of travel d (modern::sampleHenyeyGreenstein).
vec3 hgSample(float g, vec3 d, vec2 u) {
    float cosTheta;
    if (abs(g) < 1e-3) {
        cosTheta = 1.0 - 2.0 * u.x;
    } else {
        float s = (1.0 - g * g) / (1.0 - g + 2.0 * g * u.x);
        cosTheta = (1.0 + g * g - s * s) / (2.0 * g);
    }
    cosTheta = clamp(cosTheta, -1.0, 1.0);
    float sinTheta = sqrt(max(0.0, 1.0 - cosTheta * cosTheta));
    float phi = 2.0 * kMediumPi * u.y;
    // An orthonormal basis about d (Duff et al. 2017).
    float sz = d.z >= 0.0 ? 1.0 : -1.0;
    float a = -1.0 / (sz + d.z);
    float b = d.x * d.y * a;
    vec3 t = vec3(1.0 + sz * d.x * d.x * a, sz * b, -sz * d.x);
    vec3 bt = vec3(b, sz + d.y * d.y * a, -d.y);
    return normalize(d * cosTheta + (t * cos(phi) + bt * sin(phi)) * sinTheta);
}

// --- Fog ---------------------------------------------------------------------------------------------------

// sigma_t of the fog at p (its exponent clamped so it stays finite far below the base height).
float fogSigma(vec3 p) {
    return mediumData.fogDensity *
           exp(clamp(-mediumData.fogFalloff * (p.y - mediumData.fogBaseHeight), -80.0, 80.0));
}

// (1 - exp(-x)) / x, accurate near 0 (infinite for very negative x).
float oneMinusExpOver(float x) {
    return abs(x) < 1e-3 ? 1.0 - x * (0.5 - x / 6.0) : (1.0 - exp(-x)) / x;
}

// The fog's optical depth along o + t d (d unit) from t0 to t1 (t1 >= kMediumInfinity: to infinity; the
// result is then kMediumInfinity where it diverges): the closed form of modern::fogOpticalDepth.
float fogDepth(vec3 o, vec3 d, float t0, float t1) {
    if (!fogActive() || !(t1 > t0) || !(mediumData.fogDensity > 0.0)) {
        return 0.0;
    }
    float start = fogSigma(o + t0 * d);
    float a = mediumData.fogFalloff * d.y;
    if (t1 >= kMediumInfinity) {
        return a > 0.0 ? start / a : kMediumInfinity;
    }
    float len = t1 - t0;
    float depth = start * len * oneMinusExpOver(a * len);
    return isFinite(depth) ? depth : kMediumInfinity;
}

// The distance past t0 along o + t d at which the fog's optical depth reaches tau, or kMediumInfinity when it
// never does (the ray rises out of thinning fog first): the inverse of fogDepth.
float fogInvert(vec3 o, vec3 d, float t0, float tau) {
    float start = fogSigma(o + t0 * d);
    if (!(start > 0.0)) {
        return kMediumInfinity;
    }
    float q = tau / start; // (1 - exp(-a s)) / a = q
    float a = mediumData.fogFalloff * d.y;
    float aq = a * q;
    if (abs(aq) < 1e-3) {
        return q * (1.0 + aq * (0.5 + aq / 3.0));
    }
    if (aq >= 1.0) {
        return kMediumInfinity;
    }
    return -log(1.0 - aq) / a;
}

// --- The grid ----------------------------------------------------------------------------------------------
// Positions in "cell space": the grid's box is [0, gridSize / kMajorantCell]^3, majorant cell c covers
// [c, c + 1)^3 and voxel v [v, v + 1) / kMajorantCell. A ray keeps its world parameter t (world units), so
// densities per world unit integrate over t directly.

struct GridRay {
    vec3 origin;    // cell space at t = 0
    vec3 direction; // cell space per world unit
};

GridRay gridRay(vec3 o, vec3 d) {
    Mat34 m = mediumData.worldToMedium;
    vec3 toCells = vec3(mediumData.gridSize) / kMajorantCell;
    GridRay r;
    r.origin = vec3(dot(m.rows[0], vec4(o, 1.0)), dot(m.rows[1], vec4(o, 1.0)), dot(m.rows[2], vec4(o, 1.0))) *
               toCells;
    r.direction = vec3(dot(m.rows[0].xyz, d), dot(m.rows[1].xyz, d), dot(m.rows[2].xyz, d)) * toCells;
    return r;
}

// The baked voxel (density, emission rgb) at cell-space point c: trilinear between the baked voxels' centres
// (clamped to them: the image may be larger than this frame's grid). With kTraceFlagFilteredGrid (every
// driver but lavapipe) one fetch through the bound trilinear sampler at the same point: the texture unit
// interpolates the same eight texels, with weights of 8 bits on most GPUs, so the result still lies between
// them and the cell's majorant still bounds it (the trackers clamp it there anyway). Otherwise by hand from
// eight texelFetches: lavapipe's filtered fetch from a 32^3 image was measured off, its texelFetch exact, and
// its pictures are the byte-exact reference.
vec4 gridVoxel(vec3 c) {
    EVO_COUNT(kCountGridLookups, 1);
    ivec3 last = ivec3(mediumData.gridSize) - 1;
    vec3 u = clamp(c * kMajorantCell - 0.5, vec3(0.0), vec3(last));
    if ((frame.flags & kTraceFlagFilteredGrid) != 0u) {
        return textureLod(mediumDensity, (u + 0.5) / vec3(textureSize(mediumDensity, 0)), 0.0);
    }
    ivec3 i0 = min(ivec3(u), last);
    ivec3 i1 = min(i0 + 1, last);
    vec3 f = u - vec3(i0);
    vec4 x00 = mix(texelFetch(mediumDensity, i0, 0), texelFetch(mediumDensity, ivec3(i1.x, i0.y, i0.z), 0), f.x);
    vec4 x10 = mix(texelFetch(mediumDensity, ivec3(i0.x, i1.y, i0.z), 0),
                   texelFetch(mediumDensity, ivec3(i1.x, i1.y, i0.z), 0), f.x);
    vec4 x01 = mix(texelFetch(mediumDensity, ivec3(i0.x, i0.y, i1.z), 0),
                   texelFetch(mediumDensity, ivec3(i1.x, i0.y, i1.z), 0), f.x);
    vec4 x11 = mix(texelFetch(mediumDensity, ivec3(i0.x, i1.y, i1.z), 0), texelFetch(mediumDensity, i1, 0), f.x);
    return mix(mix(x00, x10, f.y), mix(x01, x11, f.y), f.z);
}

// sigma_t of the grid at cell-space point c.
float gridSigma(vec3 c) {
    return mediumData.densityScale * max(gridVoxel(c).r, 0.0);
}

// A DDA over the majorant cells along a GridRay, from t to tEnd.
struct GridWalk {
    ivec3 cell;
    ivec3 stepDir;
    vec3 tNext;  // where the ray leaves the cell on each axis
    vec3 tDelta; // the length of one cell on each axis
    float t;
    float tEnd;
    uint left; // cells still allowed (a safety net)
};

// Clips [t0, t1] to the grid's box and starts a walk there; false when the ray misses the box.
bool gridWalkBegin(GridRay r, float t0, float t1, out GridWalk w) {
    vec3 hi = vec3(mediumData.gridSize) / kMajorantCell;
    float ta = t0;
    float tb = t1;
    for (int a = 0; a < 3; ++a) {
        float o = r.origin[a];
        float dir = r.direction[a];
        if (abs(dir) < 1e-20) {
            if (o < 0.0 || o > hi[a]) {
                ta = 1.0;
                tb = 0.0;
            }
        } else {
            float lo = (0.0 - o) / dir;
            float up = (hi[a] - o) / dir;
            ta = max(ta, min(lo, up));
            tb = min(tb, max(lo, up));
        }
    }
    w.t = ta;
    w.tEnd = tb;
    w.left = mediumData.majorantSize.x + mediumData.majorantSize.y + mediumData.majorantSize.z + 3u;
    w.cell = ivec3(0);
    w.stepDir = ivec3(0);
    w.tNext = vec3(kMediumInfinity);
    w.tDelta = vec3(kMediumInfinity);
    if (!(tb > ta)) {
        return false;
    }
    vec3 p = r.origin + r.direction * ta;
    ivec3 last = ivec3(mediumData.majorantSize) - 1;
    for (int a = 0; a < 3; ++a) {
        float dir = r.direction[a];
        int cell = int(floor(p[a]));
        if (dir < 0.0 && float(cell) == p[a]) {
            cell -= 1; // on a boundary, moving down: the cell below
        }
        cell = clamp(cell, 0, last[a]);
        w.cell[a] = cell;
        if (abs(dir) >= 1e-20) {
            w.stepDir[a] = dir > 0.0 ? 1 : -1;
            w.tDelta[a] = abs(1.0 / dir);
            float boundary = dir > 0.0 ? float(cell + 1) : float(cell);
            w.tNext[a] = ta + (boundary - p[a]) / dir;
        }
    }
    return true;
}

// The walk's next cell: its segment [s0, s1] and majorant (sigma_t units); false when the walk is done.
bool gridWalkNext(inout GridWalk w, out float s0, out float s1, out float majorant) {
    s0 = w.t;
    s1 = w.t;
    majorant = 0.0;
    if (!(w.t < w.tEnd) || w.left == 0u) {
        return false;
    }
    w.left -= 1u;
    EVO_COUNT(kCountGridCells, 1);
    float exitT = min(min(w.tNext.x, w.tNext.y), w.tNext.z);
    s1 = min(max(exitT, w.t), w.tEnd);
    majorant = mediumData.densityScale * texelFetch(mediumMajorant, w.cell, 0).r;
    int axis = exitT == w.tNext.x ? 0 : (exitT == w.tNext.y ? 1 : 2);
    w.cell[axis] += w.stepDir[axis];
    w.tNext[axis] += w.tDelta[axis];
    w.t = s1;
    ivec3 last = ivec3(mediumData.majorantSize) - 1;
    if (any(lessThan(w.cell, ivec3(0))) || any(greaterThan(w.cell, last))) {
        w.t = w.tEnd; // left the box
    }
    return true;
}

// How many sub-steps residual ratio tracking splits a cell's segment [s0, s1] into: about one per voxel the
// ray crosses (at most 32).
uint gridSubsteps(GridRay r, float s0, float s1) {
    return uint(clamp(ceil((s1 - s0) * length(r.direction) * kMajorantCell), 1.0, 32.0));
}

// The grid's transmittance along o + t d over [t0, t1] by residual ratio tracking: each cell's segment is cut
// into sub-steps of about a voxel; in each, the line through the densities at its ends is a control whose
// transmittance is exact (exp(-length * mean)), and the residual sigma - control is ratio-tracked at the
// cell's majorant rate, which bounds |sigma - control| (both lie in [0, majorant]: every factor stays in
// [0, 2]). Unbiased; exact where the density is linear along the ray, and nearly deterministic where it is
// smooth over a voxel.
float gridTransmittance(vec3 o, vec3 d, float t0, float t1) {
    if (!gridActive()) {
        return 1.0;
    }
    GridRay r = gridRay(o, d);
    GridWalk w;
    if (!gridWalkBegin(r, t0, t1, w)) {
        return 1.0;
    }
    EVO_COUNT(kCountGridWalks, 1);
    float transmittance = 1.0;
    uint steps = 0u;
    float s0;
    float s1;
    float majorant;
    while (gridWalkNext(w, s0, s1, majorant)) {
        if (!(majorant > 0.0)) {
            continue;
        }
        uint n = gridSubsteps(r, s0, s1);
        float h = (s1 - s0) / float(n);
        float sigmaA = min(gridSigma(r.origin + r.direction * s0), majorant);
        for (uint i = 0u; i < n; ++i) {
            float a = s0 + float(i) * h;
            float b = i + 1u == n ? s1 : a + h;
            float sigmaB = min(gridSigma(r.origin + r.direction * b), majorant);
            float slope = (sigmaB - sigmaA) / max(b - a, 1e-30);
            transmittance *= exp(-0.5 * (sigmaA + sigmaB) * (b - a));
            float t = a;
            while (transmittance >= kMediumTransmittanceCutoff && steps < kMediumMaxSteps) {
                t += mediumExponential() / majorant;
                if (!(t < b)) {
                    break;
                }
                steps += 1u;
                EVO_COUNT(kCountTrackingSteps, 1);
                float control = sigmaA + slope * (t - a);
                transmittance *= clamp(1.0 - (gridSigma(r.origin + r.direction * t) - control) / majorant, 0.0, 2.0);
            }
            if (transmittance < kMediumTransmittanceCutoff) {
                return 0.0;
            }
            sigmaA = sigmaB;
        }
    }
    return transmittance;
}

// Delta tracking through the grid over [t0, t1]: true at the first real collision (tHit). At every tentative
// collision the emission's collision estimator adds sigma_a Le / majorant to `emitted` (the caller multiplies
// it by the path's throughput).
bool gridCollision(vec3 o, vec3 d, float t0, float t1, out float tHit, inout vec3 emitted) {
    tHit = t1;
    GridRay r = gridRay(o, d);
    GridWalk w;
    if (!gridWalkBegin(r, t0, t1, w)) {
        return false;
    }
    vec3 absorbing = 1.0 - mediumData.gridAlbedo;
    bool emits = mediumData.emissionScale > 0.0 && maxComponent(absorbing) > 0.0;
    uint steps = 0u;
    float s0;
    float s1;
    float majorant;
    while (gridWalkNext(w, s0, s1, majorant)) {
        if (!(majorant > 0.0)) {
            continue;
        }
        float t = s0;
        for (;;) {
            t += mediumExponential() / majorant;
            if (!(t < s1)) {
                break;
            }
            EVO_COUNT(kCountTrackingSteps, 1);
            vec4 voxel = gridVoxel(r.origin + r.direction * t);
            float sigma = mediumData.densityScale * max(voxel.r, 0.0);
            if (emits) {
                emitted += (sigma / majorant) * absorbing * (mediumData.emissionScale * max(voxel.gba, vec3(0.0)));
            }
            steps += 1u;
            if (mediumRand() * majorant < sigma || steps >= kMediumMaxSteps) {
                tHit = t;
                return true;
            }
        }
    }
    return false;
}

// The media's transmittance along a shadow ray from origin along direction (unit) up to tMax: the fog's
// closed form times the grid's residual ratio tracking. Every next-event estimate multiplies its
// contribution by it once its shadow ray is unoccluded. Exactly 1 without a medium.
float mediumTransmittance(vec3 origin, vec3 direction, float tMax) {
    if (!mediumActive()) {
        return 1.0;
    }
    float t = exp(-fogDepth(origin, direction, 0.0, tMax));
    if (t > 0.0 && gridActive()) {
        t *= gridTransmittance(origin, direction, 0.0, tMax);
    }
    return t;
}

// Delta tracking through all the media along o + t d over [0, tMax] (a later segment of a path): true at a
// real collision at tHit, in the fog (which = 0, its free-flight distance sampled by inverting its closed
// form) or in the grid (which = 1); `emitted` gets the grid emission's collision estimator.
bool mediumCollision(vec3 o, vec3 d, float tMax, out float tHit, out uint which, out vec3 emitted) {
    emitted = vec3(0.0);
    tHit = tMax;
    which = 0u;
    float tFog = kMediumInfinity;
    if (fogActive()) {
        tFog = fogInvert(o, d, 0.0, mediumExponential());
    }
    float limit = min(tMax, tFog);
    if (gridActive()) {
        float tGrid;
        if (gridCollision(o, d, 0.0, limit, tGrid, emitted)) {
            tHit = tGrid;
            which = 1u;
            return true;
        }
    }
    if (tFog < tMax) {
        tHit = tFog;
        return true;
    }
    return false;
}

#endif
