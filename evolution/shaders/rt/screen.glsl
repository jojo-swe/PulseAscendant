#ifndef EVO_RT_SCREEN_GLSL
#define EVO_RT_SCREEN_GLSL

// The CRT's picture on the Studio's flat screen (Decision 10: the curvature is only a UV warp): the retro
// image (linear light through its sRGB view) bulged by 2 % like crt.comp's barrel, but not renormalised, so
// the picture's corners pull in and the screen is black outside the warped picture; odd source lines 25 %
// darker, faded into their mean (87.5 %) from LOD 0 to LOD 1, where the lines merge; a 10 % vignette towards
// the corners. The emissive hit (at the ray cone's LOD) and the light's NEE (at kDiffuseTextureLod) both read
// it, so the two estimators see the same screen. Needs common.glsl (the retroScreen binding).

const float kScreenBarrel = 0.02;
const float kScreenScanline = 0.75;
const float kScreenVignette = 0.10;
const float kRetroLines = 200.0;
// The glow of the lit glass over a black picture: a CRT's black is never quite black.
const vec3 kScreenGlow = vec3(0.0012, 0.0016, 0.0018);

// uv: the screen quad's own uv, (0, 0) at the picture's top left.
vec3 retroScreenPicture(vec2 uv, float lod) {
    vec2 centred = uv * 2.0 - 1.0;
    float r2 = dot(centred, centred);
    vec2 bulged = centred * (1.0 + kScreenBarrel * r2);
    if (any(greaterThan(abs(bulged), vec2(1.0)))) {
        return vec3(0.0);
    }
    vec2 st = bulged * 0.5 + 0.5;
    vec3 color = textureLod(retroScreen, st, lod).rgb;
    float line = min(floor(st.y * kRetroLines), kRetroLines - 1.0);
    float scan = mod(line, 2.0) == 1.0 ? kScreenScanline : 1.0;
    float merged = clamp(lod, 0.0, 1.0);
    color *= mix(scan, 0.5 * (1.0 + kScreenScanline), merged);
    return (color + kScreenGlow) * (1.0 - kScreenVignette * 0.5 * r2);
}

#endif
