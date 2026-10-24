#pragma once

// A frame's caustic photon maps on receiver surfaces (step 5, Task 13).
// Declared empty by Task 1 of step 5 so that SceneFrame can carry it; Task 13 fills it in. Until then no
// scene sets it, and the path tracer binds the feature's resources to one-element dummies.

namespace evo::modern {

struct CausticFrame {};

} // namespace evo::modern
