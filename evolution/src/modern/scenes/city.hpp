#pragma once

// The City (step 5, Task 23): VECTOR CITY, ASCENT, CITY NIGHT and METROPOLIS. One night city on the 1993 city's
// grid (retro/city_plan.hpp: streets kCityStreet wide every kCityCell, the lots and buildings of cityLot), so
// a wipe between the eras shows the same streets, grown into a city of about 2 by 3 km with a downtown of
// towers up to 330 m:
// - Towers are instanced boxes and cylinders (set-back tiers, podiums, crowns, rooftop plant, antennas) under
//   procedural facades (shaders/rt/city_patterns.glsl): punched windows in brick, stone and concrete, glass
//   curtain walls with mullions, ribbon bands and round glass towers. Every window is a cell of the facade
//   pattern with its own lit or dark state, a slow clock that switches it, and a room behind its glass seen in
//   parallax: millions of windows at no geometry cost.
// - Every street is wet asphalt: a water film over a darker base, with puddles and ruts filling first, lane
//   markings, zebra crossings and manholes. The streets reflect the lights the way wet streets do.
// - Every lot has eight street lamps on its kerbs, and every tower over kBeaconHeight a red aviation beacon on
//   its roof: tens of thousands of emissive spheres, all of them lights of the light tree (Task 12). Their
//   positions never change, so a frame only refits the tree for their scales (lamps switching on, beacons
//   blinking): the picture has every one of them as a light, path traced, nothing baked.
// - Shop signs are rect lights (lightboxes and blades) along the two streets the cameras walk.
// - Traffic: cars on fixed lanes per chapter, each a dark body and a light trail (a stretched emissive box
//   whose glow fades from the car to its tail, kPatternCityTrail), as in a long exposure. Their positions are
//   pure functions of the time.
// - Splats (shaders/modern/city_splats.comp): the glow of the haze around every lamp near the camera (single
//   scattering of each lamp's light, sampled as points around it), and in CITY NIGHT the rain, each drop a
//   short streak stretched along its fall and lit by the lamps near it.
//
// World units are metres, +y up; the main street of 1993 runs along +z at x = 0. Chapters:
//   VECTOR CITY  the 1993 flight (retro::cityPoseAt) at dusk, the sun setting at the main street's end; at the
//                accent the street lamps switch on in a wave that runs from the camera to the horizon
//   ASCENT       out of the window of a room in an apartment tower (the Studio's window: the same size, the
//                moon high beyond it) and up its facade, over the roofs and high above the city as the
//                beacons of the downtown flash together on the accent
//   CITY NIGHT   in the rain: down a wet shopping street at eye level among the signs, then from the accent
//                straight down onto its junction, the traffic trails crossing, then a crane up the facades
//   METROPOLIS   along an elevated avenue through the downtown at speed (the export shutter blurs it), and
//                from the accent up off the avenue to the whole city to the horizon, its beacons on the beat

#include "modern/environment.hpp"
#include "modern/modern_scene.hpp"
#include "modern/scene.hpp"
#include "show/timeline.hpp"

#include <cstdint>
#include <vector>

