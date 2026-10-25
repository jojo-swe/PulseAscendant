#ifndef EVO_RT_PLASMA_SEA_GLSL
#define EVO_RT_PLASMA_SEA_GLSL

// PLASMA PLANET's sea (Pattern PlasmaSea, the Nebula scene: src/modern/scenes/nebula.cpp), on an analytic
// sphere: the 1993 plasma (shaders/retro/plasma.comp's sum of sines, at continuous pixel positions and time)
// laid over the planet and glowing in the 1993 palette ring, rotating as the 1993 palette cycles, under
// drifting plates of basalt crust whose cracks glow hottest. The 1993 picture's pixels are mapped onto the
// sphere by longitude about the planet's z axis (0 at +y, the seam at -y, out of every shot) and latitude
// from its equator, patternScale pixels to the radian (a pixel 1.2 times taller than wide, as on the 4:3
// screen). Included by pbr.rchit after hit.glsl. The material carries:
//   baseColor      the crust (basalt), lit like any rough surface; the molten sea between is near black and
//                  glossy, so the sun glitters on it
//   baseColor2     (the plasma's time in 1993 seconds, the ring's rotation in entries, the cracks' glow)
//   emission       the plasma's gain (emission * emissionScale times the ring's colour, linear light)
//   roughness      the crust's; patternScale the plasma's pixels per radian

const float kSeaPixelAspect = 1.2;  // shaders/retro/plasma.comp's pixel aspect
const float kSeaFolds = 3.0;        // its kFolds
const float kSeaRingEntries = 224.0; // retro::kPlasmaColours

float seaHash01(uint x) {
    x ^= x >> 16;
    x *= 0x7feb352du;
    x ^= x >> 15;
    x *= 0x846ca68bu;
    x ^= x >> 16;
    return float(x >> 8) * (1.0 / 16777216.0);
}

float seaNoise(vec3 p, uint seed) {
    vec3 i = floor(p);
    vec3 f = p - i;
    vec3 s = f * f * (3.0 - 2.0 * f);
    uvec3 c = uvec3(ivec3(i));
    uint base = c.x * 0x8DA6B343u + c.y * 0xD8163841u + c.z * 0xCB1AB31Fu + seed;
    float n000 = seaHash01(base);
    float n100 = seaHash01(base + 0x8DA6B343u);
    float n010 = seaHash01(base + 0xD8163841u);
    float n110 = seaHash01(base + 0x8DA6B343u + 0xD8163841u);
    float n001 = seaHash01(base + 0xCB1AB31Fu);
    float n101 = seaHash01(base + 0x8DA6B343u + 0xCB1AB31Fu);
    float n011 = seaHash01(base + 0xD8163841u + 0xCB1AB31Fu);
    float n111 = seaHash01(base + 0x8DA6B343u + 0xD8163841u + 0xCB1AB31Fu);
    return mix(mix(mix(n000, n100, s.x), mix(n010, n110, s.x), s.y),
               mix(mix(n001, n101, s.x), mix(n011, n111, s.x), s.y), s.z);
}

// `octaves` of value noise, each twice the frequency and half the weight, about [0, 1]; octaves finer than
// `finest` (a feature size in the same units as p) fade out so the plates never alias into sparkle.
float seaFbm(vec3 p, uint seed, int octaves, float finest) {
    float sum = 0.0, weight = 0.5, total = 0.0, size = 1.0;
    for (int i = 0; i < octaves; ++i) {
        float keep = smoothstep(2.0 * finest, 4.0 * finest, size);
        sum += weight * mix(0.5, seaNoise(p, seed + uint(i) * 0x1F123BB5u), keep);
        total += weight;
        p = p * 2.03 + vec3(17.1, 5.3, 11.7);
        weight *= 0.5;
        size *= 0.5;
    }
    return sum / total;
}

// The 1993 plasma's field in [-1, 1] at a (fractional) pixel and 1993 time (modern::plasmaField).
float seaPlasmaField(vec2 p, float t) {
    vec2 screen = vec2(p.x, p.y * kSeaPixelAspect);
    vec2 centre1 = vec2(160.0 + 120.0 * sin(t * 0.31), (100.0 + 75.0 * sin(t * 0.43 + 1.0)) * kSeaPixelAspect);
    vec2 centre2 = vec2(160.0 + 110.0 * cos(t * 0.27 + 2.0), (100.0 + 80.0 * cos(t * 0.37)) * kSeaPixelAspect);
    float field = sin(p.x / 41.0 + t * 0.83);
    field += sin(p.y / 29.0 - t * 0.61);
    field += sin((p.x + p.y) / 53.0 + t * 0.47);
    field += sin(length(screen - centre1) / 17.0 - t * 1.30);
    field += sin(length(screen - centre2) / 23.0 + t * 0.90);
    field += 0.5 * sin(p.x / 13.0 + 2.0 * sin(p.y / 19.0 + t * 0.70) + t);
    field += 0.5 * sin(p.y / 11.0 + 2.0 * sin(p.x / 23.0 - t * 0.55));
    return field / 6.0;
}

// The 1993 palette ring (src/retro/palettes.cpp, modern::plasmaRingColour): violet, magenta, orange, yellow,
// cyan, deep blue; sRGB in [0, 1].
vec3 seaRingMix(vec3 a, vec3 b, float t) {
    return floor(a + (b - a) * t + 0.5);
}

