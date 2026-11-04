// The CRT's geometry and constants, shared by crt.comp (the 1993 picture on the output) and any pass that
// shows the same picture on a screen, so both bulge and frame it identically.

#ifndef EVO_CRT_COMMON_GLSL
#define EVO_CRT_COMMON_GLSL

const ivec2 kRetroSize = ivec2(320, 200);
const float kBarrel = 0.02;
const float kBloom = 0.15;
const float kScanline = 0.75;
const float kMask = 0.06;
const float kVignette = 0.10;

// The barrel bulge: centred runs from -1 to 1 across the content rectangle; the result is where to sample the
// picture in the same space. Normalised so the corners stay put, so the rectangle's edges still sample the
// picture's edges.
vec2 crtBulge(vec2 centred) {
    float r2 = dot(centred, centred); // 2 in the corners
    return centred * (1.0 + kBarrel * r2) / (1.0 + 2.0 * kBarrel);
}

// The centred 4:3 rectangle the picture fills on a w x h output: the height times 4/3 wide, or the width
// times 3/4 high when the output is narrower than 4:3. Returns (origin.x, origin.y, width, height) in pixels.
uvec4 crtContentRect(uint w, uint h) {
    uvec2 content = uvec2(w, h);
    if (w * 3u >= h * 4u) {
        content.x = h * 4u / 3u;
    } else {
        content.y = w * 3u / 4u;
    }
    uvec2 origin = (uvec2(w, h) - content) / 2u;
    return uvec4(origin, content);
}

#endif
