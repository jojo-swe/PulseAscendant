#ifndef EVO_MODERN_LOGO_TARGETS_GLSL
#define EVO_MODERN_LOGO_TARGETS_GLSL

// The "EVOLUTION" logo as particle targets (the Particles scene; ASCENDANT reuses it, the Cosmos scene). The GLSL twin of
// src/modern/scenes/logo_targets.hpp, which documents the layout; scene_particles_test checks that the
// shader's particles land where the C++ says for every id of a field.
//
// kLogoBitmap[row * 9 + glyph] is row `row` (top first) of the glyph's 8x8 font cell, its most significant
// bit the leftmost pixel: the letters E V O L U T I O N of text/font8x8.cpp, side by side.

const uint kLogoGlyphs = 9u;
const uint kLogoRows = 8u;
const uint kLogoDepth = 6u;         // voxels deep
const uint kLogoPixels = 234u;      // lit pixels
const float kLogoCentreColumn = 35.0; // the lit pixels' bounding box centre
const float kLogoCentreRow = 3.0;

const uint kLogoBitmap[72] = uint[72](
    0xFEu, 0xC6u, 0x7Cu, 0xC0u, 0xC6u, 0xFCu, 0xFCu, 0x7Cu, 0xC6u,
    0xC0u, 0xC6u, 0xC6u, 0xC0u, 0xC6u, 0x30u, 0x30u, 0xC6u, 0xE6u,
    0xC0u, 0xC6u, 0xC6u, 0xC0u, 0xC6u, 0x30u, 0x30u, 0xC6u, 0xF6u,
    0xFCu, 0xC6u, 0xC6u, 0xC0u, 0xC6u, 0x30u, 0x30u, 0xC6u, 0xDEu,
    0xC0u, 0x6Cu, 0xC6u, 0xC0u, 0xC6u, 0x30u, 0x30u, 0xC6u, 0xCEu,
    0xC0u, 0x38u, 0xC6u, 0xC0u, 0xC6u, 0x30u, 0x30u, 0xC6u, 0xC6u,
    0xFEu, 0x10u, 0x7Cu, 0xFEu, 0x7Cu, 0x30u, 0xFCu, 0x7Cu, 0xC6u,
    0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u);

// A 32-bit integer hash (lowbias32), and the top 24 bits of a 32-bit fraction as a float in [0, 1).
uint logoHash(uint x) {
    x ^= x >> 16;
    x *= 0x7FEB352Du;
    x ^= x >> 15;
    x *= 0x846CA68Bu;
    x ^= x >> 16;
    return x;
}

float logoUnit(uint x) {
    return float(x >> 8) * (1.0 / 16777216.0);
}

// The j-th lit pixel (row-major: rows top to bottom, columns 0..71 left to right) as (column, row).
uvec2 logoPixel(uint j) {
    uint seen = 0u;
    for (uint i = 0u; i < kLogoRows * kLogoGlyphs; ++i) {
        uint bits = kLogoBitmap[i];
        uint n = uint(bitCount(bits));
        if (j < seen + n) {
            uint k = j - seen; // the k-th set bit from the left
            for (uint c = 0u; c < 8u; ++c) {
                if ((bits & (0x80u >> c)) != 0u) {
                    if (k == 0u) {
                        return uvec2((i % kLogoGlyphs) * 8u + c, i / kLogoGlyphs);
                    }
                    --k;
                }
            }
        }
        seen += n;
    }
    return uvec2(0u);
}

// Particle `id` of `count`: voxel v = id % (kLogoPixels * kLogoDepth), pixel v % kLogoPixels in layer
// v / kLogoPixels (0 the front), and the k-th = id / voxels sample of that voxel at an R2 point offset by a
// hash of v (32-bit fixed point, so the C++ twin agrees exactly). Centred on the lit pixels, the front face at
// z = +3 cells, x right and y up; `cell` is the pixel's size.
vec3 logoTarget(uint id, float cell) {
    uint voxels = kLogoPixels * kLogoDepth;
    uint v = id % voxels, k = id / voxels;
    uvec2 pixel = logoPixel(v % kLogoPixels);
    uint layer = v / kLogoPixels;
    uint h = logoHash(v);
    float jx = logoUnit(h + k * 0xD1B54A32u);
    float jy = logoUnit(logoHash(h) + k * 0xABC98388u);
    float jz = logoUnit(logoHash(h ^ 0x68E31DA4u) + k * 0x8CB92BA7u);
    return vec3((float(pixel.x) + jx - 0.5 - kLogoCentreColumn) * cell,
                (kLogoCentreRow - float(pixel.y) - jy + 0.5) * cell, (3.0 - float(layer) - jz) * cell);
}

#endif