vec3 seaRingColour(uint entry) {
    const vec3 kViolet = vec3(40.0, 0.0, 90.0);
    const vec3 kMagenta = vec3(210.0, 20.0, 170.0);
    const vec3 kOrange = vec3(255.0, 120.0, 20.0);
    const vec3 kYellow = vec3(255.0, 235.0, 90.0);
    const vec3 kCyan = vec3(40.0, 220.0, 235.0);
    const vec3 kDeepBlue = vec3(10.0, 30.0, 140.0);
    vec3 from = kViolet, middle = kMagenta, to = kOrange;
    float span = 75.0;
    float i = float(entry % 224u);
    if (i >= 150.0) {
        from = kCyan;
        middle = kDeepBlue;
        to = kViolet;
        span = 74.0;
        i -= 150.0;
    } else if (i >= 75.0) {
        from = kOrange;
        middle = kYellow;
        to = kCyan;
        i -= 75.0;
    }
    float t = 2.0 * i / span;
    vec3 c = t < 1.0 ? seaRingMix(from, middle, t) : seaRingMix(middle, to, t - 1.0);
    return c / 255.0;
}

vec3 seaLinear(vec3 c) {
    return mix(c / 12.92, pow((c + 0.055) / 1.055, vec3(2.4)), step(vec3(0.04045), c));
}

// The width a ray's footprint covers on the surface here (world units; pbr.rchit's patternFootprint).
float seaFootprint(HitPoint h) {
    if (payload.coneSpread < 0.0) {
        return 0.5;
    }
    float width = abs(payload.coneWidth + payload.coneSpread * gl_HitTEXT);
    float cosine = max(abs(dot(gl_WorldRayDirectionEXT, h.geometricNormal)), 0.2);
    return width / cosine;
}

vec3 plasmaSeaPattern(Material m, HitPoint h, inout float roughness, inout vec3 emission) {
    vec3 d = normalize(h.geometricNormal);
    float radius = max(length(h.dPdv) / 3.14159265, 1e-3); // dPdv spans the half circle of a meridian
    float footprint = seaFootprint(h) / radius;          // radians
    float t = m.baseColor2.x, shift = m.baseColor2.y, cracks = m.baseColor2.z;
    float scale = max(m.patternScale, 1.0);

    // The 1993 picture on the sphere, warped by the sea's currents (more at small scales: the plasma boils).
    float longitude = atan(d.x, d.y);
    float latitude = asin(clamp(d.z, -1.0, 1.0));
    vec3 q = d * 9.0 + vec3(0.0, 0.0, 0.05 * t);
    vec2 warp = vec2(seaFbm(q, 0x51u, 3, footprint * 9.0), seaFbm(q + 4.7, 0x52u, 3, footprint * 9.0)) - 0.5;
    vec2 pixel = vec2(160.0 + longitude * scale, 100.0 - latitude * scale / kSeaPixelAspect) + 26.0 * warp;
    float field = seaPlasmaField(pixel, t);
    float level = clamp(0.5 + 0.5 * field, 0.0, 1.0);
    float position = mod(floor(level * kSeaFolds * kSeaRingEntries) + shift, kSeaRingEntries);
    uint entry = uint(position);
    vec3 plasma = seaLinear(mix(seaRingColour(entry), seaRingColour((entry + 1u) % 224u), fract(position)));
    // Incandescent: the palette's hues seen as hot matter, a quarter towards their own brightness and warmed.
    plasma = mix(plasma, vec3(dot(plasma, vec3(0.2126, 0.7152, 0.0722))), 0.25) * vec3(1.0, 0.82, 0.66);

    // The crust: the sea runs molten where the 1993 field runs high (so its bands and rings show as rivers of
    // light), its banks broken into floes by plates at two scales; the banks glow hottest (the fronts), and a
    // web of fine cracks glows faintly across the crust.
    vec3 c = d * 26.0 + 0.6 * vec3(warp, 0.0) + vec3(0.0, 0.01 * t, 0.0);
    float plates = 0.65 * seaFbm(c, 0x61u, 5, footprint * 26.0) + 0.35 * seaFbm(c * 3.7, 0x62u, 3, footprint * 96.0);
    float heat = level + 0.45 * (plates - 0.5);
    const float kMolten = 0.6;
    float edge = clamp(footprint * 26.0 * 0.06, 0.014, 0.04); // a bank's width in heat, about the footprint
    float crust = 1.0 - smoothstep(kMolten - 0.5 * edge, kMolten + 0.5 * edge, heat);
    float bank = exp(-pow((heat - kMolten) / edge, 2.0)) * (0.014 / edge);
    float fine = seaFbm(c * 11.0, 0x64u, 3, footprint * 286.0);
    float web = exp(-pow((fine - 0.5) / max(0.02, footprint * 286.0 * 0.05), 2.0)) * smoothstep(0.35, 0.6, heat);

    // Molten: dark glass that the sun glitters on, glowing; crust: rough basalt, dark. The banks run hottest:
    // their colour goes towards white.
    float glow = (1.0 - crust) * (0.45 + 0.55 * level) + crust * 0.12 * web;
    float luma = dot(plasma, vec3(0.2126, 0.7152, 0.0722));
    vec3 hot = mix(plasma, vec3(1.0, 0.82, 0.6) * max(luma, 0.08) * 2.5, 0.5);
    emission *= plasma * glow + hot * (cracks * bank);
    roughness = mix(0.14, roughness, crust);
    return mix(vec3(0.012, 0.011, 0.012), m.baseColor * (0.75 + 0.5 * seaNoise(c * 9.0, 0x63u)), crust);
}

#endif
