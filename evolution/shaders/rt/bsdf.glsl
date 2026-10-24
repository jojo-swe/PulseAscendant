#ifndef EVO_RT_BSDF_GLSL
#define EVO_RT_BSDF_GLSL

// The path tracer's BSDFs: physically based plastic, metal, clear-coated surfaces and glass.
//
// Opaque (transmission <= 0.5): a clear coat (F0 0.04, roughness coatRoughness, weight coat) over a base of GGX
// specular with F0 = mix(F0(ior), baseColor, metallic) plus (1 - metallic) Lambert in baseColor, where
// F0(ior) = ((ior - 1) / (ior + 1))^2 (0.04 at IOR 1.5, 0 at IOR 1). Each layer passes on what its Fresnel
// does not reflect, symmetrically in wo and wi (the base is scaled by (1 - Fcoat(n.wo)) (1 - Fcoat(n.wi)),
// the Lambert lobe by (1 - Fspec(n.wo)) (1 - Fspec(n.wi))), so the sum stays reciprocal and reflects at
// most what arrives. Schlick Fresnel with F90 = saturate(50 F0), so F0 = 0 means no specular lobe at all.
// Opaque surfaces are two-sided: wo below the shading normal mirrors the whole lobe below it.
//
// Dielectric (transmission > 0.5): exact unpolarised Fresnel at the given IOR (wo above n = outside, below n
// = inside), transmission tinted by baseColor; smooth (delta) when roughness < 0.02 or the IOR is within
// 1e-3 of 1, else rough GGX reflection and transmission (Walter et al. 2007). The transmitted weight carries
// no (eta_i / eta_t)^2 radiance scaling, so a sample's weight never exceeds 1.
//
// GGX: alpha = max(roughness^2, 1e-3), visible-normal sampling (Heitz 2018), Smith height-correlated G2. A
// specular lobe with roughness < 0.02 is a delta lobe: bsdfEval leaves it out (no NEE, no MIS) and
// bsdfSample reports it with delta = true and pdf 0. Lobes are picked by approximate albedo weights at wo,
// and a non-delta sample's pdf is the mixture over every non-delta lobe: bsdfEval's pdf for the same pair.
//
// Directions are unit vectors pointing away from the surface; n is the unit shading normal.

#include "random.glsl"

const float kBsdfPi = 3.14159265358979;
const float kDeltaRoughness = 0.02;
const float kMinAlpha = 1e-3;
const float kCoatF0 = 0.04;

struct Surface {
    vec3 baseColor;
    float roughness;
    float metallic;
    float ior;
    float transmission;
    float coat;
    float coatRoughness;
    vec3 emission;
};

// A right-handed basis (t, b, n) for a unit n (Duff et al., "Building an Orthonormal Basis, Revisited", 2017).
void orthonormalBasis(vec3 n, out vec3 t, out vec3 b) {
    float s = n.z >= 0.0 ? 1.0 : -1.0;
    float a = -1.0 / (s + n.z);
    float c = n.x * n.y * a;
    t = vec3(1.0 + s * n.x * n.x * a, s * c, -s * n.x);
    b = vec3(c, s + n.y * n.y * a, -n.y);
}

float bsdfLuminance(vec3 c) {
    return dot(c, vec3(0.2126, 0.7152, 0.0722));
}

float ggxAlpha(float roughness) {
    return max(roughness * roughness, kMinAlpha);
}

// GGX normal distribution for a unit half vector in the local frame (+z = n). Written with x^2 + y^2 rather
// than 1 - z^2, so it stays accurate for tiny alpha.
float ggxD(vec3 h, float a) {
    float a2 = a * a;
    float d = (h.x * h.x + h.y * h.y) + h.z * h.z * a2;
    return a2 / (kBsdfPi * d * d);
}

// sqrt(z^2 + alpha^2 (x^2 + y^2)): Smith's Lambda(w) = (smithS / |z| - 1) / 2. Every G term below is written
// with it so that nothing divides by a grazing cosine.
float smithS(vec3 w, float a) {
    return sqrt(w.z * w.z + a * a * (w.x * w.x + w.y * w.y));
}

