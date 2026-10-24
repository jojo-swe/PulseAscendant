#include "modern/modern_scene.hpp"
#include "modern/studio.hpp"

namespace evo::modern {

SceneId sceneFor(show::Effect effect) {
    switch (effect) {
    case show::Effect::Finale:
    case show::Effect::Portal:
    case show::Effect::StudioNight:
    case show::Effect::Return:
    case show::Effect::Reboot:
        return SceneId::Studio;
    default:
        return SceneId::Placeholder;
    }
}

std::unique_ptr<ModernScene> makeScene(SceneId id) {
    switch (id) {
    case SceneId::Studio:
        return std::make_unique<Studio>();
    case SceneId::Placeholder:
        break;
    }
    return nullptr;
}

} // namespace evo::modern
