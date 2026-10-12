#pragma once
#include <array>
#include <cstdint>
namespace remix::act1 {
// An act owns its music and editorial timing. Act II gets its own data and
// renderer when its soundtrack arrives; it must not extend this audio clock.
inline constexpr int number=1;
inline constexpr std::uint64_t frames=14400000;
inline constexpr double duration=frames/48000.0;
inline constexpr const char* title="ACT I / FULL THROTTLE";
inline constexpr const char* music="music/luminous-flow-act1.wav";
struct Cue {double start;int scene,variant;float drive;const char* title;};
inline constexpr std::array<Cue,31> cues={{
 {0,5,0,.35f,"IGNITION"},{8,0,0,.7f,"PHOTON LAUNCH"},{18,3,0,.75f,"NEON DESCENT"},
 {28,6,0,.6f,"QUANTUM OCEAN"},{38,4,0,.85f,"FRACTURE GATE"},{46,0,1,1,"HYPERDRIVE"},
 {56,3,1,1,"SKYLINE CHASE"},{66,2,0,.95f,"REACTOR OVERDRIVE"},{76,5,1,.95f,"ORBITAL STORM"},
 {86,4,1,.9f,"INFINITE MACHINE"},{96,1,1,.8f,"MATTER COLLAPSE"},{106,7,2,1,"ORBITAL ARMADA"},
 {116,0,2,1,"CORE DIVE"},{126,2,1,1,"CRYSTAL ACCELERATOR"},{136,4,2,1,"FRACTURE CASCADE"},
 {146,5,2,1,"SUPERNOVA"},{156,3,3,1,"TERMINAL VELOCITY"},{166,7,3,1,"EVENT HORIZON"},
 {178,1,2,.7f,"AFTERSHOCK"},{188,6,3,.3f,"ZERO GRAVITY"},{198,2,2,.45f,"REBUILD"},
 {208,7,3,.65f,"RESONANCE"},{218,6,3,.65f,"REBIRTH"},{228,3,0,.9f,"SECOND IGNITION"},
 {238,0,1,1,"FULL THROTTLE"},{248,4,0,.95f,"VECTOR STORM"},{258,5,0,.95f,"SOLAR WIND"},
 {268,2,3,1,"CRYSTAL SUPREMACY"},{278,7,1,1,"FINAL PURSUIT"},{286,5,2,1,"LIGHTSPEED"},
 {294,2,0,.85f,"ASCENT VECTOR"}
}};
}
