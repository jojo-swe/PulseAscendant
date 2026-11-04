#ifndef EVO_RT_TESSERAE_GLSL
#define EVO_RT_TESSERAE_GLSL

// The 1993 rotozoom's texture, texel by texel, as the 2026 Lens scene lays it in stone (the Checker hit
// group's Tesserae patterns, shaders/rt/checker.rchit): the same 128x128 tile retro::rotozoomTexture builds on
// the CPU (src/retro/rotozoom.cpp) and the colours rotozoomPalette gives its indices, as 8-bit sRGB.
// scene_lens_test runs both through a compute probe (shaders/test/tesserae_probe.comp) against the CPU for
// every texel. Plain GLSL: no bindings, no ray tracing built-ins.

const int kRotozoomTile = 128;
const int kRotozoomLogoX = 28; // retro::kRotozoomLogoX, kRotozoomLogoY
const int kRotozoomLogoY = 57;
const uint kRotozoomTileA = 16u; // retro::kRotozoomTileA, ...
const uint kRotozoomTileB = 40u;
const uint kRotozoomInk = 64u;
const uint kRotozoomOutline = 78u;
const uint kRotozoomShadow = 79u;

// "EVOLUTION" in the demo's 8x8 font (text::glyph): per glyph, rows 0-3 then 4-7, a byte each, row 0 lowest.
const uint kRotozoomLogoRows[18] = uint[18](
    0xFCC0C0FEu, 0x00FEC0C0u, 0xC6C6C6C6u, 0x0010386Cu, 0xC6C6C67Cu, 0x007CC6C6u, 0xC0C0C0C0u, 0x00FEC0C0u,
    0xC6C6C6C6u, 0x007CC6C6u, 0x303030FCu, 0x00303030u, 0x303030FCu, 0x00FC3030u, 0xC6C6C67Cu, 0x007CC6C6u,
    0xDEF6E6C6u, 0x00C6C6CEu);

// Whether texel (u, v) is ink of the logo: each glyph row doubled, glyph i's columns at x + 8 i.
bool rotozoomLogoInk(int u, int v) {
    int x = u - kRotozoomLogoX, y = v - kRotozoomLogoY;
    if (x < 0 || y < 0 || x >= 72 || y >= 16) {
        return false;
    }
    int row = y / 2;
    uint bits = (kRotozoomLogoRows[2 * (x / 8) + row / 4] >> (8 * (row % 4))) & 0xFFu;
    return (bits & (0x80u >> uint(x % 8))) != 0u;
}

// The shade (0 dark .. 23 light) of texel (i, j) of a 16x16 checker tile: a three-texel bevel, light along the
// top and left, dark along the bottom and right, a diagonal gradient inside.
int rotozoomTileShade(int i, int j) {
    if (max(i, j) >= 13) {
        return 15 - max(i, j);
    }
    if (min(i, j) <= 2) {
        return 23 - min(i, j);
    }
    return 20 - (i + j - 6) * 17 / 18;
}

// The palette index of texel t of the endlessly repeated tile.
uint rotozoomIndex(ivec2 t) {
    int u = t.x & (kRotozoomTile - 1), v = t.y & (kRotozoomTile - 1);
    if (rotozoomLogoInk(u, v)) {
        return kRotozoomInk + uint(v - kRotozoomLogoY);
    }
    bool outline = false;
    for (int dv = -1; dv <= 1; ++dv) {
        for (int du = -1; du <= 1; ++du) {
            outline = outline || rotozoomLogoInk(u + du, v + dv);
        }
    }
    if (outline) {
        return kRotozoomOutline;
    }
    if (rotozoomLogoInk(u - 2, v - 2)) {
        return kRotozoomShadow;
    }
    uint base = ((u / 16 + v / 16) % 2) == 0 ? kRotozoomTileA : kRotozoomTileB;
    return base + uint(rotozoomTileShade(u % 16, v % 16));
}

// rotozoomPalette's linear ramp from `from` to `to` over `count` entries, entry i (8-bit, rounded).
vec3 rotozoomRamp(vec3 from, vec3 to, uint count, uint i) {
    float t = count > 1u ? float(i) / float(count - 1u) : 0.0;
    return floor(from + (to - from) * t + 0.5);
}

// The 8-bit sRGB colour of a texture index (16..79).
vec3 rotozoomColour(uint index) {
    if (index >= kRotozoomTileA && index < kRotozoomTileB) {
        return rotozoomRamp(vec3(6, 10, 44), vec3(84, 150, 232), 24u, index - kRotozoomTileA);
    }
    if (index >= kRotozoomTileB && index < kRotozoomInk) {
        return rotozoomRamp(vec3(34, 4, 42), vec3(214, 104, 196), 24u, index - kRotozoomTileB);
    }
    if (index >= kRotozoomInk && index < kRotozoomInk + 5u) {
        return rotozoomRamp(vec3(255, 255, 232), vec3(255, 214, 120), 5u, index - kRotozoomInk);
    }
    if (index >= kRotozoomInk + 5u && index < kRotozoomInk + 8u) {
        return rotozoomRamp(vec3(248, 186, 92), vec3(235, 160, 80), 3u, index - kRotozoomInk - 5u);
    }
    if (index >= kRotozoomInk + 8u && index < kRotozoomOutline) {
        return rotozoomRamp(vec3(255, 244, 210), vec3(255, 190, 96), 6u, index - kRotozoomInk - 8u);
    }
    if (index == kRotozoomOutline) {
        return vec3(20, 12, 40);
    }
    if (index == kRotozoomShadow) {
        return vec3(6, 4, 16);
    }
    return vec3(0.0);
}

#endif
