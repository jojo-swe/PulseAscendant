#pragma once

// A frame's participating media: height fog and a baked density grid (step 5, Task 11).
// Declared empty by Task 1 of step 5 so that SceneFrame can carry it; Task 11 fills it in. Until then no
// scene sets it, and the path tracer binds the feature's resources to one-element dummies.

namespace evo::modern {

struct MediumFrame {};

} // namespace evo::modern
