#ifndef EVO_RT_ENVIRONMENT_GLSL
#define EVO_RT_ENVIRONMENT_GLSL

// The environment a ray sees when it leaves the scene, and its sampling for next-event estimation (step 5,
// scene environments): the scene's EnvironmentUniforms `environment` (binding EVO_BINDING_ENVIRONMENT, packed from
// SceneFrame::environment, src/modern/environment.hpp) over the sky models of sky.glsl. Needs the
// `environment` block declared before it (common.glsl declares it; the test probe its own).
//
// Which sky a ray sees (MIS stays consistent because NEE and the BSDF rays it is weighed against see the
// same function, environmentLighting):
// - environmentView: primary rays and chains of delta events from the camera (sky.rmiss with coneSpread >=
//   0). No NEE strategy competes for these, so the view may hold what NEE does not sample: stars, town
//   lights and a limb-darkened disc of the same power.
// - environmentLighting: next-event estimation (the environment's samples and portals) and every ray after
//   a rough vertex. Smooth: the gradient, the glow and the uniform disc.

#include "sky.glsl"

const float kEnvironmentPi = 3.14159265358979;

// What a primary ray or a chain of delta events from the camera sees: the full sky, with stars and town
// lights filtered over the ray cone's angular footprint (radians).
vec3 environmentView(vec3 d, float footprint) {
    d = normalize(d);
    vec3 sky = skyBackground(d) + skyDisc(d, true);
    if (environment.starBrightness > 0.0 && environment.starDensity > 0.0) {
        float visibility = skyStarVisibility(d);
        if (visibility > 0.0) {
            sky += visibility * skyStars(d, footprint);
        }
    }
    sky += skyTownLights(d, footprint);
    return environment.intensity * sky;
}

// What lights the scene: next-event estimation through a portal or towards the environment, and every miss
// after a rough vertex. Smooth (no stars, no town lights), so it does not speckle.
vec3 environmentLighting(vec3 d) {
    d = normalize(d);
    return environment.intensity * (skyBackground(d) + skyDisc(d, false));
}

// The solid angle density with which sampleEnvironment picks direction d (unit): the disc's share spread
// uniformly over its cone, plus the dome's share with density cos / pi about +y.
float environmentPdf(vec3 d) {
    float pdf = 0.0;
    if (environment.discShare > 0.0 && dot(d, environment.discDirection) >= environment.discCosRadius) {
        // 1 - cos is exact in float for cos in [0.5, 1] (Sterbenz), the cone's solid angle / 2 pi.
        pdf += environment.discShare / (2.0 * kEnvironmentPi * (1.0 - environment.discCosRadius));
    }
    if (environment.domeShare > 0.0 && d.y > 0.0) {
        pdf += environment.domeShare * d.y / kEnvironmentPi;
    }
    return pdf;
}

// Samples a direction wi towards the environment with radiance le (environmentLighting) and solid angle
// density pdf (environmentPdf(wi)): with probability discShare uniformly in the disc's cone, else cosine
// weighted about +y (u.x picks the strategy and is reused within it). False when the environment has
// nothing to sample (both shares 0).
bool sampleEnvironment(vec2 u, out vec3 wi, out vec3 le, out float pdf) {
    wi = vec3(0.0, 1.0, 0.0);
    le = vec3(0.0);
    pdf = 0.0;
    float discShare = environment.discShare;
    if (!(discShare + environment.domeShare > 0.0)) {
        return false;
    }
    if (u.x < discShare) {
        float h = (u.x / discShare) * (1.0 - environment.discCosRadius);
        float cosTheta = 1.0 - h;
        float sinTheta = sqrt(max(0.0, h * (2.0 - h)));
        float phi = 2.0 * kEnvironmentPi * u.y;
        // An orthonormal basis about the disc's axis (Duff et al. 2017).
        vec3 n = environment.discDirection;
        float s = n.z >= 0.0 ? 1.0 : -1.0;
        float a = -1.0 / (s + n.z);
        float b = n.x * n.y * a;
        vec3 t = vec3(1.0 + s * n.x * n.x * a, s * b, -s * n.x);
        vec3 bt = vec3(b, s + n.y * n.y * a, -n.y);
        wi = normalize(n * cosTheta + (t * cos(phi) + bt * sin(phi)) * sinTheta);
    } else {
        vec3 c = sampleCosineHemisphere(vec2((u.x - discShare) / environment.domeShare, u.y));
        wi = vec3(c.x, c.z, c.y); // about +y
    }
    pdf = environmentPdf(wi);
    le = environmentLighting(wi);
    return pdf > 0.0;
}

#endif
