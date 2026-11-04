// Shared by the 1993 shaders: the retro frame size and the integer helpers a 1993 effect would have used.

const uint kRetroWidth = 320u;
const uint kRetroHeight = 200u;

// The 4x4 ordered-dither (Bayer) threshold of a pixel, 0..15.
uint bayer4(uvec2 pixel) {
    const uint matrix[16] = uint[16](0u, 8u, 2u, 10u, 12u, 4u, 14u, 6u, 3u, 11u, 1u, 9u, 15u, 7u, 13u, 5u);
    return matrix[(pixel.y & 3u) * 4u + (pixel.x & 3u)];
}

// A 32-bit integer hash (lowbias32): well mixed, the same on every GPU.
uint hash(uint x) {
    x ^= x >> 16;
    x *= 0x7feb352du;
    x ^= x >> 15;
    x *= 0x846ca68bu;
    x ^= x >> 16;
    return x;
}
