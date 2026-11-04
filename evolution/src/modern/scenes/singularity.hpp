#pragma once

// The Singularity scene: SINGULARITY, Act II's climax. A Schwarzschild black hole of r_s = 1
// unit at the origin bends every main ray (modern::LensingFrame, shaders/rt/lensing.glsl: up to
// kGeodesicSteps RK4 steps of the null geodesic inside 30 r_s). Its accretion disc, in the xz plane from the
// innermost stable orbit (3 r_s) to kDiscOuter, is a grid medium of kDiscGrid voxels
// (shaders/modern/singularity_medium.comp) that the lens marches along the bent rays: Doppler-boosted
// blackbody emission (the approaching side, on the camera's left, is brighter and bluer), sheared spiral
// streaks whose turbulence follows the bass, brightness beating with the bass. So the disc's far side shows
// lensed over the top of the hole and its underside below it. Behind it: the environment's stars (bent
// exactly), a galaxy of splats (singularity_splats.comp, bent by the weak-field point-lens equation, two
// images per star) and one bright star.
//
// Shots, on the beat grid: S0 the far approach on 85 mm, the disc edge-on as a line; S1 (two bars before D)
// on 35 mm, orbiting inward and up so the far side of the disc rises over the hole; S2 (D) crosses the disc's
// plane and on the accent the bright star passes exactly behind the hole and becomes an Einstein ring, the
// frame held still for one bar; S3 (two bars after D) a slow fall towards the horizon while the exposure
// rises and the sky burns white, the match cut into RETURN.

#include "modern/modern_scene.hpp"
#include "modern/scene.hpp"
#include "show/timeline.hpp"

#include <array>
#include <cstdint>

namespace evo::modern {

inline constexpr std::array<std::uint32_t, 3> kDiscGrid{256, 32, 256};
inline constexpr float kDiscHalfWidth = 15.f, kDiscHalfHeight = 1.6f; // the disc's box, r_s
inline constexpr float kDiscInner = 3.f, kDiscOuter = 14.f;
inline constexpr std::uint32_t kSingularityStars = 2'097'152; // background stars, two splats each
inline constexpr std::uint32_t kMinSingularityStars = 2048;
inline constexpr const char* kSingularitySplatShader = "modern_singularity_splats.comp.spv";
inline constexpr const char* kSingularityMediumShader = "modern_singularity_medium.comp.spv";

std::uint32_t singularityStarCount(SceneDetail detail);
std::array<std::uint32_t, 3> discGridSize(SceneDetail detail);

// The lens of every SINGULARITY frame (r_s 1 at the origin, influence 30 r_s, kGeodesicSteps, the disc).
LensingFrame singularityLens();
// Where the bright background star stands (a direction from the origin): exactly behind the hole as the
// camera sees it from the accent through the bar after it, drifting across before and after.
Vec3 ringStarDirection(const show::TimelineFrame& frame);

Camera singularityCamera(const show::TimelineFrame& frame);
float singularityExposureEV(const show::TimelineFrame& frame);

class Singularity final : public ModernScene {
public:
    explicit Singularity(SceneDetail detail = {});
    const SceneStatic& staticScene() const override { return static_; }
    SceneFrame frame(const show::TimelineFrame& frame) const override;
    Camera camera(const show::TimelineFrame& frame) const override { return singularityCamera(frame); }
    bool samplesRetro() const override { return false; }
    std::string nowCard(show::Effect effect) const override;
    std::uint32_t starCount() const { return stars_; }

private:
    SceneStatic static_;
    std::uint32_t stars_;
    std::array<std::uint32_t, 3> grid_;
};

} // namespace evo::modern
