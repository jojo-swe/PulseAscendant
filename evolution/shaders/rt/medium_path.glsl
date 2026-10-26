#ifndef EVO_RT_MEDIUM_PATH_GLSL
#define EVO_RT_MEDIUM_PATH_GLSL

// The participating-media integrator, built on medium.glsl. pathtrace.rgen includes it after
// its own helpers (traceScene, shadowTransmittance, sampleLight, addContribution, bounceRand, ...) and calls it from
// its hooks marked "[medium]", all behind mediumActive():
// - mediumStep() after every trace of a path: at bounce 0 mediumPrimary(), the primary segment's
//   transmittance and in-scattered radiance into gMediumVolume (the volume layer) and the start of its
//   multiple-scattering continuation (gMediumContinue*); later mediumSegment(), delta tracking along the
//   segment and the path's medium vertex when it collides (next-event estimation with the phase function and
//   MIS, then a phase-sampled direction);
// - a second pass of the raygen's bounce loop from the continuation's start (bounce 1, random dimensions
//   offset by kMediumBounceOffset), whose radiance goes to the volume layer. One loop serves both paths: the
//   ray tracing code is inlined once (lavapipe compiles a pipeline as one function).
//
// The primary segment (camera to the first surface, or to infinity) is estimated three ways:
// - T: the fog's closed form times the grid's residual ratio tracking, a continuous estimate;
// - the grid's emission: integrated along the same ratio-tracking walk, sub-step by sub-step (about one voxel
//   each), as the integral of T(t) S(t) with the control's exponential and S (= sigma_a Le) linear between
//   the sub-step's ends. That is a quadrature at the grid's own resolution rather than a few random points,
//   so a glowing nebula's emission (fine fronts, knots) carries no sampling noise of its own; only the
//   walk's residual factors vary between samples, and they are nearly 1 where the density is smooth over a
//   voxel;
// - the in-scattered light: kPrimaryScatterSamples points sampled along the segment from a mixture of the
//   fog's collision density (truncated to the segment), the grid's majorant (piecewise constant over its
//   cells) and, when the frame has only a few tree lights (kEquiangularMaxLights; a star field's), equiangular
//   sampling towards one of them (Kulla and Fajardo 2012), which follows the 1 / r^2 of a light inside the
//   medium. Every point adds a next-event estimate weighted T(t) / (pdf(t) * kPrimaryScatterSamples), pdf
//   the mixture's density, with T(t) from the same ratio-tracking walk, so every pixel gets a dense estimate
//   (which the denoiser's volume history clamp needs). One of the points, chosen uniformly, continues as a
//   full path (weight T(t) / pdf(t)), which counts multiple scattering; its bounces use another range of the
//   per-bounce random dimensions than the surface path's.
// The surface path is traced unattenuated; the denoiser's combine pass computes T * surface + L_v.

const uint kMediumSegmentSurface = 0u;   // no collision: the surface (or the sky) the ray found
const uint kMediumSegmentScattered = 1u; // the path scattered in a medium and goes on from there
const uint kMediumSegmentEnd = 2u;       // the path ends in the medium
// The continuation's bounces draw the per-bounce random dimensions of bounce + this (the surface path's own
// stay below it).
const uint kMediumBounceOffset = 64u;
const uint kPrimaryScatterSamples = 4u; // modern::kPrimaryScatterSamples
// Equiangular sampling of the primary segment's points serves frames with 1 to this many tree lights (stars
// in a nebula); a city's thousands of lamps leave it to the majorant. The share of the points it draws:
const uint kEquiangularMaxLights = 64u;   // modern::kEquiangularMaxLights
const float kEquiangularShare = 0.5;

// The scattering at a point in the media: a mixture of up to two Henyey-Greenstein lobes. value(cos) =
// weight0 HG(g0) + weight1 HG(g1) (what a direction contributes), pdf(cos) = select0 HG(g0) + (1 - select0)
// HG(g1) (how sampleMediumPhase picks one).
struct MediumPhase {
    float g0;
    float g1;
    float select0;
    vec3 weight0;
    vec3 weight1;
};