// Microfacet reflection f cos(wi) without Fresnel, for zo, zi > 0:
// D G2 / (4 zo zi) * zi with G2 = 2 zo zi / (so zi + si zo).
float ggxReflection(vec3 wo, vec3 wi, vec3 h, float a) {
    float so = smithS(wo, a);
    float si = smithS(wi, a);
    return ggxD(h, a) * wi.z / (2.0 * (so * wi.z + si * wo.z));
}

// Visible-normal pdf of h divided by 4 (wo.h): the reflected direction's density, D / (2 (zo + so)).
float ggxReflectionPdf(vec3 wo, vec3 h, float a) {
    return ggxD(h, a) / (2.0 * (wo.z + smithS(wo, a)));
}

// Samples a microfacet normal from the distribution of normals visible from wo (wo.z > 0).
vec3 sampleGgxVndf(vec3 wo, float a, vec2 u) {
    vec3 vh = normalize(vec3(a * wo.x, a * wo.y, wo.z));
    float lensq = vh.x * vh.x + vh.y * vh.y;
    vec3 t1 = lensq > 0.0 ? vec3(-vh.y, vh.x, 0.0) * inversesqrt(lensq) : vec3(1.0, 0.0, 0.0);
    vec3 t2 = cross(vh, t1);
    float r = sqrt(u.x);
    float phi = 2.0 * kBsdfPi * u.y;
    float p1 = r * cos(phi);
    float p2 = r * sin(phi);
    float s = 0.5 * (1.0 + vh.z);
    p2 = (1.0 - s) * sqrt(max(0.0, 1.0 - p1 * p1)) + s * p2;
    vec3 nh = p1 * t1 + p2 * t2 + sqrt(max(0.0, 1.0 - p1 * p1 - p2 * p2)) * vh;
    return normalize(vec3(a * nh.x, a * nh.y, max(0.0, nh.z)));
}

vec3 fresnelSchlick(vec3 f0, float f90, float cosTheta) {
    float m = clamp(1.0 - cosTheta, 0.0, 1.0);
    float m2 = m * m;
    return f0 + (vec3(f90) - f0) * (m2 * m2 * m);
}

// The coat's reflectance at an angle, scaled by its weight.
float coatFresnel(float coat, float cosTheta) {
    return coat * fresnelSchlick(vec3(kCoatF0), 1.0, cosTheta).x;
}

// Unpolarised Fresnel reflectance of a smooth dielectric for cosI >= 0; eta = eta_incident / eta_transmitted.
// 1 under total internal reflection.
float fresnelDielectric(float cosI, float eta) {
    float sin2T = eta * eta * (1.0 - cosI * cosI);
    if (sin2T >= 1.0) {
        return 1.0;
    }
    float cosT = sqrt(1.0 - sin2T);
    float rs = (eta * cosI - cosT) / max(eta * cosI + cosT, 1e-30);
    float rp = (cosI - eta * cosT) / max(cosI + eta * cosT, 1e-30);
    return clamp(0.5 * (rs * rs + rp * rp), 0.0, 1.0);
}

float bsdfIor(Surface s) {
    return clamp(s.ior, 1.0, 4.0);
}

// The cosine between the half vector of a reflection pair and either direction, taken from both, so that
// swapping wo and wi gives the same value bit for bit. When wo and wi are nearly opposite grazing directions
// wo + wi cancels and normalize(wo + wi) is no exact bisector: wo.h and wi.h then differ by up to 1 %, which a
// steep Fresnel curve would turn into a non-reciprocal f.
float bisectorCosine(vec3 wo, vec3 wi, vec3 h) {
    return max(0.5 * (dot(wo, h) + dot(wi, h)), 0.0);
}

bool isDielectric(Surface s) {
    return s.transmission > 0.5;
}

// --- Opaque ------------------------------------------------------------------------------------------------

struct OpaqueLobes {
    vec3 baseColor;
    float metallic;
    float coat;
    vec3 f0; // base specular
    float f90;
    float alphaSpec;
    float alphaCoat;
    bool deltaSpec;
    bool deltaCoat;
    float pCoat; // lobe choice at wo
    float pSpec;
    float pDiffuse;
};

