#version 460
#extension GL_GOOGLE_include_directive : require

// Hit group 0 (MaterialClass Pbr): plastic, metal and clear-coated surfaces, with the WoodPlanks,
// BrushedMetal, Concrete, Hull, Planet and WORMHOLE's patterns and the City's facades and streets
// (city_patterns.glsl).
// Evaluates the material into the payload; never traces.

#include "hit.glsl"
#include "city_patterns.glsl"

// Smooth 3D value noise in [0, 1] (integer hashes of the lattice corners).
float valueNoise3(vec3 p, uint seed) {
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

// Polished concrete in world space (Task 14 of step 5): cloudy tones between baseColor and baseColor2 at
// three scales, a fine aggregate, roughness varying with the polish and, when patternScale > 0, saw-cut
// joints every patternScale in the surface's plane (the plane across ng's largest axis).
vec3 concrete(Material m, vec3 p, vec3 ng, inout float roughness) {
    float cloud = 0.55 * valueNoise3(p * 1.3, 0x1F2Eu) + 0.3 * valueNoise3(p * 5.1, 0x2E3Du) +
                  0.15 * valueNoise3(p * 19.0, 0x3D4Cu);
    float t = clamp((cloud - 0.5) * 2.2 + 0.5, 0.0, 1.0);
    vec3 color = mix(m.baseColor, m.baseColor2, t);
    color *= 0.93 + 0.14 * valueNoise3(p * 70.0, 0x4C5Bu); // the aggregate
    roughness = clamp(roughness * (0.7 + 0.6 * valueNoise3(p * 2.3, 0x5B6Au)), 0.0, 1.0);
    if (m.patternScale > 0.0) {
        vec3 a = abs(ng);
        vec2 plane = a.y >= a.x && a.y >= a.z ? p.xz : (a.x >= a.z ? p.zy : p.xy);
        vec2 q = plane / m.patternScale + 0.31;
        vec2 edge = min(fract(q), 1.0 - fract(q)) * m.patternScale;
        if (min(edge.x, edge.y) < 0.0025) {
            color *= 0.3;
            roughness = 1.0;
        }
    }
    return color;
}

// --- The Ship's patterns (step 5, Task 16) -----------------------------------------------------------------

// The width a ray's footprint covers on the surface here (metres): procedural lines thinner than that are
// spread over it with their energy kept (filtered), so they neither alias nor sparkle. After a rough vertex
// (no cone) a coarse 5 cm.
float patternFootprint(HitPoint h) {
    if (payload.coneSpread < 0.0) {
        return 0.05;
    }
    float width = abs(payload.coneWidth + payload.coneSpread * gl_HitTEXT);
    float cosine = max(abs(dot(gl_WorldRayDirectionEXT, h.geometricNormal)), 0.2);
    return width / cosine;
}

// The coverage of a line `width` wide at `distance` from its centre line, seen through a footprint: 1 on a
// line wider than the footprint, else width / footprint over the footprint's width (the same energy).
float filteredLine(float distance, float width, float footprint) {
    float spread = max(width, footprint);
    return distance < 0.5 * spread ? width / spread : 0.0;
}

// Titanium's oxide colours over a heat from 0 (bare) to 1 (hottest): straw, bronze, purple, then blue.
vec3 heatTint(vec3 bare, float heat) {
    const vec3 straw = vec3(0.78, 0.6, 0.3), bronze = vec3(0.6, 0.36, 0.18);
    const vec3 purple = vec3(0.4, 0.24, 0.5), blue = vec3(0.22, 0.36, 0.72);
    float x = clamp(heat, 0.0, 1.0) * 4.0;
    if (x < 1.0) {
        return mix(bare, straw, x);
    }
    if (x < 2.0) {
        return mix(straw, bronze, x - 1.0);
    }
    if (x < 3.0) {
        return mix(bronze, purple, x - 2.0);
    }
    return mix(purple, blue, x - 3.0);
}

// Brushing along u: grooves 0.4 mm apart across v, each a shallow round-bottomed scratch of its own depth,
// tilt the shading normal across the brushing direction by up to `slope`. Over a pixel the tilts make an
// anisotropic microfacet distribution: a highlight drawn out across the brush lines, as on brushed metal. The
// tilt is dropped where it would put the viewer behind the surface.
void brushedGrooves(inout HitPoint h, float slope) {
    vec3 n = h.normal;
    vec3 t = h.dPdu - n * dot(n, h.dPdu);
    float tl = length(t);
    if (!(tl > 1e-6)) {
        return;
    }
    vec3 b = cross(n, t / tl);
    float g = h.uv.y * 2500.0;
    float gi = floor(g);
    float depth = 0.45 + hash01(uint(int(gi)) * 0x9E3779B1u + 0x5151u);
    vec3 tilted = normalize(n + b * (slope * depth * (2.0 * (g - gi) - 1.0)));
    vec3 dir = gl_WorldRayDirectionEXT;
    if ((dot(tilted, dir) < 0.0) == (dot(h.geometricNormal, dir) < 0.0)) {
        h.normal = tilted;
    }
}

// The hull (Pattern Hull): panels laid out in the surface's uv, which is in metres with u along the flow
// (the brushing direction). Every panel has its own tone and roughness, and a seam (dark and rough) 1 % of
// patternScale wide runs between panels.
// - Metal: skin panels patternScale long and 0.55 patternScale wide, alternate rows staggered by half a
//   panel, brushed along u (brushedGrooves); baseColor2.x > 0 adds titanium's heat tint, baseColor2.x at
//   u = 0 falling off over baseColor2.y metres along u.
// - Otherwise: ceramic tiles patternScale square with scuffs of baseColor2; the material's emission shows
//   only in glowing seams 2 % of patternScale wide (every third row line along the flow, and, dimmer, the
//   cross seams of the rows between, every fourth tile), filtered over the footprint.
vec3 hullPattern(Material m, inout HitPoint h, inout float roughness, inout vec3 emission) {
    bool metal = m.metallic > 0.5;
    float panel = max(m.patternScale, 1e-3);
    vec2 size = metal ? vec2(panel, 0.55 * panel) : vec2(panel);
    vec2 q = h.uv / size;
    float row = floor(q.y);
    if (metal) {
        q.x += 0.5 * row;
    }
    vec2 cell = floor(q);
    vec2 f = q - cell;
    uint id = uint(int(cell.x)) * 0x8DA6B343u + uint(int(cell.y)) * 0xD8163841u + (metal ? 0x3C6Fu : 0x7A11u);
    float tone = hash01(id), grain = hash01(id + 0x2545u);
    vec2 edge = min(f, 1.0 - f) * size; // metres to the nearest seam across (x) and along (y) the flow
    float footprint = patternFootprint(h);
    float seamWidth = 0.01 * panel;
    float seam = max(filteredLine(edge.x, seamWidth, footprint), filteredLine(edge.y, seamWidth, footprint));
    vec3 color;
    if (metal) {
        color = m.baseColor * (0.9 + 0.16 * tone);
        roughness = clamp(roughness * (0.8 + 0.4 * grain), 0.0, 1.0);
        if (m.baseColor2.x > 0.0) {
            color = heatTint(color, m.baseColor2.x * exp(-max(h.uv.x, 0.0) / max(m.baseColor2.y, 1e-3)));
        }
        brushedGrooves(h, 0.07); // shallow enough that 16 samples resolve the streak without grain
        emission = vec3(0.0);
    } else {
        // Tiles: tone, roughness, and scuffs (soft value noise across each tile) toward baseColor2.
        float scuff = valueNoise(h.uv.x * 7.0 + 31.0 * tone, id + 0x77u) * valueNoise(h.uv.y * 9.0, id + 0x99u);
        color = mix(m.baseColor * (0.72 + 0.56 * tone), m.baseColor2, smoothstep(0.35, 0.8, scuff));
        roughness = clamp(roughness * (0.75 + 0.5 * grain), 0.0, 1.0);
        float lineRow = f.y < 0.5 ? cell.y : cell.y + 1.0;
        float lineColumn = f.x < 0.5 ? cell.x : cell.x + 1.0;
        bool rowGlows = mod(lineRow, 3.0) == 0.0;
        bool crossGlows = mod(lineColumn, 4.0) == 0.0 && mod(cell.y, 3.0) == 1.0;
        float glow = max(rowGlows ? filteredLine(edge.y, 2.0 * seamWidth, footprint) : 0.0,
                         crossGlows ? 0.35 * filteredLine(edge.x, 2.0 * seamWidth, footprint) : 0.0);
        emission *= glow;
    }
    color = mix(color, color * 0.15, seam);
    roughness = mix(roughness, 0.9, seam);
    return color;
}

// Fractal value noise, `octaves` of them, each twice the frequency and half the weight; about [0, 1].
float fbm3(vec3 p, uint seed, int octaves) {
    float sum = 0.0, weight = 0.5, total = 0.0;
    for (int i = 0; i < octaves; ++i) {
        sum += weight * valueNoise3(p, seed + uint(i) * 0x1F123BB5u);
        total += weight;
        p = p * 2.03 + vec3(17.1, 5.3, 11.7);
        weight *= 0.5;
    }
    return sum / total;
}

// A planet's ocean under clouds (Pattern Planet), on an analytic sphere: the point's direction from the
// sphere's uv, clouds from domain-warped fractal noise at patternScale cells per radian, baseColor the ocean
// (two depths), baseColor2 the clouds (rough; the ocean keeps the material's roughness, so the sun glitters on
// it). The emission is the air in front of the surface (a thin atmosphere's single scattering, plane
// parallel): emission * (1 - exp(-0.08 / mu)), mu the cosine to the ray, so the air thickens to the limb.
vec3 planetPattern(Material m, HitPoint h, inout float roughness, inout vec3 emission) {
    float theta = h.uv.y * 3.14159265, phi = h.uv.x * 6.28318531;
    vec3 d = vec3(sin(theta) * cos(phi), cos(theta), -sin(theta) * sin(phi));
    vec3 p = d * m.patternScale;
    vec3 warp = vec3(fbm3(p * 0.6, 0x11u, 3), fbm3(p * 0.6 + 7.3, 0x22u, 3), fbm3(p * 0.6 + 13.9, 0x33u, 3));
    float cloud = fbm3(p + 2.4 * (warp - 0.5), 0x44u, 5);
    float cover = smoothstep(0.5, 0.64, cloud);
    float haze = 0.4 * smoothstep(0.42, 0.52, cloud);
    float depth = fbm3(p * 0.35 + 3.1, 0x55u, 3);
    vec3 ocean = m.baseColor * mix(0.6, 1.6, depth);
    float c = max(cover, haze);
    vec3 color = mix(ocean, m.baseColor2 * (0.85 + 0.3 * cloud), c);
    roughness = mix(roughness, 1.0, c);
    float mu = max(abs(dot(h.geometricNormal, gl_WorldRayDirectionEXT)), 0.02);
    emission *= 1.0 - exp(-0.08 / mu);
    return color;
}

// --- WORMHOLE (step 6): the tunnel's wall and its rings ----------------------------------------------------

// Fractal value noise whose octaves stop where they would be finer than `footprint` (in the units of `cell`,
// the first octave's size): each octave fades out over the octave above the footprint, so a far wall is its
// smooth mean instead of sparkle. About [0, 1].
float filteredFbm3(vec3 p, uint seed, int octaves, float cell, float footprint) {
    float sum = 0.0, weight = 0.5, total = 0.0;
    for (int i = 0; i < octaves; ++i) {
        float keep = clamp(log2(cell / max(footprint, 1e-6)) - 1.0, 0.0, 1.0);
        float n = keep > 0.0 ? valueNoise3(p, seed + uint(i) * 0x1F123BB5u) : 0.5;
        sum += weight * mix(0.5, n, keep);
        total += weight;
        p = p * 2.03 + vec3(17.1, 5.3, 11.7);
        weight *= 0.5;
        cell *= 0.4926;
    }
    return sum / total;
}

// The mouth's length in metres (modern::kWormholeMouthLength): the wall's v runs from -this at its rim.
const float kWormholeMouth = 900.0;

// The wormhole's wall (Pattern WormholeWall): the inside of a tube of radius patternScale, uv in metres (u
// round the tube, v along it from where the mesh starts; coatRoughness, unused by the wall, is how far that is
// from the gate, so that v + coatRoughness is the distance from the gate however the mesh is moved on). Gas spiralling once round every 30 km and streaming on towards
// the exit at 260 m/s: a glacial-blue glow (the emission) in folds of domain-warped noise drawn out along the
// flow, thin gold filaments (baseColor2 times the emission scale) along its brightest ridges, and dark lanes
// of basalt dust (baseColor) between. In the mouth (v < 0) the glow fades out towards the rim.
vec3 wormholeWall(Material m, HitPoint h, inout float roughness, inout vec3 emission) {
    float radius = max(m.patternScale, 1.0);
    float seconds = frame.seconds + frame.shutterOffset;
    float v = h.uv.y + m.coatRoughness;
    float twist = h.uv.x / radius + v * (6.2831853 / 30000.0);
    float along = (v - 260.0 * seconds) / 520.0;
    // On a cylinder 6 noise cells in radius: a cell is radius / 6 metres round and 520 m along.
    vec3 p = vec3(6.0 * cos(twist), 6.0 * sin(twist), along);
    float cell = radius / 6.0;
    roughness = 0.65;
    if (payload.coneSpread < 0.0) {
        // After a rough vertex the wall is only light on the hull: its mean glow, so that glossy metal that
        // finds it by chance does not sparkle.
        emission = (0.12 * emission + 0.02 * m.emissionScale * m.baseColor2) *
                   smoothstep(-0.95 * kWormholeMouth, -0.2 * kWormholeMouth, h.uv.y + m.coatRoughness);
        return m.baseColor;
    }
    float footprint = patternFootprint(h);
    vec3 warp = vec3(filteredFbm3(p * 0.5, 0x5A1u, 2, 2.0 * cell, footprint),
                     filteredFbm3(p * 0.5 + 9.7, 0x5A2u, 2, 2.0 * cell, footprint),
                     filteredFbm3(p * 0.5 + 3.3, 0x5A3u, 2, 2.0 * cell, footprint));
    vec3 q = p + 2.2 * (warp - 0.5);
    float gas = filteredFbm3(q, 0x5A4u, 4, cell, footprint);
    float ridge = 1.0 - abs(2.0 * filteredFbm3(q * 1.9 + 4.1, 0x5A5u, 3, 0.53 * cell, footprint) - 1.0);
    float glow = smoothstep(0.4, 0.8, gas);
    float filament = pow(ridge, 16.0) * smoothstep(0.5, 0.68, gas);
    float lane = smoothstep(0.3, 0.5, gas); // the dust lanes: dark where the gas thins
    float mouth = smoothstep(-0.95 * kWormholeMouth, -0.2 * kWormholeMouth, v);
    vec3 blue = emission * (0.03 + 1.4 * glow * glow * glow) * lane;
    vec3 gold = m.baseColor2 * (1.4 * m.emissionScale * filament);
    emission = (blue + gold) * mouth;
    return m.baseColor * (0.6 + 0.8 * gas);
}

// A ring's basalt (Pattern WormholeRing): a torus, u in metres round the ring and v round its tube (0 on its
// outer equator, pi * tube on its inner face). Blocks patternScale long with dark joints between them, a fine
// strata grain, and the material's emission only in gold inlays running round the inner face either side of
// its lamps (lines along the ring, filtered over the footprint; no radial ticks).
vec3 wormholeRing(Material m, HitPoint h, inout float roughness, inout vec3 emission) {
    float block = max(m.patternScale, 1.0);
    const float tube = 10.0; // modern::kWormholeRingTube
    float b = h.uv.y / tube; // the angle round the tube
    float x = h.uv.x / block;
    uint id = uint(int(floor(x))) * 0x8DA6B343u + 0x2B07u;
    float tone = hash01(id);
    float joint = min(fract(x), 1.0 - fract(x)) * block;
    float footprint = patternFootprint(h);
    float seam = filteredLine(joint, 0.12, footprint);
    float strata = valueNoise3(vec3(h.uv.x * 0.08, h.uv.y * 1.7, tone * 31.0), 0x2B11u);
    vec3 color = m.baseColor * (0.75 + 0.5 * tone) * (0.85 + 0.3 * strata);
    color = mix(color, color * 0.25, seam);
    roughness = mix(clamp(roughness * (0.8 + 0.4 * strata), 0.0, 1.0), 0.9, seam);
    float inner = abs(b - 3.14159265); // 0 on the inner face
    float inlay = max(filteredLine(abs(inner - 0.5) * tube, 0.3, footprint),
                      0.4 * filteredLine(abs(inner - 0.75) * tube, 0.12, footprint));
    emission *= inlay;
    return color;
}

void main() {
    HitPoint h = hitPoint();
    Material m = h.material;
    vec3 baseColor = m.baseColor;
    float roughness = m.roughness;
    vec3 emission = materialEmission(m);
    if (m.pattern == kPatternWoodPlanks) {
        baseColor = woodPlanks(m, h.position);
    } else if (m.pattern == kPatternBrushedMetal) {
        baseColor = brushedMetal(m, h.position, roughness);
    } else if (m.pattern == kPatternConcrete) {
        baseColor = concrete(m, h.position, h.geometricNormal, roughness);
    } else if (m.pattern >= kPatternCityPunched && m.pattern <= kPatternCityRound) {
        cityFacade(h, m.pattern, baseColor, roughness, emission); // Task 23
    } else if (m.pattern == kPatternCityStreet) {
        cityStreet(h, baseColor, roughness);
    } else if (m.pattern == kPatternHull) {
        baseColor = hullPattern(m, h, roughness, emission);
    } else if (m.pattern == kPatternPlanet) {
        baseColor = planetPattern(m, h, roughness, emission);
    } else if (m.pattern == kPatternWormholeWall) {
        baseColor = wormholeWall(m, h, roughness, emission);
    } else if (m.pattern == kPatternWormholeRing) {
        baseColor = wormholeRing(m, h, roughness, emission);
    }
    writeSurface(h, baseColor, roughness, m.transmission, emission);
}
