#ifndef EVO_RT_CITY_PATTERNS_GLSL
#define EVO_RT_CITY_PATTERNS_GLSL

// The City's procedural surfaces (step 5, Task 23; src/modern/scenes/city.hpp): the facades of its towers with
// millions of windows at no geometry cost, its wet streets, and the light trails of its traffic. Included by
// pbr.rchit (facades and streets) and emissive.rchit (trails) after hit.glsl. Everything is a pure function of
// the hit point, the instance's placement and the frame's time (frame.seconds + frame.shutterOffset), with
// integer hashes only, so a window blinks the same way however the show got to that time.
//
// Facades (kPatternCityPunched, kPatternCityCurtain, kPatternCityBands on boxes; kPatternCityRound on a
// cylinder): a building is a unit box (or a cylinder of radius 0.5, height 1) placed by its instance. Its walls
// are cut into bays across (whole bays, about a floor wide, fitted to the wall's width) and floors up
// (patternScale high, counted from the ground in world y, so a set-back tier continues its base's floors, with
// a taller shop floor at street level; the material's coat, unused by a facade, switches on that share of the
// dark windows). A cell holds a window whose share of the cell depends on the style:
// punched windows in masonry, a glass curtain wall with metal mullions and spandrels, or ribbon bands. The
// material's baseColor is the wall (spandrel, mullion) colour, baseColor2 the glass, emission *
// emissionScale a lit room's radiance (each room tints it). The building's own seed is its instance's
// translation, so no two towers light the same windows.
//
// A window is glass (a smooth coat over the dark baseColor2) in front of a room: lit or dark by its own hash
// and a slow clock (each window keeps its state for tens of seconds, then may switch), with a room seen in
// parallax behind the glass (interior mapping: the view ray continued into a box one bay wide, one floor high
// and most of a bay deep; its ceiling, walls, floor and back wall each lit by the room's fixture), blinds or
// curtains in some, and the cold flicker of a television in a few. The roof (the box's top) is gravel and
// tar.
//
// Streets (kPatternCityStreet, world xz): the asphalt between the lots of the grid (streets kCityStreet wide
// every kCityCell, the 1993 city's; with patternScale > 0 an avenue along z with no junctions), with lane
// dashes, a double centre line, zebra crossings before every junction, manhole covers and patched repairs.
// Wet asphalt is a water film (the coat, smooth) over a darker rougher base: `coat` is the street's wetness
// (0 dry, 1 after rain), which fills puddles from a noise field and the wheel ruts first, and `coatRoughness`
// the ripples on the standing water.
//
// Trails (kPatternCityTrail, Emissive): a car's light seen through a long exposure, a stretched box whose
// emission fades from its head (uv.x = 1) to its tail (uv.x = 0).
//
// Signs (kPatternCitySign, Emissive, on a rect light's quad): lettering, as the camera and its mirror chains
// see it; every other ray sees the uniform mean, which is the light's emission, so next-event estimation and
// the rays that find the sign agree.

// The 1993 city's grid (retro/city_plan.hpp: kCityCell, kCityStreet), mirrored.
const float kCityCellSize = 42.0;
const float kCityStreetWidth = 12.0;
// The shop floor's height at street level.
const float kCityShopFloor = 4.6;

uint cityHash(uint a, uint b) {
    return pcgHash(a ^ pcgHash(b + 0x9E3779B9u));
}

float cityUnit(uint h) {
    return float(h >> 8u) * (1.0 / 16777216.0);
}

// Draw k of hash h: a number in [0, 1) independent of h's other draws (multiplying h instead would put
// several draws of one hash on a lattice).
float cityDraw(uint h, uint k) {
    return cityUnit(pcgHash(h + k * 0x9E3779B9u));
}

// Smooth 2D value noise in [0, 1].
float cityNoise2(vec2 p, uint seed) {
    vec2 i = floor(p);
    vec2 f = p - i;
    vec2 s = f * f * (3.0 - 2.0 * f);
    uvec2 c = uvec2(ivec2(i));
    uint base = c.x * 0x8DA6B343u + c.y * 0xD8163841u + seed;
    float a = cityUnit(pcgHash(base));
    float b = cityUnit(pcgHash(base + 0x8DA6B343u));
    float d = cityUnit(pcgHash(base + 0xD8163841u));
    float e = cityUnit(pcgHash(base + 0x8DA6B343u + 0xD8163841u));
    return mix(mix(a, b, s.x), mix(d, e, s.x), s.y);
}

