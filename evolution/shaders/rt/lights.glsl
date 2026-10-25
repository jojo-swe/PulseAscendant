#ifndef EVO_RT_LIGHTS_GLSL
#define EVO_RT_LIGHTS_GLSL

// Rectangular area lights: choosing one at a shading point, sampling a point on it, its radiance and the
// solid angle densities that multiple importance sampling (power heuristic, beta = 2) weighs against the
// BSDF's. A light emits from one side, along normalize(cross(edgeU, edgeV)). Next-event estimation samples
// the environment with probability frame.environmentSelect and a rect light otherwise. Needs common.glsl.
//
// Light selection is a function of the shading point (p, its shading normal n turned to the side the path
// arrived from, and whether the surface transmits): pickLight() and lightSelectPdf() use the same weights,
// lightImportance(), and a BSDF ray that hits a light evaluates lightSelectPdf() at the vertex it left,
// so the two MIS strategies always agree on the selection probability. One light of kind
// Tree stands for the light tree's spheres (light_tree.glsl), chosen within it the same way. A light that gets
// no weight at a point is simply never sampled there: BSDF rays that find it keep MIS weight 1.

#include "environment.glsl"
#include "light_tree.glsl" // the light tree
#include "screen.glsl"

vec3 lightNormal(RectLight l) {
    return safeNormalize(cross(l.edgeU, l.edgeV), vec3(0.0, 1.0, 0.0));
}

vec3 lightCentre(RectLight l) {
    return l.corner + 0.5 * (l.edgeU + l.edgeV);
}

// Where a ray from origin along d leaves through portal l (from its emitting side into the sky beyond): the
// distance and the cosine on the portal; false if it does not cross it.
bool crossesPortal(RectLight l, vec3 origin, vec3 d, out float t, out float cosLight) {
    t = 0.0;
    cosLight = 0.0;
    vec3 n = lightNormal(l);
    float denom = dot(d, n);
    if (!(denom < 0.0)) {
        return false;
    }
    t = dot(l.corner - origin, n) / denom;
    if (!(t > 0.0)) {
        return false;
    }
    vec3 q = origin + t * d - l.corner;
    float u = dot(q, l.edgeU) / dot(l.edgeU, l.edgeU);
    float v = dot(q, l.edgeV) / dot(l.edgeV, l.edgeV);
    cosLight = -denom;
    return u >= 0.0 && u <= 1.0 && v >= 0.0 && v <= 1.0;
}

// The light's power as selection sees it: the luminance of its radiance times its area. A RetroScreen's
// radiance is the picture's mean, the retro image's 1x1 mip level (already on the GPU: no readback), times
// emissionScale (its emission is (s, s, s)); a Portal's is the environment's lighting radiance straight out
// through it.
float lightPower(RectLight l) {
    float radiance = luminance(l.emission);
    if (l.kind == kLightPortal) {
        radiance = luminance(environmentLighting(-lightNormal(l)));
    } else if (l.kind == kLightRetroScreen) {
        float top = float(textureQueryLevels(retroScreen) - 1);
        radiance *= luminance(textureLod(retroScreen, vec2(0.5), top).rgb);
    }
    return radiance * l.area;
}

