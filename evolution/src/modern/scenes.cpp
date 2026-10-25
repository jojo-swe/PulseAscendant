#include "modern/modern_scene.hpp"
#include "modern/scene.hpp"
#include "modern/scenes/city.hpp"
#include "modern/scenes/glass.hpp"
#include "modern/scenes/lens.hpp"
#include "modern/scenes/particles.hpp"
#include "modern/scenes/ship.hpp"
#include "modern/scenes/spheres.hpp"
#include "modern/scenes/tunnel.hpp"
#include "modern/studio.hpp"

#include <array>
#include <stdexcept>
#include <string>
#include <utility>

namespace evo::modern {

namespace {

using show::Effect;

template <typename Scene> std::unique_ptr<ModernScene> make(SceneDetail detail) {
    return std::make_unique<Scene>(detail);
}

// The scene table, one row per SceneId in order. A scene task gives its row a make function.
constexpr std::array<SceneEntry, kSceneCount> kSceneTable{{
    {SceneId::Placeholder, "Placeholder", nullptr},
    {SceneId::Studio, "Studio", &make<Studio>},
    {SceneId::Ship, "Ship", &make<Ship>},                // Task 16
    {SceneId::Particles, "Particles", &make<Particles>}, // Task 17
    {SceneId::Nebula, "Nebula", nullptr},                // Task 18
    {SceneId::Tunnel, "Tunnel", &make<Tunnel>},          // Task 19
    {SceneId::Glass, "Glass", &make<Glass>},             // Task 20
    {SceneId::Lens, "Lens", &make<Lens>},                // Task 21
    {SceneId::Spheres, "Spheres", &make<Spheres>},       // Task 22
    {SceneId::City, "City", &make<City>},                // Task 23
    {SceneId::Cosmos, "Cosmos", nullptr},                // Task 24
    {SceneId::Singularity, "Singularity", nullptr},      // Task 25
}};

// The scene each effect shows, one row per effect in show::Effect's order. The five Studio chapters show the
// Studio; the 1993-only chapters (BOOT, CREDITS, THE END) never show a 2026 picture
// and stay on the placeholder; each scene task moves its own chapters' rows to its scene (the comment).
constexpr std::array<std::pair<Effect, SceneId>, show::kEffectCount> kSceneOfEffect{{
    {Effect::Boot, SceneId::Placeholder},
    {Effect::Flyby, SceneId::Placeholder}, // the Studio's pull-out
    {Effect::Dots, SceneId::Particles},
    {Effect::Plasma, SceneId::Placeholder}, // Nebula
    {Effect::Tunnel, SceneId::Tunnel},
    {Effect::Glenz, SceneId::Glass},
    {Effect::Lens, SceneId::Lens},
    {Effect::Raytrace, SceneId::Spheres}, // Spheres
    {Effect::City, SceneId::City},
    {Effect::Finale, SceneId::Studio},
    {Effect::Portal, SceneId::Studio},
    {Effect::StudioNight, SceneId::Studio},
    {Effect::Ascent, SceneId::City},
    {Effect::CityNight, SceneId::City},
    {Effect::Nebula, SceneId::Placeholder},    // Nebula
    {Effect::Cathedral, SceneId::Glass},
    {Effect::Ocean, SceneId::Particles},
    {Effect::SphereStorm, SceneId::Spheres},     // Spheres
    {Effect::Singularity, SceneId::Placeholder}, // Singularity
    {Effect::Return, SceneId::Studio},
    {Effect::Reboot, SceneId::Studio},
    {Effect::Galaxy, SceneId::Placeholder},       // Cosmos
    {Effect::PlasmaPlanet, SceneId::Placeholder}, // Nebula
    {Effect::Wormhole, SceneId::Ship},            // the ship's crossing; the wormhole is Tunnel's
    {Effect::CrystalCity, SceneId::Glass},
    {Effect::LensOfGod, SceneId::Lens},
    {Effect::RaytraceInfinity, SceneId::Spheres}, // Spheres
    {Effect::Metropolis, SceneId::City},
    {Effect::Ascendant, SceneId::Placeholder}, // Cosmos
    {Effect::Credits, SceneId::Placeholder},
    {Effect::TheEnd, SceneId::Placeholder},
}};

constexpr bool tablesInOrder() {
    for (std::size_t i = 0; i < kSceneTable.size(); ++i)
        if (kSceneTable[i].id != static_cast<SceneId>(i))
            return false;
    for (std::size_t i = 0; i < kSceneOfEffect.size(); ++i) {
        if (kSceneOfEffect[i].first != static_cast<Effect>(i))
            return false;
        const SceneId scene = kSceneOfEffect[i].second;
        if (scene != SceneId::Placeholder && kSceneTable[static_cast<std::size_t>(scene)].make == nullptr)
            return false; // an effect may only show a scene that exists
    }
    return true;
}
static_assert(tablesInOrder(), "kSceneTable and kSceneOfEffect must be in id order, and every scene an "
                               "effect shows must have a make function");

} // namespace

std::span<const SceneEntry> sceneTable() {
    return kSceneTable;
}

SceneId sceneFor(show::Effect effect) {
    const auto index = static_cast<std::size_t>(effect);
    if (index >= kSceneOfEffect.size())
        throw std::out_of_range("sceneFor: no effect " + std::to_string(index));
    return kSceneOfEffect[index].second;
}

const char* sceneName(SceneId id) {
    const auto index = static_cast<std::size_t>(id);
    return index < kSceneTable.size() ? kSceneTable[index].name : "?";
}

const char* effectSceneName(show::Effect effect) {
    return sceneName(sceneFor(effect));
}

std::unique_ptr<ModernScene> makeScene(SceneId id, SceneDetail detail) {
    const auto index = static_cast<std::size_t>(id);
    if (index >= kSceneTable.size())
        throw std::out_of_range("makeScene: no scene " + std::to_string(index));
    const SceneEntry& entry = kSceneTable[index];
    return entry.make != nullptr ? entry.make(detail) : nullptr;
}

} // namespace evo::modern
