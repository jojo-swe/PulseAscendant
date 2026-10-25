#pragma once

#include "modern/card_format.hpp"
#include "show/cues.hpp"
#include "show/timeline.hpp"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <span>
#include <string>

namespace evo::modern {

// The path tracer's scene description (src/modern/scene.hpp): the meshes and materials built once, and what
// one frame shows (camera, instances, lights, exposure). Declared here so the interface does not depend on
// the path tracer; an implementation includes scene.hpp.
struct SceneStatic;
struct SceneFrame;
struct Camera;

// How much of a scene to build: particle, splat, voxel and photon counts scale with `scale` in (0, 1]. The
// show and the cards use 1; tests build scenes at 1/256 or less (the cards' numbers are pinned against the
// full-detail configuration).
struct SceneDetail {
    float scale = 1.f;
};

// A 2026 scene the path tracer renders. frame() and camera() are pure functions of show::TimelineFrame
// (continuous time is allowed). Cameras read only the camera-safe signals (cameraDrive, flowSeconds, the beat
// grid, the chapter's fixed facts); instances, materials and lights may also read kick, bass, energy and
// beatPhase.
class ModernScene {
public:
    virtual ~ModernScene() = default;
    virtual const SceneStatic& staticScene() const = 0;
    virtual SceneFrame frame(const show::TimelineFrame&) const = 0;
    // The frame's camera on its own, cheaply: the renderer reads its shot every frame (a change resets the
    // history) without building the frame. Must equal frame(f).camera (each scene's test checks it).
    virtual Camera camera(const show::TimelineFrame&) const = 0;
    // Whether the scene shows the retro image (the Studio's CRT), so the retro stage must run with it.
    virtual bool samplesRetro() const = 0;
    // The 2026 epoch card of an effect the scene shows, its numbers from the full-detail configuration
    // (whatever detail this object was built at); empty means the cue sheet's line (show::cues).
    virtual std::string nowCard(show::Effect) const { return {}; }
};

// The scenes of the 2026 side. Placeholder is the fill.comp gradient and beat circle, through the HDR image
// and the tonemap like every 2026 picture; every other id is a path-traced scene (the Studio from step 4,
// the rest from step 5's scene tasks). Count is the number of ids.
enum class SceneId : std::uint32_t {
    Placeholder,
    Studio,
    Ship,
    Particles,
    Nebula,
    Tunnel,
    Glass,
    Lens,
    Spheres,
    City,
    Cosmos,
    Singularity,
    Count
};
inline constexpr std::size_t kSceneCount = static_cast<std::size_t>(SceneId::Count);

// One row of the scene table: the id, the name --show-info prints, and how to build the scene's CPU side
// (meshes, materials, frame function) at a detail. `make` is null for the placeholder, and for a scene whose
// task has not landed yet (no effect maps to it until then).
struct SceneEntry {
    SceneId id;
    const char* name;
    std::unique_ptr<ModernScene> (*make)(SceneDetail);
};

// The scene table (src/modern/scenes.cpp), indexed by SceneId: sceneTable()[i].id == SceneId(i).
std::span<const SceneEntry> sceneTable();

// The scene an effect shows (kSceneOfEffect in src/modern/scenes.cpp, one row per effect): FINALE, PORTAL,
// STUDIO NIGHT, RETURN and REBOOT show the Studio; each step 5 scene task moves its
// own chapters' rows off the placeholder. Every scene but the placeholder that an effect maps to has a make
// function.
SceneId sceneFor(show::Effect effect);

// The table's name of a scene ("Placeholder", "Studio", ...).
const char* sceneName(SceneId id);

// The name of the scene an effect shows, sceneName(sceneFor(effect)): --show-info's "scene" field.
const char* effectSceneName(show::Effect effect);

// A new scene object (CPU only; the GPU work is the path tracer's): the table's make function at `detail`.
// nullptr for SceneId::Placeholder, which the modern stage fills without one, and for a row without one.
// Throws std::out_of_range for an id past the table.
std::unique_ptr<ModernScene> makeScene(SceneId id, SceneDetail detail = {});

// Where the modern stage gets its scenes: makeScene in the show; tests inject their own (a factory that makes
// SceneId::Ship a Cornell box). A factory may return nullptr for an id it does not make.
using SceneFactory = std::function<std::unique_ptr<ModernScene>(SceneId, SceneDetail)>;
// Which scene an effect shows: sceneFor in the show; tests may map an effect to an injected scene.
using SceneMap = std::function<SceneId(show::Effect)>;

} // namespace evo::modern
