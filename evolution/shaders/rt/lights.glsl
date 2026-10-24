#ifndef EVO_RT_LIGHTS_GLSL
#define EVO_RT_LIGHTS_GLSL

// Rectangular area lights: choosing one at a shading point, sampling a point on it, its radiance and the
// solid angle densities that multiple importance sampling (power heuristic, beta = 2) weighs against the
// BSDF's. A light emits from one side, along normalize(cross(edgeU, edgeV)). Next-event estimation samples
// the environment with probability frame.environmentSelect and a rect light otherwise. Needs common.glsl.
//
// Light selection is a function of the shading point (p, its shading normal n, and whether the surface
// transmits): pickLight() and lightSelectPdf() must use the same weights, and a BSDF ray that hits a light
// evaluates lightSelectPdf() at the vertex it left (Review Focus 2 of step 5). Task 3 makes the weights
// per point (lightImportance); until then they are the fixed selectWeight / lightWeightSum of step 4.

#include "environment.glsl"
#include "screen.glsl"

// The unnormalised weight with which light i is chosen at the shading point.
float lightImportance(uint i, vec3 p, vec3 n, bool transmissive) {
    return max(lights[i].selectWeight, 0.0);
}

// The probability that pickLight() at the shading point returns light index.
float lightSelectPdf(uint index, vec3 p, vec3 n, bool transmissive) {
    return frame.lightWeightSum > 0.0 ? lightImportance(index, p, n, transmissive) / frame.lightWeightSum : 0.0;
}

// Picks a light by its weight at the shading point; false when no light has weight.
bool pickLight(float u, vec3 p, vec3 n, bool transmissive, out uint index, out float pdf) {
    index = 0u;
    pdf = 0.0;
    if (!(frame.lightWeightSum > 0.0)) {
        return false;
    }
    float target = u * frame.lightWeightSum;
    float accumulated = 0.0;
    bool found = false;
    for (uint i = 0u; i < frame.lightCount; ++i) {
        float w = lightImportance(i, p, n, transmissive);
        if (w > 0.0) {
            index = i; // the last light with weight, should rounding leave target past the sum
            found = true;
            accumulated += w;
            if (target < accumulated) {
                break;
            }
        }
    }
    if (found) {
        pdf = lightSelectPdf(index, p, n, transmissive);
    }
    return found && pdf > 0.0;
}

// The probability that next-event estimation at the shading point chooses rect light index: it picks a rect
// light at all with probability 1 - environmentSelect.
float lightChoicePdf(uint index, vec3 p, vec3 n, bool transmissive) {
    return (1.0 - frame.environmentSelect) * lightSelectPdf(index, p, n, transmissive);
}

vec3 lightNormal(RectLight l) {
    return safeNormalize(cross(l.edgeU, l.edgeV), vec3(0.0, 1.0, 0.0));
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