MediumPhase singlePhase(float g) {
    MediumPhase ph;
    ph.g0 = g;
    ph.g1 = 0.0;
    ph.select0 = 1.0;
    ph.weight0 = vec3(1.0);
    ph.weight1 = vec3(0.0);
    return ph;
}

vec3 mediumPhaseValue(MediumPhase ph, float cosTheta) {
    return ph.weight0 * hgPhase(ph.g0, cosTheta) + ph.weight1 * hgPhase(ph.g1, cosTheta);
}

float mediumPhasePdf(MediumPhase ph, float cosTheta) {
    float pdf = ph.select0 * hgPhase(ph.g0, cosTheta);
    if (ph.select0 < 1.0) {
        pdf += (1.0 - ph.select0) * hgPhase(ph.g1, cosTheta);
    }
    return pdf;
}

vec3 sampleMediumPhase(MediumPhase ph, vec3 travel, vec3 u) {
    return hgSample(u.x < ph.select0 ? ph.g0 : ph.g1, travel, u.yz);
}

// Next-event estimation from a point x in the media, for a path travelling along `travel`: like sampleLight
// (the environment with probability frame.environmentSelect, else one rect light chosen at x as a
// transmissive point would choose it), with the phase function in place of the BSDF and no surface to offset
// from. u: the light choice and the point on it. Returns value(phase) * Le * T * w_MIS / pdf_NEE. Both kinds
// of light end in the one shadow ray (a single ray tracing site).
vec3 mediumSampleLight(vec3 x, vec3 travel, MediumPhase ph, vec3 u, uint treeWord) {
    EVO_COUNT(kCountMediumNee, 1);
    const vec3 facing = vec3(0.0, 1.0, 0.0); // light selection ignores it at a transmissive point
    float environmentSelect = frame.environmentSelect;
    vec3 wi;
    vec3 le;
    float lightPdf;
    vec3 shadowDirection;
    float tMax = kRayMax;
    if (u.x < environmentSelect) {
        float environmentDensity;
        if (!sampleEnvironment(u.yz, wi, le, environmentDensity)) {
            return vec3(0.0);
        }
        lightPdf = escapePdf(x, wi, x, facing, true);
        shadowDirection = wi;
    } else {
        uint index;
        float selectPdf;
        if (!pickLight((u.x - environmentSelect) / (1.0 - environmentSelect), x, facing, true, index,
                       selectPdf)) {
            return vec3(0.0);
        }
        RectLight l = lights[index];
        if (l.kind == kLightTree) {
            // The light tree, as sampleTreeLight: one sphere light by the walk (treeWord), one
            // direction in its cone, the shadow ray to its near surface lifted off it.
            uint k;
            float treePdf;
            float dist;
            float conePdf;
            if (!pickTreeLight(treeWord, frame.treeLightCount, x, facing, true, treeUniformSelection(), k,
                               treePdf)) {
                return vec3(0.0);
            }
            TreeLight light = treeLights[k];
            if (!sampleSphereCone(x, light.center, light.radius, u.yz, wi, dist, conePdf)) {
                return vec3(0.0);
            }
            lightPdf = selectPdf * (1.0 - environmentSelect) * treePdf * conePdf;
            vec3 q = x + dist * wi;
            vec3 toTarget = offsetOrigin(q, safeNormalize(q - light.center, -wi), -wi) - x;
            tMax = length(toTarget);
            if (!(tMax > 0.0)) {
                return vec3(0.0);
            }
            shadowDirection = toTarget / tMax;
            le = light.emission;
        } else {
            if (!(l.area > 0.0)) {
                return vec3(0.0);
            }
            vec3 q = l.corner + u.y * l.edgeU + u.z * l.edgeV;
            vec3 toLight = q - x;
            float dist = length(toLight);
            if (!(dist > 0.0)) {
                return vec3(0.0);
            }
            wi = toLight / dist;
            float cosLight = -dot(wi, lightNormal(l));
            if (!(cosLight > 0.0)) {
                return vec3(0.0);
            }
            lightPdf = selectPdf * (1.0 - environmentSelect) * rectSolidAnglePdf(l, dist, cosLight);
            shadowDirection = wi;
            if (l.kind == kLightPortal) {
                lightPdf = escapePdf(x, wi, x, facing, true);
            } else {
                vec3 toTarget = offsetOrigin(q, lightNormal(l), -wi) - x;
                tMax = length(toTarget);
                if (!(tMax > 0.0)) {
                    return vec3(0.0);
                }
                shadowDirection = toTarget / tMax;
            }
            le = lightRadiance(l, u.yz, wi);
        }
    }
    float c = dot(travel, wi);
    vec3 f = mediumPhaseValue(ph, c);
    if (!(lightPdf > 0.0) || !isFinite(lightPdf) || !(maxComponent(f) > 0.0)) {
        return vec3(0.0);
    }
    vec3 through = shadowTransmittance(x, shadowDirection, tMax); // stained glass colours the shafts
    if (!(maxComponent(through) > 0.0)) {
        return vec3(0.0);
    }
    return f * le * through * (powerHeuristic(lightPdf, mediumPhasePdf(ph, c)) / lightPdf) *
           mediumTransmittance(x, shadowDirection, tMax);
}

