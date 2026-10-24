#pragma once

// A scene's environment: the sky a ray sees when it leaves the scene, with an importance-sampled sun or moon
// disc (step 5, Task 2; packed into rt::EnvironmentUniforms, src/rt/gpu_types.hpp, at binding
// EVO_BINDING_ENVIRONMENT). Declared empty by Task 1 of step 5 so that SceneFrame can carry it; Task 2 fills
// it in. Until then every scene sees step 4's night sky (shaders/rt/sky.glsl) and NEE never samples it
// (FrameUniforms.environmentSelect 0).

namespace evo::modern {

struct EnvironmentDesc {};

} // namespace evo::modern
