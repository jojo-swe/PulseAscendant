#ifndef EVO_MODERN_WAYPOINT_TEXT_GLSL
#define EVO_MODERN_WAYPOINT_TEXT_GLSL

// ASCENDANT's text waypoint (src/modern/scenes/cosmos.hpp, waypointTargets): up to kWaypointGlyphs characters
// of the 8x8 font (text/font8x8.cpp, codes 32..95: its table copied here; scene_cosmos_test checks it against
// text::glyph) spelled by the particles on their way to the logo, filled as logo_targets.glsl fills the logo.
// The text comes in the particle frame's params[3] as six-bit codes (character - 32), four to a float (exact
// in a float's 24-bit mantissa), the first character in the low bits of params[3].x; spaces pad it. Its lit
// pixels are numbered row-major over the whole line (rows top to bottom, columns left to right), and the
// formation is centred on their bounding box. Include after logo_targets.glsl (logoHash, logoUnit).

const uint kWaypointGlyphs = 16u;
const uint kWaypointDepth = 2u; // voxels deep

// Codes 32..95, two words per glyph: rows 0..3 then rows 4..7, the top row in the high byte.
const uint kWaypointFont[128] = uint[128](
    0x00000000u, 0x00000000u, 0x30303030u, 0x30003000u, 0x6C6C0000u, 0x00000000u, 0x6C6CFE6Cu, 0xFE6C6C00u,
    0x107CD07Cu, 0x167C1000u, 0xC6CC1830u, 0x66C60000u, 0x70D87076u, 0xDCCC7600u, 0x30306000u, 0x00000000u,
    0x18306060u, 0x60301800u, 0x60301818u, 0x18306000u, 0x006C38FEu, 0x386C0000u, 0x003030FCu, 0x30300000u,
    0x00000000u, 0x00303060u, 0x000000FCu, 0x00000000u, 0x00000000u, 0x00303000u, 0x060C1830u, 0x60C08000u,
    0x7CC6CED6u, 0xE6C67C00u, 0x30703030u, 0x30307800u, 0x7CC6063Cu, 0x60C0FE00u, 0x7CC6063Cu, 0x06C67C00u,
    0x1C3C6CCCu, 0xFE0C0C00u, 0xFEC0FC06u, 0x06C67C00u, 0x3C60C0FCu, 0xC6C67C00u, 0xFE060C18u, 0x30303000u,
    0x7CC6C67Cu, 0xC6C67C00u, 0x7CC6C67Eu, 0x060C7800u, 0x00303000u, 0x30300000u, 0x00003030u, 0x00303060u,
    0x0C183060u, 0x30180C00u, 0x0000FC00u, 0xFC000000u, 0x6030180Cu, 0x18306000u, 0x7CC60C18u, 0x18001800u,
    0x7CC6DED6u, 0xDEC07E00u, 0x386CC6C6u, 0xFEC6C600u, 0xFCC6C6FCu, 0xC6C6FC00u, 0x7CC6C0C0u, 0xC0C67C00u,
    0xF8CCC6C6u, 0xC6CCF800u, 0xFEC0C0FCu, 0xC0C0FE00u, 0xFEC0C0FCu, 0xC0C0C000u, 0x7CC6C0DEu, 0xC6C67E00u,
    0xC6C6C6FEu, 0xC6C6C600u, 0xFC303030u, 0x3030FC00u, 0x3E0C0C0Cu, 0xCCCC7800u, 0xC6CCD8F0u, 0xD8CCC600u,
    0xC0C0C0C0u, 0xC0C0FE00u, 0xC6EEFED6u, 0xC6C6C600u, 0xC6E6F6DEu, 0xCEC6C600u, 0x7CC6C6C6u, 0xC6C67C00u,
    0xFCC6C6FCu, 0xC0C0C000u, 0x7CC6C6C6u, 0xD6CC7600u, 0xFCC6C6FCu, 0xD8CCC600u, 0x7CC6C07Cu, 0x06C67C00u,
    0xFC303030u, 0x30303000u, 0xC6C6C6C6u, 0xC6C67C00u, 0xC6C6C6C6u, 0x6C381000u, 0xC6C6C6D6u, 0xFEEEC600u,
    0xC6C66C38u, 0x6CC6C600u, 0xCCCCCC78u, 0x30303000u, 0xFE060C18u, 0x3060FE00u, 0x78606060u, 0x60607800u,
    0xC0603018u, 0x0C060200u, 0x78181818u, 0x18187800u, 0x10386CC6u, 0x00000000u, 0x00000000u, 0x000000FEu);