float cityFbm2(vec2 p, uint seed) {
    return 0.5 * cityNoise2(p, seed) + 0.3 * cityNoise2(p * 2.07 + 13.1, seed + 1u) +
           0.2 * cityNoise2(p * 4.31 + 7.7, seed + 2u);
}

// The frame's time at this trace slice.
float citySeconds() {
    return frame.seconds + frame.shutterOffset;
}

// --- Facades ---------------------------------------------------------------------------------------------

// The building's seed: its instance's translation (exact floats, so the same placement always gives the same
// seed).
uint cityBuildingSeed() {
    vec3 t = gl_ObjectToWorldEXT[3];
    uvec3 b = floatBitsToUint(t);
    return pcgHash(b.x ^ pcgHash(b.y ^ pcgHash(b.z + 0x632BE5ABu)));
}

// A lit room's colour around the material's tint: warm incandescent, neutral, cool office light, a greenish
// fluorescent; from the room's hash.
vec3 cityRoomTint(uint h, vec3 tint) {
    float u = cityUnit(h);
    vec3 k = u < 0.5    ? vec3(1.0, 0.72, 0.45)
             : u < 0.84 ? vec3(1.0, 0.88, 0.72)
             : u < 0.96 ? vec3(0.82, 0.9, 1.0) * 0.8
                        : vec3(0.85, 1.0, 0.86) * 0.8;
    return tint * k;
}

// The room behind a window, seen along the view ray (interior mapping): `cell` is the hit's position in the
// window's cell (0..1 across the bay, 0..1 up the floor), `dir` the ray's direction in the wall's frame (x
// across, y up, z into the building), `size` the room's width, height and depth in metres. Returns the
// relative radiance of what the ray meets: the ceiling around its fixture brightest, the walls, the floor
// darkest, and furniture against the back wall in some rooms.
float cityRoom(vec2 cell, vec3 dir, vec3 size, uint h) {
    vec3 p = vec3(cell * size.xy, 0.0);
    vec3 d = dir;
    float tx = d.x > 0.0 ? (size.x - p.x) / d.x : (d.x < 0.0 ? -p.x / d.x : 1e9);
    float ty = d.y > 0.0 ? (size.y - p.y) / d.y : (d.y < 0.0 ? -p.y / d.y : 1e9);
    float tz = d.z > 1e-4 ? size.z / d.z : 1e9;
    float t = min(tx, min(ty, tz));
    vec3 q = p + d * t;
    if (t == ty && d.y > 0.0) {
        // The ceiling: a fixture over the room's middle.
        vec2 c = (q.xz - 0.5 * size.xz) / (0.35 * size.xz);
        return 0.7 + 1.6 * exp(-2.0 * dot(c, c));
    }
    if (t == ty) {
        return 0.22 + 0.1 * cityDraw(h, 7u); // the floor
    }
    float depth = q.z / size.z;
    if (t == tz) {
        // The back wall, with a dark piece of furniture (a shelf, a sofa) in some rooms.
        float wall = 0.55;
        uint f = h * 13u + 5u;
        if (cityUnit(f) < 0.55) {
            float x0 = 0.15 + 0.4 * cityDraw(f, 3u), w = 0.25 + 0.35 * cityDraw(f, 5u);
            float top = 0.25 + 0.45 * cityDraw(f, 11u);
            float u = q.x / size.x, v = q.y / size.y;
            if (u > x0 && u < x0 + w && v < top) {
                wall = 0.12;
            }
        }
        return wall;
    }
    return 0.42 - 0.12 * depth; // a side wall, darker towards the back
}

