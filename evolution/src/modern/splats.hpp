#pragma once

// A frame's emissive splats: points added into the emission guide in any order (step 5, Task 10).
// Declared empty by Task 1 of step 5 so that SceneFrame can carry it; Task 10 fills it in. Until then no
// scene sets it, and the path tracer binds the feature's resources to one-element dummies.

namespace evo::modern {

struct SplatFrame {};

} // namespace evo::modern
