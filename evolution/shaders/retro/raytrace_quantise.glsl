// RAYTRACE's quantiser: a colour (0..255 a channel) to the index of the nearest entry of the fixed palette
// (retro/raytrace.hpp: the 6x6x6 cube at 16..231, the greys 8 + 10 k at 232..255) after the 4x4 Bayer move.
// In integers, so the CPU reference (retro::raytraceQuantise) agrees exactly. Units are 1/32 of a level:
//   T = 32 c + 51 (2 bayer - 15)                   the colour moved by ((bayer + 0.5) / 16 - 0.5) cube steps
//   q = (T + 816) / 1632                           the nearest cube level, round(T / 51), always 0..5
//   k = clamp(floor((sum T - 288) / 960), 0, 23)  the grey nearest the mean of T
// and the grey is taken when its squared distance to T is strictly smaller than the cube entry's.
uint raytraceQuantise(uvec3 colour, uint bayer) {
    ivec3 t = 32 * ivec3(colour) + (102 * int(bayer) - 765);
    ivec3 q = (t + 816) / 1632;
    ivec3 dc = t - 1632 * q;
    int cube = dc.x * dc.x + dc.y * dc.y + dc.z * dc.z;
    int sum = t.x + t.y + t.z;
    int k = sum < 288 ? 0 : min(23, (sum - 288) / 960);
    ivec3 dg = t - 32 * (8 + 10 * k);
    int grey = dg.x * dg.x + dg.y * dg.y + dg.z * dg.z;
    if (grey < cube) {
        return 232u + uint(k);
    }
    return 16u + uint(36 * q.x + 6 * q.y + q.z);
}