// Evaluates a facade at the hit: fills baseColor, roughness and the emission, and sets the hit material's
// metallic and coat. The box's top (or the cylinder's cap) is the roof.
void cityFacade(inout HitPoint h, uint style, out vec3 baseColor, out float roughness, out vec3 emission) {
    Material m = h.material;
    baseColor = m.baseColor;
    roughness = m.roughness;
    emission = vec3(0.0);
    float seconds = citySeconds();
    uint building = cityBuildingSeed();

    vec3 o = gl_WorldToObjectEXT * vec4(h.position, 1.0);
    vec3 nObj = (h.geometricNormal * gl_ObjectToWorldEXT).xyz;
    vec3 an = abs(nObj);
    vec3 scaleWorld = vec3(length(gl_ObjectToWorldEXT[0]), length(gl_ObjectToWorldEXT[1]),
                           length(gl_ObjectToWorldEXT[2]));
    bool isRound = style == kPatternCityRound;
    bool roof = isRound ? an.y > 0.5 * length(nObj) : (an.y >= an.x && an.y >= an.z);
    if (roof) {
        // Gravel and tar, with darker patches and pale flashing near the parapet.
        vec2 q = h.position.xz;
        float g = cityFbm2(q * 0.35, building) * 0.6 + 0.4 * cityNoise2(q * 9.0, building + 3u);
        baseColor = vec3(0.16, 0.155, 0.15) * (0.55 + 0.7 * g);
        roughness = 0.92;
        h.material.metallic = 0.0;
        h.material.coat = 0.0;
        return;
    }

    // The wall's own frame: u across in metres (from the wall's left edge), its width, and the direction
    // into the building.
    float u, width;
    vec3 across;
    if (isRound) {
        float radius = 0.5 * scaleWorld.x;
        float a = atan(o.z, o.x);
        width = 6.2831853 * radius;
        u = (a + 3.14159265) * radius;
        across = normalize(vec3(-sin(a), 0.0, cos(a)));
    } else if (an.x >= an.z) {
        width = scaleWorld.z;
        u = (o.z + 0.5) * width;
        across = normalize(gl_ObjectToWorldEXT[2]);
    } else {
        width = scaleWorld.x;
        u = (o.x + 0.5) * width;
        across = normalize(gl_ObjectToWorldEXT[0]);
    }
    uint face = uint(an.x >= an.z ? (nObj.x > 0.0 ? 0 : 1) : (nObj.z > 0.0 ? 2 : 3));
    if (isRound) {
        face = 4u;
    }
    float floorHeight = max(m.patternScale, 2.5);
    float y = h.position.y;
    bool shop = y < kCityShopFloor;
    float storey = shop ? 0.0 : floor((y - kCityShopFloor) / floorHeight) + 1.0;
    float v = shop ? y / kCityShopFloor : fract((y - kCityShopFloor) / floorHeight);
    float cellHeight = shop ? kCityShopFloor : floorHeight;

    // Bays about one floor wide (narrower in glass towers), varied per building, fitted to the wall.
    float bayTarget = floorHeight * (style == kPatternCityPunched ? 0.95 + 0.5 * cityDraw(building, 3u)
                                                                    : 0.55 + 0.35 * cityDraw(building, 3u));
    if (shop) {
        bayTarget = 5.5;
    }
    float bays = max(1.0, floor(width / bayTarget + 0.5));
    float bayWidth = width / bays;
    float bay = clamp(floor(u / bayWidth), 0.0, bays - 1.0);
    float fu = u / bayWidth - bay;
    uint cellId = cityHash(building ^ (face * 0x51ED27u), uint(bay) * 0x1000193u + uint(storey) * 0x9E3779B1u);

    // The window's share of the cell (half extents from its centre) by style.
    vec2 extent;
    float sill;
    if (shop) {
        extent = vec2(0.44, 0.36);
        sill = 0.1;
    } else if (style == kPatternCityPunched) {
        extent = vec2(0.22 + 0.06 * cityDraw(building, 5u), 0.27);
        sill = 0.28;
    } else if (style == kPatternCityBands) {
        extent = vec2(0.5, 0.2 + 0.05 * cityDraw(building, 5u));
        sill = 0.4;
    } else {
        extent = vec2(0.47, 0.34);
        sill = 0.22;
    }
    vec2 centre = vec2(0.5, sill + extent.y);
    vec2 d = abs(vec2(fu, v) - centre) - extent;
    bool window = d.x < 0.0 && d.y < 0.0;
    float windowShare = 4.0 * extent.x * extent.y; // of the cell's area
    bool glassWall = style == kPatternCityCurtain || style == kPatternCityRound;

    // Lit or dark. Homes (punched windows) switch room by room; offices (glass, bands) by runs of bays on a
    // floor, so their floors light up in bands. A slow clock per run keeps a state for 25 to 220 s, then
    // draws again; the building sets how many are lit.
    float litShare = shop ? 0.4 : 0.12 + 0.3 * cityDraw(building, 17u);
    litShare = mix(litShare, 1.0, clamp(m.coat, 0.0, 1.0)); // the material's coat: dark windows switched on
    uint run = cellId;
    if (!(style == kPatternCityPunched || shop)) {
        run = cityHash(building ^ (face * 0x51ED27u), uint(floor(bay / 5.0)) * 0x1000193u + uint(storey) * 0x9E3779B1u);
    }
    float period = 25.0 + 195.0 * cityDraw(run, 3u);
    float epoch = floor((seconds + period * cityDraw(run, 5u)) / period);
    uint state = cityHash(run, uint(int(epoch)) + 0x2545F491u);
    bool lit = cityUnit(state) < litShare;
    uint room = cityHash(cellId, uint(int(epoch)) + 0x68E31DA4u); // this room's own draw
    bool office = !(style == kPatternCityPunched || shop);
    // An office floor shares one light (its tint and level, a little uneven); a home's rooms each their own.
    uint light = office ? state : room;
    vec3 tint = cityRoomTint(light * 7u, materialEmission(m));
    float level = (0.35 + 0.8 * cityDraw(light, 11u)) * (office ? 0.9 + 0.2 * cityDraw(room, 13u) : 1.0);

    // Prefiltering: where a cell is no bigger than the ray cone's footprint (far away, or after a rough
    // bounce), the facade is its average instead of a pattern that would alias into sparkle.
    float footprint = payload.coneSpread < 0.0 ? 1e6 : abs(payload.coneWidth + payload.coneSpread * gl_HitTEXT);
    float facing = max(abs(dot(gl_WorldRayDirectionEXT, h.geometricNormal)), 0.2);
    float cellPixels = min(bayWidth, cellHeight) * facing / max(footprint, 1e-6);
    float detail = smoothstep(1.0, 3.0, cellPixels);

    // The average: wall and glass by their shares, the coat as much as there is glass, and the rooms' light
    // spread over the cell (the building's lit share at a typical level, its tint), held under the true
    // mean: resolved, the lit windows clip in the tonemap and the walls between them stay dark, which a
    // uniform average would not. The wall's share is darkened too (soot, and the sills' and reveals'
    // shadows a distant wall still has): a far facade at night is a dark mass with a warm glow, never grey.
    float glassShare = glassWall ? 1.0 : windowShare;
    vec3 avgColor = mix(m.baseColor * 0.42, m.baseColor2, glassShare);
    float avgRough = mix(m.roughness, 0.3, glassShare);
    vec3 avgEmission = materialEmission(m) * vec3(1.0, 0.82, 0.62) * (litShare * windowShare * 0.5);
    if (detail <= 0.0) {
        baseColor = avgColor;
        roughness = avgRough;
        h.material.metallic = 0.0;
        h.material.coat = glassShare;
        h.material.coatRoughness = 0.02;
        emission = avgEmission;
        return;
    }

    vec3 cellColor;
    float cellRough, cellCoat, cellMetal = 0.0;
    vec3 cellEmission = vec3(0.0);
    // Weathering on the wall: per-floor tone, soot streaks running down from the sills.
    float streak = cityNoise2(vec2(u * 1.7, y * 0.08), building + 9u);
    float tone = 0.86 + 0.22 * cityUnit(cityHash(building, uint(storey) + 77u)) - 0.18 * streak * streak;
    if (!window) {
        if (glassWall) {
            // Spandrel glass between the floors, framed by metal mullions.
            float mullion = min(min(fu, 1.0 - fu) * bayWidth, min(v, 1.0 - v) * cellHeight);
            if (mullion < 0.06) {
                cellColor = m.baseColor * tone;
                cellRough = m.roughness;
                cellMetal = m.metallic;
                cellCoat = 0.0;
            } else {
                cellColor = m.baseColor2 * (0.8 + 0.3 * tone);
                cellRough = 0.3;
                cellCoat = 1.0;
            }
        } else {
            cellColor = m.baseColor * tone;
            float block = cityNoise2(vec2(u, y) * 3.3, building + 21u);
            cellRough = clamp(m.roughness * (0.8 + 0.4 * block), 0.0, 1.0);
            cellCoat = 0.0;
            // Courses of brick or stone (75 mm or 0.6 m high) where the footprint resolves them: mortar joints
            // and each unit's own shade.
            bool brick = style == kPatternCityPunched && m.baseColor.r > 1.6 * m.baseColor.b;
            float course = brick ? 0.075 : 0.6;
            float unitLength = brick ? 0.23 : 1.2;
            float courseDetail = smoothstep(1.5, 4.0, course * facing / max(footprint, 1e-6));
            if (courseDetail > 0.0) {
                float row = floor(y / course);
                float x = u / unitLength + 0.5 * mod(row, 2.0);
                float joint = min(min(fract(y / course), 1.0 - fract(y / course)) * course,
                                  min(fract(x), 1.0 - fract(x)) * unitLength);
                uint unitId = cityHash(uint(int(floor(x))) * 0x632BE5ABu, uint(int(row)) + building);
                float shadeUnit = 0.85 + 0.3 * cityUnit(unitId);
                vec3 mortar = vec3(0.32, 0.31, 0.29);
                vec3 unitColor = joint < 0.006 ? mortar : cellColor * shadeUnit;
                cellColor = mix(cellColor, unitColor, courseDetail);
            }
        }
    } else {
        // The glass: a smooth reflective coat over the dark tint, and the room behind it when lit.
        cellColor = m.baseColor2;
        cellRough = 0.3;
        cellCoat = 1.0;
        if (lit) {
            // The room behind the glass, seen in parallax.
            vec3 n = normalize(cross(across, vec3(0.0, 1.0, 0.0)));
            if (dot(n, h.geometricNormal) < 0.0) {
                n = -n;
            }
            vec3 dir = gl_WorldRayDirectionEXT;
            vec3 local = vec3(dot(dir, across), dir.y, -dot(dir, n));
            vec2 inWindow = vec2((fu - centre.x + extent.x) / (2.0 * extent.x),
                                 (v - centre.y + extent.y) / (2.0 * extent.y));
            float shade = cityRoom(vec2(fu, v), normalize(local),
                                   vec3(bayWidth, cellHeight, 0.8 * max(bayWidth, 3.0)), room);
            float kind = office ? 0.5 + 0.5 * cityDraw(room, 19u) : cityDraw(room, 19u);
            if (office && kind < 0.62) {
                kind = 0.1; // blinds drawn in some office bays
            }
            if (shop) {
                // A shop after hours: its room dimly lit, a display brighter near the glass.
                shade = 0.12 * shade + 0.1 * smoothstep(0.55, 0.0, inWindow.y);
                // The shopfront's mullions: three panes and a transom.
                float pane = fract(inWindow.x * 3.0);
                if (min(pane, 1.0 - pane) < 0.03 || abs(inWindow.y - 0.78) < 0.02) {
                    shade = 0.0;
                }
            } else if (kind < 0.2) {
                // Blinds: slats across the window, the room glowing through.
                shade = 0.5 * (0.35 + 0.65 * step(0.42, fract(inWindow.y * 16.0)));
            } else if (kind < 0.36) {
                // Curtains: a soft warm glow with folds.
                shade = 0.45 + 0.1 * sin(inWindow.x * 40.0 + 6.0 * cityUnit(room));
                tint *= vec3(1.0, 0.8, 0.58);
            } else if (kind < 0.4) {
                // A television: a cold flickering light.
                float flicker = 0.4 + 0.6 * cityNoise2(vec2(seconds * 7.0, float(cellId & 1023u)), 31u);
                tint = materialEmission(m) * vec3(0.5, 0.65, 1.0);
                shade *= 0.7 * flicker;
            }
            cellEmission = tint * level * shade;
        }
    }
    baseColor = mix(avgColor, cellColor, detail);
    roughness = mix(avgRough, cellRough, detail);
    h.material.metallic = mix(0.0, cellMetal, detail);
    h.material.coat = mix(glassShare, cellCoat, detail);
    h.material.coatRoughness = 0.01;
    emission = mix(avgEmission, cellEmission, detail);
}

