#pragma once

#include <array>
#include <cstddef>

namespace evo::show {

// One effect per chapter, in chapter order. Act I (Boot to Portal) pairs each 1993 effect with its 2026
// counterpart; Act II (StudioNight to Return) is 2026 only; Act III (Reboot to TheEnd) boots 1993 again and
// brings every effect back at planetary scale, ending on the 1993 end screen.
enum class Effect {
    Boot,
    Flyby,
    Dots,
    Plasma,
    Tunnel,
    Glenz,
    Lens,
    Raytrace,
    City,
    Finale,
    Portal,
    StudioNight,
    Ascent,
    CityNight,
    Nebula,
    Cathedral,
    Ocean,
    SphereStorm,
    Singularity,
    Return,
    Reboot,
    Galaxy,
    PlasmaPlanet,
    Wormhole,
    CrystalCity,
    LensOfGod,
    RaytraceInfinity,
    Metropolis,
    Ascendant,
    Credits,
    TheEnd,
};

// How a chapter moves between the 1993 and 2026 looks.
enum class Transition { Cut, Portal, BeatSwap, Wipe };

// The epoch cards (src/render/epoch_cards.hpp), lines of the form "YEAR · FEAT · FEAT": thenCard is the
// 1993 card, nowCard the 2026 card, "" for none. They are the cards as written; the cards on screen take their
// numbers from the configuration (the retro constants and each scene's ModernScene::nowCard), which the
// tests pin against these lines.
//
// One authored chapter. target is the authored start in show seconds (the directed start in
// generated::chapterStarts lies on a tracked beat near it). drive scales how hard the chapter moves.
// eraFixed is the era a Cut chapter holds throughout (0 = 1993, 1 = 2026); other transitions ignore it.
struct Cue {
    double target;
    Effect effect;
    Transition transition;
    float drive;
    float eraFixed;
    const char* title;
    const char* thenCard;
    const char* nowCard;
};

// clang-format off
inline constexpr std::array<Cue, 31> cues = {{
    // Act I: Amiga Trance Demo, 1993 against 2026.
    {0, Effect::Boot, Transition::Cut, .2f, 0, "BOOT", "1993 · MS-DOS · 320×200", ""},
    {8, Effect::Flyby, Transition::Portal, .6f, 0, "FLYBY",
        "1993 · 320×200 · 256 COLORS · FLAT SHADING", "2026 · PATH TRACED · THE ROOM IS LIT BY THE SCREEN"},
    {26, Effect::Dots, Transition::BeatSwap, .8f, 0, "DOTS",
        "1993 · 512 POINTS · 35 FPS",
        "2026 · 2,097,152 TRACED PARTICLES · MOTION BLUR · DEPTH OF FIELD"},
    {44, Effect::Plasma, Transition::Wipe, .7f, 0, "PLASMA",
        "1993 · PALETTE CYCLING · 256 COLORS", "2026 · VOLUMETRIC NEBULA · DELTA TRACKING · MULTIPLE SCATTERING"},
    {62, Effect::Tunnel, Transition::Portal, .8f, 0, "TUNNEL",
        "1993 · 320×200 · 35 FPS · 6,144 POINTS",
        "2026 · THE POINTS BECAME LIGHTS · 6,144 LIGHTS · LIGHT TREE · FOG"},
    {80, Effect::Glenz, Transition::BeatSwap, .9f, 0, "GLENZ",
        "1993 · 24 TRIANGLES · TRANSPARENT VECTORS", "2026 · DISPERSION · 2,097,152 PHOTONS · CAUSTICS"},
    {98, Effect::Lens, Transition::Wipe, .7f, 0, "LENS",
        "1993 · ROTOZOOM · LENS", "2026 · THE PIXELS BECAME STONE · GLASS SPHERE · CAUSTICS"},
    {116, Effect::Raytrace, Transition::BeatSwap, 1.f, 0, "RAYTRACE",
        "1993 · PRECALCULATED · 10 FPS", "2026 · REAL-TIME PATH TRACING · 49 SPHERES · 60 FPS"},
    {150, Effect::City, Transition::Wipe, .9f, 0, "VECTOR CITY",
        "1993 · 300 POLYGONS · GOURAUD", "2026 · 4 MILLION WINDOWS · 41,839 LIGHTS · WET STREETS"},
    {168, Effect::Finale, Transition::BeatSwap, 1.f, 0, "FINALE", "1993 · ALL EFFECTS · ONE SCREEN",
        "2026 · EVERY SCENE · ONE PATH TRACER"},
    // The pull-back out of the CRT into the Studio: the era reaches 2026 and Act I ends there.
    {182, Effect::Portal, Transition::Portal, .7f, 0, "PORTAL",
        "1993 · THE SCREEN", "2026 · OUT OF THE SCREEN"},

    // Act II: Guitar Trance, 2026 only; from the Studio at night and back to it.
    {194, Effect::StudioNight, Transition::Cut, .4f, 1, "STUDIO NIGHT", "",
        "2026 · THE STUDIO · 480 INSTANCES · LIT BY ONE SCREEN AND ONE MOON"},
    {214, Effect::Ascent, Transition::Cut, .7f, 1, "ASCENT", "", "2026 · ASCENT · VOLUMETRIC CLOUDS · MOONLIGHT SHAFTS"},
    {238, Effect::CityNight, Transition::Cut, .8f, 1, "CITY NIGHT", "",
        "2026 · CITY NIGHT · 41,839 LIGHTS · LIGHT TREE · RAIN"},
    {264, Effect::Nebula, Transition::Cut, .6f, 1, "NEBULA", "",
        "2026 · NEBULA · 16,777,216 VOXELS · MULTIPLE SCATTERING · 3 STARS"},
    {292, Effect::Cathedral, Transition::Cut, .8f, 1, "GLASS CATHEDRAL", "",
        "2026 · GLASS CATHEDRAL · 64 GLASS PILLARS · 4,194,304 PHOTONS"},
    {322, Effect::Ocean, Transition::Cut, .9f, 1, "PARTICLE OCEAN", "",
        "2026 · 2,097,152 TRACED PARTICLES · ONE SHOT"},
    {344, Effect::SphereStorm, Transition::Cut, 1.f, 1, "SPHERE STORM", "",
        "2026 · 32,768 SPHERES · 4,096 OF THEM LIGHTS · LIGHT TREE · MOTION BLUR"},
    {368, Effect::Singularity, Transition::Cut, .9f, 1, "SINGULARITY", "",
        "2026 · GRAVITATIONAL LENSING · 48-STEP GEODESICS · DOPPLER DISC · PATH TRACED"},
    {390, Effect::Return, Transition::Cut, .5f, 1, "RETURN", "", "2026 · RETURN · THE STUDIO AT DAWN"},

    // Act III: Rigid Arpeggios. The Studio's CRT boots 1993 again, the camera goes through the glass and
    // every effect returns at planetary scale; it ends on the credits scroller and the 1993 end screen.
    {412.4, Effect::Reboot, Transition::Portal, .5f, 0, "REBOOT",
        "1993 · REBOOT", "2026 · THROUGH THE GLASS"},
    {430.4, Effect::Galaxy, Transition::Cut, .7f, 1, "GALAXY", "",
        "2026 · 16,777,216 STARS · 256×64×256 DUST · ONE SPLAT PASS"},
    {458.4, Effect::PlasmaPlanet, Transition::Cut, .7f, 1, "PLASMA PLANET", "",
        "2026 · PLASMA PLANET · 256×128×32 CLOUD SHELL · SUNRISE"},
    {486.4, Effect::Wormhole, Transition::Cut, .9f, 1, "WORMHOLE", "",
        "2026 · WORMHOLE · 1 KM RINGS · VOLUMETRIC · MOTION BLUR"},
    {514.4, Effect::CrystalCity, Transition::Cut, .8f, 1, "CRYSTAL CITY", "",
        "2026 · CRYSTAL CITY · 400 PRISMS · DISPERSION · CAUSTICS"},
    {542.4, Effect::LensOfGod, Transition::Cut, .7f, 1, "LENS OF GOD", "",
        "2026 · LENS OF GOD · 200 UNIT LENS · SUN FOCUS · PLANETARY SCALE"},
    {570.4, Effect::RaytraceInfinity, Transition::Cut, 1.f, 1, "RAYTRACE INFINITY", "",
        "2026 · 32,768 MIRROR SPHERES · INFINITE REFLECTIONS"},
    {602.4, Effect::Metropolis, Transition::Cut, .9f, 1, "METROPOLIS", "",
        "2026 · METROPOLIS · 41,839 LIGHTS · 4 MILLION WINDOWS · PATH TRACED"},
    {634.4, Effect::Ascendant, Transition::Cut, 1.f, 1, "ASCENDANT", "",
        "2026 · 2,097,152 PARTICLES · 16,777,216 STARS · ASCENDANT"},
    // The credits roll as a 1993 sine scroller, so the chapter holds 1993 and its card is the 1993 one.
    {666.4, Effect::Credits, Transition::Cut, .4f, 0, "CREDITS", "1993 → 2026 · CREDITS", ""},
    {696.4, Effect::TheEnd, Transition::Cut, .2f, 0, "THE END", "", ""},
}};
// clang-format on

// The credits, set once in the 2026 card face in the margins beside the CREDITS scroller.
inline constexpr const char* kCreditsLine = "CODE: JOJO \u00B7 MUSIC: JOYOLO \u00B7 CONCEPT: JOHN";

// One effect per chapter, in enum order.
constexpr bool effectsInChapterOrder() {
    for (std::size_t i = 0; i < cues.size(); ++i)
        if (static_cast<std::size_t>(cues[i].effect) != i)
            return false;
    return true;
}
static_assert(effectsInChapterOrder(), "cues must list one effect per chapter in Effect order");

// How many effects (and chapters) there are: Effect's values run from 0 to kEffectCount - 1.
inline constexpr std::size_t kEffectCount = cues.size();
static_assert(static_cast<std::size_t>(Effect::TheEnd) + 1 == kEffectCount, "TheEnd is the last effect");

inline constexpr std::array<const char*, kEffectCount> effectNames = {
    "Boot",        "Flyby",        "Dots",
    "Plasma",      "Tunnel",       "Glenz",
    "Lens",        "Raytrace",     "City",
    "Finale",      "Portal",       "StudioNight",
    "Ascent",      "CityNight",    "Nebula",
    "Cathedral",   "Ocean",        "SphereStorm",
    "Singularity", "Return",       "Reboot",
    "Galaxy",      "PlasmaPlanet", "Wormhole",
    "CrystalCity", "LensOfGod",    "RaytraceInfinity",
    "Metropolis",  "Ascendant",    "Credits",
    "TheEnd"};

constexpr const char* effectName(Effect effect) {
    return effectNames[static_cast<int>(effect)];
}

constexpr const char* transitionName(Transition transition) {
    constexpr const char* names[] = {"Cut", "Portal", "BeatSwap", "Wipe"};
    return names[static_cast<int>(transition)];
}

} // namespace evo::show
