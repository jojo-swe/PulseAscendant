#ifndef EVO_RT_DETAIL_PATTERNS_GLSL
#define EVO_RT_DETAIL_PATTERNS_GLSL

// Patterns 32..35 (rt::Pattern SeaBead, LightRing, CrtGlass, KeyCap): small surface details of the Particles
// scene and the Studio, applied in pbr.rchit and emissive.rchit before the surface is written. Each is a pure
// function of the hit (and, for the sea, of the frame's time).

#include "sea_waves.glsl"

// A hash of a 32-bit integer to [0, 1).
float detailUnit(uint x) {
    x ^= x >> 16;
    x *= 0x7FEB352Du;
    x ^= x >> 15;
    x *= 0x846CA68Bu;
    x ^= x >> 16;
    return float(x >> 8) * (1.0 / 16777216.0);
}

// --- SeaBead (Pbr spheres): PARTICLE OCEAN's beads ---------------------------------------------------------
// A bead is a little liquid-metal mirror riding the swell: its shading normal is pulled onto the swell's
// normal under it (seaNormal at the frame's time, the wave scale in patternScale), squashed more along the
// swell's slope than across it, so the moon glints off the beads whose wave faces it (the glitter path) instead
// of off every bead's own highlight. Each bead's roughness is the material's times 0.4 .. 1.6 (hashed).
void seaBead(inout HitPoint h, Material m, inout float roughness) {
    float t = frame.seconds + frame.shutterOffset;
    vec3 wave = seaNormal(h.position.xz, t, m.patternScale);
    vec3 n = h.normal;
    float along = dot(n, wave);
    if (along > 0.0) {
        vec3 slope = wave - vec3(0.0, 1.0, 0.0);
        vec3 tangent = dot(slope, slope) > 1e-8 ? normalize(slope - dot(slope, wave) * wave)
                                                : normalize(cross(wave, vec3(0.0, 0.0, 1.0)));
        vec3 bitangent = cross(wave, tangent);
        vec3 squashed = along * wave + 0.16 * dot(n, tangent) * tangent + 0.42 * dot(n, bitangent) * bitangent;
        n = normalize(squashed);
        // Never turn the shading normal away from the viewer's side of the bead.
        vec3 dir = gl_WorldRayDirectionEXT;
        if ((dot(n, dir) < 0.0) == (dot(h.geometricNormal, dir) < 0.0)) {
            h.normal = n;
        }
    }
    roughness *= 0.4 + 1.2 * detailUnit(h.primitive * 0x9E3779B9u + 0x51u);
}

// --- LightRing (Pbr): DOTS' floor and the landing's ring of light ------------------------------------------
// A ring of light about the world's y axis, radius patternScale (none at or below 0), its radiance baseColor2
// at the crest; it is 6 cm plus an eighth of its radius wide (a Gaussian), so it softens as it spreads.
vec3 lightRing(HitPoint h, Material m) {
    if (m.patternScale <= 0.0) {
        return vec3(0.0);
    }
    float r = length(h.position.xz);
    float width = 0.06 + 0.125 * m.patternScale;
    float x = (r - m.patternScale) / width;
    return m.baseColor2 * exp(-x * x);
}

// --- CrtGlass (Emissive, the retro texture): the Studio's CRT ----------------------------------------------
// The tube's face is not clean: dust specks on the glass (2 % of 1.6 mm cells hold one, 0.12 .. 0.4 mm
// across: matte grey fluff that catches the lamp and dims the picture under it), and a fingerprint low on the
// right (an oval smudge of ridges 0.45 mm apart, where the coat is oily and rough). In the screen's uv (0 .. 1
// across its 0.28 x 0.21 m, v up).
const vec2 kCrtSize = vec2(0.28, 0.21);

void crtGlass(HitPoint h, inout Material m, inout vec3 baseColor, inout vec3 emission) {
    vec2 p = h.uv * kCrtSize; // metres
    // The fingerprint: an oval 18 x 24 mm, tilted, centred low right.
    vec2 c = p - vec2(0.226, 0.046);
    float angle = 0.5;
    vec2 q = vec2(cos(angle) * c.x + sin(angle) * c.y, -sin(angle) * c.x + cos(angle) * c.y);
    q /= vec2(0.009, 0.012);
    float oval = dot(q, q);
    float smudge = 1.0 - smoothstep(0.55, 1.0, oval);
    if (smudge > 0.0) {
        // Ridges: loops about a core a little off the oval's centre, wavy.
        vec2 core = q - vec2(0.12, -0.2);
        float rr = length(core * vec2(1.0, 0.8)) * 0.0105;
        float ridge = 0.5 + 0.5 * sin(6.2831853 * rr / 0.00045 + 2.0 * sin(5.0 * atan(core.y, core.x)));
        float film = smudge * (0.45 + 0.55 * ridge);
        m.coatRoughness = mix(m.coatRoughness, 0.3, film);
        baseColor = mix(baseColor, vec3(0.1, 0.105, 0.1), 0.6 * film);
    }
    // The dust.
    const float cell = 0.0016;
    vec2 g = floor(p / cell);
    uint key = uint(int(g.x) + 4096) * 0x8DA6B343u ^ uint(int(g.y) + 4096) * 0xD8163841u;
    if (detailUnit(key) < 0.02) {
        vec2 centre = (g + vec2(0.2, 0.2) + 0.6 * vec2(detailUnit(key + 1u), detailUnit(key + 2u))) * cell;
        float radius = 0.00006 + 0.00014 * detailUnit(key + 3u);
        float speck = 1.0 - smoothstep(0.6 * radius, radius, length(p - centre));
        if (speck > 0.0) {
            baseColor = mix(baseColor, vec3(0.42, 0.41, 0.39), speck);
            m.roughness = mix(m.roughness, 1.0, speck);
            m.coat *= 1.0 - speck;
            emission *= 1.0 - 0.6 * speck;
        }
    }
}

// --- KeyCap (Pbr): the Studio's key caps ------------------------------------------------------------------
// Thirty years of sun: the ABS is pale in the middle of a cap's top and yellowed towards its edges and down
// its sides. Measured in metres from the cap's edge in the cap mesh's frame (a bevelled box of half size
// kKeyCapHalf; wide keys stretch it along x).
const vec3 kKeyCapHalf = vec3(0.0083, 0.0045, 0.0083);

vec3 keyCap(HitPoint h, vec3 baseColor) {
    vec3 local = gl_WorldToObjectEXT * vec4(h.position, 1.0);
    mat4x3 o = gl_ObjectToWorldEXT;
    vec2 stretch = vec2(length(o[0]), length(o[2]));
    vec2 edge = (kKeyCapHalf.xz - abs(local.xz)) * stretch; // metres to the nearest side
    float inside = min(edge.x, edge.y);
    float top = smoothstep(kKeyCapHalf.y - 0.0012, kKeyCapHalf.y - 0.0002, local.y);
    float pale = top * smoothstep(0.0008, 0.0042, inside);
    vec3 yellowed = baseColor * vec3(0.9, 0.82, 0.62);
    return mix(yellowed, baseColor * vec3(1.06, 1.05, 1.02), pale);
}

#endif