// Next-event estimates requested by the primary segment's points or a medium vertex, all traced at the one
// mediumSampleLight call in mediumStep (lavapipe inlines every call site of a ray tracing function).
uint gMediumRequestCount = 0u;
vec3 gMediumRequestPoint[kPrimaryScatterSamples];
vec3 gMediumRequestTravel[kPrimaryScatterSamples];
MediumPhase gMediumRequestPhase[kPrimaryScatterSamples];
vec3 gMediumRequestWeight[kPrimaryScatterSamples];
vec3 gMediumRequestRandom[kPrimaryScatterSamples];
uint gMediumRequestTreeWord[kPrimaryScatterSamples];

void mediumRequestLight(vec3 x, vec3 travel, MediumPhase ph, vec3 weight, vec3 u, uint treeWord) {
    uint i = gMediumRequestCount;
    if (i < kPrimaryScatterSamples) {
        gMediumRequestPoint[i] = x;
        gMediumRequestTravel[i] = travel;
        gMediumRequestPhase[i] = ph;
        gMediumRequestWeight[i] = weight;
        gMediumRequestRandom[i] = u;
        gMediumRequestTreeWord[i] = treeWord;
        gMediumRequestCount = i + 1u;
    }
}

// A later segment of a path (the ray just traced from origin along direction, to payload.t or the sky): delta
// tracking, the grid's emission into radiance (clamped while the path is rough), and
// at a real collision the medium vertex: the albedo into the throughput, a request for next-event estimation
// (MIS against the phase function), a direction sampled from it, Russian roulette as at a surface, and the path's state
// for what it finds next (the vertex is transmissive for light selection).
uint mediumSegment(uint bounce, bool nee, bool roulette, inout bool rough, inout vec3 throughput,
                   inout vec3 radiance, inout vec3 origin, inout vec3 direction, inout float coneSpread,
                   inout bool previousDelta, inout float previousPdf, inout vec3 prevP, inout vec3 prevN,
                   inout bool prevTransmissive) {
    float tMax = payload.t < 0.0 ? kMediumInfinity : payload.t;
    float tHit;
    uint which;
    vec3 emitted;
    bool collided = mediumCollision(origin, direction, tMax, tHit, which, emitted);
    if (maxComponent(emitted) > 0.0) {
        addContribution(radiance, throughput * emitted, rough);
    }
    if (!collided) {
        return kMediumSegmentSurface;
    }
    if (bounce >= frame.maxBounces) {
        return kMediumSegmentEnd;
    }
    EVO_COUNT(kCountMediumScatter, 1);
    vec3 x = origin + tHit * direction;
    float g = which == 0u ? mediumData.fogPhaseG : mediumData.gridPhaseG;
    throughput *= which == 0u ? mediumData.fogAlbedo : mediumData.gridAlbedo;
    MediumPhase ph = singlePhase(g);
    if (nee && (frame.lightCount > 0u || frame.environmentSelect > 0.0)) {
        vec3 u = vec3(bounceRand(bounce, 3u), bounceRand(bounce, 4u), bounceRand(bounce, 5u));
        uint treeWord = randomHash(frame.frameSeed, gPixel, gSample, 4u + (bounce + gBounceOffset) * kDimsPerBounce + 7u);
        mediumRequestLight(x, direction, ph, throughput, u, treeWord);
    }
    vec3 wi = hgSample(g, direction, vec2(bounceRand(bounce, 0u), bounceRand(bounce, 1u)));
    if (roulette && bounce >= 3u) {
        float survive = clamp(maxComponent(throughput), 0.05, 0.95);
        if (bounceRand(bounce, 6u) >= survive) {
            return kMediumSegmentEnd;
        }
        throughput /= survive;
    }
    if (!(maxComponent(throughput) > 0.0) || !allFinite(throughput)) {
        return kMediumSegmentEnd;
    }
    coneSpread = -1.0;
    rough = true; // a medium vertex scatters like a rough surface
    previousDelta = false;
    previousPdf = hgPhase(g, dot(direction, wi));
    prevP = x;
    prevN = wi;
    prevTransmissive = true;
    origin = x;
    direction = wi;
    return kMediumSegmentScattered;
}

