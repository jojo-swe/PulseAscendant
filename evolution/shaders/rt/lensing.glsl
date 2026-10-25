#ifndef EVO_RT_LENSING_GLSL
#define EVO_RT_LENSING_GLSL

// Gravitational lensing (step 5, Task 25): main rays bent piecewise around a Schwarzschild black hole, from
// LensingUniforms `lensingData` (binding EVO_BINDING_LENSING; src/modern/lensing.hpp documents the model).
// pathtrace.rgen sends every main ray through traceLensed(), which is a plain trace while !lensingActive().
// Included by pathtrace.rgen after its payload, rand(dimension) and kMissIndexSky, and after medium.glsl,
// whose grid lookups (gridRay, gridVoxel) march the accretion disc when the lens owns the grid medium.
//
// One traceRayEXT call site, in a loop over segments: the straight way into the sphere of influence, the RK4
// steps of the geodesic inside it (each a straight segment), and the straight way out to infinity.

const float kLensRayMax = 1e30; // = pathtrace.rgen's kRayMax
const uint kLensDiscSamples = 6u;
const uint kLensDimension = 0x6C656E73u; // the disc march's jitter, far from the path's dimensions

// Whether this frame bends rays.
bool lensingActive() {
    return frame.lensing != 0u;
}

// v turned by `angle` towards the unit vector n (perpendicular to v, towards the hole).
vec3 lensTurn(vec3 v, vec3 n, float angle) {
    return normalize(v * cos(angle) + n * sin(angle));
}

// The unit vector from the line through x along unit v towards the centre, and the impact parameter b.
vec3 lensInward(vec3 x, vec3 v, out float b) {
    vec3 perp = x - dot(x, v) * v;
    b = length(perp);
    return b > 1e-20 ? -perp / b : vec3(0.0);
}

// x'' = -k x / |x|^5, k = 3/2 r_s h^2.
vec3 lensAcceleration(vec3 x, float k) {
    float r2 = dot(x, x);
    return -k * x / (r2 * r2 * sqrt(r2));
}

// A blackbody's colour of temperature `kelvin` at luminance 1 (shaders/modern/galaxy.glsl's blackbodyColour).
vec3 lensBlackbody(float kelvin) {
    const vec3 lambda = vec3(605.0, 545.0, 460.0);
    vec3 planck = 1.0 / (pow(lambda / 500.0, vec3(5.0)) * (exp(1.4388e7 / (lambda * max(kelvin, 500.0))) - 1.0));
    return planck / max(dot(planck, vec3(0.2126, 0.7152, 0.0722)), 1e-30);
}