OpaqueLobes opaqueLobes(Surface s, float zo) {
    OpaqueLobes l;
    l.baseColor = clamp(s.baseColor, 0.0, 1.0);
    l.metallic = clamp(s.metallic, 0.0, 1.0);
    l.coat = clamp(s.coat, 0.0, 1.0);
    float r = (bsdfIor(s) - 1.0) / (bsdfIor(s) + 1.0);
    l.f0 = mix(vec3(r * r), l.baseColor, l.metallic);
    l.f90 = clamp(50.0 * (l.f0.x + l.f0.y + l.f0.z) / 3.0, 0.0, 1.0);
    l.alphaSpec = ggxAlpha(s.roughness);
    l.alphaCoat = ggxAlpha(s.coatRoughness);
    l.deltaSpec = s.roughness < kDeltaRoughness;
    l.deltaCoat = s.coatRoughness < kDeltaRoughness;

    float fc = coatFresnel(l.coat, zo);
    vec3 fs = fresnelSchlick(l.f0, l.f90, zo);
    float wCoat = fc;
    float wSpec = (1.0 - fc) * bsdfLuminance(fs);
    float wDiffuse = (1.0 - fc) * (1.0 - l.metallic) * bsdfLuminance(l.baseColor * (1.0 - fs));
    float total = wCoat + wSpec + wDiffuse;
    if (total > 0.0) {
        l.pCoat = wCoat / total;
        l.pSpec = wSpec / total;
        l.pDiffuse = wDiffuse / total;
    } else {
        l.pCoat = 0.0;
        l.pSpec = 0.0;
        l.pDiffuse = 1.0;
    }
    return l;
}

// f cos(wi) of the non-delta lobes and each non-delta lobe's own pdf (0 for a delta lobe); local frame, wo.z > 0.
struct OpaqueEval {
    vec3 f;
    float pdfCoat;
    float pdfSpec;
    float pdfDiffuse;
};

OpaqueEval evalOpaqueLocal(OpaqueLobes l, vec3 wo, vec3 wi) {
    OpaqueEval e;
    e.f = vec3(0.0);
    e.pdfCoat = 0.0;
    e.pdfSpec = 0.0;
    e.pdfDiffuse = 0.0;
    if (!(wi.z > 0.0)) {
        return e;
    }
    vec3 h = normalize(wo + wi);
    float woh = bisectorCosine(wo, wi, h);
    float throughCoat = (1.0 - coatFresnel(l.coat, wo.z)) * (1.0 - coatFresnel(l.coat, wi.z));
    if (!l.deltaCoat) {
        e.f += coatFresnel(l.coat, woh) * ggxReflection(wo, wi, h, l.alphaCoat);
        e.pdfCoat = ggxReflectionPdf(wo, h, l.alphaCoat);
    }
    if (!l.deltaSpec) {
        e.f += throughCoat * fresnelSchlick(l.f0, l.f90, woh) * ggxReflection(wo, wi, h, l.alphaSpec);
        e.pdfSpec = ggxReflectionPdf(wo, h, l.alphaSpec);
    }
    vec3 throughSpec = (1.0 - fresnelSchlick(l.f0, l.f90, wo.z)) * (1.0 - fresnelSchlick(l.f0, l.f90, wi.z));
    e.f += throughCoat * (1.0 - l.metallic) * l.baseColor * throughSpec * (wi.z / kBsdfPi);
    e.pdfDiffuse = wi.z / kBsdfPi;
    return e;
}

float opaqueMixturePdf(OpaqueLobes l, OpaqueEval e) {
    return l.pCoat * e.pdfCoat + l.pSpec * e.pdfSpec + l.pDiffuse * e.pdfDiffuse;
}

// --- Dielectric --------------------------------------------------------------------------------------------

bool dielectricIsSmooth(Surface s) {
    return s.roughness < kDeltaRoughness || bsdfIor(s) - 1.0 < 1e-3;
}