// --- The primary segment -----------------------------------------------------------------------------------

vec4 gMediumVolume = vec4(0.0, 0.0, 0.0, 1.0); // this sample's (L_v, T)
bool gMediumContinue = false;                  // whether a continuation starts from the primary segment
vec3 gMediumContinueOrigin = vec3(0.0);
vec3 gMediumContinueDirection = vec3(0.0, 0.0, -1.0);
vec3 gMediumContinueThroughput = vec3(0.0);
float gMediumContinuePdf = 0.0;

// 1 - exp(-tau), accurate for small tau.
float collisionProbability(float tau) {
    return tau < 1e-3 ? tau * (1.0 - 0.5 * tau) : 1.0 - exp(-tau);
}

// The grid's majorant optical depth over a walk (a copy: the caller's walk stays at its start).
float majorantDepth(GridWalk w) {
    float depth = 0.0;
    float s0;
    float s1;
    float majorant;
    while (gridWalkNext(w, s0, s1, majorant)) {
        depth += majorant * (s1 - s0);
    }
    return depth;
}

// The distance at which the walk's majorant optical depth reaches target (< its total).
float majorantInvert(GridWalk w, float target) {
    float depth = 0.0;
    float s0;
    float s1;
    float majorant;
    float last = w.t;
    while (gridWalkNext(w, s0, s1, majorant)) {
        float piece = majorant * (s1 - s0);
        if (piece > 0.0 && depth + piece >= target) {
            return min(s0 + (target - depth) / majorant, s1);
        }
        depth += piece;
        last = s1;
    }
    return last;
}

// The majorant at distance t along a GridRay (0 outside [ta, tb], the walk's span).
float majorantAt(GridRay r, float t, float ta, float tb) {
    if (!(t >= ta && t <= tb)) {
        return 0.0;
    }
    ivec3 cell = clamp(ivec3(floor(r.origin + r.direction * t)), ivec3(0), ivec3(mediumData.majorantSize) - 1);
    return mediumData.densityScale * texelFetch(mediumMajorant, cell, 0).r;
}

// The integral of exp(-tau s) over s in [0, 1] (E0) and of s exp(-tau s) (E1), accurate for small tau: the
// weights of a sub-step's two ends when a linear source is integrated against the control's exponential.
float expMoment0(float tau) {
    return tau < 1e-3 ? 1.0 - tau * (0.5 - tau / 6.0) : (1.0 - exp(-tau)) / tau;
}
float expMoment1(float tau) {
    return tau < 0.05 ? 0.5 - tau * (1.0 / 3.0 - tau * 0.125) : (1.0 - (1.0 + tau) * exp(-tau)) / (tau * tau);
}

