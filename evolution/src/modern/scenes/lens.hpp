#pragma once

// The Lens scene: LENS and LENS OF GOD, real glass bending light over the 1993 rotozoom's
// picture turned to stone. Everything the glass does on screen is traced: the mosaic seen through it is
// refracted and magnified, its rim splits white light into fringes by wavelength (the integrator's hero
// wavelength through dispersive glass), and the sunlight it gathers lands as a caustic photon map on the
// stone (photons shot from the sun's disc through the glass, each at one wavelength, so the focus burns white
// with spectral rings round it).
//
// The floor of both chapters is the 1993 rotozoom's 128x128 texture laid as a mosaic (the Checker hit group's
// Tesserae patterns, shaders/rt/tesserae.glsl): every 1993 pixel a polished tessera in its palette colour,
// marble for the light shades, basalt for the dark, gold leaf for the logo's chrome, each set by hand a
// degree off true, grey mortar in the joints.
//
// LENS: until the chapter's settling downbeat (D, where the wipe completes) the camera looks straight down on
// the mosaic and turns and zooms exactly as the 1993 rotozoom does (rotozoomAngle and rotozoomZoom, the
// formulas of retro/rotozoom.hpp, one tessera per texel), so the wipe shows one picture twice. From D a low
// 50 mm camera tracks a ball of dense flint glass (kLensBallRadius, IOR kLensBallIor, Abbe kLensBallAbbe)
// rolling along a row of tesserae below the logo: it magnifies the gold letters behind it, mirrors itself in
// the stone, and drags its caustic, a white spot with a spectrum, two tesserae behind it (the sun stands 45
// degrees up straight ahead of it, the camera three-quarters behind on the near side, the ball on the right
// third). It rolls one tessera per beat (its contact point crosses a joint on every beat), the spot with it,
// the accent's beat among them.
//
// LENS OF GOD, the same at planetary scale: the planet (an analytic sphere of radius kLensPlanetRadius) is the
// mosaic again, kPlanetTessera units a tessera, so the EVOLUTION logo reads from orbit; a flint lens
// kGodLensDiameter units across in a brushed gold bezel hovers over it, its axis on the afternoon sun. Shots,
// cut on bar lines: from orbit with the sun behind the camera; down to the lens's rim, the tesserae below
// seen through it magnified (the lens hangs inside its focal length, rising); from D a view down on the
// lens's shadow from the side: the lens reaches its focus on D, the focus walks across the stone and on the
// accent stops on the camera's foreground tessera, which goes white-hot and cools to an ember; then the pull
// back to orbit. The focus lies at the world's origin (on top of the planet), so nothing strays beyond
// |coordinate| 10^4.
//
// The cards' numbers are this file's constants (scene_lens_test pins them): lensEpochCard().

#include "modern/modern_scene.hpp"
#include "modern/scene.hpp"
#include "rt/gpu_types.hpp"
#include "show/timeline.hpp"

#include <cstdint>
#include <vector>

