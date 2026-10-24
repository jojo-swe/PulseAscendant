#include "retro/portal.hpp"

#include "retro/boot.hpp"
#include "retro/flyby.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <numbers>
#include <string>

namespace evo::retro {

namespace {

double smoothstep(double x) {
    x = std::clamp(x, 0.0, 1.0);
    return x * x * (3 - 2 * x);
}

double radians(double degrees) {
    return degrees * std::numbers::pi / 180;
}

// The caption's script for the prompt's typing engine.
constexpr std::array<PromptWrite, 1> kCaption = {{{1.0, kPortalCaptionText, 8}}};

// Appends a flat screen-space quad (corners in whole pixels, in order round it) as two triangles, leaving
// out either one if it has no area; depth 0, as the bezel is never sorted.
void addQuad(RetroScene& scene, const std::array<std::array<int, 2>, 4>& corners, std::uint8_t index) {
    const auto add = [&](int a, int b, int c) {
        RetroTriangle t{};
        const int k[3] = {a, b, c};
        for (int v = 0; v < 3; ++v) {
            t.x[v] = corners[static_cast<std::size_t>(k[v])][0] * kSubpixel;
            t.y[v] = corners[static_cast<std::size_t>(k[v])][1] * kSubpixel;
            t.index[v] = index;
        }
        t.mode = kFlat;
        const std::int64_t area = std::int64_t{t.x[1] - t.x[0]} * (t.y[2] - t.y[0]) -
                                  std::int64_t{t.y[1] - t.y[0]} * (t.x[2] - t.x[0]);
        if (area == 0)
            return;
        scene.triangles.push_back(t);
        scene.triangleDepth.push_back(0);
    };
    add(0, 1, 2);
    add(0, 2, 3);
}

// The ring between the rectangle [ox0, ox1) x [oy0, oy1) and the one [ix0, ix1) x [iy0, iy1) inside it, as
// four trapezoids in their own indices: top, left, right, bottom.
void addFrame(RetroScene& scene, int ox0, int oy0, int ox1, int oy1, int ix0, int iy0, int ix1, int iy1,
              const std::array<std::uint8_t, 4>& indices) {
    addQuad(scene, {{{ox0, oy0}, {ox1, oy0}, {ix1, iy0}, {ix0, iy0}}}, indices[0]);
    addQuad(scene, {{{ox0, oy0}, {ix0, iy0}, {ix0, iy1}, {ox0, oy1}}}, indices[1]);
    addQuad(scene, {{{ox1, oy0}, {ox1, oy1}, {ix1, iy1}, {ix1, iy0}}}, indices[2]);
    addQuad(scene, {{{ox0, oy1}, {ix0, iy1}, {ix1, iy1}, {ox1, oy1}}}, indices[3]);
}

} // namespace

double portalProgress(const show::TimelineFrame& frame, std::uint32_t retroFrame) {
    const double seconds = frame.accentSeconds > 0 ? frame.accentSeconds : frame.chapterDuration;
    if (seconds <= 0)
        return 1;
    return std::clamp(static_cast<double>(retroFrame) / (seconds * kPortalRetroRate), 0.0, 1.0);
}

PortalInset portalInset(const show::TimelineFrame& frame, std::uint32_t retroFrame) {
    const double e = smoothstep(portalProgress(frame, retroFrame));
    return {static_cast<int>(std::lround(kPortalInsetX * e)),
            static_cast<int>(std::lround(kPortalInsetY * e))};
}

Camera portalCamera(const show::TimelineFrame& frame, std::uint32_t retroFrame) {
    const double p = portalProgress(frame, retroFrame);
    // The turn, over the first 60 %: from the ship's right side, a little ahead, round to dead behind it.
    const double turn = smoothstep(p / 0.6);
    const double azimuth = radians(100 * (1 - turn));
    const double elevation = radians(16 - 9 * turn);
    // The pull-back, from the start: 6.5 units to 16 times that, where the ship is a speck of a few pixels.
    const double distance = 6.5 * std::pow(16.0, smoothstep(p));
    Camera camera{};
    camera.eye[0] = distance * std::cos(elevation) * std::sin(azimuth);
    camera.eye[1] = distance * std::sin(elevation);
    camera.eye[2] = -distance * std::cos(elevation) * std::cos(azimuth);
    camera.up[1] = 1;
    camera.fovYDegrees = kFlybyFovY; // FLYBY's, which its stars keep
    return camera;
}

void portalPalette(Palette& palette, const show::TimelineFrame& frame, std::uint32_t retroFrame) {
    flybyPalette(palette, frame, retroFrame);
    // The beige plastic of a 1993 monitor, lit from the top left.
    palette[kPortalBezelTop] = rgba(222, 214, 190);
    palette[kPortalBezelLeft] = rgba(198, 190, 166);
    palette[kPortalBezelRight] = rgba(160, 152, 130);
    palette[kPortalBezelBottom] = rgba(130, 124, 106);
}

void portalScene(RetroScene& scene, const show::TimelineFrame& frame, std::uint32_t retroFrame) {
    flybySceneFrom(scene, portalCamera(frame, retroFrame), retroFrame);
    const PortalInset inset = portalInset(frame, retroFrame);

    // Only the stars wholly inside the picture: points are drawn after every triangle, so the bezel cannot
    // hide them.
    std::size_t kept = 0;
    for (std::size_t i = 0; i < scene.points.size(); ++i) {
        const RetroPoint p = scene.points[i];
        const auto [x, y] = pointCorner(p);
        if (x < inset.x || x + p.size > kScreenWidth - inset.x || y < inset.y ||
            y + p.size > kScreenHeight - inset.y)
            continue;
        scene.points[kept] = p;
        scene.pointDepth[kept] = scene.pointDepth[i];
        ++kept;
    }
    scene.points.resize(kept);
    scene.pointDepth.resize(kept);

    if (inset.x == 0 && inset.y == 0)
        return;
    // The bevel from the screen's edges in to the rim, then the black rim round the picture.
    const int rim = std::min({kPortalRim, inset.x, inset.y});
    const int x0 = inset.x, y0 = inset.y, x1 = kScreenWidth - inset.x, y1 = kScreenHeight - inset.y;
    addFrame(scene, 0, 0, kScreenWidth, kScreenHeight, x0 - rim, y0 - rim, x1 + rim, y1 + rim,
             {kPortalBezelTop, kPortalBezelLeft, kPortalBezelRight, kPortalBezelBottom});
    if (rim > 0)
        addFrame(scene, x0 - rim, y0 - rim, x1 + rim, y1 + rim, x0, y0, x1, y1, {0, 0, 0, 0});
}

std::vector<text::Item> portalText(const show::TimelineFrame&, std::uint32_t retroFrame) {
    constexpr std::uint32_t kWhite = 0xFF000000u | kPortalCaption; // drawIndex writes the low byte
    const std::string typed = promptWritten(kCaption, retroFrame);
    std::vector<text::Item> items;
    if (!typed.empty())
        items.push_back({kPortalCaptionX, kPortalCaptionY, 1, kWhite, typed});
    if (promptCursorOn(retroFrame))
        items.push_back({static_cast<float>(kPortalCaptionX + 8 * static_cast<int>(typed.size())),
                         kPortalCaptionY, 1, kWhite, "_"});
    return items;
}

} // namespace evo::retro
