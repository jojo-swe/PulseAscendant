#pragma once

#include <array>
#include <cstddef>

namespace evo::show {

// One effect per chapter, in chapter order. Act I (Boot to Portal) pairs each 1993 effect with its 2026
// counterpart; Act II (StudioNight to Credits) is 2026 only.
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
    Credits,
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
inline constexpr std::array<Cue, 21> cues = {{
    // Act I: Adventurous Uplift, 1993 against 2026.
    {0, Effect::Boot, Transition::Cut, .2f, 0, "BOOT", "1993 · MS-DOS · 320×200", ""},
    {10, Effect::Flyby, Transition::Portal, .6f, 0, "FLYBY",
        "1993 · 320×200 · 256 COLORS · FLAT SHADING", "2026 · PATH TRACED · VOLUMETRIC TRAILS"},
    {34, Effect::Dots, Transition::BeatSwap, .8f, 0, "DOTS",
        "1993 · 512 POINTS", "2026 · 2,097,152 PARTICLES"},
    {58, Effect::Plasma, Transition::Wipe, .7f, 0, "PLASMA",
        "1993 · PALETTE CYCLING", "2026 · VOLUMETRIC NEBULA"},
    {82, Effect::Tunnel, Transition::Portal, .8f, 0, "TUNNEL",
        "1993 · 320×200 · 35 FPS", "2026 · PATH TRACED REFLECTIONS"},
    {108, Effect::Glenz, Transition::BeatSwap, .9f, 0, "GLENZ",
        "1993 · 24 TRIANGLES · TRANSPARENT VECTORS", "2026 · REFRACTION · DISPERSION · CAUSTICS"},
    {134, Effect::Lens, Transition::Wipe, .7f, 0, "LENS",
        "1993 · ROTOZOOM · LENS", "2026 · GLASS SPHERE · RAY TRACED FLOOR"},
    {162, Effect::Raytrace, Transition::BeatSwap, 1.f, 0, "RAYTRACE",
        "1993 · PRECALCULATED · 8 FPS", "2026 · REAL-TIME PATH TRACING · 60 FPS"},
    {194, Effect::City, Transition::Wipe, .9f, 0, "VECTOR CITY",
        "1993 · 200 POLYGONS · GOURAUD", "2026 · MILLIONS OF WINDOWS · WET STREETS"},
    {226, Effect::Finale, Transition::BeatSwap, 1.f, 0, "FINALE", "1993 · ALL EFFECTS", "2026 · THE STUDIO"},
    // The pull-back into the Studio: the era reaches 2026 and Act I ends there.
    {258, Effect::Portal, Transition::Portal, .7f, 0, "PORTAL", "1993 · THE SCREEN", "2026 · THE STUDIO"},

    // Act II: Rigid Arpeggios, 2026 only; from the Studio at night and back to it.
    {289.92, Effect::StudioNight, Transition::Cut, .4f, 1, "STUDIO NIGHT", "",
        "2026 · THE STUDIO · PATH TRACED"},
    {313.92, Effect::Ascent, Transition::Cut, .7f, 1, "ASCENT", "", "2026 · ASCENT · VOLUMETRIC LIGHT"},
    {341.92, Effect::CityNight, Transition::Cut, .8f, 1, "CITY NIGHT", "", "2026 · CITY NIGHT · WET STREETS"},
    {373.92, Effect::Nebula, Transition::Cut, .6f, 1, "NEBULA", "", "2026 · NEBULA · VOLUMETRIC"},
    {405.92, Effect::Cathedral, Transition::Cut, .8f, 1, "GLASS CATHEDRAL", "",
        "2026 · GLASS CATHEDRAL · REFRACTION"},
    {437.92, Effect::Ocean, Transition::Cut, .9f, 1, "PARTICLE OCEAN", "",
        "2026 · PARTICLE OCEAN · 2,097,152 PARTICLES"},
    {469.92, Effect::SphereStorm, Transition::Cut, 1.f, 1, "SPHERE STORM", "",
        "2026 · SPHERE STORM · PATH TRACED"},
    {501.92, Effect::Singularity, Transition::Cut, .9f, 1, "SINGULARITY", "", "2026 · SINGULARITY"},
    {531.92, Effect::Return, Transition::Cut, .5f, 1, "RETURN", "", "2026 · RETURN · THE STUDIO"},
    {559.92, Effect::Credits, Transition::Cut, .4f, 0, "CREDITS", "", "1993 → 2026 · CREDITS"},
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

inline constexpr std::array<const char*, 21> effectNames = {
    "Boot",     "Flyby",     "Dots",   "Plasma",      "Tunnel",      "Glenz",  "Lens",
    "Raytrace", "City",      "Finale", "Portal",      "StudioNight", "Ascent", "CityNight",
    "Nebula",   "Cathedral", "Ocean",  "SphereStorm", "Singularity", "Return", "Credits"};

constexpr const char* effectName(Effect effect) {
    return effectNames[static_cast<int>(effect)];
}

constexpr const char* transitionName(Transition transition) {
    constexpr const char* names[] = {"Cut", "Portal", "BeatSwap", "Wipe"};
    return names[static_cast<int>(transition)];
}

} // namespace evo::show
