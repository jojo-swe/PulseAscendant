#version 460
#extension GL_GOOGLE_include_directive : require

// Hit group 3 (MaterialClass Checker): a procedural checker of patternScale tiles in world space, baseColor
// and baseColor2, over the material's BSDF; with the Tesserae patterns, the 1993 rotozoom's texture laid as a
// polished stone mosaic. Never traces.

#include "hit.glsl"
#include "tesserae.glsl"

// --- The mosaic (Pattern Tesserae and TesseraeSphere) -------------------------------------------------------

// Smooth 3D value noise in [0, 1] (integer hashes of the lattice corners).
float stoneNoise(vec3 p, uint seed) {
    vec3 i = floor(p);
    vec3 f = p - i;
    vec3 s = f * f * (3.0 - 2.0 * f);
    uvec3 c = uvec3(ivec3(i));
    uint base = c.x * 0x8DA6B343u + c.y * 0xD8163841u + c.z * 0xCB1AB31Fu + seed;
    float n000 = hash01(base);
    float n100 = hash01(base + 0x8DA6B343u);
    float n010 = hash01(base + 0xD8163841u);
    float n110 = hash01(base + 0x8DA6B343u + 0xD8163841u);
    float n001 = hash01(base + 0xCB1AB31Fu);
    float n101 = hash01(base + 0x8DA6B343u + 0xCB1AB31Fu);
    float n011 = hash01(base + 0xD8163841u + 0xCB1AB31Fu);
    float n111 = hash01(base + 0x8DA6B343u + 0xD8163841u + 0xCB1AB31Fu);
    return mix(mix(mix(n000, n100, s.x), mix(n010, n110, s.x), s.y),
               mix(mix(n001, n101, s.x), mix(n011, n111, s.x), s.y), s.z);
}

// Three octaves of it, in [0, 1].
float stoneFbm(vec3 p, uint seed) {
    float sum = 0.0, amplitude = 0.5;
    for (int k = 0; k < 3; ++k) {
        sum += amplitude * stoneNoise(p, seed + uint(k) * 0x9E3779B9u);
        p = p * 2.03 + vec3(1.7, -2.3, 0.9);
        amplitude *= 0.5;
    }
    return sum / 0.875;
}

vec3 srgbToLinear(vec3 c) {
    return mix(c / 12.92, pow((c + 0.055) / 1.055, vec3(2.4)), greaterThan(c, vec3(0.04045)));
}

