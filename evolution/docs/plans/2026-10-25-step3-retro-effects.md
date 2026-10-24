# EVOLUTION Step 3: The 1993 Effects Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Every chapter's 1993 side is a real effect of the era instead of the grey test pattern: the flat-shaded FLYBY ship over a starfield, morphing DOTS, the ring TUNNEL, the credited GLENZ vector, ROTOZOOM with a LENS, the precalculated-look RAYTRACE at 10 fps, the gouraud VECTOR CITY, a FINALE megamix, the PORTAL fly-away, the act III REBOOT prompt and THE END screen.

**Architecture:** 1993 polygon and point effects work the way they did on a 486: the CPU transforms the scene once per retro frame (double precision, then quantised to 1/16-pixel fixed point) and hands the GPU a list of screen-space primitives; two small compute rasterisers at 320×200 draw them into the R8_UINT index image (`retro/polys.comp`: flat, gouraud and glenz modes, painter's order from the CPU; `retro/points.comp`: depth-shaded dots). Image effects (tunnel, rotozoom/lens, raytrace) are their own compute shaders. Everything stays a pure function of (chapter, retroFrame) so the retro cache, seeking and export keep working.

**Tech Stack:** C++20, GLSL 460 compute, the step 2 render foundation (`Image`, `Buffer`, `FrameRing`, `ComputePipeline`, `RetroStage`, `TextPass`).

**Spec:** `evolution/docs/plans/2026-10-21-evolution-design.md` sections 2.3 (the 1993 column), 2.4 (act III REBOOT and THE END), 3.6; step 3 of section 7. Step 2 recommendations recorded in `HANDOFF.md`.

## Global Constraints

- Everything under `evolution/`; namespaces `evo::retro`, `evo::render`. Style, commit rules and trailer `Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>` as in the SDD global-constraints file.
- 320×200, 256 colours, index 0 black, palettes built on the CPU per retro frame. Every builder, provider and scene function depends only on (chapter, retroFrame) and the chapter's fixed facts (`chapterDuration`, `accentSeconds`); the time inside any shader is `pc.retroSeconds` (never a shader-local rate constant).
- Retro rates: 35 Hz for every effect except RAYTRACE (10 Hz, "precalculated").
- CPU geometry: transforms in `double`, projected vertices quantised to 1/16 px (`int32` fixed point 28.4) before upload; at most 2048 triangles and 8192 points per retro frame (asserted).
- Rasteriser rules: top-left fill rule, watertight (no pixel drawn twice or missed on a shared edge), triangles fully or partly off-screen clipped per pixel, painter's order = upload order (the CPU sorts back to front).
- Glenz mode follows the classic trick: each transparent face carries a small colour code; a pixel's index is `base | OR of the codes of every glenz face covering it` (palette entries for every combination), so overlaps tint without blending.
- Original assets only: the FLYBY ship, the city, the rotozoom texture and the logo are authored here as data or procedures. The only borrowed geometry is the 14-vertex / 24-triangle glenz mesh, credited to Future Crew (Unlicense), ported from `src/scene_math.cpp` of the parent project with the credit in a header comment.
- Per-slot GPU buffers (two frames in flight): the primitive lists live in host-visible buffers per ring slot, rewritten only after the slot's timeline wait (`FrameRing::onBegin`), flushed after writing.
- Tests use lavapipe with validation on and assert zero messages. Each effect gets: a determinism test (same retroFrame → byte-identical index image, also after rendering another frame in between), a content test (distinct indices / coverage within the effect's expected range), and a capture in `build/linux/build-out/step3/`.

## Review Focus

1. **Shared triangle edges** (every mesh): no cracks and no double-drawn pixels along shared edges under rotation — pinned in Task 1 with a fan of triangles around a centre at 64 random rotations, counting each pixel's coverage on the CPU from a readback (must be exactly 1 inside the fan, 0 outside).
2. **Primitives far off-screen or behind the camera** (FLYBY swoops, the city flight): the CPU clips against the near plane (z ≥ 0.1) before projection and culls fully off-screen triangles; the shader never loops over an unbounded bounding box — pinned in Task 1 (a triangle with a vertex at x = 10⁶ draws only its on-screen part, quickly) and in Task 6.
3. **Effect switches inside one chapter** (FINALE megamix): the cache key already includes the effect, but a megamix changes the visible effect within one chapter — the megamix must be one effect whose scene function selects the sub-effect from retroFrame, so the cache stays correct. Pinned in Task 8.
4. **Seeking into the middle of an effect** (morphs, flights, accumulating trails): no effect may depend on previous frames; every state is computed from retroFrame alone. Pinned by the registry-wide stream-vs-fresh-capture test in Task 1, extended to every new effect.
5. **Windows/MSVC**: no compiler-specific maths in the CPU transforms (`std::sin` etc. in double are fine; avoid `long double`, x87-dependent tricks and `-ffast-math`); fixed-point quantisation by `std::lround` on double.

---

### Task 1: Registry in a .cpp, scene providers, point and polygon rasterisers, determinism tests

**Files:** `src/retro/effects.cpp` (registry moves here; `effects.hpp` only declares `effectFor` and the types, includes no effect headers), `src/retro/scene.hpp/.cpp` (`RetroTriangle`, `RetroPoint`, `RetroScene`, projection helpers, near-plane clipping, fixed-point quantisation), `src/render/retro_stage.*` (per-slot primitive buffers, runs `polys.comp` then `points.comp` after the effect shader or clear when the effect has a scene provider), `shaders/retro/polys.comp`, `shaders/retro/points.comp`, `tests/raster_test.cpp`, `tests/retro_registry_test.cpp`, `tests/stream_capture_test.py`.

**Interfaces:**
```cpp
namespace evo::retro {
struct RetroTriangle { std::int32_t x[3], y[3];  // 28.4 fixed point, screen space 320x200
                       std::uint8_t index[3];    // per-vertex palette index (flat: all equal)
                       std::uint8_t mode;        // 0 flat, 1 gouraud (index interpolated, Bayer-dithered), 2 glenz (OR into the pixel)
                       std::uint8_t pad[2]; };   // 32 bytes, std430
struct RetroPoint { std::int32_t x, y; std::uint8_t index; std::uint8_t size; std::uint8_t pad[2]; };   // 12 bytes
struct RetroScene { std::vector<RetroTriangle> triangles; std::vector<RetroPoint> points; };
using SceneProvider = void (*)(RetroScene&, const show::TimelineFrame&, std::uint32_t retroFrame);
struct Camera { double eye[3], target[3], up[3], fovYDegrees; };
// Clips against z >= 0.1 in view space, projects to 320x200 with 1.2 pixel aspect (4:3 display), quantises.
void addTriangle(RetroScene&, const Camera&, const double world[3][3], const std::uint8_t index[3], std::uint8_t mode);
void addPoint(RetroScene&, const Camera&, const double world[3], std::uint8_t index, std::uint8_t size);
void sortBackToFront(RetroScene&);   // stable, by mean view depth recorded at add time
}
```
`EffectInfo` gains `SceneProvider scene = nullptr`. The polygon shader: one invocation per pixel, loop over all triangles in order (bounded by the uploaded count), edge functions in fixed point with the top-left rule; later triangles overwrite earlier ones (flat/gouraud), glenz ORs. Points: one invocation per point writing a `size × size` square, later points overwrite.

- [ ] Tests first: `raster_test` (Review Focus 1 and 2; flat/gouraud/glenz modes on known triangles compared with a CPU reference rasteriser written in the test; points sizes and clipping), `retro_registry_test` (for every `show::Effect`: palette, text, background and scene identical for two frames in the same retro frame differing in localSeconds, showSeconds, kick, energy, beatPhase; this replaces ad hoc per-effect purity tests), `stream_capture_test.py` (for one time per chapter: frame 5 of a `--render-stream` starting 5/fps earlier equals a fresh `--capture-dir` at that time, byte for byte, at 160×90).
- [ ] Implement; full suite; commit `Rasterise 1993 polygons and points from per-frame scenes`.

### Task 2: FLYBY — the flat-shaded ship over a starfield

Original low-poly ship (40–80 triangles: fuselage, swept wings, twin engines, cockpit), 16-shade ramps for hull, wings and engine glow (palette 16–127), starfield of 512 points streaming towards the camera (points sized 1–2 by depth, palette 128–143 greys), a camera that swoops from behind to a side view over the chapter (keyed by chapter-local retroFrame), engine flicker by retroFrame. The existing greeting scroller stays on top with its shadow. Lambert flat shading with one directional light, computed on the CPU per face. Tests per Global Constraints plus: the ship covers 5–40 % of the frame at three sampled times; no pixel of the ship colour outside the ship's projected bounding box.

### Task 3: DOTS — 512 morphing points

A 512-point cloud that morphs cube → sphere → torus → the letters `1993` (from the 8×8 font's set pixels) and back, rotating on two axes, points depth-shaded through a 32-entry ramp and sized 2×2 when near. Morph targets interpolated with smoothstep in retroFrame; the morph schedule spans the chapter (fixed facts only). Tests: point count 512 every frame; at the morph target frames the points match the target shapes within one pixel after projection.

### Task 4: TUNNEL — the ring tunnel

The classic dot-ring tunnel: 96 rings × 64 dots, each ring's centre following summed sines over depth (the parent project's TUN10 principle, new frequencies), perspective by 1/z, dots fading into the distance through a 64-entry ramp, the whole tunnel twisting slowly; a second variant halfway through draws the rings as connected flat quads (polygon mode) for a solid tube. Tests: ring centres follow the analytic path within one pixel; the nearest ring is the largest.

### Task 5: GLENZ — the credited vector

The 14-vertex / 24-triangle glenz mesh (Future Crew, Unlicense; credit in the header), scaled, spinning on three axes, bouncing on the beat grid of the chapter **only through the chapter's fixed beat times** (compute bounce phase from retroFrame and the chapter's tracked beats via `show::beatGrid`, which is fixed data — document this as an allowed fixed fact), two-colour glenz faces (front faces code 1, back faces code 2, drawn back faces first), palette with 4 combinations × 16 brightness steps, over a vertical copper-bar gradient background drawn by a tiny `retro/copper.comp`. Tests: glenz overlap pixels carry index `base | 3`; the mesh's silhouette area stays within ±30 % across rotations.

### Task 6: ROTOZOOM and LENS

`retro/rotozoom.comp`: an original 128×128 procedural texture (a checkerboard with an `EVOLUTION` logo stamped from the 8×8 font, 64 colours) rotated and zoomed per pixel with integer wrap-around addressing; a circular lens of radius 48 px bouncing across the screen magnifying with a refraction table (`1 − sqrt(1 − r²)` profile) and a highlight rim. The texture is built on the CPU once and uploaded as an `R8_UINT` storage image owned by the effect. Tests: at zoom 1 / angle 0 the output equals the texture tile; inside the lens the magnification at the centre is 2× within 5 %.

### Task 7: RAYTRACE — "precalculated" at 10 fps

`retro/raytrace.comp`: a real ray tracer at 320×200 — five chrome spheres and one glass-coloured sphere above an infinite red/white checkerboard, one point light, hard shadows, two reflection bounces, sky gradient — output quantised through a fixed 6×6×6 colour cube plus 40 greys (palette entries 16–255) with 4×4 Bayer dithering, at retroRate 10. Sphere positions orbit by retroFrame. This is the 1993 "it took a night to render" look, deliberately aliased and dithered. Tests: the checkerboard is visible in the lower half, a reflection of the checkerboard is visible in the central sphere, consecutive retro frames differ, the same retro frame is identical.

### Task 8: VECTOR CITY and FINALE megamix

VECTOR CITY: a procedural city of ~200 polygons (blocks of varying height on a grid, roads as flat dark quads), gouraud-shaded with a sunset palette, a camera flying down the main street and banking at the end; near-plane clipping exercised (Review Focus 2). FINALE: one effect whose scene/shader selection cycles through FLYBY, DOTS, TUNNEL, GLENZ, ROTOZOOM and RAYTRACE every two bars of the chapter's beat grid (fixed facts) with a white flash frame between them; implemented as its own shader entry that dispatches the sub-effect's shader and scene provider (Review Focus 3). Tests: the city covers the lower half; the megamix shows each sub-effect at least once and its frames equal the sub-effect's own frame at the same local retroFrame.

### Task 9: PORTAL, REBOOT and THE END

PORTAL (1993 side): the FLYBY ship turning away and shrinking into the starfield while the screen border closes in to a 4:3 frame — `THE SCREEN` typed in the 8×8 font in the corner. REBOOT (act III, chapter 20, 1993 side before its portal): the DOS prompt reused with a second script — `C:\DEMO>evolution.exe /2026`, `Reinitialising reality ...`, `Ray tracing hardware ... found`, `Entering the glass` — reusing `boot.cpp`'s typing engine (generalise it to take a script). THE END (chapter 30): `THE END` centred in 8×8 at scale 3 (drawn as text) over black, the palette fading the text from white to black over the chapter, then the final frames black. Tests: script timing like BOOT's; THE END fades monotonically.

### Task 10: Storyboard, README, HANDOFF

Regenerate all three storyboards at 640×360, update README (the 1993 effects, the rasteriser), HANDOFF (step 3 table, decisions, deferred minors), and review the act 1 storyboard visually; fix any obviously broken frame before closing the step.

## Self-review notes
- Spec 2.3 1993 column → Tasks 2–8; 2.4 act III REBOOT/THE END → Task 9; 3.6 retro pipeline rules (indexed, CPU palettes, fps lock, dithering) → every task; step 2 recommendations (registry in .cpp, retroSeconds, registry-wide purity, stream-vs-capture test) → Task 1.
- Review Focus 1, 2 → Task 1 (+2 in Task 8); 3 → Task 8; 4 → Task 1 (registry-wide); 5 → Global Constraints and every CPU transform.