// The grid's emitted radiance per unit length at a baked voxel value: sigma_a Le.
vec3 gridSource(vec4 voxel) {
    float sigma = mediumData.densityScale * max(voxel.r, 0.0);
    return sigma * (1.0 - mediumData.gridAlbedo) * mediumData.emissionScale * max(voxel.gba, vec3(0.0));
}

// Residual ratio tracking (as gridTransmittance) over the walk, which also estimates the transmittance at
// each of the sorted points ts (each estimate unbiased on its own) and adds the grid's emission seen along
// the walk into `emitted` (each sub-step's integral of T S, the control's exponential against S linear
// between the sub-step's ends, times the walk's transmittance at the sub-step's start and the fog's there);
// returns the walk's whole transmittance. o, d: the world ray (for the fog).
float gridTransmittanceAt(GridRay r, GridWalk w, vec3 o, vec3 d, float ts[kPrimaryScatterSamples],
                          out float transmittances[kPrimaryScatterSamples], inout vec3 emitted) {
    uint next = 0u;
    for (uint k = 0u; k < kPrimaryScatterSamples; ++k) {
        transmittances[k] = 1.0;
    }
    while (next < kPrimaryScatterSamples && ts[next] <= w.t) {
        next += 1u; // before the box
    }
    bool emits = mediumData.emissionScale > 0.0 && maxComponent(1.0 - mediumData.gridAlbedo) > 0.0;
    float transmittance = 1.0;
    uint steps = 0u;
    float s0;
    float s1;
    float majorant;
    while (transmittance > 0.0 && gridWalkNext(w, s0, s1, majorant)) {
        bool dense = majorant > 0.0;
        uint n = dense ? gridSubsteps(r, s0, s1) : 1u;
        float h = (s1 - s0) / float(n);
        vec4 voxelA = dense ? gridVoxel(r.origin + r.direction * s0) : vec4(0.0);
        float sigmaA = min(mediumData.densityScale * max(voxelA.r, 0.0), majorant);
        vec3 sourceA = emits ? gridSource(voxelA) : vec3(0.0);
        for (uint i = 0u; i < n; ++i) {
            float a = s0 + float(i) * h;
            float b = i + 1u == n ? s1 : a + h;
            vec4 voxelB = dense ? gridVoxel(r.origin + r.direction * b) : vec4(0.0);
            float sigmaB = min(mediumData.densityScale * max(voxelB.r, 0.0), majorant);
            vec3 sourceB = emits ? gridSource(voxelB) : vec3(0.0);
            float slope = (sigmaB - sigmaA) / max(b - a, 1e-30);
            if (emits && maxComponent(sourceA + sourceB) > 0.0) {
                float tau = 0.5 * (sigmaA + sigmaB) * (b - a);
                float m0 = expMoment0(tau), m1 = expMoment1(tau);
                float fog = fogActive() ? exp(-fogDepth(o, d, 0.0, a)) : 1.0;
                emitted += transmittance * fog * (b - a) * (sourceA * (m0 - m1) + sourceB * m1);
            }
            float here = a;
            float controlHere = sigmaA;
            for (;;) {
                float t = dense && steps < kMediumMaxSteps ? here + mediumExponential() / majorant : b;
                float reach = min(t, b);
                while (next < kPrimaryScatterSamples && ts[next] <= reach) {
                    float controlThere = sigmaA + slope * (ts[next] - a);
                    transmittances[next] =
                        transmittance * exp(-0.5 * (controlHere + controlThere) * (ts[next] - here));
                    next += 1u;
                }
                float controlReach = sigmaA + slope * (reach - a);
                transmittance *= exp(-0.5 * (controlHere + controlReach) * (reach - here));
                here = reach;
                controlHere = controlReach;
                if (!(t < b)) {
                    break;
                }
                steps += 1u;
                EVO_COUNT(kCountTrackingSteps, 1);
                transmittance *=
                    clamp(1.0 - (gridSigma(r.origin + r.direction * t) - controlReach) / majorant, 0.0, 2.0);
                if (transmittance < kMediumTransmittanceCutoff) {
                    transmittance = 0.0;
                    break;
                }
            }
            if (!(transmittance > 0.0)) {
                break;
            }
            sigmaA = sigmaB;
            sourceA = sourceB;
        }
    }
    while (next < kPrimaryScatterSamples) {
        transmittances[next] = transmittance; // past the box (or the walk was cut off: 0)
        next += 1u;
    }
    return transmittance;
}