// --- Streets ---------------------------------------------------------------------------------------------

// Evaluates the street at a world point on the ground (or on the elevated avenue's deck, which lies over a
// street of the grid): fills baseColor, roughness and the coat.
void cityStreet(inout HitPoint h, out vec3 baseColor, out float roughness) {
    Material m = h.material;
    vec2 p = h.position.xz;
    // Position relative to the nearest street centre lines of the grid.
    vec2 q = p - kCityCellSize * floor(p / kCityCellSize + 0.5);
    if (m.patternScale > 0.0) {
        q.y = 0.5 * kCityCellSize; // an elevated avenue along z: no junctions
    }
    float halfStreet = 0.5 * kCityStreetWidth;
    bool alongZ = abs(q.x) < halfStreet; // on a street running along z
    bool alongX = abs(q.y) < halfStreet;
    vec2 cellIndex = floor(p / kCityCellSize + 0.5);
    uint segment = cityHash(uint(int(cellIndex.x)) * 0x8DA6B343u, uint(int(cellIndex.y)) + 0x9E37u);

    // Asphalt: aggregate, tone and patched repairs.
    float aggregate = cityNoise2(p * 37.0, 0x51u) * 0.6 + cityNoise2(p * 113.0, 0x52u) * 0.4;
    float tone = cityFbm2(p * 0.08, 0x53u);
    vec3 asphalt = m.baseColor * (0.75 + 0.35 * tone) * (0.85 + 0.3 * aggregate);
    float rough = m.roughness;
    // A patch: a darker, smoother rectangle in some street segments.
    if (cityUnit(segment) < 0.35) {
        vec2 c = vec2(cityDraw(segment, 3u) - 0.5, cityDraw(segment, 5u) - 0.5) * 18.0;
        vec2 e = vec2(1.0 + 3.0 * cityDraw(segment, 7u), 1.5 + 5.0 * cityDraw(segment, 9u));
        vec2 r = abs(q - c) - e;
        if (max(r.x, r.y) < 0.0) {
            asphalt *= 0.62;
            rough *= 0.85;
        }
    }

    // Markings.
    float paint = 0.0;
    vec3 paintColour = vec3(0.75, 0.74, 0.7);
    if (alongZ != alongX) {
        float across = alongZ ? q.x : q.y; // across the street
        float along = alongZ ? p.y : p.x;  // along it
        float fromJunction = alongZ ? abs(q.y) : abs(q.x);
        if (fromJunction > halfStreet + 3.6) {
            // The double centre line (warm), the lane dashes (3 m in 9 m) and the edge lines.
            if (abs(abs(across) - 0.12) < 0.06) {
                paint = 1.0;
                paintColour = vec3(0.8, 0.62, 0.25);
            } else if (abs(abs(across) - 3.0) < 0.07 && fract(along / 9.0) < 0.34) {
                paint = 1.0;
            } else if (abs(abs(across) - (halfStreet - 0.45)) < 0.06) {
                paint = 0.9;
            }
        } else if (fromJunction > halfStreet + 0.5) {
            // A zebra crossing before the junction: bars along the street, 0.5 m wide every 1 m.
            if (fract(across + 0.25) < 0.5 && abs(across) < halfStreet - 0.6) {
                paint = 1.0;
            }
        } else if (fromJunction > halfStreet + 0.2 && fromJunction < halfStreet + 0.45) {
            paint = 0.9; // the stop line
        }
        // A manhole cover in some segments, in a lane.
        vec2 lane = alongZ ? vec2(q.x, q.y) : vec2(q.y, q.x);
        float coverAlong = (cityDraw(segment, 13u) - 0.5) * 20.0;
        vec2 cover = vec2(1.6 * sign(cityDraw(segment, 17u) - 0.5), coverAlong);
        float r = length(lane - cover);
        if (cityDraw(segment, 19u) < 0.6 && r < 0.36) {
            asphalt = vec3(0.05, 0.048, 0.045) * (0.8 + 0.4 * step(0.5, fract(r * 14.0)));
            rough = 0.45;
            h.material.metallic = 0.6;
        }
    }
    float wear = cityNoise2(p * 2.3, 0x61u);
    paint *= smoothstep(0.15, 0.55, wear); // worn by the traffic
    baseColor = mix(asphalt, paintColour * (0.7 + 0.3 * aggregate), paint);
    rough = mix(rough, 0.6, paint);

    // Water: puddles from a noise field and the wheel ruts first, as the wetness rises.
    float wetness = clamp(m.coat, 0.0, 1.0);
    float field = cityFbm2(p * 0.22, 0x71u);
    float rut = 0.0;
    if (alongZ != alongX) {
        float across = alongZ ? q.x : q.y;
        float inLane = abs(abs(across) - 1.6); // the lane centres are 1.6 and 4.4 m out
        float inLane2 = abs(abs(across) - 4.4);
        rut = max(0.0, 1.0 - min(abs(inLane - 0.9), abs(inLane2 - 0.9)) / 0.45);
    }
    float water = field + 0.25 * rut + 1.1 * (wetness - 0.5) - 0.1 * paint;
    float film = smoothstep(0.42, 0.5, water);   // standing water: a mirror
    float damp = smoothstep(0.15, 0.45, wetness); // everything else darkens and shines a little
    baseColor *= mix(1.0, 0.45, max(film, 0.7 * damp));
    roughness = mix(rough, 0.3, damp);
    h.material.coat = max(film, 0.55 * damp);
    // Ripples: the standing water stays a mirror (coat roughness 0.01: a delta lobe), the damp asphalt a
    // glossy smear that stretches the lights' reflections down the street.
    h.material.coatRoughness = film > 0.5 ? 0.01 + m.coatRoughness : mix(0.22, 0.09, damp); // + ripples
}