// f cos(wi) and pdf of rough glass in the local frame with wo.z > 0; eta = eta(wo's side) / eta(other side).
vec3 evalRoughDielectricLocal(vec3 tint, float a, float eta, vec3 wo, vec3 wi, out float pdf) {
    pdf = 0.0;
    float so = smithS(wo, a);
    if (wi.z > 0.0) {
        vec3 h = normalize(wo + wi);
        float f = fresnelDielectric(bisectorCosine(wo, wi, h), eta);
        pdf = f * ggxReflectionPdf(wo, h, a);
        return vec3(f * ggxReflection(wo, wi, h, a));
    }
    if (!(wi.z < 0.0)) {
        return vec3(0.0);
    }
    // Generalised half vector -(eta wo + wi), on wo's side.
    vec3 h = -(eta * wo + wi);
    float len2 = dot(h, h);
    if (!(len2 > 1e-20)) {
        return vec3(0.0);
    }
    h *= inversesqrt(len2);
    if (h.z < 0.0) {
        h = -h;
    }
    float woh = dot(wo, h);
    float wih = dot(wi, h);
    if (!(woh > 0.0) || !(wih < 0.0)) {
        return vec3(0.0);
    }
    float f = fresnelDielectric(woh, eta);
    float denom = eta * woh + wih;
    float denom2 = denom * denom;
    if (!(f < 1.0) || !(denom2 > 1e-20)) {
        return vec3(0.0);
    }
    float d = ggxD(h, a);
    float zi = -wi.z;
    float si = smithS(wi, a);
    // Visible-normal density times the refraction Jacobian |wi.h| / (eta wo.h + wi.h)^2.
    pdf = (1.0 - f) * (2.0 * woh * d / (wo.z + so)) * (-wih) / denom2;
    // Walter's BTDF |wi.h| |wo.h| / (|zi| zo) (1 - F) D G2 / (eta wo.h + wi.h)^2, times |zi|.
    return tint * (2.0 * (-wih) * woh * zi * (1.0 - f) * d / ((so * zi + si * wo.z) * denom2));
}

// --- Public interface --------------------------------------------------------------------------------------

// f cos(wi) and the pdf bsdfSample would give wi from wo (0 for delta lobes); n is the shading normal, wo and
// wi point away from the surface.
vec3 bsdfEval(Surface s, vec3 n, vec3 wo, vec3 wi, out float pdf) {
    pdf = 0.0;
    vec3 t;
    vec3 b;
    orthonormalBasis(n, t, b);
    vec3 woL = vec3(dot(wo, t), dot(wo, b), dot(wo, n));
    vec3 wiL = vec3(dot(wi, t), dot(wi, b), dot(wi, n));
    if (woL.z < 0.0) {
        woL.z = -woL.z;
        wiL.z = -wiL.z;
    }
    if (!(woL.z > 0.0)) {
        return vec3(0.0);
    }
    if (isDielectric(s)) {
        if (dielectricIsSmooth(s)) {
            return vec3(0.0);
        }
        float ior = bsdfIor(s);
        float eta = dot(wo, n) > 0.0 ? 1.0 / ior : ior;
        return evalRoughDielectricLocal(clamp(s.baseColor, 0.0, 1.0), ggxAlpha(s.roughness), eta, woL, wiL, pdf);
    }
    OpaqueLobes l = opaqueLobes(s, woL.z);
    OpaqueEval e = evalOpaqueLocal(l, woL, wiL);
    pdf = opaqueMixturePdf(l, e);
    return e.f;
}