uint waypointCode(vec4 packed, uint i) {
    uint word = uint(packed[i / 4u]);
    return (word >> (6u * (i % 4u))) & 63u;
}

uint waypointBits(vec4 packed, uint glyph, uint row) {
    uint code = waypointCode(packed, glyph);
    uint word = kWaypointFont[2u * code + row / 4u];
    return (word >> (8u * (3u - row % 4u))) & 0xFFu;
}

// The text's lit pixels: their count and bounding box (columns, rows).
struct WaypointBox {
    uint pixels;
    vec2 centre;
};

WaypointBox waypointBox(vec4 packed) {
    WaypointBox box;
    box.pixels = 0u;
    uvec2 lo = uvec2(1000u), hi = uvec2(0u);
    for (uint row = 0u; row < 8u; ++row) {
        for (uint glyph = 0u; glyph < kWaypointGlyphs; ++glyph) {
            uint bits = waypointBits(packed, glyph, row);
            if (bits == 0u) {
                continue;
            }
            box.pixels += uint(bitCount(bits));
            uint first = uint(findMSB(bits)), last = uint(findLSB(bits)); // bit 7 - column
            lo = min(lo, uvec2(glyph * 8u + 7u - first, row));
            hi = max(hi, uvec2(glyph * 8u + 7u - last, row));
        }
    }
    box.centre = 0.5 * (vec2(lo) + vec2(hi));
    return box;
}

// The j-th lit pixel (column, row).
uvec2 waypointPixel(vec4 packed, uint j) {
    uint seen = 0u;
    for (uint row = 0u; row < 8u; ++row) {
        for (uint glyph = 0u; glyph < kWaypointGlyphs; ++glyph) {
            uint bits = waypointBits(packed, glyph, row);
            uint n = uint(bitCount(bits));
            if (j < seen + n) {
                uint k = j - seen;
                for (uint c = 0u; c < 8u; ++c) {
                    if ((bits & (0x80u >> c)) != 0u) {
                        if (k == 0u) {
                            return uvec2(glyph * 8u + c, row);
                        }
                        --k;
                    }
                }
            }
            seen += n;
        }
    }
    return uvec2(0u);
}

// Particle id's target in the text (in cells, centred on the lit pixels; the front face at z = +1 cell), as
// logoTarget places it in the logo; false when the text has no lit pixel.
bool waypointTextTarget(vec4 packed, uint id, float cell, out vec3 target) {
    WaypointBox box = waypointBox(packed);
    if (box.pixels == 0u) {
        return false;
    }
    uint voxels = box.pixels * kWaypointDepth;
    uint v = id % voxels, k = id / voxels;
    uvec2 pixel = waypointPixel(packed, v % box.pixels);
    uint layer = v / box.pixels;
    uint h = logoHash(v);
    float jx = logoUnit(h + k * 0xD1B54A32u);
    float jy = logoUnit(logoHash(h) + k * 0xABC98388u);
    float jz = logoUnit(logoHash(h ^ 0x68E31DA4u) + k * 0x8CB92BA7u);
    target = vec3((float(pixel.x) + jx - 0.5 - box.centre.x) * cell, (box.centre.y - float(pixel.y) - jy + 0.5) * cell,
                  (1.0 - float(layer) - jz) * cell);
    return true;
}

#endif
