#pragma once

#include "show/cues.hpp"
#include "show/timeline.hpp"

namespace evo::modern {

// The path tracer's scene description (src/modern/scene.hpp, Task 7): the meshes and materials built once,
// and what one frame shows (camera, instances, lights, exposure). Declared here so the interface does not
// depend on the path tracer; an implementation includes scene.hpp.
struct SceneStatic;
struct SceneFrame;

// A 2026 scene the path tracer renders. frame() and its camera are pure functions of the chapter's fixed
// facts and localSeconds (like the retro scene providers, but continuous time is allowed).
class ModernScene {
public:
    virtual ~ModernScene() = default;
    virtual const SceneStatic& staticScene() const = 0;
    virtual SceneFrame frame(const show::TimelineFrame&) const = 0;
    // Whether the scene shows the retro image (the Studio's CRT), so the retro stage must run with it.
    virtual bool samplesRetro() const = 0;
};

// What the modern stage renders for an effect. Placeholder: the fill.comp gradient and beat circle, through
// the HDR image and the tonemap like every 2026 picture. Studio: the path-traced room (Task 10).
enum class SceneId { Placeholder, Studio };

// The scene of an effect. Every effect is the placeholder until the Studio exists; Task 10 moves this into
// src/modern/scenes.cpp with the Studio's chapters (Decision 1).
inline SceneId sceneFor(show::Effect /*effect*/) {
    return SceneId::Placeholder;
}

} // namespace evo::modern