// Marches the disc (the grid medium) along the straight segment o + t d, t in [0, len]: emission and
// absorption, each sample Doppler-shifted and beamed by the gas's circular orbit and redshifted by gravity
// (its colour a blackbody of g T, its radiance times g^4, the bolometric transformation).
void lensMarchDisc(vec3 o, vec3 d, float len, uint segment, inout vec3 radiance, inout float transmittance) {
    GridRay g = gridRay(o, d);
    vec3 hi = vec3(mediumData.gridSize) / kMajorantCell;
    float ta = 0.0;
    float tb = len;
    for (int a = 0; a < 3; ++a) {
        if (abs(g.direction[a]) < 1e-20) {
            if (g.origin[a] < 0.0 || g.origin[a] > hi[a]) {
                return;
            }
        } else {
            float lo = (0.0 - g.origin[a]) / g.direction[a];
            float up = (hi[a] - g.origin[a]) / g.direction[a];
            ta = max(ta, min(lo, up));
            tb = min(tb, max(lo, up));
        }
    }
    if (!(tb > ta)) {
        return;
    }
    float rs = lensingData.massRadius;
    vec3 centre = lensingData.center;
    vec3 axis = normalize(mediumData.worldToMedium.rows[1].xyz);
    float dt = (tb - ta) / float(kLensDiscSamples);
    float jitter = rand(kLensDimension + segment);
    for (uint i = 0u; i < kLensDiscSamples; ++i) {
        float t = ta + (float(i) + jitter) * dt;
        vec4 voxel = gridVoxel(g.origin + g.direction * t);
        float sigma = mediumData.densityScale * max(voxel.r, 0.0);
        if (!(sigma > 0.0)) {
            continue;
        }
        vec3 x = o + d * t - centre;
        vec3 radial = x - dot(x, axis) * axis;
        float r = max(length(radial), 1.05 * rs);
        float beta = min(sqrt(rs / (2.0 * max(r - rs, 0.01 * rs))), 0.95);
        vec3 velocity = normalize(cross(axis, radial));
        float gamma = inversesqrt(1.0 - beta * beta);
        float shift = sqrt(max(1.0 - rs / r, 0.01)) / (gamma * (1.0 - beta * dot(velocity, -d)));
        vec3 emitted = mediumData.emissionScale * max(voxel.b, 0.0) * (shift * shift) * (shift * shift) *
                       lensBlackbody(1e4 * max(voxel.g, 0.0) * shift);
        float absorbed = 1.0 - exp(-sigma * dt);
        radiance += transmittance * absorbed * emitted;
        transmittance *= 1.0 - absorbed;
    }
}

// The payload of a ray that ends at the horizon (or deep in the disc): a black surface that scatters nothing,
// at p, with the light gathered on the way as its emission (and a depth, so splats behind it are hidden).
void lensStop(vec3 p, vec3 d, float t, vec3 emission) {
    payload.position = p;
    payload.t = t;
    payload.normal = -d;
    payload.geometricNormal = -d;
    payload.light = kNoLight;
    payload.frontFace = 1u;
    payload.baseColor = vec3(0.0);
    payload.roughness = 1.0;
    payload.metallic = 0.0;
    payload.ior = 1.0; // no Fresnel reflection: the horizon (and the opaque disc) end the path
    payload.transmission = 0.0;
    payload.coat = 0.0;
    payload.coatRoughness = 0.0;
    payload.materialClass = 0u;
    payload.emission = emission;
    payload.materialFlags = 0u;
    payload.abbe = 0.0;
    payload.medium = kNoMedium;
    payload.primitive = 0u;
    payload.prevPosition = p;
}

