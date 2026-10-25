#ifndef EVO_RT_SCREEN_GLSL
#define EVO_RT_SCREEN_GLSL

// The CRT's picture on the Studio's flat screen (Decision 10: the curvature is a UV warp, and the curved
// glass only bends the coat's shading normal, emissive.rchit): the retro image (linear light through its sRGB
// view) bulged by 2 % like crt.comp's barrel, but not renormalised, so the picture's corners pull in and the
// screen is black outside the warped picture; a 10 % vignette towards the corners. Up close it shows how a
// CRT draws (Task 14 of step 5):
// - the beam profile: each of the 200 source lines is one pass of the beam, brightest along its centre with
//   a dark gap to the next, in its own row's colour. The profile is the same for every colour (so the screen
//   scales every picture alike), its mean over a line is step 4's 87.5 % (its alternate rows 25 % darker),
//   and it fades into that mean over LOD -0.5 .. 0.5 (review M6: no moire where the lines approach a pixel);
// - an aperture-grille phosphor mask, three RGB stripes per source texel, faded in over LOD -2 .. -3 (where
//   a stripe spans 1.3 to 2.7 pixels: only a 4K close-up resolves it), of mean 1 too.
// So the picture's energy does not depend on the distance it is seen from. The emissive hit (at the ray
// cone's LOD, unclamped) and the light's NEE (at kDiffuseTextureLod, where both have faded) read the same
// function, so the two estimators see the same screen. Needs common.glsl (the retroScreen binding).

const float kScreenBarrel = 0.02;
const float kScreenVignette = 0.10;
const float kRetroLines = 200.0;
// The beam across a line: kBeamMean (1 + kBeam cos(2 pi (t - 1/2))) at t in [0, 1) across it (0.21 of the
// peak in the gap).
const float kBeamMean = 0.875;
const float kBeam = 0.65;
// The aperture grille: each channel is 1 + kMaskDepth cos(2 pi (x - its stripe)) across a triad.
const float kMaskDepth = 0.55;
const float kMaskTriadsPerTexel = 3.0;
// The glow of the lit glass over a black picture: a CRT's black is never quite black.
const vec3 kScreenGlow = vec3(0.0012, 0.0016, 0.0018);

// uv: the screen quad's own uv, (0, 0) at the picture's top left; lod: the footprint's mip level, unclamped
// (below 0 a texel covers more than a pixel).
vec3 retroScreenPicture(vec2 uv, float lod) {
    vec2 centred = uv * 2.0 - 1.0;
    float r2 = dot(centred, centred);
    vec2 bulged = centred * (1.0 + kScreenBarrel * r2);
    if (any(greaterThan(abs(bulged), vec2(1.0)))) {
        return vec3(0.0);
    }
    vec2 st = bulged * 0.5 + 0.5;
    float y = st.y * kRetroLines;
    float line = min(floor(y), kRetroLines - 1.0);
    // How much of the line structure survives the footprint: 1 up to LOD -0.5, 0 from LOD 0.5.
    float lines = 1.0 - smoothstep(-0.5, 0.5, lod);
    // While the lines show, each takes its own row's colour (sampled towards the row's centre).
    vec2 rowSt = vec2(st.x, mix(st.y, (line + 0.5) / kRetroLines, lines));
    vec3 color = textureLod(retroScreen, rowSt, max(lod, 0.0)).rgb;
    color *= kBeamMean;
    if (lines > 0.0) {
        color *= 1.0 + lines * kBeam * cos(6.2831853 * (y - line - 0.5));
        float mask = clamp(-lod - 2.0, 0.0, 1.0) * kMaskDepth;
        if (mask > 0.0) {
            float x = st.x * float(textureSize(retroScreen, 0).x) * kMaskTriadsPerTexel;
            color *= 1.0 + mask * cos(6.2831853 * (x - vec3(0.0, 1.0 / 3.0, 2.0 / 3.0)));
        }
    }
    return (color + kScreenGlow) * (1.0 - kScreenVignette * 0.5 * r2);
}

#endif
