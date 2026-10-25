#pragma once

// Tunnel: the 2026 side of TUNNEL.
//
// TUNNEL. 1993's dot tunnel (retro/tunnel.*) drew 96 rings of 64 points flying at the camera along a path of
// summed sines, twisting slowly. Here every one of those points is a real light: a small glowing bead riding
// in the channel of a dark brushed-steel rib, one rib per 1993 ring, 6,144 beads in all, sampled through the
// light tree. The ribs stand in a tube of dark glazed ceramic panels laid in 1993's 16-segment checker, and
// the tube is full of a thin haze that the beads light. Same path, same ring spacing, same twist, same speed:
// the camera flies the 1993 flight exactly (Echo) until the accent, then eases off the axis and banks.
//
// The difference from 1993 is that this tunnel is real. 1993 faked the bends with a shear of view space; here
// the rings stand square to the path in the world and the camera looks along the path's tangent. The rings
// and the tube are static world geometry built once; only the beads move (they run round their ribs with the
// twist, and their 96 rings are recycled ahead of the camera as it flies), so they are a particle field
// (shaders/modern/particles_tunnel.comp, mirrored by tunnelLight) and the tree is rebuilt every frame.
//
// The music: the beads idle at a pilot glow until the downbeat before the accent (where the Portal crosses
// the screen: a 1993 dot on black is what the crossing must match). On that downbeat they ignite: a front
// rushes out of the far end towards the camera, ring after ring flaring as it passes, and reaches the camera
// on the accent, the first frame with the haze fully lit. From then on every beat sends a pulse of light
// down the rings, away from the camera.
//
// World units are 1993's (the tunnel's radius is 1), +y up. Screen right along 1993's +x, so the world's x is
// 1993's mirrored: a point (x, y) of 1993's path at depth w is (-x, y, w) here.

#include "modern/modern_scene.hpp"
#include "modern/scene.hpp"
#include "show/timeline.hpp"

#include <array>
#include <cstdint>
#include <span>