// The surface of the mosaic at tile coordinates q (one unit per tessera, the material's patternScale; tessera
// (u, v) is texel (u, v) of the 1993 texture) with p the point in tessera units for the noise. Each tessera
// is a stone cut in its pixel's colour and set by hand: light shades are marble with veins of their own, dark
// ones basalt with fine grain, the logo's chrome is gold-leaf smalti (metal); each sits tilted by up to about
// a degree, its colour and polish vary a little, the polish wears in broad patches, and grey mortar 6 % of a
// tessera wide fills the joints. Writes the colour, roughness, metallic and normal.
void tessera(vec2 q, vec3 p, inout HitPoint h, out vec3 color, inout float roughness) {
    vec2 cell = floor(q);
    vec2 f = q - cell;
    ivec2 texel = ivec2(cell);
    uint index = rotozoomIndex(texel);
    vec3 srgb = rotozoomColour(index) / 255.0;
    uint id = uint(texel.x) * 0x632BE5ABu + uint(texel.y) * 0x85157AF5u + 0x2F1Du;
    float shade = hash01(id);
    // Stone, not paint: the pixel's colour a little greyed towards its own lightness, and never darker than
    // dark basalt (real stone reflects a few per cent; a near-black channel would also starve the denoiser's
    // demodulation).
    vec3 linear = srgbToLinear(srgb);
    float lightness = dot(linear, vec3(0.2126, 0.7152, 0.0722));
    vec3 albedo = (0.025 + 0.93 * mix(vec3(lightness), linear, 0.72)) * (0.94 + 0.12 * shade);
    bool gold = index >= kRotozoomInk && index < kRotozoomOutline;
    if (gold) {
        // Gold leaf under a thin glass skin: a metal, its sheen a little crumpled.
        color = mix(albedo, vec3(1.0, 0.76, 0.36), 0.55) * (0.92 + 0.08 * stoneNoise(p * 6.0, id));
        h.material.metallic = 1.0;
        roughness = 0.1 + 0.08 * hash01(id + 7u);
    } else if (dot(srgb, vec3(0.2126, 0.7152, 0.0722)) > 0.3) {
        // Marble: a turbulent sine through the stone, darker veins of its own hue.
        float angle = 6.2831853 * hash01(id + 3u);
        float turbulence = stoneFbm(p * 2.2 + vec3(shade * 17.0), 0x51A7u + id);
        float phase = dot(f, vec2(cos(angle), sin(angle))) * 4.0 + 5.0 * turbulence + 6.2831853 * shade;
        float vein = 1.0 - smoothstep(0.0, 0.14, abs(sin(phase)));
        color = mix(albedo, albedo * vec3(0.45, 0.47, 0.5), 0.7 * vein);
        roughness *= 1.0 + 1.2 * vein;
    } else {
        // Basalt: fine dark grain with a few glinting crystals.
        float grain = stoneNoise(p * 23.0, 0x2E3Du + id);
        color = albedo * (0.75 + 0.5 * grain);
        if (stoneNoise(p * 47.0, 0x4C5Bu) > 0.86) {
            color = color * 1.5 + vec3(0.01);
            roughness *= 0.6;
        }
    }
    // Wear: the polish is duller in broad patches.
    roughness *= 0.7 + 0.8 * stoneFbm(p * 0.21, 0x9D1Fu);
    // Set by hand: each tessera tilted a little (about a degree), a fragmented mirror.
    vec3 axis = abs(h.normal.y) < 0.9 ? vec3(0.0, 1.0, 0.0) : vec3(1.0, 0.0, 0.0);
    vec3 t1 = normalize(cross(axis, h.normal)), t2 = cross(h.normal, t1);
    vec2 tilt = vec2(hash01(id + 11u), hash01(id + 13u)) * 2.0 - 1.0;
    vec3 n = normalize(h.normal + 0.018 * (tilt.x * t1 + tilt.y * t2));
    if ((dot(n, gl_WorldRayDirectionEXT) < 0.0) == (dot(h.geometricNormal, gl_WorldRayDirectionEXT) < 0.0)) {
        h.normal = n;
    }
    // The mortar in the joints.
    float edge = min(min(f.x, 1.0 - f.x), min(f.y, 1.0 - f.y));
    if (edge < 0.03) {
        color = vec3(0.07, 0.068, 0.064) * (0.8 + 0.4 * stoneNoise(p * 31.0, 0x1234u));
        roughness = 0.85;
        h.material.metallic = 0.0;
        h.normal = h.geometricNormal;
    }
    // Never a delta lobe: the mosaic is a rough vertex for the integrator and a caustic receiver's Lambert.
    roughness = clamp(roughness, 0.03, 1.0);
}

// Tesserae's coordinates: the world xz plane in tesserae (u along +x, v along +z), moved by baseColor2.xy
// tesserae. TesseraeSphere's: the
// sphere about the instance's origin, mapped along great circles from the point straight above its centre
// (azimuthal equidistant: square tesserae near the top, continuous everywhere but the point straight below).
vec2 tesseraCoordinates(Material m, vec3 p) {
    float size = max(m.patternScale, 1e-4);
    if (m.pattern == kPatternTesseraeSphere) {
        vec3 d = p - gl_ObjectToWorldEXT[3];
        float r = length(d);
        float theta = acos(clamp(d.y / max(r, 1e-6), -1.0, 1.0));
        float across = length(d.xz);
        vec2 heading = across > 0.0 ? d.xz / across : vec2(1.0, 0.0);
        return heading * (r * theta / size) + m.baseColor2.xy;
    }
    return p.xz / size + m.baseColor2.xy;
}

void main() {
    HitPoint h = hitPoint();
    Material m = h.material;
    if (m.pattern == kPatternTesserae || m.pattern == kPatternTesseraeSphere) {
        float roughness = m.roughness;
        vec3 color;
        tessera(tesseraCoordinates(m, h.position), h.position / max(m.patternScale, 1e-4), h, color, roughness);
        writeSurface(h, color, roughness, m.transmission, materialEmission(m));
        return;
    }
    writeSurface(h, checkerColor(m, h.position, h.geometricNormal), m.roughness, m.transmission,
                 materialEmission(m));
}
