#pragma once

// A frame's particle field: stateless traced spheres from a scene's particle shader (step 5, Task 8).
// Declared empty by Task 1 of step 5 so that SceneFrame can carry it; Task 8 fills it in. Until then no scene
// sets it, and the path tracer binds the feature's resources to one-element dummies.

namespace evo::modern {

struct ParticleFrame {};

} // namespace evo::modern
