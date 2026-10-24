// The sky's constants, written once in the subset C++ and GLSL share: C++ includes this file as
// "rt/sky_constants.hpp" and shaders/rt/sky.glsl as "../../src/rt/sky_constants.hpp". Plain macros and one
// declaration per language only (an include guard rather than #pragma once, which GLSL does not have).

#ifndef EVO_RT_SKY_CONSTANTS_HPP
#define EVO_RT_SKY_CONSTANTS_HPP

// Towards the moon (+y up, normalised): the centre of the sky's moon glow, and the direction in which the
// Studio stands its moon emitter outside the window. If the two drifted apart, the glow and the emitter that
// NEE finds would separate.
#define EVO_MOON_DIRECTION -0.6807f, 0.5327f, -0.5031f

#ifdef __cplusplus
namespace evo::rt {
inline constexpr float kMoonDirection[3] = {EVO_MOON_DIRECTION};
} // namespace evo::rt
#else
const vec3 kMoonDirection = vec3(EVO_MOON_DIRECTION);
#endif

#endif
