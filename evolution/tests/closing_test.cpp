// The 1993 side's closing chapters: PORTAL (chapter 10: FLYBY's ship turning away and shrinking into the
// stars while a monitor bezel closes in to a 4:3 frame, `THE SCREEN` typed in its corner), REBOOT (chapter
// 20: the DOS prompt again with a second script) and THE END (chapter 30: the end screen fading from white to
// black). Their registry entries, palettes, scripts and scenes on the CPU, and their 320x200 palette index
// images from the retro stage on the GPU. Runs with the validation layer on and fails on any message it
// reports.

#include "gfx/context.hpp"
#include "retro/boot.hpp"
#include "retro/effects.hpp"
#include "retro/flyby.hpp"
#include "retro/palette.hpp"
#include "retro/portal.hpp"
#include "retro/scene.hpp"
#include "retro/the_end.hpp"
#include "retro_stage_fixture.hpp"
#include "show/timeline.hpp"
#include "text/font8x8.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

using evo::retro::EffectInfo;
using evo::retro::Palette;
using evo::retro::RetroScene;
using evo::show::Effect;
using evo::show::TimelineFrame;
using evo::test::RetroPicture;
using evo::test::RetroStageFixture;

int checksPassed = 0;

#define CHECK(x)                                                                                             \
    do {                                                                                                     \
        if (!(x))                                                                                            \
            throw std::runtime_error(#x);                                                                    \
        ++checksPassed;                                                                                      \
    } while (false)

void setEnv(const char* name, const char* value) {
#ifdef _WIN32
    _putenv_s(name, value);
#else
    setenv(name, value, 1);
#endif
}

constexpr int kWidth = static_cast<int>(RetroStageFixture::kWidth);
constexpr int kHeight = static_cast<int>(RetroStageFixture::kHeight);
constexpr int kPortalChapter = 10, kRebootChapter = 20, kTheEndChapter = 30;
constexpr std::uint32_t kRate = 35;

// A frame of `chapter` (showing `effect`) at `local` seconds into it.
TimelineFrame at(int chapter, Effect effect, double local) {
    const TimelineFrame frame = evo::show::sampleTimeline(evo::show::chapterStart(chapter) + local);
    if (frame.chapter != chapter || frame.effect != effect)
        throw std::runtime_error("expected chapter " + std::to_string(chapter) + " at local time " +
                                 std::to_string(local));
    return frame;
}
TimelineFrame portal(double local) {
    return at(kPortalChapter, Effect::Portal, local);
}
TimelineFrame reboot(double local) {
    return at(kRebootChapter, Effect::Reboot, local);
}
TimelineFrame theEnd(double local) {
    return at(kTheEndChapter, Effect::TheEnd, local);
}

double middleOf(std::uint32_t retroFrame) {
    return (retroFrame + .5) / kRate;
}

// Retro frames that begin inside the chapter.
std::uint32_t frameCount(const TimelineFrame& frame) {
    return static_cast<std::uint32_t>(std::ceil(frame.chapterDuration * kRate));
}

// The middle of a retro frame, or just before the chapter's end for the last, partial one.
double timeIn(const TimelineFrame& any, std::uint32_t retroFrame, double within = .5) {
    return std::min((retroFrame + within) / kRate, any.chapterDuration - 1e-6);
}

std::array<int, 3> rgbOf(std::uint32_t entry) {
    return {static_cast<int>(entry & 0xff), static_cast<int>(entry >> 8 & 0xff),
            static_cast<int>(entry >> 16 & 0xff)};
}

int brightness(std::uint32_t entry) {
    const auto c = rgbOf(entry);
    return c[0] + c[1] + c[2];
}

std::size_t count(const std::vector<std::uint8_t>& image, std::uint8_t index) {
    return static_cast<std::size_t>(std::count(image.begin(), image.end(), index));
}

// Whether the image shows `text` in the 8x8 font at (x, y), scale `scale`: `ink` where the glyph is set,
// `paper` elsewhere in its cells (paper < 0: anything but ink).
bool showsText(const std::vector<std::uint8_t>& image, const std::string& text, int x, int y, int scale,
               std::uint8_t ink, int paper) {
    for (std::size_t i = 0; i < text.size(); ++i) {
        const evo::text::GlyphRows& rows = evo::text::glyph(text[i]);
        for (int row = 0; row < 8 * scale; ++row)
            for (int col = 0; col < 8 * scale; ++col) {
                const bool set = rows[row / scale] & (0x80u >> (col / scale));
                const int px = x + static_cast<int>(i) * 8 * scale + col, py = y + row;
                const std::uint8_t got = image[static_cast<std::size_t>(py) * kWidth + px];
                if (set ? got != ink : (paper >= 0 ? got != paper : got == ink))
                    return false;
            }
    }
    return true;
}

// --- REBOOT --------------------------------------------------------------------------------------------

void testRebootRegistry() {
    const EffectInfo& info = evo::retro::effectFor(Effect::Reboot);
    CHECK(info.shader == nullptr && info.scene == nullptr && info.background == nullptr);
    CHECK(info.text == &evo::retro::rebootText);
    CHECK(info.palette == &evo::retro::bootPalette);
    CHECK(info.retroRate == evo::retro::kBootRetroRate);
}

// The script at the moments it writes, as boot_test checks BOOT's: each write lands on its time, not
// before; typing at 10 characters a second; the dots of each check one every half second.
void testRebootScript() {
    const auto screen = [](double seconds) {
        return evo::retro::rebootScreen(reboot(seconds),
                                        static_cast<std::uint32_t>(std::floor(seconds * 35)));
    };
    using Lines = std::vector<std::string>;
    const std::string command = "C:\\DEMO>evolution.exe /2026";
    CHECK(screen(0.0) == Lines{"C:\\DEMO>"});
    CHECK(screen(1.49) == Lines{"C:\\DEMO>"});
    // 2.0 s is retro frame 70, 17 frames after typing starts at frame 53 (1.5 s): 17 * 10 / 35 = 4.
    CHECK(screen(2.0) == Lines{"C:\\DEMO>evol"});
    CHECK(screen(3.3) == Lines{"C:\\DEMO>evolution.exe /20"});
    CHECK(screen(3.5) == Lines{command});
    CHECK(screen(3.99) == Lines{command});
    CHECK(screen(4.01) == (Lines{command, "", "Reinitialising reality"}));
    CHECK(screen(4.49) == (Lines{command, "", "Reinitialising reality"}));
    CHECK(screen(5.01) == (Lines{command, "", "Reinitialising reality ."}));
    CHECK(screen(5.99) == (Lines{command, "", "Reinitialising reality .."}));
    CHECK(screen(6.01) == (Lines{command, "", "Reinitialising reality ..."}));
    CHECK(screen(6.59).size() == 3);
    CHECK(screen(6.61).back() == "Ray tracing hardware");
    CHECK(screen(8.61).back() == "Ray tracing hardware ...");
    CHECK(screen(9.39).back() == "Ray tracing hardware ...");
    CHECK(screen(9.41).back() == "Ray tracing hardware ... found");
    CHECK(screen(10.39).size() == 4);
    const Lines full = {command, "", "Reinitialising reality ...", "Ray tracing hardware ... found",
                        "Entering the glass"};
    CHECK(screen(10.41) == full);

    // Every line fits the 39 columns unwrapped, and the screen holds to the chapter's end.
    const TimelineFrame any = reboot(1);
    for (std::uint32_t f = 0; f < frameCount(any); ++f)
        for (const std::string& line : evo::retro::rebootScreen(any, f))
            CHECK(line.size() <= evo::retro::kBootColumns);
    CHECK(evo::retro::rebootScreen(any, frameCount(any) - 1) == full);

    // Grey 8x8 text from BOOT's margin, the cursor after the last line while it blinks on.
    const auto items = evo::retro::rebootText(any, 402);
    CHECK(evo::retro::promptCursorOn(402));
    CHECK(items.size() == 5); // four written rows (the empty one has no item) and the cursor
    CHECK(items[0].text == command && items[0].x == 4 && items[0].y == 4 && (items[0].rgba & 0xff) == 7);
    CHECK(items[3].text == "Entering the glass" && items[3].y == 4 + 4 * 8);
    CHECK(items[4].text == "_" && items[4].x == 4 + 18 * 8 && items[4].y == 4 + 4 * 8);
    CHECK(!evo::retro::promptCursorOn(395));
    CHECK(evo::retro::rebootText(any, 395).size() == 4);
}

void testRebootImages(evo::gfx::Context& ctx) {
    RetroStageFixture fixture(ctx);
    const std::uint32_t frame = 402; // 11.49 s: the whole script, the cursor on
    bool rendered = false;
    const std::vector<std::uint8_t> image = fixture.render(reboot(middleOf(frame)), &rendered);
    CHECK(rendered && fixture.stage().retroFrame() == frame);
    CHECK(showsText(image, "C:\\DEMO>evolution.exe /2026", 4, 4, 1, 7, 0));
    CHECK(showsText(image, "Reinitialising reality ...", 4, 20, 1, 7, 0));
    CHECK(showsText(image, "Ray tracing hardware ... found", 4, 28, 1, 7, 0));
    CHECK(showsText(image, "Entering the glass", 4, 36, 1, 7, 0));
    CHECK(count(image, 7) + count(image, 0) == image.size());
    for (int x = 0; x < 7; ++x)
        CHECK(image[static_cast<std::size_t>(36 + 7) * kWidth + 4 + 18 * 8 + x] == 7); // the cursor
    // Deterministic: the same retro frame later within it, after another frame, and from a fresh stage.
    fixture.render(reboot(middleOf(100)));
    TimelineFrame again = reboot((frame + .95) / kRate);
    again.kick = 1;
    again.energy = 1;
    CHECK(fixture.render(again) == image);
    RetroStageFixture other(ctx);
    CHECK(other.render(reboot(middleOf(frame))) == image);
}

// --- THE END -------------------------------------------------------------------------------------------

void testTheEndRegistry() {
    const EffectInfo& info = evo::retro::effectFor(Effect::TheEnd);
    CHECK(info.shader == nullptr && info.scene == nullptr && info.background == nullptr);
    CHECK(info.text == &evo::retro::theEndText);
    CHECK(info.palette == &evo::retro::theEndPalette);
    CHECK(info.retroRate == evo::retro::kTheEndRetroRate && info.retroRate == 35);
}

// The text starts white and only ever darkens, through the DAC's 64 levels, to black kTheEndBlackSeconds
// before the chapter ends; nothing else in the palette shows.
void testTheEndFade() {
    using namespace evo::retro;
    const TimelineFrame any = theEnd(1);
    const std::uint32_t fade = theEndFadeFrames(any);
    const std::uint32_t frames = frameCount(any);
    std::printf("the end: %.3f s, %u retro frames, black from frame %u (%.3f s)\n", any.chapterDuration,
                frames, fade, fade / 35.0);
    CHECK(fade == static_cast<std::uint32_t>(std::floor((any.chapterDuration - kTheEndBlackSeconds) * 35)));
    CHECK(theEndLevel(any, 0) == 255);
    int last = 256, steps = 0;
    for (std::uint32_t f = 0; f < frames; ++f) {
        const TimelineFrame frame = theEnd(timeIn(any, f));
        const int level = theEndLevel(frame, f);
        CHECK(level <= last);
        steps += level < last && last != 256;
        last = level;
        // A DAC level, 0..63, as 8 bits.
        bool dacLevel = false;
        for (int dac = 0; dac < 64; ++dac)
            dacLevel |= level == (dac * 255 + 31) / 63;
        CHECK(dacLevel);
        Palette palette{};
        palette.fill(0x12345678u);
        theEndPalette(palette, frame, f);
        for (std::size_t i = 0; i < palette.size(); ++i)
            CHECK(palette[i] ==
                  (i == kTheEndText ? rgba(static_cast<std::uint8_t>(level), static_cast<std::uint8_t>(level),
                                           static_cast<std::uint8_t>(level))
                                    : rgba(0, 0, 0)));
        const auto text = theEndText(frame, f);
        if (f < fade) {
            CHECK(level > 0);
            CHECK(text.size() == 1);
            CHECK(text[0].text == "THE END" && text[0].scale == 3);
            CHECK(text[0].x == (320 - 7 * 24) / 2 && text[0].y == (200 - 24) / 2);
            CHECK((text[0].rgba & 0xff) == kTheEndText);
        } else {
            CHECK(level == 0 && text.empty());
        }
    }
    std::printf("the end: the text darkens in %d steps\n", steps);
    CHECK(steps == 63);
    CHECK(fade + 2 * 35 <= frames);
}

void testTheEndImages(evo::gfx::Context& ctx) {
    RetroStageFixture fixture(ctx, true);
    const TimelineFrame any = theEnd(1);
    const std::uint32_t fade = evo::retro::theEndFadeFrames(any);
    int lastBrightness = 1000;
    std::size_t lit = 0;
    for (std::uint32_t f : {0u, fade / 4, fade / 2, fade * 3 / 4, fade - 1}) {
        const RetroPicture picture = fixture.renderPicture(theEnd(timeIn(any, f)));
        CHECK(showsText(picture.indices, "THE END", 76, 88, 3, evo::retro::kTheEndText, 0));
        if (f == 0)
            lit = count(picture.indices, evo::retro::kTheEndText);
        CHECK(count(picture.indices, evo::retro::kTheEndText) == lit);
        CHECK(count(picture.indices, 0) + lit == picture.indices.size());
        // The colour of a pixel of the T's bar.
        const std::size_t i = (static_cast<std::size_t>(88) * kWidth + 76 + 4) * 3;
        const int b = picture.rgb[i] + picture.rgb[i + 1] + picture.rgb[i + 2];
        std::printf("the end: retro frame %u: text brightness %d\n", f, b);
        CHECK(f == 0 ? b == 3 * 255 : b < lastBrightness);
        lastBrightness = b;
    }
    CHECK(lit > 0);
    for (std::uint32_t f : {fade, frameCount(any) - 1}) {
        const RetroPicture picture = fixture.renderPicture(theEnd(timeIn(any, f)));
        CHECK(count(picture.indices, 0) == picture.indices.size());
        CHECK(std::count(picture.rgb.begin(), picture.rgb.end(), 0) == static_cast<long>(picture.rgb.size()));
    }
    // Deterministic within a retro frame.
    const std::vector<std::uint8_t> once = fixture.render(theEnd(timeIn(any, 100, .1)));
    fixture.render(theEnd(timeIn(any, 10)));
    TimelineFrame again = theEnd(timeIn(any, 100, .9));
    again.kick = 1;
    CHECK(fixture.render(again) == once);
}

// --- PORTAL --------------------------------------------------------------------------------------------

std::uint32_t accentFrame(const TimelineFrame& any) {
    return static_cast<std::uint32_t>(std::ceil(any.accentSeconds * kRate));
}

void testPortalRegistry() {
    const EffectInfo& info = evo::retro::effectFor(Effect::Portal);
    CHECK(info.shader == nullptr && info.background == nullptr);
    CHECK(info.palette == &evo::retro::portalPalette);
    CHECK(info.text == &evo::retro::portalText);
    CHECK(info.scene == &evo::retro::portalScene);
    CHECK(info.retroRate == 35 && evo::retro::kPortalRetroRate == evo::retro::kFlybyRetroRate);
}

// FLYBY's colours, plus the bezel's bevel lit from the top left; fixed.
void testPortalPalette() {
    using namespace evo::retro;
    Palette palette{}, flyby{};
    portalPalette(palette, portal(1), 10);
    flybyPalette(flyby, portal(1), 10);
    for (int i = 0; i < 256; ++i)
        if (i < kPortalBezelTop || i > kPortalBezelBottom)
            CHECK(palette[static_cast<std::size_t>(i)] == flyby[static_cast<std::size_t>(i)]);
    CHECK(brightness(palette[kPortalBezelTop]) > brightness(palette[kPortalBezelLeft]));
    CHECK(brightness(palette[kPortalBezelLeft]) > brightness(palette[kPortalBezelRight]));
    CHECK(brightness(palette[kPortalBezelRight]) > brightness(palette[kPortalBezelBottom]));
    CHECK(brightness(palette[kPortalBezelBottom]) > 150);
    CHECK(palette[kPortalCaption] == rgba(255, 255, 255));
    Palette later{};
    portalPalette(later, portal(9), 300);
    CHECK(later == palette);
}

// The bezel closes in, never opening again, to the 240 x 150 picture (4:3 on the display) by the accent,
// where the portal's cross-fade is centred, and holds it.
void testPortalBorder() {
    using namespace evo::retro;
    const TimelineFrame any = portal(1);
    const std::uint32_t accent = accentFrame(any);
    std::printf("portal: %.3f s, accent %.3f s (retro frame %u)\n", any.chapterDuration, any.accentSeconds,
                accent);
    CHECK(accent < frameCount(any));
    PortalInset last{0, 0};
    for (std::uint32_t f = 0; f < frameCount(any); ++f) {
        const TimelineFrame frame = portal(timeIn(any, f));
        const PortalInset inset = portalInset(frame, f);
        CHECK(inset.x >= last.x && inset.y >= last.y);
        CHECK(std::abs(inset.x * kPortalInsetY - inset.y * kPortalInsetX) <= kPortalInsetX); // about 4:3
        last = inset;
        if (f == 0)
            CHECK(inset.x == 0 && inset.y == 0 && portalProgress(frame, f) == 0);
        if (f >= accent) {
            CHECK(portalProgress(frame, f) == 1);
            CHECK(inset.x == kPortalInsetX && inset.y == kPortalInsetY);
        }
    }
    const int w = kWidth - 2 * kPortalInsetX, h = kHeight - 2 * kPortalInsetY;
    CHECK(w == 240 && h == 150);
    CHECK(std::abs(w / (h * evo::retro::kPixelAspect) - 4.0 / 3.0) < 1e-12);
}

// The ship's projected extent (the diagonal of its vertices' bounding box on the screen) at a retro frame.
double shipSize(std::uint32_t f) {
    const TimelineFrame frame = portal(timeIn(portal(1), f));
    const evo::retro::Camera camera = evo::retro::portalCamera(frame, f);
    double minX = 1e9, maxX = -1e9, minY = 1e9, maxY = -1e9;
    for (const evo::retro::FlybyFace& face : evo::retro::flybyShipFacesFrom(camera, f))
        for (const auto& v : face.world) {
            const evo::retro::Projected p = evo::retro::project(camera, v);
            CHECK(p.z > 1);
            minX = std::min(minX, p.x), maxX = std::max(maxX, p.x);
            minY = std::min(minY, p.y), maxY = std::max(maxY, p.y);
        }
    return std::hypot(maxX - minX, maxY - minY);
}

// The ship shrinks over the chapter (5 % of slack for its banking between samples), from filling a good
// part of the screen to a speck of a few pixels by the accent, and stays one.
void testPortalShipShrinks() {
    const TimelineFrame any = portal(1);
    const std::uint32_t accent = accentFrame(any);
    double last = shipSize(0);
    const double first = last;
    for (std::uint32_t f = 5; f < frameCount(any); f += 5) {
        const double size = shipSize(f);
        CHECK(size <= last * 1.05);
        last = std::min(last, size);
    }
    std::printf("portal: the ship's extent %.1f px at the start, %.1f px at the accent, %.1f px at the end\n",
                first, shipSize(accent), shipSize(frameCount(any) - 1));
    CHECK(first >= 120);
    CHECK(shipSize(accent) <= 16);
    CHECK(shipSize(frameCount(any) - 1) <= 16);
}

// The scene on every retro frame: the ship's faces in FLYBY's painter's order (flybySceneFrom from
// portalCamera), then the bezel's triangles in its four shades and the black rim; the stars only wholly
// inside the picture; a bezel exactly when the inset is not 0.
void testPortalScene() {
    using namespace evo::retro;
    const TimelineFrame any = portal(1);
    std::size_t bezelFrames = 0;
    for (std::uint32_t f = 0; f < frameCount(any); ++f) {
        const TimelineFrame frame = portal(timeIn(any, f));
        RetroScene scene, ship;
        portalScene(scene, frame, f);
        flybySceneFrom(ship, portalCamera(frame, f), f);
        CHECK(scene.triangles.size() >= ship.triangles.size());
        CHECK(std::equal(ship.triangles.begin(), ship.triangles.end(), scene.triangles.begin(),
                         [](const RetroTriangle& a, const RetroTriangle& b) {
                             return std::equal(a.x, a.x + 3, b.x) && std::equal(a.y, a.y + 3, b.y) &&
                                    a.index[0] == b.index[0] && a.mode == b.mode;
                         }));
        for (std::size_t i = ship.triangles.size(); i < scene.triangles.size(); ++i) {
            const std::uint8_t index = scene.triangles[i].index[0];
            CHECK(scene.triangles[i].mode == kFlat);
            CHECK(index == 0 || (index >= kPortalBezelTop && index <= kPortalBezelBottom));
        }
        const PortalInset inset = portalInset(frame, f);
        CHECK((scene.triangles.size() > ship.triangles.size()) == (inset.x > 0 || inset.y > 0));
        bezelFrames += inset.x > 0;
        CHECK(scene.points.size() <= ship.points.size());
        for (const RetroPoint& p : scene.points) {
            const int firstX = (p.x - p.size * 8 + 8) >> 4, firstY = (p.y - p.size * 8 + 8) >> 4;
            CHECK(firstX >= inset.x && firstX + p.size <= kWidth - inset.x);
            CHECK(firstY >= inset.y && firstY + p.size <= kHeight - inset.y);
        }
        CHECK(scene.triangleDepth.size() == scene.triangles.size());
        CHECK(scene.pointDepth.size() == scene.points.size());
    }
    CHECK(bezelFrames + 35 >= frameCount(any)); // the bezel shows within the first second
}

// FLYBY's painter's order holds from PORTAL's camera too, which passes behind the ship between the planes
// x = +-0.34 that FLYBY's flight never crosses (the fuselage is then drawn last): a ray through every 2nd
// pixel of every 7th retro frame must see the face drawn last be the nearest one (or show its index), except
// at FLYBY's two known seams.
using Vec = std::array<double, 3>;
Vec sub(const Vec& a, const Vec& b) {
    return {a[0] - b[0], a[1] - b[1], a[2] - b[2]};
}
Vec cross(const Vec& a, const Vec& b) {
    return {a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0]};
}
double dot(const Vec& a, const Vec& b) {
    return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
}
Vec normalize(const Vec& v) {
    const double l = std::sqrt(dot(v, v));
    return {v[0] / l, v[1] / l, v[2] / l};
}
double hit(const Vec& origin, const Vec& direction, const double world[3][3]) {
    const Vec a = {world[0][0], world[0][1], world[0][2]};
    const Vec e1 = sub({world[1][0], world[1][1], world[1][2]}, a);
    const Vec e2 = sub({world[2][0], world[2][1], world[2][2]}, a);
    const Vec p = cross(direction, e2);
    const double det = dot(e1, p);
    if (std::abs(det) < 1e-14)
        return -1;
    const Vec s = sub(origin, a);
    const double u = dot(s, p) / det;
    if (u < 0 || u > 1)
        return -1;
    const Vec q = cross(s, e1);
    const double v = dot(direction, q) / det;
    if (v < 0 || u + v > 1)
        return -1;
    return dot(e2, q) / det;
}