// --- Signs -----------------------------------------------------------------------------------------------

// The signs' lettering: words of a Nordic high street (KAFE, APOTEK, HOTELL, BAR, KIOSK, BAGERI, BIO, TAXI, PUB, PIZZA, BAKERI, NATT, HOTEL, BUTIK, ANTIK, FISK, KINO, SPORT) in a 5 x 7 dot font (the
// show's own glyph grid), letters packed 5 bits each (index + 1, 0 ends the word), the glyphs' 35 dots as
// 32 low bits and 3 high bits (bit row * 5 + column, row 0 at the top).
const uint kCitySignLetters = 18u; // ABEFGHIKLNOPRSTUXZ
const uint kCitySignGlyphLow[18] = uint[18](
    0x631FC62Eu, 0xE317C62Fu, 0xC217843Fu, 0x4217843Fu, 0xA31E862Eu, 0x631FC631u,
    0x8842108Eu, 0x52519531u, 0xC2108421u, 0x631CD671u, 0xA318C62Eu, 0x4217C62Fu,
    0x5257C62Fu, 0xE107043Eu, 0x0842109Fu, 0xA318C631u, 0x62A22A31u, 0xC222221Fu);
const uint kCitySignGlyphHigh[18] = uint[18](4u, 3u, 7u, 0u, 7u, 4u, 3u, 4u, 7u, 4u, 3u, 0u, 4u, 3u, 1u, 3u, 4u, 7u);
const uint kCitySignWords = 18u;
const uint kCitySignWord[18] = uint[18](
    0x00019028u, 0x1037AD81u, 0x1291BD66u, 0x00003422u, 0x00872CE8u, 0x0ED19422u,
    0x00002CE2u, 0x0003C42Fu, 0x00000A0Cu, 0x001948ECu, 0x0ED1A022u, 0x0007BC2Au,
    0x0091BD66u, 0x0083BE02u, 0x0083BD41u, 0x000438E4u, 0x0005A8E8u, 0x00F6AD8Eu);