// The unnormalised weight with which light i is chosen at the shading point: selectWeight * front * power *
// cosBound / d^2 (per-point light selection), where
// - front: p is on the light's emitting side;
// - cosBound: 1 on a transmissive surface, else the largest cosine between n and the directions to the
//   light's corners and centre (0: the whole light is below the surface, as cos is linear over the quad);
// - d^2: the squared distance to the light's centre, at least area / 4;
// - a gated light (gate != kNoLight: a moon outside a window) only where the segment from p to its centre
//   crosses that portal.
// The flag kTraceFlagFixedLightSelection returns step 4's fixed max(selectWeight, 0) instead (comparisons).
// A Tree light (the light tree) stands for the whole light tree: selectWeight times the root's importance.
float lightImportance(uint i, vec3 p, vec3 n, bool transmissive) {
    RectLight l = lights[i];
    float weight = max(l.selectWeight, 0.0);
    if ((frame.flags & kTraceFlagFixedLightSelection) != 0u) {
        return weight;
    }
    if (l.kind == kLightTree) {
        float tw = frame.treeLightCount > 0u ? weight * treeNodeImportance(treeNodes[0], p, n, transmissive) : 0.0;
        return tw > 0.0 && isFinite(tw) ? tw : 0.0;
    }
    if (!(weight > 0.0) || !(l.area > 0.0)) {
        return 0.0;
    }
    vec3 centre = lightCentre(l);
    vec3 toCentre = centre - p;
    if (!(dot(toCentre, lightNormal(l)) < 0.0)) {
        return 0.0; // behind the light, or in its plane
    }
    float cosBound = 1.0;
    if (!transmissive) {
        cosBound = max(dot(n, safeNormalize(toCentre, vec3(0.0))), 0.0);
        cosBound = max(cosBound, dot(n, safeNormalize(l.corner - p, vec3(0.0))));
        cosBound = max(cosBound, dot(n, safeNormalize(l.corner + l.edgeU - p, vec3(0.0))));
        cosBound = max(cosBound, dot(n, safeNormalize(l.corner + l.edgeV - p, vec3(0.0))));
        cosBound = max(cosBound, dot(n, safeNormalize(l.corner + l.edgeU + l.edgeV - p, vec3(0.0))));
        if (!(cosBound > 0.0)) {
            return 0.0;
        }
    }
    float d2 = dot(toCentre, toCentre);
    if (l.gate != kNoLight) {
        float t;
        float cosPortal;
        float dist = sqrt(d2);
        if (!crossesPortal(lights[l.gate], p, toCentre / dist, t, cosPortal) || !(t < dist)) {
            return 0.0;
        }
    }
    float w = weight * lightPower(l) * cosBound / max(d2, 0.25 * l.area);
    return w > 0.0 && isFinite(w) ? w : 0.0;
}

// The sum of every light's weight at the shading point.
float lightImportanceSum(vec3 p, vec3 n, bool transmissive) {
    float total = 0.0;
    for (uint i = 0u; i < frame.lightCount; ++i) {
        total += lightImportance(i, p, n, transmissive);
    }
    return total;
}

// The probability that pickLight() at the shading point returns light index.
float lightSelectPdf(uint index, vec3 p, vec3 n, bool transmissive) {
    float total = lightImportanceSum(p, n, transmissive);
    return total > 0.0 && isFinite(total) ? lightImportance(index, p, n, transmissive) / total : 0.0;
}

// Picks a light by its weight at the shading point, in two passes over the lights (the sum, then the light u
// falls in); false when no light has weight there. pdf = lightSelectPdf(index, p, n, transmissive).
bool pickLight(float u, vec3 p, vec3 n, bool transmissive, out uint index, out float pdf) {
    index = 0u;
    pdf = 0.0;
    float total = lightImportanceSum(p, n, transmissive);
    if (!(total > 0.0) || !isFinite(total)) {
        return false;
    }
    float target = u * total;
    float accumulated = 0.0;
    float chosen = 0.0;
    for (uint i = 0u; i < frame.lightCount; ++i) {
        float w = lightImportance(i, p, n, transmissive);
        if (w > 0.0) {
            index = i; // the last light with weight, should rounding leave target past the sum
            chosen = w;
            accumulated += w;
            if (target < accumulated) {
                break;
            }
        }
    }
    pdf = chosen / total;
    return pdf > 0.0;
}

// The probability that next-event estimation at the shading point chooses rect light index: it picks a rect
// light at all with probability 1 - environmentSelect.
float lightChoicePdf(uint index, vec3 p, vec3 n, bool transmissive) {
    return (1.0 - frame.environmentSelect) * lightSelectPdf(index, p, n, transmissive);
}

// --- The light tree ------------------------------------------------------------------------------
// The tree's lights are emissive spheres: the hit sphere's index in its buffer (HitPayload.primitive) names
// its tree light, k = sphere - treeLights[0].sphere (light k is sphere firstSphere + k).

// Within the tree, fixed light selection (comparisons) picks uniformly.
bool treeUniformSelection() {
    return (frame.flags & kTraceFlagFixedLightSelection) != 0u;
}