void testPortalPainterOrder() {
    using evo::retro::FlybyFace;
    using evo::retro::FlybyPart;
    const TimelineFrame any = portal(1);
    std::size_t rays = 0, seams = 0, wrong = 0, between = 0;
    for (std::uint32_t f = 0; f < frameCount(any); f += 7) {
        const TimelineFrame frame = portal(timeIn(any, f));
        const evo::retro::Camera camera = evo::retro::portalCamera(frame, f);
        const std::vector<FlybyFace> faces = evo::retro::flybyShipFacesFrom(camera, f);
        const Vec eye = {camera.eye[0], camera.eye[1], camera.eye[2]};
        between += std::abs(eye[0]) < 0.34;
        const Vec forward = normalize(sub({camera.target[0], camera.target[1], camera.target[2]}, eye));
        const Vec right = normalize(cross({camera.up[0], camera.up[1], camera.up[2]}, forward));
        const Vec up = cross(forward, right);
        const double focalY = 100.0 / std::tan(camera.fovYDegrees * 3.14159265358979323846 / 360.0);
        const double focalX = focalY * evo::retro::kPixelAspect;
        for (int gy = 0; gy < 100; ++gy)
            for (int gx = 0; gx < 160; ++gx) {
                const double vx = (gx * 2 + .5 - 160) / focalX, vy = (100 - gy * 2 - .5) / focalY;
                const Vec direction = {forward[0] + right[0] * vx + up[0] * vy,
                                       forward[1] + right[1] * vx + up[1] * vy,
                                       forward[2] + right[2] * vx + up[2] * vy};
                int nearest = -1, last = -1;
                double nearestT = 1e30, lastT = 0;
                for (std::size_t i = 0; i < faces.size(); ++i) {
                    const double t = hit(eye, direction, faces[i].world);
                    if (t <= 0)
                        continue;
                    if (t < nearestT)
                        nearest = static_cast<int>(i), nearestT = t;
                    last = static_cast<int>(i), lastT = t;
                }
                if (last < 0)
                    continue;
                ++rays;
                const FlybyFace& a = faces[static_cast<std::size_t>(nearest)];
                const FlybyFace& b = faces[static_cast<std::size_t>(last)];
                if (lastT <= nearestT * (1 + 1e-9) || a.index == b.index)
                    continue;
                const bool wingRoot = (a.part == FlybyPart::Wing && b.part == FlybyPart::Fuselage) ||
                                      (a.part == FlybyPart::Fuselage && b.part == FlybyPart::Wing);
                const bool flame =
                    a.part == FlybyPart::Flame && b.part == FlybyPart::Flame && a.side == b.side;
                (wingRoot || flame ? seams : wrong)++;
            }
    }
    std::printf("portal: painter's order: %zu rays on the ship, %zu at FLYBY's seams, %zu wrong; %zu sampled "
                "frames from between x = +-0.34\n",
                rays, seams, wrong, between);
    CHECK(rays >= 6000);
    CHECK(wrong == 0);
    CHECK(seams <= rays / 50);
    CHECK(between >= 3);
}