// Equiangular sampling towards tree light k over [ta, tb] of the ray o + t d: the light's centre projects
// to t = delta at distance D from the ray, and the angles thetaA, thetaB bound the interval as seen from it.
struct Equiangular {
    float delta;
    float distance;
    float thetaA;
    float thetaB;
};

Equiangular equiangularFor(vec3 o, vec3 d, uint k, float ta, float tb) {
    TreeLight light = treeLights[k];
    Equiangular e;
    vec3 toCentre = light.center - o;
    e.delta = dot(toCentre, d);
    // Never closer than half the light's radius: a ray through the sphere ends on it anyway.
    e.distance = max(length(toCentre - e.delta * d), max(0.5 * light.radius, 1e-4));
    e.thetaA = atan(ta - e.delta, e.distance);
    e.thetaB = atan(tb - e.delta, e.distance);
    return e;
}

float equiangularPdf(Equiangular e, float t) {
    float span = e.thetaB - e.thetaA;
    if (!(span > 0.0)) {
        return 0.0;
    }
    float x = t - e.delta;
    return e.distance / (span * (e.distance * e.distance + x * x));
}

// How much of the primary segment's equiangular share light k gets, before normalising: its power over the
// square of its distance from the ray (a light near the ray lights more of the segment brightly).
float equiangularWeight(vec3 o, vec3 d, uint k) {
    TreeLight light = treeLights[k];
    vec3 toCentre = light.center - o;
    float along = dot(toCentre, d);
    float d2 = max(dot(toCentre, toCentre) - along * along, light.radius * light.radius);
    float power = luminance(light.emission) * light.radius * light.radius;
    return isFinite(power) && power > 0.0 ? power / max(d2, 1e-8) : 0.0;
}