namespace evo::modern {

// The grid of lots at full detail: i (x) and m (z) in [kCityMinLot, kCityMaxLot). A lower detail keeps only
// the lots near the chapters' cameras (cityLotKept), so tests build a small part of the same city.
inline constexpr int kCityMinLotI = -24, kCityMaxLotI = 48, kCityMinLotM = -20, kCityMaxLotM = 52;
// Street lamps per lot (two on each kerb), their head's height and radius.
inline constexpr std::uint32_t kCityLampsPerLot = 8;
inline constexpr float kCityLampHeight = 8.2f, kCityLampRadius = 0.2f;
// Buildings at least this high carry a red aviation beacon.
inline constexpr float kBeaconHeight = 70.f;
// The downtown: its centre on the ground and how far its towers rise above the 1993 heights.
inline constexpr float kDowntownX = 1281.f, kDowntownZ = 1281.f, kDowntownRadius = 520.f;
// METROPOLIS's elevated avenue: along z over the street at x = kAvenueX from kAvenueStartZ to kAvenueEndZ, its
// deck kAvenueDeck high through the old town, then climbing to a crest kAvenueCrest high (cityAvenueHeight).
inline constexpr float kAvenueX = 1260.f, kAvenueDeck = 24.f, kAvenueCrest = 150.f;
inline constexpr float kAvenueStartZ = -400.f, kAvenueEndZ = 2150.f;
// The deck's height at z: kAvenueDeck to z = 330, climbing (smoothstep) to kAvenueCrest at z = 700, level to
// 900, down to 60 m at 1250 and level on into the downtown.
float cityAvenueHeight(float z);
// CITY NIGHT's street: along z at x = kNightStreetX.
inline constexpr float kNightStreetX = 252.f;

// The lots a detail keeps: every lot at detail 1, else the lots within a radius (that shrinks with the
// detail, at least 260 m) of the chapters' camera paths.
bool cityLotKept(int i, int m, SceneDetail detail);

// A street lamp's head (the centre of its light sphere): lamp k in [0, kCityLampsPerLot) of lot (i, m). Lamps
// 0-1 stand on the lot's low-z kerb, 2-3 on its high-z kerb, 4-5 on its low-x kerb and 6-7 on its high-x kerb,
// at a quarter and three quarters along it, each on an arm reaching over the street. Mirrored in
// shaders/modern/city_splats.comp.
Vec3 cityLampHead(int i, int m, std::uint32_t k);

// VECTOR CITY's switch-on wave: it starts on D (the downbeat at or before the accent, where the wipe completes)
// from (kWaveX, kWaveZ) (over the main street, near the camera then) and runs out at kWaveSpeed m/s.
inline constexpr float kWaveX = 0.f, kWaveZ = 160.f, kWaveSpeed = 420.f;

// A lamp's scale this frame (TreeLightFrame::scale): 1 lit, 0 off. `wave` is its distance from the wave's
// start, `hash` its own (the lot's cityHash with the lamp's salt). In VECTOR CITY (`switching`) a few lamps are
// lit from the start and the rest strike in the wave from `start` (D), flickering for a moment; a few lamps
// are dead everywhere. Mirrored in shaders/modern/city_splats.comp.
float cityLampScale(bool switching, float seconds, float start, float wave, std::uint32_t hash);

// The downbeat `bars` bars after D (before it, for a negative count), in seconds into the chapter: D is the
// downbeat at or before the accent (the chapter's beat grid). The City's cuts and cues fall on these.
float cityBar(const show::TimelineFrame& frame, int bars);

// What the full city holds, for the epoch card (the City at detail 1 computes it; its test pins it).
struct CityStats {
    std::uint32_t treeLights = 0; // every lamp, beacon and avenue lamp: lights of the light tree
    std::uint32_t lamps = 0, beacons = 0;
    std::uint32_t buildings = 0;
    std::uint64_t windows = 0; // window cells of the facade patterns, every face of every building
    std::uint32_t signLights = 0;
};

// The epoch card's numbers: the full city's light-tree lights (every lamp and beacon, each path traced as a
// light) and at least this many window cells; the test pins them against the City at detail 1.
inline constexpr std::uint32_t kCityCardLights = 41839;
inline constexpr std::uint64_t kCityCardWindows = 4000000;
inline constexpr const char* kCityCardLine = "41,839 LIGHTS \u00B7 4 MILLION WINDOWS \u00B7 PATH TRACED \u00B7 NO BAKING";

// A camera shot of a City chapter: from start to end in seconds into the chapter.
struct CityShot {
    float start, end;
};
// The shots of a chapter in order; they cover [0, chapterDuration]. Cuts fall on the accent and on bar lines.
std::vector<CityShot> cityShots(const show::TimelineFrame& frame);

// The camera of a City chapter's frame: a pure function of the chapter's effect, localSeconds,
// chapterDuration, accentSeconds and the beat grid (the bar lines its cuts fall on).
Camera cityCamera(const show::TimelineFrame& frame);

// The exposure (EV, before the tonemap) of a City frame, authored per shot by eye.
float cityExposureEV(const show::TimelineFrame& frame);

// The sky of a City chapter: VECTOR CITY's dusk with the sun on the horizon at the main street's end, the
// others a night sky with a high moon over the downtown and the city's glow along the horizon.
EnvironmentDesc cityEnvironment(show::Effect effect);

class City final : public ModernScene {
public:
    explicit City(SceneDetail detail = {});

    const SceneStatic& staticScene() const override { return static_; }
    SceneFrame frame(const show::TimelineFrame& frame) const override;
    Camera camera(const show::TimelineFrame& frame) const override { return cityCamera(frame); }
    bool samplesRetro() const override { return false; }

    const CityStats& stats() const { return stats_; }
    // The tree's lights in the order of the static spheres: lamps first (lot by lot), then the avenue's lamps,
    // then the beacons.
    std::uint32_t lampCount() const { return lampCount_; }
    std::uint32_t beaconCount() const { return beaconCount_; }
    // The static instances every frame starts with (ground, lots, buildings, signs, the light tree's spheres).
    std::uint32_t staticInstanceCount() const { return static_cast<std::uint32_t>(instances_.size()); }

    // The material that is the street's asphalt (its coat is the wetness), and its dry and wet levels.
    static constexpr std::uint32_t kAsphaltMaterial = 0;

private:
    struct Lane {
        Vec3 origin;    // the lane's start, on the road (y = the road's surface)
        Vec3 direction; // unit, along the road
        float length, speed;
        std::uint32_t cars;
        std::uint32_t seed;
        bool head; // the trail is white (head lights, coming towards the chapter's view) or red (tail lights)
    };

    void buildCity(SceneDetail detail);
    std::vector<Lane> lanesFor(show::Effect effect) const;
    void addTraffic(SceneFrame& f, show::Effect effect, float seconds) const;
    SplatFrame splatsFor(const show::TimelineFrame& frame) const;

    SceneStatic static_;
    std::vector<SceneInstance> instances_;
    std::vector<rt::RectLight> lights_;
    std::vector<TreeLightFrame> treeLights_;
    std::vector<float> lampWave_; // per tree light: its distance along VECTOR CITY's switch-on wave
    std::vector<std::uint32_t> lampHash_; // per tree light: its hash (cityLampScale)
    std::vector<std::uint32_t> signMaterial_; // per sign light (lights 1..): its material
    std::uint32_t lampCount_ = 0, beaconCount_ = 0;
    std::uint32_t treeInstance_ = 0;
    SceneDetail detail_;
    CityStats stats_;
};

} // namespace evo::modern