namespace evo::modern {

// --- The glass, the stone and the photons (the cards' numbers) --------------------------------------------

inline constexpr std::uint32_t kLensPhotons = 1u << 20;    // per frame at full detail (an export at >= 16 spp
inline constexpr std::uint32_t kGodLensPhotons = 1u << 20; // shoots kExportPhotonFactor times as many)
inline constexpr std::uint32_t kMinLensPhotons = 1u << 12; // at the smallest detail
inline constexpr std::uint32_t kRotozoomTesserae = 128u * 128u; // the 1993 texture's texels, one stone each
inline constexpr float kLensBallRadius = 0.5f;                  // metres
inline constexpr float kLensBallIor = 1.75f, kLensBallAbbe = 22.f; // dense flint: wide fringes
inline constexpr float kTessera = 0.25f;                         // LENS: metres a tessera (1 m per 4 pixels)
inline constexpr float kGodLensDiameter = 200.f;                 // LENS OF GOD's units
inline constexpr float kGodLensIor = 1.62f, kGodLensAbbe = 30.f; // flint
inline constexpr float kGodLensFocalLength = 420.f; // the thin-lens design; godLensFocusDistance() is traced
inline constexpr float kLensPlanetRadius = 4000.f;
inline constexpr float kPlanetTessera = 25.f; // the lens spans 8 tesserae, its focus fits in one
inline constexpr std::uint32_t kLensReceiverResolution = 512; // LENS's photon map, texels per side
inline constexpr std::uint32_t kGodReceiverResolution = 512;  // the most LENS OF GOD's ever uses
inline constexpr float kLensReceiverSide = 1.4f;              // metres round the focus
// The texels the balls and foci are set on: LENS's ball rolls along row kLensRow (below the logo), LENS OF
// GOD's focus stops on texel kGodFocusTexel (a light blue tessera below the logo).
inline constexpr int kLensRow = 80;
inline constexpr float kLensTrail = 2.f; // tesserae from the contact point back to the spot's centre
inline constexpr int kGodFocusTexel[2] = {50, 81};
// The stones keep this share of their 1993 pixel's chroma: the plum and blue of the palette as mauve marble
// and slate, the gold leaf still gold.
inline constexpr float kStoneSaturation = 0.42f;
// LENS OF GOD's air: the haze's mean free path at the ground (units), and the lens's cone of light in it: the
// haze's density inside the cone and the glow of its in-scattered sunlight where the cone is as wide as the
// lens (it grows with the concentration towards the focus).
inline constexpr float kGodHazeDepth = 1400.f;
inline constexpr float kGodConeHaze = 0.004f, kGodConeGlow = 3.f;

// The 2026 epoch card's feat line of each chapter, "" for an effect the scene does not show.
const char* lensEpochCard(show::Effect effect);

// The photons a frame shoots at a detail: kLensPhotons * scale rounded down to a power of two (an even photon
// grid), at least kMinLensPhotons.
std::uint32_t lensPhotonCount(std::uint32_t full, SceneDetail detail);

// --- Time
// ---------------------------------------------------------------------------------------------------

// The local seconds of the chapter's bar line (a downbeat of the beat grid) nearest `target` local seconds,
// or `target` when the grid has none in the chapter; and of the last downbeat at or before `target` (the
// settling downbeat D for target = the accent), or 0 without one.
double lensBarLine(const show::TimelineFrame& frame, double target);
double lensDownbeatBefore(const show::TimelineFrame& frame, double target);
// LENS OF GOD's D, in chapter seconds: where its focus starts the walk onto the accent and the camera comes
// down by the foreground tessera. The focus walks onto the accent, so this is the downbeat at least most of a
// beat before it: a bar before the accent when the accent is itself a downbeat (as Act III's form puts it).
double godSettle(const show::TimelineFrame& frame);
// The chapter's continuous beat position at the frame: beats since the chapter's settling downbeat D (whole
// numbers on the beats, linear between them).
double lensBeatsSinceD(const show::TimelineFrame& frame);

// --- LENS
// ---------------------------------------------------------------------------------------------------

// The 1993 rotozoom's turn (radians) and zoom (texels per pixel) at chapter time t (retro/rotozoom.hpp).
double rotozoomAngle(double t);
double rotozoomZoom(double t);
// The mosaic point under the middle of 1993's screen at chapter time t: texel (64 + 23 t, 64 + 40 sin 0.29
// t).
Vec3 rotozoomCentre(double t);

// Until D, the 1993 lens (a disc kRotozoomLensRadius pixels in radius bouncing over the rotozoom, magnifying
// its middle twice) as glass: LENS OF GOD's lens and bezel scaled down to `scale`, hanging at half its focal
// length over the mosaic (so it magnifies the stones under its middle twice too), where and as large as
// 1993's lens shows on the plan view. Its place follows 1993's lens formula without rounding to pixels.
struct PlanLens {
    Vec3 centre;  // world
    float scale;  // of the god lens's mesh (kGodLensDiameter across)
    float radius; // of its glass, world units
};
PlanLens planLens(double t);

// The ball's centre: on row kLensRow, its contact point at texel column 40 on D and one tessera further each
// beat.
Vec3 lensBallCentre(const show::TimelineFrame& frame);
// The local time of the beat nearest the accent (LENS: where the ball's caustic first lies on the tessera the
// held camera faces, kLensFacingTile).
double lensAccentBeat(const show::TimelineFrame& frame);
// The direction towards LENS's sun (unit): straight ahead of the rolling ball, 45 degrees up, so that the
// ball's light (best focus a little above the stone) lands as a spectral spot kLensTrail tesserae behind the
// contact point.
Vec3 lensSunDirection();
// Where the ball's light lands: its centre moved against the sun until it meets the stone.
Vec3 lensFocusPoint(const show::TimelineFrame& frame);

// --- LENS OF GOD -------------------------------------------------------------------------------------------

// The distance from a symmetric biconvex lens's centre to its best focus for parallel light (the plane of
// least RMS spot radius over the aperture, traced through both surfaces in double precision): surfaces of
// curvature radius `curvature`, centre thickness `thickness`, aperture radius `aperture`, index `ior`. A ball
// (curvature = aperture = thickness / 2) works too.
double bestFocusDistance(double curvature, double thickness, double aperture, double ior);
double godLensCurvature();
double godLensThickness();
double godLensFocusDistance();

// The direction towards LENS OF GOD's sun (unit, 42 degrees up, behind the orbiting camera so that the logo
// reads upright from orbit).
Vec3 godSunDirection();
// How far the lens hangs beyond its focus along the sun's line (negative: inside its focal length) at the
// frame: from -100 at the start rising to 0 on D, and 0 from then on.
double godLensDefocus(const show::TimelineFrame& frame);
// Where its focus line meets the stone: kGodFocusWalk units sunwards of the foreground tessera's centre (the
// origin) until D, walking onto it between D and the accent, there from the accent on.
inline constexpr float kGodFocusWalk = 75.f;
Vec3 godFocusPoint(const show::TimelineFrame& frame);
Vec3 godLensCentre(const show::TimelineFrame& frame);
// The diameter of the lens's spot of light across the sun's line.
double godSpotDiameter(const show::TimelineFrame& frame);

// --- The scene ---------------------------------------------------------------------------------------------

// The chapter cameras (pure functions of localSeconds, chapterDuration, accentSeconds and the beat grid):
//   LENS         shot 0 until D: the plan view matched to 1993's rotozoom; shot 1 from D: low beside the
//   ball. LENS OF GOD  shot 0: orbit; shot 1 from four bars before D: down to the lens's rim; shot 2 from D
//   (godSettle): low by the foreground tessera; shot 3 from four bars after the accent's downbeat: back up
//   to orbit.
// Any other effect gets LENS's camera.
Camera lensCamera(const show::TimelineFrame& frame);
float lensExposureEV(show::Effect effect);

class Lens final : public ModernScene {
public:
    explicit Lens(SceneDetail detail = {});

