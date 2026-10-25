#pragma once

// A scene's environment (step 5, Task 2): the sky a ray sees when it leaves the scene, and how next-event
// estimation samples it. SceneFrame::environment carries it; the path tracer packs it into
// rt::EnvironmentUniforms (src/rt/gpu_types.hpp) at binding EVO_BINDING_ENVIRONMENT every frame, and
// shaders/rt/environment.glsl (with the sky models of shaders/rt/sky.glsl) reads it:
// - environmentView(d, footprint): what the camera sees directly or through a chain of delta events: the
//   gradient, the glow, the disc with limb darkening, stars and town lights filtered over the ray cone's
//   footprint (each a point of fixed flux, whatever the resolution);
// - environmentLighting(d): what lights the scene (next-event estimation, portals, every ray after a rough
//   vertex): the gradient, the glow and the uniform disc only, smooth so it does not speckle;
// - sampleEnvironment / environmentPdf: the disc (uniform in its cone) with probability discShare, else the
//   dome (cosine about +y); NEE samples the environment with probability `select`.

#include "modern/math.hpp"
#include "rt/gpu_types.hpp"
#include "rt/sky_constants.hpp"

#include <cstdint>

namespace evo::modern {

// The sky models of shaders/rt/sky.glsl. NightSky is step 4's night sky (gradient, moon glow, stars, a town
// under the horizon); Space is black with dense stars; Dusk and Day have a sun.
enum class EnvironmentKind : std::uint32_t { NightSky = 0, Space = 1, Dusk = 2, Day = 3 };

// Radiances are linear light, multiplied by `intensity`; directions are +y up (normalised when packed).
struct EnvironmentDesc {
    EnvironmentKind kind = EnvironmentKind::NightSky;
    // The sun or moon: its direction (also the glow's centre), angular radius in radians (0: no disc; the
    // Studio's moon is an emitter of its own) and radiance inside it.
    Vec3 discDirection{rt::kMoonDirection[0], rt::kMoonDirection[1], rt::kMoonDirection[2]};
    float discAngularRadius = 0;
    Vec3 discRadiance{};
    float intensity = 1;
    float starDensity = 1;    // x the kind's share of star cells holding a star (at most every cell)
    float starBrightness = 1; // x every star's flux
    float townDensity = 1;    // x the town's lights and its glow (NightSky)
    float haze = 0;           // a pale band along the horizon, 0..1
    // The gradient: zenith and horizon above, ground below (environmentDefaults(kind) fills them; these are
    // NightSky's).
    Vec3 zenith{0.006f, 0.011f, 0.034f}, horizon{0.03f, 0.045f, 0.1f}, ground{0.0015f, 0.0018f, 0.003f};
    float glow = 1; // x the kind's halo around discDirection
    // The probability that NEE samples the environment instead of a rect light: 1 when the scene has no
    // lights, about 0.5 outdoors, 0 indoors (the Studio, whose window portal samples the sky).
    float select = 0;
};

// The defaults of each kind. EnvironmentDesc{} equals environmentDefaults(NightSky).
EnvironmentDesc environmentDefaults(EnvironmentKind kind);

// The uniforms environment.glsl reads (112 bytes): kind, intensity, the disc's cosine of radius and share of
// the environment's samples, its normalised direction, the dome's share (1 - discShare when the dome has
// light, both 0 when nothing has), the disc's radiance, the stars, the gradient, haze, and the glow (towards
// discDirection). The shares follow the disc's and the dome's estimated power. Throws std::invalid_argument
// for an unknown kind, a non-finite or negative value, select outside [0, 1], a zero direction or a radius
// outside [0, pi / 2).
rt::EnvironmentUniforms packEnvironment(const EnvironmentDesc& desc);

} // namespace evo::modern
