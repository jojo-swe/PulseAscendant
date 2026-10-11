#pragma once
#include "act1.hpp"
#include "music_data_act3.hpp"
namespace remix::act3 {
inline constexpr int number=3;
inline constexpr std::uint64_t frames=music_data_act3::frames;
inline constexpr double duration=music_data_act3::duration;
inline constexpr const char* title="ACT III / REALITY ASCENDANT";
inline constexpr const char* music="music/rigid-arpeggios.wav";
// Editorial targets. The music director selects the final onset/phrase cuts.
inline constexpr std::array<act1::Cue,24> cues={{
 {0,12,0,.75f,"SUNRISE OF GIANTS"},{10,12,1,.9f,"DUNE COLOSSUS"},
 {22,13,0,.85f,"THE LIVING CIRCUIT"},{34,14,0,.95f,"GRAVITY REBELLION"},
 {46,15,0,.9f,"ARCH OF TOMORROW"},{58,12,3,1,"SANDSTORM PURSUIT"},
 {70,13,1,1,"VERDANT SINGULARITY"},{82,14,1,1,"FALLING HORIZONS"},
 {94,15,1,1,"CRYSTAL WINGS"},{106,12,2,1,"CERAMIC TITANS"},
 {118,13,3,1,"SEED VELOCITY"},{130,14,3,1,"TIDAL FRACTURE"},
 {142,15,2,.95f,"REALITY CONVERGENCE"},{154,13,2,.75f,"BREATH OF LIGHT"},
 {166,12,0,.9f,"SUNFIRE ADVANCE"},{178,14,2,1,"ORBITAL UPRISING"},
 {190,13,1,1,"GROWTH CASCADE"},{202,15,3,1,"ASCENDANT ARRAY"},
 {214,12,3,1,"TITAN OVERDRIVE"},{226,14,1,1,"BREAK THE HORIZON"},
 {238,13,3,1,"INFINITE BLOOM"},{250,15,0,1,"THREE ACTS ONE REALITY"},
 {266,15,2,1,"THE FINAL ASCENT"},{282,15,1,.95f,"REALITY ASCENDANT"}
}};
}