uint citySignWordLength(uint word) {
    uint n = 0u;
    while (n < 6u && ((word >> (5u * n)) & 31u) != 0u) {
        ++n;
    }
    return n;
}

bool citySignDot(uint letter, int column, int row) {
    if (column < 0 || column > 4 || row < 0 || row > 6) {
        return false;
    }
    uint bit = uint(row * 5 + column);
    return bit < 32u ? ((kCitySignGlyphLow[letter] >> bit) & 1u) != 0u
                     : ((kCitySignGlyphHigh[letter] >> (bit - 32u)) & 1u) != 0u;
}

// The ink of a letter at g, in dot units (x 0..5 right, y 0..7 down, dots centred on the half units): the dots
// joined into strokes, a capsule between every two neighbouring dots (and the diagonal ones where no
// right-angle path joins them), `width` dots wide. 1 on a stroke, 0 off, with a soft edge `soft` dots wide.
float citySignInk(uint letter, vec2 g, float width, float soft) {
    ivec2 c = ivec2(floor(g));
    float best = 1e9;
    for (int dy = -1; dy <= 1; ++dy) {
        for (int dx = -1; dx <= 1; ++dx) {
            ivec2 a = c + ivec2(dx, dy);
            if (!citySignDot(letter, a.x, a.y)) {
                continue;
            }
            vec2 pa = vec2(a) + 0.5;
            best = min(best, length(g - pa));
            // Strokes to the right, down, and down the diagonals.
            for (int k = 0; k < 4; ++k) {
                ivec2 step = k == 0 ? ivec2(1, 0) : k == 1 ? ivec2(0, 1) : k == 2 ? ivec2(1, 1) : ivec2(-1, 1);
                ivec2 b = a + step;
                if (!citySignDot(letter, b.x, b.y)) {
                    continue;
                }
                if (k >= 2 && (citySignDot(letter, a.x + step.x, a.y) || citySignDot(letter, a.x, a.y + 1))) {
                    continue; // a corner, already joined round the right angle
                }
                vec2 pb = vec2(b) + 0.5;
                vec2 ab = pb - pa;
                float t = clamp(dot(g - pa, ab) / dot(ab, ab), 0.0, 1.0);
                best = min(best, length(g - pa - t * ab));
            }
        }
    }
    return 1.0 - smoothstep(0.5 * width - 0.5 * soft, 0.5 * width + 0.5 * soft, best);
}