namespace evo::modern {

// --- 1993's tunnel, mirrored (evo_modern cannot link the retro code; scene_tunnel_test pins every value)
// ----

inline constexpr std::uint32_t kTunnelRetroRings = 96;    // retro::kTunnelRings
inline constexpr std::uint32_t kTunnelRetroDots = 64;     // retro::kTunnelRingDots
inline constexpr double kTunnelRetroRadius = 1.0;         // retro::kTunnelRadius
inline constexpr double kTunnelRingSpacing = 0.25;        // retro::kTunnelRingSpacing
inline constexpr double kTunnelSpeed = 3.0;               // retro::kTunnelSpeed, units per second
inline constexpr double kTunnelRetroNear = 0.5;           // retro::kTunnelNear
inline constexpr double kTunnelTwistRate = 0.35;          // retro::kTunnelTwistRate
inline constexpr double kTunnelTwistPerDepth = 0.08;      // retro::kTunnelTwistPerDepth
inline constexpr std::uint32_t kTunnelRetroSegments = 16; // retro::kTunnelSegments (the solid tube)
inline constexpr double kTunnelBandLength = 0.5;          // retro::kTunnelBandLength

struct TunnelWaveTerm {
    double amplitude, frequency, phase;
};
inline constexpr std::array<TunnelWaveTerm, 2> kTunnelWavesX = {{{2.6, 0.13, 0.0}, {1.0, 0.31, 1.1}}};
inline constexpr std::array<TunnelWaveTerm, 2> kTunnelWavesY = {{{1.6, 0.17, 0.6}, {0.6, 0.37, 2.3}}};

// --- The full-detail configuration (the card's number is pinned against it)
// ---------------------------------

// Every 1993 point is a light: 96 rings of 64 beads.
inline constexpr std::uint32_t kTunnelLights = kTunnelRetroRings * kTunnelRetroDots; // 6,144
// The 96 lit rings are 1993's 96, but their window starts kTunnelLightsBehind units behind 1993's near plane:
// the beads the camera passes keep burning behind it (so the light on the ribs round the camera never pops)
// and go out out of sight, kTunnelLightsBehind behind it; the far ones fade in over kTunnelFarFade at the far
// end, deep in the haze.
inline constexpr double kTunnelLightsBehind = 4.0;
inline constexpr double kTunnelLitNear = kTunnelRetroNear - kTunnelLightsBehind;                 // -3.5
inline constexpr double kTunnelLitFar = kTunnelLitNear + kTunnelRetroRings * kTunnelRingSpacing; // 20.5
inline constexpr double kTunnelFarFade = 5.0, kTunnelNearFade = 1.5;

inline constexpr float kTunnelBeadRadius = 0.0135f; // a bead (1993's unit is the tunnel's radius)
inline constexpr float kTunnelShellRadius = 1.36f;  // the ceramic tube's corners (16 sides)
// The static geometry covers depths kTunnelBuiltFrom .. kTunnelBuiltTo: the whole flight of a chapter of up
// to kTunnelLongestChapter seconds plus the view ahead.
inline constexpr double kTunnelBuiltFrom = -8.0, kTunnelLongestChapter = 24.0;
inline constexpr double kTunnelBuiltTo = kTunnelSpeed * kTunnelLongestChapter + 34.0;

// 1993's path P(w) (the 1993 x, y of the tunnel's axis at depth w) and its slope.
std::array<double, 2> tunnelPath1993(double w);
std::array<double, 2> tunnelSlope1993(double w);
// The twist at chapter-local `seconds` and depth w (1993's at retro frame seconds * 35).
double tunnelTwist1993(double seconds, double w);

// The tunnel's frame at depth w in the world: the axis point (-Px, Py, w), the unit tangent of the path, and
// the ring's right and up (right = tangent x up, up the world's +y made square to the tangent). A 1993 point
// at angle a on the ring at w lies at centre + radius (cos a right + sin a up).
struct TunnelRing {
    Vec3 centre, tangent, right, up;
};
TunnelRing tunnelRing(double w);

// How far the camera has flown at chapter-local `seconds`.
double tunnelTravel(double seconds);

// Beads per ring at a detail: 64 at full detail, down to 4 (64 sqrt(scale)); there are always 96 rings.
std::uint32_t tunnelDotsPerRing(SceneDetail detail);
std::uint32_t tunnelLightCount(SceneDetail detail);

// The particle shader's params (shaders/modern/particles_tunnel.comp): (count, dots per ring, bead radius,
// 0), the rest 0.
std::array<float, 16> tunnelLightParams(SceneDetail detail);

// The ring a bead rides in: particle id = slot * dots + dot. Slot k holds the ring j with j = k (mod 96)
// whose depth w = j * kTunnelRingSpacing lies in (s + kTunnelLitNear, s + kTunnelLitFar], s the travel: a
// bead keeps its ring until the ring falls behind and is recycled at the far end.
std::int64_t tunnelRingOfSlot(std::uint32_t slot, double travel);

// The C++ mirror of shaders/modern/particles_tunnel.comp's particle(): bead `id` at chapter-local `seconds`
// (centre, radius, colour and emission; material 0, prevCenter = centre).
rt::SphereRecord tunnelLight(std::uint32_t id, float seconds, std::span<const float, 16> params);

// The chapter's moments, chapter-local seconds: the downbeat at or before the accent (the bar line where the
// ignition starts; the accent itself without a beat grid there), the accent, and the beat and bar there.
struct TunnelMoments {
    double downbeat, accent, beatSeconds, barSeconds;
};
TunnelMoments tunnelMoments(const show::TimelineFrame& frame);

// The ignition: how far ahead of the camera the ignition front stands (beyond the lit window's far end until
// the downbeat, then rushing in, at the camera on the accent, then past it; continuous and never moving
// away), and the scale on a bead's emission `ahead` units in front of the camera at the frame (the pilot
// glow, lit once the front has passed, the flare as it passes, the beat pulses, the fades at both ends).
inline constexpr float kTunnelPilot = 0.1f; // the beads' idle glow before the ignition
double tunnelIgnitionFront(const show::TimelineFrame& frame);
float tunnelLightScale(const show::TimelineFrame& frame, double ahead);

// The camera of a Tunnel frame (a pure function of the effect, localSeconds, the beat grid and the chapter's
// fixed facts):
//   TUNNEL    one shot: until the accent, 1993's flight exactly (the eye on the axis at the travel, looking
//             along the tangent, no roll), 24 mm; over the bar after the accent it eases off the axis towards
//             the lower right wall, turns so the far end sits on the left third and banks into the bends,
//             then rolls slowly to the chapter's end.
Camera tunnelCamera(const show::TimelineFrame& frame);
// The 1993 camera's place and direction at a travel (the axis point and the tangent there).
Camera tunnelEchoCamera(double travel);

// The exposure (EV) of a frame, chosen by eye.
float tunnelExposureEV(const show::TimelineFrame& frame);

class Tunnel final : public ModernScene {
public:
    // Builds the meshes (ribs, the ceramic tube) and materials once (CPU only). The detail scales the beads
    // per ring only.
    explicit Tunnel(SceneDetail detail = {});

    enum Material : std::uint32_t {
        kRib,                   // dark brushed steel, three finishes
        kPanelA = kRib + 3,     // the tube's checker: blue-black glazed ceramic, three glazes
        kPanelB = kPanelA + 3,  // violet-black glazed ceramic, three glazes
        kBacking = kPanelB + 3, // the matte black behind the panels' seams
        kBead,                  // the beads (colour and emission per bead)
        kMaterialCount
    };
    enum Mesh : std::uint32_t {
        kRibMesh,                       // one rib, about the z axis (instanced along the path)
        kPanelMeshA,                    // the tube's panels of the checker's A squares, three glaze variants
        kPanelMeshB = kPanelMeshA + 3,  // and B
        kBackingMesh = kPanelMeshB + 3, // the tube behind the panels
        kMeshCount
    };
    static constexpr std::uint32_t kVariants = 3;

    const SceneStatic& staticScene() const override { return static_; }
    SceneFrame frame(const show::TimelineFrame& frame) const override;
    Camera camera(const show::TimelineFrame& frame) const override { return tunnelCamera(frame); }
    bool samplesRetro() const override { return false; }
    std::string nowCard(show::Effect effect) const override;

    SceneDetail detail() const { return detail_; }

private:
    SceneFrame tunnelFrame(const show::TimelineFrame& frame) const;

    SceneDetail detail_;
    SceneStatic static_;
};

} // namespace evo::modern
