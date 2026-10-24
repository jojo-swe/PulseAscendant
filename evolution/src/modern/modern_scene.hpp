#pragma once

#include "show/cues.hpp"
#include "show/timeline.hpp"

#include <memory>

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
// the HDR image and the tonemap like every 2026 picture. Studio: the path-traced room (src/modern/studio.hpp).
enum class SceneId { Placeholder, Studio };

// The scene of an effect (src/modern/scenes.cpp): FINALE, PORTAL, STUDIO NIGHT, RETURN and REBOOT show the
// Studio (Decision 1 of step 4); every other effect is the placeholder until its own scene exists.
SceneId sceneFor(show::Effect effect);

// A new scene object for a path-traced scene: its meshes, materials and frame function (CPU only; the GPU
// work is the path tracer's). nullptr for SceneId::Placeholder, which the modern stage fills without one.
std::unique_ptr<ModernScene> makeScene(SceneId id);

} // namespace evo::modern