// Samples wi from wo with three uniforms; weight = f cos / pdf (the throughput factor). delta = a smooth lobe
// was chosen (no NEE, no MIS; pdf 0). For a non-delta sample, f and pdf are bsdfEval's at the direction
// returned, so a BSDF sample that hits a light and a light sample in the same direction see one density (a
// near-opposite grazing pair fixes its half vector only to ~1e-7 / |wo + wi|, so a density taken from the
// sampled half vector instead could differ from bsdfEval's by more than 1e-3). Returns false when no
// direction was produced (wo in the tangent plane, a sampled direction on the wrong side): the path ends.
bool bsdfSample(Surface s, vec3 n, vec3 wo, vec3 u, out vec3 wi, out vec3 weight, out float pdf,
                out bool delta) {
    wi = n;
    weight = vec3(0.0);
    pdf = 0.0;
    delta = false;
    vec3 t;
    vec3 b;
    orthonormalBasis(n, t, b);
    vec3 woL = vec3(dot(wo, t), dot(wo, b), dot(wo, n));
    float side = woL.z < 0.0 ? -1.0 : 1.0; // the local frame is mirrored so that wo is above it
    woL.z *= side;
    if (!(woL.z > 0.0)) {
        return false;
    }

    vec3 wiL;
    OpaqueLobes l;
    float eta = 1.0;
    float alpha = 1.0;
    vec3 tint = clamp(s.baseColor, 0.0, 1.0);

    if (isDielectric(s)) {
        float ior = bsdfIor(s);
        eta = side > 0.0 ? 1.0 / ior : ior;
        if (dielectricIsSmooth(s)) {
            float f = fresnelDielectric(woL.z, eta);
            if (u.x < f) {
                wiL = vec3(-woL.xy, woL.z);
                weight = vec3(1.0);
            } else {
                float cosT = sqrt(max(0.0, 1.0 - eta * eta * (1.0 - woL.z * woL.z)));
                wiL = vec3(-eta * woL.xy, -cosT);
                weight = tint;
            }
            wi = normalize(t * wiL.x + b * wiL.y + n * (side * wiL.z));
            delta = true;
            return true;
        }
        alpha = ggxAlpha(s.roughness);
        vec3 h = sampleGgxVndf(woL, alpha, u.yz);
        float woh = dot(woL, h);
        if (!(woh > 0.0)) {
            return false;
        }
        if (u.x < fresnelDielectric(woh, eta)) {
            wiL = 2.0 * woh * h - woL;
        } else {
            float sin2T = eta * eta * (1.0 - woh * woh);
            if (!(sin2T < 1.0)) {
                return false;
            }
            wiL = -eta * woL + (eta * woh - sqrt(1.0 - sin2T)) * h;
        }
    } else {
        l = opaqueLobes(s, woL.z);
        if (u.x < l.pCoat) {
            if (l.deltaCoat) {
                wiL = vec3(-woL.xy, woL.z);
                weight = vec3(coatFresnel(l.coat, woL.z) / l.pCoat);
                delta = true;
            } else {
                vec3 h = sampleGgxVndf(woL, l.alphaCoat, u.yz);
                wiL = 2.0 * dot(woL, h) * h - woL;
            }
        } else if (u.x < l.pCoat + l.pSpec) {
            if (l.deltaSpec) {
                wiL = vec3(-woL.xy, woL.z);
                float throughCoat = 1.0 - coatFresnel(l.coat, woL.z);
                weight = throughCoat * throughCoat * fresnelSchlick(l.f0, l.f90, woL.z) / l.pSpec;
                delta = true;
            } else {
                vec3 h = sampleGgxVndf(woL, l.alphaSpec, u.yz);
                wiL = 2.0 * dot(woL, h) * h - woL;
            }
        } else {
            wiL = sampleCosineHemisphere(u.yz);
        }
        if (!(wiL.z > 0.0)) {
            return false; // reflected below the surface (or a grazing diffuse direction)
        }
        if (delta) {
            wi = normalize(t * wiL.x + b * wiL.y + n * (side * wiL.z));
            return true;
        }
    }

    wi = normalize(t * wiL.x + b * wiL.y + n * (side * wiL.z));
    // The direction as bsdfEval will see it; a microfacet sample on the wrong side of the surface, or a grazing
    // one that rounding moved across it, produces nothing (eval is 0 there).
    vec3 wiE = vec3(dot(wi, t), dot(wi, b), side * dot(wi, n));
    vec3 f;
    if (isDielectric(s)) {
        f = evalRoughDielectricLocal(tint, alpha, eta, woL, wiE, pdf);
    } else {
        OpaqueEval e = evalOpaqueLocal(l, woL, wiE);
        f = e.f;
        pdf = opaqueMixturePdf(l, e);
    }
    if (!(pdf > 0.0) || isinf(pdf)) {
        pdf = 0.0;
        return false;
    }
    weight = f / pdf;
    return true;
}

#endif
