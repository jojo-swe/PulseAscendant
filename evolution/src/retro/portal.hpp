#pragma once

#include "retro/palette.hpp"
#include "retro/scene.hpp"
#include "show/timeline.hpp"
#include "text/text_pass.hpp"

#include <cstdint>
#include <vector>

namespace evo::retro {

// PORTAL (chapter 10), the 1993 side before the camera pulls out of the CRT: FLYBY's ship (its model,
// shading, painter's order and stars, retro/flyby.hpp) turns away from the camera and shrinks into the
// starfield, while a beige monitor bezel closes in from the screen's edges to a 4:3 frame and `THE SCREEN` is
// typed in its top-left corner with the 8x8 font. A scene and text effect on black at 35 retro frames a
// second.
//
// Everything runs on progress p = retroFrame / (accentSeconds * 35), clamped to [0, 1]: the portal's
// cross-fade to 2026 is centred on the accent, so the ship is a speck and the frame closed when 1993 gives
// way, and both hold from there to the end of the chapter.
inline constexpr std::uint32_t kPortalRetroRate = 35;

// The palette: FLYBY's (ship ramps, engine glow, star greys, 255 white for the caption) plus the bezel's
// four bevel shades. The bezel's inner rim is index 0.
inline constexpr std::uint8_t kPortalBezelTop = 200;
inline constexpr std::uint8_t kPortalBezelLeft = 201;
inline constexpr std::uint8_t kPortalBezelRight = 202;
inline constexpr std::uint8_t kPortalBezelBottom = 203;
inline constexpr std::uint8_t kPortalCaption = 255;

// The bezel's inset from the screen's edges, in pixels, at full progress: the picture left inside is
// 240 x 150, which the 1.2 pixel aspect shows at 4:3. The inset grows as (40 e, 25 e) rounded, e the eased
// progress; the inner 2 pixels of a bezel at least that wide are its black rim.
inline constexpr int kPortalInsetX = 40;
inline constexpr int kPortalInsetY = 25;
inline constexpr int kPortalRim = 2;
struct PortalInset {
    int x, y;
};
PortalInset portalInset(const show::TimelineFrame& frame, std::uint32_t retroFrame);
double portalProgress(const show::TimelineFrame& frame, std::uint32_t retroFrame);

// The caption, typed with the prompt's engine (retro/boot.hpp) from 1 s at 8 characters a second, its first
// cell at (kPortalCaptionX, kPortalCaptionY): inside the closed frame's top-left corner.
inline constexpr const char* kPortalCaptionText = "THE SCREEN";
inline constexpr int kPortalCaptionX = kPortalInsetX + 4;
inline constexpr int kPortalCaptionY = kPortalInsetY + 4;

// The camera on the ship (at the world origin, nose along +z): from a three-quarter view of its front right
// it swings round behind it while it pulls back from 6.5 to 104 units, always aimed at the ship, so the ship
// turns away and shrinks to a few pixels at the centre of the screen, where the stars stream from.
Camera portalCamera(const show::TimelineFrame& frame, std::uint32_t retroFrame);

// The EffectInfo parts of PORTAL (see retro/effects.hpp). The scene is FLYBY's from portalCamera
// (flybySceneFrom: the ship's faces in its painter's order, then the stars it does not cover), less the stars
// not wholly inside the picture, then the bezel's triangles over it.
void portalPalette(Palette& palette, const show::TimelineFrame& frame, std::uint32_t retroFrame);
void portalScene(RetroScene& scene, const show::TimelineFrame& frame, std::uint32_t retroFrame);
std::vector<text::Item> portalText(const show::TimelineFrame& frame, std::uint32_t retroFrame);

} // namespace evo::retro