bool isShip(std::uint8_t index) {
    return index >= evo::retro::kFlybyShipFirst && index <= evo::retro::kFlybyShipLast;
}

void testPortalImages(evo::gfx::Context& ctx) {
    using namespace evo::retro;
    RetroStageFixture fixture(ctx);
    const TimelineFrame any = portal(1);
    const std::uint32_t accent = accentFrame(any);
    std::size_t lastShip = kWidth * kHeight;
    for (std::uint32_t f : {90u, accent / 2, accent, frameCount(any) - 1}) {
        const TimelineFrame frame = portal(timeIn(any, f, .2));
        const std::vector<std::uint8_t> image = fixture.render(frame);
        const PortalInset inset = portalInset(frame, f);
        std::size_t ship = 0, stars = 0, bezel = 0, outside = 0;
        for (int y = 0; y < kHeight; ++y)
            for (int x = 0; x < kWidth; ++x) {
                const std::uint8_t index = image[static_cast<std::size_t>(y) * kWidth + x];
                const bool inside =
                    x >= inset.x && x < kWidth - inset.x && y >= inset.y && y < kHeight - inset.y;
                const bool isBezel = index >= kPortalBezelTop && index <= kPortalBezelBottom;
                ship += isShip(index);
                stars += index >= kFlybyStar && index < kFlybyStar + kFlybyShades;
                bezel += isBezel;
                if (!inside) {
                    ++outside;
                    // The bezel, or its black rim next to the picture.
                    const bool rim = x >= inset.x - kPortalRim && x < kWidth - inset.x + kPortalRim &&
                                     y >= inset.y - kPortalRim && y < kHeight - inset.y + kPortalRim;
                    CHECK(isBezel || (rim && index == 0));
                } else {
                    CHECK(!isBezel);
                }
            }
        std::printf("portal: retro frame %u: inset (%d, %d), %zu ship px, %zu star px, %zu bezel px\n", f,
                    inset.x, inset.y, ship, stars, bezel);
        CHECK(ship > 0 && ship <= lastShip); // still there, never growing
        lastShip = ship;
        CHECK(stars >= 100);
        CHECK(bezel + 2 * kPortalRim * (2 * kWidth + 2 * kHeight) >= outside);
        // The caption, typed by 2.25 s, in white inside the corner of the closed frame.
        CHECK(showsText(image, kPortalCaptionText, kPortalCaptionX, kPortalCaptionY, 1, kPortalCaption, -1));

        // Deterministic: the same retro frame later within it, with the music pushed, after another frame.
        fixture.render(portal(timeIn(any, f > 10 ? f - 10 : f + 10)));
        TimelineFrame again = portal(timeIn(any, f, .9));
        again.kick = 1;
        again.energy = 1;
        again.beatPhase = .5f;
        CHECK(fixture.render(again) == image);
    }
    CHECK(lastShip <= 40);
    RetroStageFixture other(ctx);
    CHECK(other.render(portal(timeIn(any, accent))) == fixture.render(portal(timeIn(any, accent, .7))));
}

