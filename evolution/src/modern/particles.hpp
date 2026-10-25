#pragma once

// A frame's particle field: stateless traced spheres from a scene's particle shader (step 5, Task 8; the GPU
// side is rt::ParticleField, which ModernStage keeps once for every scene).

#include <array>
#include <cstdint>

namespace evo::modern {

// The particles a frame shows: `count` (at most the scene's capacity.particles) spheres from the scene's
// SceneStatic::particleShader at the frame's chapter-local time, its params (vec4 params[4] in the shader),
// and the material every particle uses (an index into the frame's material table). The frame places the
// field with a sphere instance: kind Spheres, dynamicSpheres, firstSphere 0, sphereCount = count and this
// material (its class is the field's hit group); the particles' colour and emission vary it per sphere.
struct ParticleFrame {
    std::uint32_t count = 0;
    std::array<float, 16> params{};
    std::uint32_t material = 0;
};

} // namespace evo::modern