// Traces a main ray from origin along direction, bent by the lens, and leaves the result in the payload as
// traceMain() does (a hit, or a miss with the environment it reaches). Without a lens (or with r_s = 0) it is
// that very trace: the loop's one traceRayEXT with the same arguments, so the raygen has a single call site
// for main rays (lavapipe inlines the hit groups at every call site).
void traceLensed(vec3 origin, vec3 direction, float coneWidth, float coneSpread) {
    float rs = lensingActive() ? lensingData.massRadius : 0.0;
    vec3 centre = lensingData.center;
    float big = lensingData.influenceRadius;
    bool disc = lensingActive() && (lensingData.flags & kLensingDisc) != 0u;
    vec3 x0 = origin - centre;
    float r0 = length(x0);
    float along = dot(x0, direction);
    float b = 0.0;
    vec3 v = direction;

    // Phase 0: the straight way into the sphere; 1: the geodesic inside it; 2: the straight way out (or the
    // whole way, unbent).
    uint phase = 2u;
    float entry = 0.0;
    if (rs > 0.0) {
        vec3 inward = lensInward(x0, direction, b);
        phase = 1u;
        if (r0 >= big) {
            float chord = big * big - b * b;
            if (along < 0.0 && chord > 0.0) {
                phase = 0u;
                entry = -along - sqrt(chord);
            } else {
                // It misses the sphere: the weak-field deflection of its whole path, as one turn at the origin.
                v = lensTurn(direction, inward, rs / max(b, 1e-6) * (1.0 - along / r0));
                phase = 2u;
            }
        }
    }

    vec3 radiance = vec3(0.0);
    float transmittance = 1.0;
    float travelled = 0.0;
    vec3 p = origin;
    float k = 1.5 * rs * b * b;
    uint integrated = 0u;
    uint steps = rs > 0.0 ? lensingData.steps : 0u;
    for (uint i = 0u; i < steps + 2u; ++i) {
        vec3 d = v;
        float len = kLensRayMax;
        vec3 nextX = vec3(0.0), nextV = vec3(0.0);
        if (phase == 1u) {
            // One RK4 step, its length about stepScale * |x|.
            vec3 xc = p - centre;
            float h = lensingData.stepScale * length(xc) / max(length(v), 1e-6);
            vec3 k1x = v, k1v = lensAcceleration(xc, k);
            vec3 k2x = v + 0.5 * h * k1v, k2v = lensAcceleration(xc + 0.5 * h * k1x, k);
            vec3 k3x = v + 0.5 * h * k2v, k3v = lensAcceleration(xc + 0.5 * h * k2x, k);
            vec3 k4x = v + h * k3v, k4v = lensAcceleration(xc + h * k3x, k);
            nextX = xc + h / 6.0 * (k1x + 2.0 * k2x + 2.0 * k3x + k4x);
            nextV = v + h / 6.0 * (k1v + 2.0 * k2v + 2.0 * k3v + k4v);
            vec3 chord = nextX - xc;
            len = length(chord);
            d = chord / max(len, 1e-20);
            if (!(len > 0.0) || !isFinite(len) || !allFinite(nextV)) {
                lensStop(p, normalize(v), travelled, radiance); // the step broke down (at the horizon)
                return;
            }
        } else if (phase == 0u) {
            len = entry;
        }
        payload.coneWidth = coneWidth;
        payload.coneSpread = coneSpread;
        traceRayEXT(tlas, gl_RayFlagsOpaqueEXT, 0xFF, 0, 0, kMissIndexSky, p, 0.0, d, len, 0);
        if (phase == 2u) {
            if (rs > 0.0) {
                // The environment (or a surface) beyond the sphere, seen through what the disc let pass.
                if (payload.t >= 0.0) {
                    payload.t += travelled;
                }
                payload.emission = payload.emission * transmittance + radiance;
            }
            return;
        }
        if (payload.t >= 0.0) {
            payload.t += travelled;
            payload.emission = payload.emission * transmittance + radiance;
            return;
        }
        if (disc && phase == 1u) {
            lensMarchDisc(p, d, len, i, radiance, transmittance);
        }
        travelled += len;
        if (phase == 0u) {
            // At the sphere: the weak-field deflection of the way in, then the geodesic.
            p += d * len;
            vec3 xe = p - centre;
            float be;
            vec3 n = lensInward(xe, d, be);
            v = lensTurn(d, n, rs / max(be, 1e-6) * (dot(xe, d) / big - along / r0));
            vec3 l = cross(xe, v);
            k = 1.5 * rs * dot(l, l);
            phase = 1u;
            continue;
        }
        ++integrated;
        p = centre + nextX;
        v = nextV;
        float r = length(nextX);
        bool spent = integrated >= lensingData.steps;
        if (r < 1.01 * rs || transmittance < 1e-3 || (spent && r < 3.0 * rs)) {
            lensStop(p, d, travelled, radiance); // captured, or lost in the disc
            return;
        }
        vec3 dir = normalize(v);
        if ((r > big && dot(nextX, dir) > 0.0) || spent) {
            // Out: the rest of the weak-field deflection, then straight on to infinity.
            float bo;
            vec3 n = lensInward(nextX, dir, bo);
            v = lensTurn(dir, n, rs / max(bo, 1e-6) * (1.0 - dot(nextX, dir) / r));
            phase = 2u;
        }
    }
    lensStop(p, normalize(v), travelled, radiance);
}

#endif