// The tree light that sphere is, or kNoLight (a sphere of the instance beyond the tree's lights).
uint treeLightOfSphere(uint sphere) {
    if (frame.treeLightCount == 0u) {
        return kNoLight;
    }
    uint k = sphere - treeLights[0].sphere; // wraps to a large number below the first
    return k < frame.treeLightCount && treeLights[k].sphere == sphere ? k : kNoLight;
}

// The radiance a ray sees on a sphere of the light tree's instance: its tree light's emission (this frame's
// scale applied: blinking) from outside, none from inside; `fallback` (the sphere's own) for a sphere that
// is no tree light. What next-event estimation samples and what a ray finds are then the same emitter.
vec3 treeHitEmission(uint sphere, bool front, vec3 fallback) {
    uint k = treeLightOfSphere(sphere);
    if (k == kNoLight) {
        return fallback;
    }
    return front ? treeLights[k].emission : vec3(0.0);
}

// The density with which next-event estimation at the shading point (p, n, transmissive) reaches a direction
// that hits `sphere` of tree `entry`: the entry's choice, the walk to its light, and the light's cone. The MIS
// partner of a BSDF ray that hits it, evaluated at the vertex the ray left; 0 for a sphere
// that is no tree light.
float treeHitPdf(uint entry, uint sphere, vec3 p, vec3 n, bool transmissive) {
    uint k = treeLightOfSphere(sphere);
    if (k == kNoLight) {
        return 0.0;
    }
    return lightChoicePdf(entry, p, n, transmissive) *
           treeLightPdf(k, frame.treeLightCount, p, n, transmissive, treeUniformSelection()) *
           sphereConeDensity(p, treeLights[k].center, treeLights[k].radius);
}

// The radiance a light sends along wi (from the shaded point towards the light) from its point (u, v).
vec3 lightRadiance(RectLight l, vec2 uv, vec3 wi) {
    if (l.kind == kLightPortal) {
        return environmentLighting(wi);
    }
    if (l.kind == kLightRetroScreen) {
        // The quad's own uv: (u, 1 - v), the picture's top left at the light's corner + edgeV.
        return retroScreenPicture(vec2(uv.x, 1.0 - uv.y), kDiffuseTextureLod) * l.emission;
    }
    return l.emission;
}

// Solid angle density of a point on the light at distance dist whose emitting side makes cosLight with the
// direction back to the shaded point (uniform area sampling).
float rectSolidAnglePdf(RectLight l, float dist, float cosLight) {
    return dist * dist / (cosLight * l.area);
}

float powerHeuristic(float a, float b) {
    if (isInfinite(a)) {
        return 1.0;
    }
    float a2 = a * a;
    float b2 = b * b;
    return a2 + b2 > 0.0 ? a2 / (a2 + b2) : 0.0;
}

// The density with which the rect-light part of NEE at the shading point (p, n, transmissive) reaches the
// sky along d from origin (its offset ray origin): selection times solid angle, summed over every portal the
// direction crosses.
float portalPdf(vec3 origin, vec3 d, vec3 p, vec3 n, bool transmissive) {
    float pdf = 0.0;
    for (uint i = 0u; i < frame.lightCount; ++i) {
        RectLight l = lights[i];
        float t;
        float cosLight;
        if (l.kind == kLightPortal && l.area > 0.0 && crossesPortal(l, origin, d, t, cosLight)) {
            pdf += lightSelectPdf(i, p, n, transmissive) * rectSolidAnglePdf(l, t, cosLight);
        }
    }
    return pdf;
}

// The whole NEE density of a direction d that escapes to the environment from the shading point: through a
// portal (the rect lights' share) or by sampling the environment (its share, environmentSelect). The MIS
// partner of a BSDF ray that escapes, and the density of an NEE sample of the sky either way.
float escapePdf(vec3 origin, vec3 d, vec3 p, vec3 n, bool transmissive) {
    return (1.0 - frame.environmentSelect) * portalPdf(origin, d, p, n, transmissive) +
           frame.environmentSelect * environmentPdf(d);
}

#endif
