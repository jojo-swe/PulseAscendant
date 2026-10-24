#ifndef EVO_RT_LIGHTS_GLSL
#define EVO_RT_LIGHTS_GLSL

// Rectangular area lights: choosing one by selectWeight, sampling a point on it, its radiance and the solid
// angle densities that multiple importance sampling (power heuristic, beta = 2) weighs against the BSDF's.
// A light emits from one side, along normalize(cross(edgeU, edgeV)). Needs common.glsl and sky.glsl.

#include "screen.glsl"

float lightSelectPdf(uint index) {
    return frame.lightWeightSum > 0.0 ? max(lights[index].selectWeight, 0.0) / frame.lightWeightSum : 0.0;
}

// Picks a light with probability selectWeight / lightWeightSum; false when no light has weight.
bool pickLight(float u, out uint index, out float pdf) {
    index = 0u;
    pdf = 0.0;
    if (!(frame.lightWeightSum > 0.0)) {
        return false;
    }
    float target = u * frame.lightWeightSum;
    float accumulated = 0.0;
    bool found = false;
    for (uint i = 0u; i < frame.lightCount; ++i) {
        float w = max(lights[i].selectWeight, 0.0);
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
        pdf = lightSelectPdf(index);
    }
    return found && pdf > 0.0;
}

vec3 lightNormal(RectLight l) {
    return safeNormalize(cross(l.edgeU, l.edgeV), vec3(0.0, 1.0, 0.0));
}

// The radiance a light sends along wi (from the shaded point towards the light) from its point (u, v).
vec3 lightRadiance(RectLight l, vec2 uv, vec3 wi) {
    if (l.kind == kLightPortal) {
        return skyRadiance(wi);
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
    if (isinf(a)) {
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

// The NEE density (selection times solid angle, summed over every portal it crosses) of a BSDF ray that
// escaped to the sky: the MIS partner of the sky it sees.
float portalPdf(vec3 origin, vec3 d) {
    float pdf = 0.0;
    for (uint i = 0u; i < frame.lightCount; ++i) {
        RectLight l = lights[i];
        float t;
        float cosLight;
        if (l.kind == kLightPortal && l.area > 0.0 && crossesPortal(l, origin, d, t, cosLight)) {
            pdf += lightSelectPdf(i) * rectSolidAnglePdf(l, t, cosLight);
        }
    }
    return pdf;
}

#endif
