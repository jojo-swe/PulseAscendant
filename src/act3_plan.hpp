#pragma once
#include <array>
#include <cstdint>
#include <limits>
#include <optional>
#include "act3.hpp"

// The initial reservation now resolves to the supplied track's real definition.
namespace remix::act3_plan {
inline constexpr int number=3;
inline constexpr bool available=true;
inline constexpr const char* title="ACT III / REALITY ASCENDANT";
inline constexpr const char* music=act3::music;
inline constexpr const char* sourceMusic="music/source/Rigid Arpeggios.mp3";
inline constexpr std::optional<std::uint64_t> decodedFrames=act3::frames;

inline constexpr int sampleRate=48000;
inline constexpr int channels=2;
inline constexpr int spectrumBands=24;
inline constexpr int analysisRate=20;

enum class Scene : int {
 DuneColossus=12,
 VerdantSingularity=13,
 GravityRebellion=14,
 RealityAscendant=15
};
struct PlannedScene {
 Scene scene;
 const char* title;
 const char* direction;
};
inline constexpr std::array<PlannedScene,4> scenes={{
 {Scene::DuneColossus,"DUNE COLOSSUS","Sunlit dunes, ceramic giants and sweeping sand fronts"},
 {Scene::VerdantSingularity,"VERDANT SINGULARITY","Branching luminous growth and flowing seed constellations"},
 {Scene::GravityRebellion,"GRAVITY REBELLION","Floating stone terrain, falling horizons and tidal debris"},
 {Scene::RealityAscendant,"REALITY ASCENDANT","A musical finale that reunites transformed motifs from all acts"}
}};

// The first Act III frame immediately follows the last decoded Act II frame.
// Integer frames are authoritative; convert to seconds only at API boundaries.
// No guessed Act II length or floating-point duration is stored in this plan.
constexpr std::optional<std::uint64_t> startFrame(std::uint64_t act1Frames,
                                                 std::uint64_t act2Frames) {
 if(act2Frames>std::numeric_limits<std::uint64_t>::max()-act1Frames)return std::nullopt;
 return act1Frames+act2Frames;
}
}
