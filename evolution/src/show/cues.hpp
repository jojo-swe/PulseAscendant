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
        "1993 · 320×200 · 256 COLORS · FLAT SHADING", "2026 · PATH TRACED · VOLUMETRIC TRAILS"},
    {26, Effect::Dots, Transition::BeatSwap, .8f, 0, "DOTS",
        "1993 · 512 POINTS", "2026 · 2,097,152 PARTICLES"},
    {44, Effect::Plasma, Transition::Wipe, .7f, 0, "PLASMA",
        "1993 · PALETTE CYCLING", "2026 · VOLUMETRIC NEBULA"},
    {62, Effect::Tunnel, Transition::Portal, .8f, 0, "TUNNEL",
        "1993 · 320×200 · 35 FPS", "2026 · PATH TRACED REFLECTIONS"},
    {80, Effect::Glenz, Transition::BeatSwap, .9f, 0, "GLENZ",
        "1993 · 24 TRIANGLES · TRANSPARENT VECTORS", "2026 · REFRACTION · DISPERSION · CAUSTICS"},
    {98, Effect::Lens, Transition::Wipe, .7f, 0, "LENS",
        "1993 · ROTOZOOM · LENS", "2026 · GLASS SPHERE · RAY TRACED FLOOR"},
    {116, Effect::Raytrace, Transition::BeatSwap, 1.f, 0, "RAYTRACE",
        "1993 · PRECALCULATED · 8 FPS", "2026 · REAL-TIME PATH TRACING · 60 FPS"},
    {150, Effect::City, Transition::Wipe, .9f, 0, "VECTOR CITY",
        "1993 · 200 POLYGONS · GOURAUD", "2026 · MILLIONS OF WINDOWS · WET STREETS"},
    {168, Effect::Finale, Transition::BeatSwap, 1.f, 0, "FINALE", "1993 · ALL EFFECTS", "2026 · THE STUDIO"},
    // The pull-back out of the CRT into the Studio: the era reaches 2026 and Act I ends there.
    {182, Effect::Portal, Transition::Portal, .7f, 0, "PORTAL",
        "1993 · THE SCREEN", "2026 · OUT OF THE SCREEN"},

    // Act II: Guitar Trance, 2026 only; from the Studio at night and back to it.
    {194, Effect::StudioNight, Transition::Cut, .4f, 1, "STUDIO NIGHT", "",
        "2026 · THE STUDIO · PATH TRACED"},
    {214, Effect::Ascent, Transition::Cut, .7f, 1, "ASCENT", "", "2026 · ASCENT · VOLUMETRIC LIGHT"},
    {238, Effect::CityNight, Transition::Cut, .8f, 1, "CITY NIGHT", "", "2026 · CITY NIGHT · WET STREETS"},
    {264, Effect::Nebula, Transition::Cut, .6f, 1, "NEBULA", "", "2026 · NEBULA · VOLUMETRIC"},
    {292, Effect::Cathedral, Transition::Cut, .8f, 1, "GLASS CATHEDRAL", "",
        "2026 · GLASS CATHEDRAL · REFRACTION"},
    {322, Effect::Ocean, Transition::Cut, .9f, 1, "PARTICLE OCEAN", "",
        "2026 · PARTICLE OCEAN · 2,097,152 PARTICLES"},
    {344, Effect::SphereStorm, Transition::Cut, 1.f, 1, "SPHERE STORM", "",
        "2026 · SPHERE STORM · PATH TRACED"},
    {368, Effect::Singularity, Transition::Cut, .9f, 1, "SINGULARITY", "", "2026 · SINGULARITY"},
    {390, Effect::Return, Transition::Cut, .5f, 1, "RETURN", "", "2026 · RETURN · THE STUDIO"},

    // Act III: Rigid Arpeggios. The Studio's CRT boots 1993 again, the camera goes through the glass and
    // every effect returns at planetary scale; it ends on the 1993 end screen.
    {412.4, Effect::Reboot, Transition::Portal, .5f, 0, "REBOOT",
        "1993 · REBOOT", "2026 · THROUGH THE GLASS"},
    {430.4, Effect::Galaxy, Transition::Cut, .7f, 1, "GALAXY", "", "2026 · GALAXY · 16,777,216 POINTS"},
    {458.4, Effect::PlasmaPlanet, Transition::Cut, .7f, 1, "PLASMA PLANET", "",
        "2026 · PLASMA PLANET · VOLUMETRIC"},
    {486.4, Effect::Wormhole, Transition::Cut, .9f, 1, "WORMHOLE", "", "2026 · WORMHOLE"},
    {514.4, Effect::CrystalCity, Transition::Cut, .8f, 1, "CRYSTAL CITY", "",
        "2026 · CRYSTAL CITY · REFRACTION"},
    {542.4, Effect::LensOfGod, Transition::Cut, .7f, 1, "LENS OF GOD", "", "2026 · LENS · PLANETARY SCALE"},
    {570.4, Effect::RaytraceInfinity, Transition::Cut, 1.f, 1, "RAYTRACE INFINITY", "",
        "2026 · RAY TRACED INFINITY"},
    {602.4, Effect::Metropolis, Transition::Cut, .9f, 1, "METROPOLIS", "", "2026 · METROPOLIS"},
    {634.4, Effect::Ascendant, Transition::Cut, 1.f, 1, "ASCENDANT", "", "2026 · ASCENDANT"},
    {666.4, Effect::Credits, Transition::Cut, .4f, 1, "CREDITS", "", "1993 → 2026 · CREDITS"},
    {696.4, Effect::TheEnd, Transition::Cut, .2f, 0, "THE END", "1993 · THE END", ""},
}};
// clang-format on

// One effect per chapter, in enum order.
constexpr bool effectsInChapterOrder() {
    for (std::size_t i = 0; i < cues.size(); ++i)
        if (static_cast<std::size_t>(cues[i].effect) != i)
            return false;
    return true;
}
static_assert(effectsInChapterOrder(), "cues must list one effect per chapter in Effect order");

inline constexpr std::array<const char*, 31> effectNames = {"Boot",        "Flyby",        "Dots",
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
static_assert(effectNames.size() == cues.size(), "one name per effect");

constexpr const char* effectName(Effect effect) {
    return effectNames[static_cast<int>(effect)];
}

constexpr const char* transitionName(Transition transition) {
    constexpr const char* names[] = {"Cut", "Portal", "BeatSwap", "Wipe"};
    return names[static_cast<int>(transition)];
}

} // namespace evo::show
