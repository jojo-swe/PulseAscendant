#pragma once

// The CPU reference of the path tracer's per-point light selection (shaders/rt/lights.glsl, Task 3 of step
// 5): the weight with which next-event estimation chooses a rect light at a shading point, and the resulting
// selection probabilities. Tests hold the shader to it; nothing at run time uses it.

#include "modern/math.hpp"
#include "rt/gpu_types.hpp"

#include <span>
#include <vector>

namespace evo::modern {

// Light l's unnormalised weight at shading point p with normal n (on the side the path arrived from):
// selectWeight * front * power * cosBound / d^2, where
// - front: p is on l's emitting side;
// - power: luminance of the radiance times the area. Emitter: luminance(emission); RetroScreen:
//   luminance(emission) (emissionScale) * screenLuminance, the luminance of the retro image's 1x1 mip level;
//   Portal: portalLuminance, the luminance of the environment's lighting radiance along -normal;
// - cosBound: 1 for a transmissive surface, else the largest max(dot(n, normalize(q - p)), 0) over l's four
//   corners and its centre q;
// - d^2: max(|centre - p|^2, area / 4).
// The gate is not applied here (it needs the portal: see lightSelectPdfsReference). 0 for a light without
// area or weight.
float lightImportanceReference(const rt::RectLight& l, Vec3 p, Vec3 n, bool transmissive,
                               float screenLuminance, float portalLuminance);

// Whether the segment from p to q leaves through portal l, from its emitting side.
bool segmentCrossesPortal(const rt::RectLight& portal, Vec3 p, Vec3 q);

// The probability with which each light is chosen at the shading point: the weights above, a gated light's
// set to 0 where the segment from p to its centre does not cross its gate, normalised by their sum (all 0
// when the sum is 0). portalLuminance[i] is light i's portal luminance (read for Portal lights only).
std::vector<float> lightSelectPdfsReference(std::span<const rt::RectLight> lights, Vec3 p, Vec3 n,
                                            bool transmissive, float screenLuminance,
                                            std::span<const float> portalLuminance);

} // namespace evo::modern