// The caption types one character every 1/8 s from 1 s, with the prompt's blinking cursor after it.
void testPortalCaption() {
    using evo::retro::portalText;
    const TimelineFrame any = portal(1);
    const auto caption = [&](std::uint32_t f) {
        const auto items = portalText(any, f);
        return items.empty() ? std::string() : items[0].text;
    };
    CHECK(portalText(any, 34).size() <= 1); // only the cursor, if on
    CHECK(caption(35 + 4) == "" || caption(35 + 4) == "_");
    CHECK(caption(35 + 5) == "T");          // (5 * 8) / 35 = 1 character
    CHECK(caption(35 + 40) == "THE SCREE"); // 40 * 8 / 35 = 9 characters
    CHECK(caption(35 + 44) == "THE SCREEN");
    CHECK(caption(200) == "THE SCREEN");
    const auto items = portalText(any, 200);
    CHECK(items[0].x == evo::retro::kPortalCaptionX && items[0].y == evo::retro::kPortalCaptionY);
    CHECK((items[0].rgba & 0xff) == evo::retro::kPortalCaption);
}

} // namespace

int main() {
    try {
        setEnv("EVO_SHADER_DIR", EVO_TEST_SHADER_DIR);
        testRebootRegistry();
        testRebootScript();
        testTheEndRegistry();
        testTheEndFade();
        testPortalRegistry();
        testPortalPalette();
        testPortalBorder();
        testPortalShipShrinks();
        testPortalScene();
        testPortalPainterOrder();
        testPortalCaption();
        {
            auto ctx = evo::gfx::Context::create({.validation = true});
            if (!ctx.validationEnabled())
                std::printf("warning: validation layer unavailable; only the pixels are checked\n");
            testRebootImages(ctx);
            testTheEndImages(ctx);
            testPortalImages(ctx);
            CHECK(ctx.validationMessageCount() == 0);
        }
        CHECK(evo::gfx::validationMessagesReported() == 0);
    } catch (const std::exception& error) {
        std::fprintf(stderr, "FAILED: %s\n", error.what());
        return 1;
    }
    std::printf("closing_test: %d checks passed\n", checksPassed);
    return 0;
}
