#pragma once
#include "act1.hpp"
#include "music_data_act2.hpp"
namespace remix::act2 {
inline constexpr int number=2;
inline constexpr std::uint64_t frames=music_data_act2::frames;
inline constexpr double duration=music_data_act2::duration;
inline constexpr const char* title="ACT II / ADVENTUROUS UPLIFT";
inline constexpr const char* music="music/adventurous-uplift.wav";
inline constexpr std::array<act1::Cue,21> cues={{
 {0,8,0,.7f,"CHROME AWAKENING"},{12,8,1,.85f,"CATHEDRAL OF LIGHT"},{26,9,0,.9f,"RAIN DISTRICT"},
 {40,10,0,.85f,"ORBITAL FOUNDRY"},{54,11,0,.8f,"PRISM CHAMBER"},{68,9,1,1,"NEON PURSUIT"},
 {82,8,2,1,"LIQUID CHROME"},{96,10,1,1,"KINETIC ARRAY"},{110,11,1,.95f,"SPECTRAL REFRACTION"},
 {124,9,2,1,"WET CIRCUIT"},{138,10,2,1,"MACHINE HEART"},{152,8,3,.9f,"GOLDEN ASCENT"},
 {166,11,2,.7f,"INNER LIGHT"},{180,9,3,.9f,"MIDNIGHT VELOCITY"},{194,10,3,1,"ORBITAL COLLISION"},
 {208,8,0,1,"CHROME RESONANCE"},{222,11,3,1,"PRISM BREAK"},{238,9,0,1,"ELECTRIC HORIZON"},
 {254,10,1,1,"UPLIFT"},{270,8,2,1,"CATHEDRAL FINALE"},{282,11,0,.85f,"BEYOND THE PRISM"}
}};
}