// A sign's face at uv (the quad's, v down) as the camera sees it, relative to its mean radiance (the rect
// light's uniform emission, which next-event estimation and every ray after a rough bounce see): a lightbox
// (dark letters on a bright panel inside a frame) or neon (bright tubes on a dark backing), a word set across a
// wide sign and stacked letter over letter down a tall one, from the sign's hash. Each kind's levels are set
// so that its mean is about 1.
float citySign(vec2 uv, vec2 size, uint seed) {
    bool tall = size.y > size.x;
    vec2 p = vec2(uv.x, 1.0 - uv.y) * size; // metres, y up
    float border = 0.08 * min(size.x, size.y);
    vec2 inner = p - border;
    vec2 innerSize = size - 2.0 * border;
    bool framePart = any(lessThan(inner, vec2(0.0))) || any(greaterThan(inner, innerSize));
    bool neon = cityDraw(seed, 7u) < 0.45;
    if (framePart) {
        return neon ? 0.25 : 0.55;
    }
    // The letters' pitch size: 7 dots high across a wide sign, 5 dots wide down a tall one.
    float pitch = tall ? innerSize.x * 0.8 / 5.0 : innerSize.y * 0.78 / 7.0;
    float advance = 6.0 * pitch; // a letter and a pitch's gap
    float lineAdvance = 8.5 * pitch;
    uint slots = uint(max(1.0, floor(tall ? innerSize.y / lineAdvance : (innerSize.x + pitch) / advance)));
    // The longest word that fits, from a few draws of the sign's hash.
    uint word = 0u, count = 0u;
    for (uint k = 0u; k < 4u && count == 0u; ++k) {
        uint w = kCitySignWord[pcgHash(seed + k * 0x9E3779B9u) % kCitySignWords];
        uint n = citySignWordLength(w);
        if (n <= slots) {
            word = w;
            count = n;
        }
    }
    if (count == 0u) {
        word = kCitySignWord[3]; // BAR
        count = min(slots, 3u);
    }
    // The letter under p and the position in it, the word centred.
    vec2 g;
    uint index;
    if (tall) {
        float top = 0.5 * (innerSize.y + float(count) * lineAdvance) - 0.75 * pitch;
        float k = floor((top - inner.y) / lineAdvance);
        index = uint(clamp(k, 0.0, float(count) - 1.0));
        g = vec2((inner.x - 0.5 * (innerSize.x - 5.0 * pitch)) / pitch, (top - inner.y - k * lineAdvance) / pitch);
        if (k < 0.0 || k >= float(count)) {
            g = vec2(-9.0);
        }
    } else {
        float left = 0.5 * (innerSize.x - (float(count) * advance - pitch));
        float k = floor((inner.x - left) / advance);
        index = uint(clamp(k, 0.0, float(count) - 1.0));
        g = vec2((inner.x - left - k * advance) / pitch, (0.5 * (innerSize.y + 7.0 * pitch) - inner.y) / pitch);
        if (k < 0.0 || k >= float(count)) {
            g = vec2(-9.0);
        }
    }
    uint letter = ((word >> (5u * index)) & 31u) - 1u;
    float ink = g.x < -1.0 ? 0.0 : citySignInk(letter, g, neon ? 0.62 : 0.9, 0.12);
    if (neon) {
        return mix(0.06, 9.0, ink);
    }
    return mix(1.25, 0.3, ink);
}

// --- Trails ----------------------------------------------------------------------------------------------

// A light trail's emission along its length: bright at the head (uv.x = 1), fading to nothing at the tail.
float cityTrail(vec2 uv) {
    float x = clamp(uv.x, 0.0, 1.0);
    return x * x * (0.35 + 0.65 * x) + 2.5 * smoothstep(0.93, 1.0, x);
}

#endif
