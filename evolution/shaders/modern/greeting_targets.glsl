#ifndef EVO_MODERN_GREETING_TARGETS_GLSL
#define EVO_MODERN_GREETING_TARGETS_GLSL

// "HI. YES, YOU." as particle targets. The GLSL twin of src/modern/scenes/greeting_targets.hpp, which
// documents the layout. kGreetingBitmap[row * 13 + glyph] is row `row` (top first) of the glyph's 8x8 cell,
// its most significant bit the leftmost pixel: the line HI. YES, YOU. of text/font8x8.cpp.

const uint kGreetingGlyphs = 13u;
const uint kGreetingRows = 8u;
const uint kGreetingDepth = 6u;
const uint kGreetingPixels = 225u;
const float kGreetingCentreColumn = 49.5;
const float kGreetingCentreRow = 3.5;

const uint kGreetingBitmap[104] = uint[104](
    0xC6u, 0xFCu, 0x00u, 0x00u, 0xCCu, 0xFEu, 0x7Cu, 0x00u, 0x00u, 0xCCu, 0x7Cu, 0xC6u, 0x00u,
    0xC6u, 0x30u, 0x00u, 0x00u, 0xCCu, 0xC0u, 0xC6u, 0x00u, 0x00u, 0xCCu, 0xC6u, 0xC6u, 0x00u,
    0xC6u, 0x30u, 0x00u, 0x00u, 0xCCu, 0xC0u, 0xC0u, 0x00u, 0x00u, 0xCCu, 0xC6u, 0xC6u, 0x00u,
    0xFEu, 0x30u, 0x00u, 0x00u, 0x78u, 0xFCu, 0x7Cu, 0x00u, 0x00u, 0x78u, 0xC6u, 0xC6u, 0x00u,
    0xC6u, 0x30u, 0x00u, 0x00u, 0x30u, 0xC0u, 0x06u, 0x00u, 0x00u, 0x30u, 0xC6u, 0xC6u, 0x00u,
    0xC6u, 0x30u, 0x30u, 0x00u, 0x30u, 0xC0u, 0xC6u, 0x30u, 0x00u, 0x30u, 0xC6u, 0xC6u, 0x30u,
    0xC6u, 0xFCu, 0x30u, 0x00u, 0x30u, 0xFEu, 0x7Cu, 0x30u, 0x00u, 0x30u, 0x7Cu, 0x7Cu, 0x30u,
    0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x60u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u);

uvec2 greetingPixel(uint j) {
    uint seen = 0u;
    for (uint i = 0u; i < kGreetingRows * kGreetingGlyphs; ++i) {
        uint bits = kGreetingBitmap[i];
        uint n = uint(bitCount(bits));
        if (j < seen + n) {
            uint k = j - seen;
            for (uint c = 0u; c < 8u; ++c) {
                if ((bits & (0x80u >> c)) != 0u) {
                    if (k == 0u)
                        return uvec2((i % kGreetingGlyphs) * 8u + c, i / kGreetingGlyphs);
                    --k;
                }
            }
        }
        seen += n;
    }
    return uvec2(0u);
}

// logoTarget's placement, on the greeting's bitmap. logoHash and logoUnit come from logo_targets.glsl.
vec3 greetingTarget(uint id, float cell) {
    uint voxels = kGreetingPixels * kGreetingDepth;
    uint v = id % voxels, k = id / voxels;
    uvec2 pixel = greetingPixel(v % kGreetingPixels);
    uint layer = v / kGreetingPixels;
    uint h = logoHash(v);
    float jx = logoUnit(h + k * 0xD1B54A32u);
    float jy = logoUnit(logoHash(h) + k * 0xABC98388u);
    float jz = logoUnit(logoHash(h ^ 0x68E31DA4u) + k * 0x8CB92BA7u);
    return vec3((float(pixel.x) + jx - 0.5 - kGreetingCentreColumn) * cell,
                (kGreetingCentreRow - float(pixel.y) - jy + 0.5) * cell, (3.0 - float(layer) - jz) * cell);
}

#endif