// The primary segment from o along d to tHit (< 0: a miss): sets gMediumVolume (its next-event estimates
// still to add, requested) and the continuation's start.
void mediumPrimary(vec3 o, vec3 d, float tHit, bool nee) {
    gMediumContinue = false;
    float tEnd = tHit < 0.0 ? kMediumInfinity : tHit;
    float fogTau = fogDepth(o, d, 0.0, tEnd);
    float fogMass = fogActive() ? collisionProbability(fogTau) : 0.0;
    GridRay gr;
    GridWalk start;
    bool inGrid = false;
    float gridDepth = 0.0;
    if (gridActive()) {
        gr = gridRay(o, d);
        inGrid = gridWalkBegin(gr, 0.0, tEnd, start);
        if (inGrid) {
            gridDepth = majorantDepth(start);
        }
    }
    float gridMass = inGrid ? collisionProbability(gridDepth) : 0.0;
    float total = fogMass + gridMass;
    if (!(total > 0.0)) {
        gMediumVolume = vec4(0.0, 0.0, 0.0, exp(-fogTau));
        return;
    }
    float pickFog = fogMass / total;

    // Equiangular sampling towards the frame's few tree lights, over the grid's part of the segment: one
    // light per point, chosen by equiangularWeight.
    uint equiangularLights = gridMass > 0.0 && frame.treeLightCount <= kEquiangularMaxLights
                                 ? frame.treeLightCount : 0u;
    float weightSum = 0.0;
    for (uint k = 0u; k < equiangularLights; ++k) {
        weightSum += equiangularWeight(o, d, k);
    }
    float equiangularShare = weightSum > 0.0 && isFinite(weightSum) ? kEquiangularShare : 0.0;

    // The points: a mixture of the fog's collision density over [0, tEnd], the grid's majorant and the
    // equiangular densities.
    float ts[kPrimaryScatterSamples];
    float pdfs[kPrimaryScatterSamples];
    EVO_COUNT(kCountInScatterPoints, kPrimaryScatterSamples);
    for (uint k = 0u; k < kPrimaryScatterSamples; ++k) {
        float strategy = mediumRand();
        float u = (float(k) + mediumRand()) / float(kPrimaryScatterSamples);
        float t;
        if (strategy < equiangularShare) {
            float pick = mediumRand() * weightSum;
            uint light = equiangularLights - 1u;
            for (uint j = 0u; j < equiangularLights; ++j) {
                float w = equiangularWeight(o, d, j);
                if (pick < w) {
                    light = j;
                    break;
                }
                pick -= w;
            }
            Equiangular e = equiangularFor(o, d, light, start.t, start.tEnd);
            t = clamp(e.delta + e.distance * tan(mix(e.thetaA, e.thetaB, u)), start.t, start.tEnd);
        } else if ((strategy - equiangularShare) < pickFog * (1.0 - equiangularShare)) {
            float x = u * fogMass; // the depth -ln(1 - x)
            float tau = x < 1e-3 ? x * (1.0 + 0.5 * x) : -log(1.0 - x);
            t = min(fogInvert(o, d, 0.0, tau), tEnd);
        } else {
            t = majorantInvert(start, u * gridDepth);
        }
        float pdf = 0.0;
        if (fogMass > 0.0) {
            pdf += pickFog * fogSigma(o + t * d) * exp(-fogDepth(o, d, 0.0, t)) / fogMass;
        }
        if (gridMass > 0.0 && gridDepth > 0.0) {
            pdf += (1.0 - pickFog) * majorantAt(gr, t, start.t, start.tEnd) / gridDepth;
        }
        pdf *= 1.0 - equiangularShare;
        if (equiangularShare > 0.0) {
            float mixed = 0.0;
            for (uint j = 0u; j < equiangularLights; ++j) {
                float w = equiangularWeight(o, d, j);
                if (w > 0.0) {
                    mixed += w * equiangularPdf(equiangularFor(o, d, j, start.t, start.tEnd), t);
                }
            }
            pdf += equiangularShare * mixed / weightSum;
        }
        ts[k] = t;
        pdfs[k] = pdf;
    }
    // Sorted by distance, for the one ratio-tracking walk.
    for (uint i = 1u; i < kPrimaryScatterSamples; ++i) {
        for (uint j = i; j > 0u && ts[j - 1u] > ts[j]; --j) {
            float t = ts[j];
            ts[j] = ts[j - 1u];
            ts[j - 1u] = t;
            float p = pdfs[j];
            pdfs[j] = pdfs[j - 1u];
            pdfs[j - 1u] = p;
        }
    }
    float gridT[kPrimaryScatterSamples];
    float gridEnd = 1.0;
    vec3 radiance = vec3(0.0); // the grid's emission, integrated along the walk
    if (inGrid) {
        gridEnd = gridTransmittanceAt(gr, start, o, d, ts, gridT, radiance);
    } else {
        for (uint k = 0u; k < kPrimaryScatterSamples; ++k) {
            gridT[k] = 1.0;
        }
    }

    bool lit = nee && frame.maxBounces >= 1u && (frame.lightCount > 0u || frame.environmentSelect > 0.0);
    uint chosen = min(uint(mediumRand() * float(kPrimaryScatterSamples)), kPrimaryScatterSamples - 1u);
    for (uint k = 0u; k < kPrimaryScatterSamples; ++k) {
        float transmittance = exp(-fogDepth(o, d, 0.0, ts[k])) * gridT[k];
        if (!(pdfs[k] > 0.0) || !(transmittance > 0.0)) {
            continue;
        }
        vec3 x = o + ts[k] * d;
        vec3 fogScatter = vec3(0.0);
        if (fogActive()) {
            fogScatter = fogSigma(x) * mediumData.fogAlbedo;
        }
        vec3 gridScatter = vec3(0.0);
        if (inGrid) {
            gridScatter = gridSigma(gr.origin + gr.direction * ts[k]) * mediumData.gridAlbedo;
        }
        float weight = transmittance / (pdfs[k] * float(kPrimaryScatterSamples));
        float fogShare = luminance(fogScatter), gridShare = luminance(gridScatter);
        if (!(fogShare + gridShare > 0.0)) {
            continue;
        }
        MediumPhase ph;
        ph.g0 = mediumData.fogPhaseG;
        ph.g1 = mediumData.gridPhaseG;
        ph.select0 = fogShare / (fogShare + gridShare);
        ph.weight0 = fogScatter;
        ph.weight1 = gridScatter;
        if (lit) {
            mediumRequestLight(x, d, ph, vec3(weight), vec3(mediumRand(), mediumRand(), mediumRand()),
                               pcgHash(gMediumSeed ^ pcgHash(gMediumCount++)));
        }
        if (k == chosen && frame.maxBounces >= 1u) {
            vec3 wi = sampleMediumPhase(ph, d, vec3(mediumRand(), mediumRand(), mediumRand()));
            float c = dot(d, wi);
            float pdf = mediumPhasePdf(ph, c);
            vec3 throughput = (transmittance / pdfs[k]) * mediumPhaseValue(ph, c) / pdf;
            if (pdf > 0.0 && allFinite(throughput) && maxComponent(throughput) > 0.0) {
                gMediumContinue = true;
                gMediumContinueOrigin = x;
                gMediumContinueDirection = wi;
                gMediumContinueThroughput = throughput;
                gMediumContinuePdf = pdf;
            }
        }
    }
    gMediumVolume = vec4(radiance, exp(-fogTau) * gridEnd);
}

