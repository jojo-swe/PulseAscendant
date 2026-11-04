#include "modern/modern_scene.hpp"
#include "modern/scene.hpp"
#include "modern/scenes/city.hpp"
#include "modern/scenes/glass.hpp"
#include "modern/scenes/lens.hpp"
#include "modern/scenes/cosmos.hpp"
#include "modern/scenes/nebula.hpp"
#include "modern/scenes/particles.hpp"
#include "modern/scenes/singularity.hpp"
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
    {SceneId::Ship, "Ship", &make<Ship>},
    {SceneId::Particles, "Particles", &make<Particles>},
    {SceneId::Nebula, "Nebula", &make<Nebula>},
    {SceneId::Tunnel, "Tunnel", &make<Tunnel>},
    {SceneId::Glass, "Glass", &make<Glass>},
    {SceneId::Lens, "Lens", &make<Lens>},
    {SceneId::Spheres, "Spheres", &make<Spheres>},
    {SceneId::City, "City", &make<City>},
    {SceneId::Cosmos, "Cosmos", &make<Cosmos>},
    {SceneId::Singularity, "Singularity", &make<Singularity>},
}};

// The scene each effect shows, one row per effect in show::Effect's order. The six Studio chapters show the
// Studio; the 1993-only chapters (BOOT, CREDITS, THE END) never show a 2026 picture
// and stay on the placeholder; each scene task moves its own chapters' rows to its scene (the comment).
constexpr std::array<std::pair<Effect, SceneId>, show::kEffectCount> kSceneOfEffect{{
    {Effect::Boot, SceneId::Placeholder},
    {Effect::Flyby, SceneId::Studio}, // the pull-out from the CRT reveals the room
    {Effect::Dots, SceneId::Particles},
    {Effect::Plasma, SceneId::Nebula},
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
    {Effect::Nebula, SceneId::Nebula},
    {Effect::Cathedral, SceneId::Glass},
    {Effect::Ocean, SceneId::Particles},
    {Effect::SphereStorm, SceneId::Spheres},     // Spheres
    {Effect::Singularity, SceneId::Singularity},
    {Effect::Return, SceneId::Studio},
    {Effect::Reboot, SceneId::Studio},
    {Effect::Galaxy, SceneId::Cosmos},
    {Effect::PlasmaPlanet, SceneId::Nebula},
    {Effect::Wormhole, SceneId::Ship},            // the ship's crossing; the wormhole is Tunnel's
    {Effect::CrystalCity, SceneId::Glass},
    {Effect::LensOfGod, SceneId::Lens},
    {Effect::RaytraceInfinity, SceneId::Spheres}, // Spheres
    {Effect::Metropolis, SceneId::City},
    {Effect::Ascendant, SceneId::Cosmos},
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