    enum MaterialIndex : std::uint32_t {
        kMosaic,    // LENS's endless floor (a caustic receiver)
        kPlanet,    // LENS OF GOD's planet (a caustic receiver)
        kBallGlass, // LENS's dense flint ball
        kGodGlass,  // LENS OF GOD's flint lens
        kGold,      // its bezel
        kEmber,     // the burning tessera at LENS OF GOD's focus (camera-only emission, a receiver too)
        kPlanGold,  // the plan view's lens's bezel (its brushing at that lens's scale)
        kMaterialCount
    };
    enum MeshIndex : std::uint32_t { kQuadMesh, kGodLensMesh, kBezelMesh, kMeshCount };
    enum SphereIndex : std::uint32_t { kBallSphere, kPlanetSphere, kSphereCount };

    const SceneStatic& staticScene() const override { return static_; }
    SceneFrame frame(const show::TimelineFrame& frame) const override;
    Camera camera(const show::TimelineFrame& frame) const override { return lensCamera(frame); }
    bool samplesRetro() const override { return false; }
    std::string nowCard(show::Effect effect) const override;
    std::uint32_t photons() const { return photons_; }

private:
    SceneFrame lensFrame(const show::TimelineFrame& frame) const;
    SceneFrame godFrame(const show::TimelineFrame& frame) const;

    SceneStatic static_;
    std::uint32_t photons_, godPhotons_;
};

} // namespace evo::modern