// The hook after every trace of a path while the frame has a medium: the primary segment at bounce 0, a later
// segment otherwise (mediumSegment's result; kMediumSegmentSurface at bounce 0), then the next-event estimates
// they requested, into the volume layer at bounce 0, into radiance after.
uint mediumStep(uint bounce, bool nee, bool roulette, inout bool rough, inout vec3 throughput, inout vec3 radiance,
                inout vec3 origin, inout vec3 direction, inout float coneSpread, inout bool previousDelta,
                inout float previousPdf, inout vec3 prevP, inout vec3 prevN, inout bool prevTransmissive) {
    gMediumRequestCount = 0u;
    uint event = kMediumSegmentSurface;
    bool neeRough = rough; // a vertex's next-event estimate is clamped as the path was before it scatters
    if (bounce == 0u) {
        mediumPrimary(origin, direction, payload.t, nee);
    } else {
        event = mediumSegment(bounce, nee, roulette, rough, throughput, radiance, origin, direction, coneSpread,
                              previousDelta, previousPdf, prevP, prevN, prevTransmissive);
    }
    for (uint i = 0u; i < gMediumRequestCount; ++i) {
        vec3 c = gMediumRequestWeight[i] * mediumSampleLight(gMediumRequestPoint[i], gMediumRequestTravel[i],
                                                             gMediumRequestPhase[i], gMediumRequestRandom[i],
                                                             gMediumRequestTreeWord[i]);
        if (bounce == 0u) {
            gMediumVolume.rgb += c;
        } else {
            addContribution(radiance, c, neeRough);
        }
    }
    return event;
}

// The volume layer's sample made safe: finite radiance >= 0 and T in [0, 1] ((0, 0, 0, 1) otherwise).
vec4 mediumSafeVolume(vec4 v) {
    if (!allFinite(v.rgb) || !isFinite(v.a)) {
        return vec4(0.0, 0.0, 0.0, 1.0);
    }
    return vec4(max(v.rgb, vec3(0.0)), clamp(v.a, 0.0, 1.0));
}

#endif
