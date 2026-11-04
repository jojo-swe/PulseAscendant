// The epoch cards (render/epoch_cards.hpp).
//   (a) the texts: every chapter's 1993 and 2026 card as drawn equals the table below (the show's card lines,
//       their numbers as the configuration has them), each number equals its accessor, and the cue sheet says
//       the same;
//   (b) the timing of all 31 chapters: each card's window from the chapter's marks, and the frames at 60 fps
//       that show it begin and end within one frame of the window wherever the picture allows the card;
//   (c) a Wipe splits the cards at the line and a BeatSwap flips them in the frame the picture flips;
//   (d) the 1993 card types at 30 characters a second, quantised to the retro frame (RAYTRACE: 10 fps);
//   (e) on the GPU at 1280x720, with the validation layer: the 2026 card blends white at 0.85 at most, only on
//       its strokes and rule, in its box, in the left margin; the 1993 card follows crtBulge with the
//       scanlines, mask and vignette.
// `epoch_cards_test --look <dir>` writes 480x270 stills (DOTS, PLASMA mid-wipe, RAYTRACE drop + 1 bar, GALAXY,
// CREDITS) instead.

#include "app/options.hpp"
#include "gfx/context.hpp"
#include "gfx/frame_ring.hpp"
#include "gfx/readback.hpp"
#include "image/png.hpp"
#include "modern/card_format.hpp"
#include "modern/scenes/city.hpp"
#include "modern/scenes/glass.hpp"
#include "modern/scenes/nebula.hpp"
#include "modern/scenes/particles.hpp"
#include "modern/scenes/ship.hpp"
#include "modern/scenes/spheres.hpp"
#include "modern/scenes/tunnel.hpp"
#include "modern/studio.hpp"
#include "render/epoch_cards.hpp"
#include "render/renderer.hpp"
#include "render/stage_plan.hpp"
#include "retro/dots.hpp"
#include "retro/dots_schedule.hpp"
#include "retro/effects.hpp"
#include "retro/glenz.hpp"
#include "retro/tunnel.hpp"
#include "show/director.hpp"
#include "show/era.hpp"
#include "show/timeline.hpp"
#include "text/font8x8.hpp"
#include "text/stroke_font.hpp"

#include "test_env.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <map>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

using namespace evo;
using show::Effect;
using show::TimelineFrame;
using show::Transition;

int checksPassed = 0;

#define CHECK(x)                                                                                             \
    do {                                                                                                     \
        if (!(x))                                                                                            \
            throw std::runtime_error(#x);                                                                    \
        ++checksPassed;                                                                                      \
    } while (false)

constexpr double kFrame = 1.0 / 60.0;

TimelineFrame at(double showSeconds) {
    return show::sampleTimeline(showSeconds);
}

double chapterEnd(int chapter) {
    return chapter + 1 < show::chapterCount() ? show::chapterStart(chapter + 1) : show::showDuration();
}

// --- (a) The texts ------------------------------------------------------------------------------------------------

struct Lines {
    const char* then;
    const char* now;
};

// The show's card lines, chapter by chapter, with the numbers the configuration has. Where a line was first
// written with another number, the configuration's stands: TUNNEL's 1993 points (96 rings of 64), the city's
// windows and lights, NEBULA's voxels (256^3 written out).
// clang-format off
const std::array<Lines, 31> kLines = {{
    {"1993 · MS-DOS · 320×200", ""},
    {"1993 · 320×200 · 256 COLORS · FLAT SHADING", "2026 · PATH TRACED · THE ROOM IS LIT BY THE SCREEN"},
    {"1993 · 512 POINTS · 35 FPS", "2026 · 2,097,152 TRACED PARTICLES · MOTION BLUR · DEPTH OF FIELD"},
    {"1993 · PALETTE CYCLING · 256 COLORS", "2026 · VOLUMETRIC NEBULA · DELTA TRACKING · MULTIPLE SCATTERING"},
    {"1993 · 320×200 · 35 FPS · 6,144 POINTS", "2026 · THE POINTS BECAME LIGHTS · 6,144 LIGHTS · LIGHT TREE · FOG"},
    {"1993 · 24 TRIANGLES · TRANSPARENT VECTORS", "2026 · DISPERSION · 2,097,152 PHOTONS · CAUSTICS"},
    {"1993 · ROTOZOOM · LENS", "2026 · THE PIXELS BECAME STONE · GLASS SPHERE · CAUSTICS"},
    {"1993 · PRECALCULATED · 10 FPS", "2026 · REAL-TIME PATH TRACING · 49 SPHERES · 60 FPS"},
    {"1993 · 300 POLYGONS · GOURAUD", "2026 · 3,896,219 WINDOWS · 41,684 LAMPS · WET STREETS"},
    {"1993 · ALL EFFECTS · ONE SCREEN", "2026 · EVERY SCENE · ONE PATH TRACER"},
    {"1993 · THE SCREEN", "2026 · OUT OF THE SCREEN"},
    {"", "2026 · THE STUDIO · 481 INSTANCES · LIT BY ONE SCREEN AND ONE MOON"},
    {"", "2026 · ASCENT · VOLUMETRIC CLOUDS · MOONLIGHT SHAFTS"},
    {"", "2026 · CITY NIGHT · 16,384 LIGHTS · LIGHT TREE · RAIN"},
    {"", "2026 · NEBULA · 16,777,216 VOXELS · MULTIPLE SCATTERING · 3 STARS"},
    {"", "2026 · GLASS CATHEDRAL · 64 GLASS PILLARS · 4,194,304 PHOTONS · SHAFTS"},
    {"", "2026 · 2,097,152 TRACED PARTICLES · 8 GERSTNER WAVES · ONE SHOT"},
    {"", "2026 · 32,768 SPHERES · 4,096 OF THEM LIGHTS · LIGHT TREE · MOTION BLUR"},
    {"", "2026 · GRAVITATIONAL LENSING · 48-STEP GEODESICS · DOPPLER DISC · PATH TRACED"},
    {"", "2026 · RETURN · THE STUDIO AT DAWN"},
    {"1993 · REBOOT", "2026 · THROUGH THE GLASS"},
    {"", "2026 · 16,777,216 STARS · 256×64×256 DUST · ONE SPLAT PASS"},
    {"", "2026 · PLASMA PLANET · 256×128×32 CLOUD SHELL · SUNRISE"},
    {"", "2026 · WORMHOLE · 1 KM RINGS · VOLUMETRIC · MOTION BLUR"},
    {"", "2026 · CRYSTAL CITY · 400 PRISMS · DISPERSION · CAUSTICS"},
    {"", "2026 · LENS OF GOD · 200 UNIT LENS · SUN FOCUS · PLANETARY SCALE"},
    {"", "2026 · 32,768 MIRROR SPHERES · INFINITE REFLECTIONS · 6 BOUNCES"},
    {"", "2026 · METROPOLIS · 57,765 INSTANCES · 41,839 LIGHTS · 44 KM TO THE HORIZON"},
    {"", "2026 · 2,097,152 PARTICLES · 16,777,216 STARS · ASCENDANT"},
    {"1993 → 2026 · CREDITS", ""},
    {"", ""},
}};
// clang-format on

// The scene objects, once per scene, at a small detail: a card's numbers are the full-detail configuration's
// whatever the detail.
const modern::ModernScene* sceneOf(Effect effect) {
    static std::map<modern::SceneId, std::unique_ptr<modern::ModernScene>> scenes;
    const modern::SceneId id = modern::sceneFor(effect);
    if (id == modern::SceneId::Placeholder)
        return nullptr;
    auto& slot = scenes[id];
    if (!slot)
        slot = modern::makeScene(id, {1.f / 256});
    return slot.get();
}

std::string nowLine(Effect effect) {
    return render::nowCardText(effect, sceneOf(effect));
}

bool contains(const std::string& text, const std::string& part) {
    return text.find(part) != std::string::npos;
}

void testTexts() {
    for (int i = 0; i < show::chapterCount(); ++i) {
        const Effect effect = show::cues[i].effect;
        const std::string then = render::thenCardText(effect), now = nowLine(effect);
        if (then != kLines[i].then || now != kLines[i].now)
            std::printf("chapter %d: \"%s\" / \"%s\"\n", i, then.c_str(), now.c_str());
        CHECK(then == kLines[i].then);
        CHECK(now == kLines[i].now);
        // The cue sheet says the same, but where two scene tests still parse a shorter line.
        CHECK(std::string(show::cues[i].thenCard) == then);
        if (effect != Effect::Ocean && effect != Effect::RaytraceInfinity)
            CHECK(std::string(show::cues[i].nowCard) == now);
    }
    // Each number is its accessor's (change one and its card changes with it).
    const auto has = [](Effect e, std::uint64_t value, const char* unit) {
        const std::string want = modern::groupThousands(value) + unit;
        if (!contains(nowLine(e), want))
            std::printf("%s: no \"%s\" in \"%s\"\n", show::effectName(e), want.c_str(), nowLine(e).c_str());
        CHECK(contains(nowLine(e), want));
    };
    has(Effect::Dots, modern::kDotsParticles, " TRACED PARTICLES");
    has(Effect::Ocean, modern::kOceanParticles, " TRACED PARTICLES");
    has(Effect::Tunnel, modern::kTunnelLights, " LIGHTS");
    has(Effect::Glenz, modern::glassCard(Effect::Glenz).exportPhotons, " PHOTONS");
    has(Effect::Cathedral, modern::glassPillarCount(), " GLASS PILLARS");
    has(Effect::Cathedral, modern::glassCard(Effect::Cathedral).exportPhotons, " PHOTONS");
    has(Effect::CrystalCity, modern::glassCard(Effect::CrystalCity).glassObjects, " PRISMS");
    has(Effect::Raytrace, modern::kRaytraceSphereCount + modern::kRaytraceGiants, " SPHERES");
    has(Effect::Raytrace, modern::kShowFps, " FPS");
    CHECK(Options{}.fps == static_cast<int>(modern::kShowFps));
    has(Effect::SphereStorm, modern::kStormSpheres, " SPHERES");
    has(Effect::SphereStorm, modern::kStormEmissive, " OF THEM LIGHTS");
    has(Effect::RaytraceInfinity, modern::kInfinitySpheres, " MIRROR SPHERES");
    has(Effect::RaytraceInfinity, render::kExportMaxBounces, " BOUNCES");
    has(Effect::Nebula, modern::nebulaStats().voxels, " VOXELS");
    has(Effect::Nebula, modern::nebulaStats().stars, " STARS");
    has(Effect::Metropolis, modern::kCityCardLights, " LIGHTS"); // the whole city; CITY NIGHT and VECTOR CITY
                                                                  // card their own street's lamps and windows
    has(Effect::Wormhole, static_cast<std::uint64_t>(modern::kWormholeRingSpacing / 1000.f), " KM RINGS");
    const modern::Studio studio;
    has(Effect::StudioNight, studio.frame(at(show::chapterStart(11) + 1.0)).instances.size(), " INSTANCES");
    const auto thenHas = [](Effect e, std::uint64_t value, const char* unit) {
        CHECK(contains(render::thenCardText(e), modern::groupThousands(value) + unit));
    };
    thenHas(Effect::Dots, retro::kDotsPoints, " POINTS");
    thenHas(Effect::Dots, retro::kDotsRetroRate, " FPS");
    thenHas(Effect::Tunnel, retro::kTunnelRings * retro::kTunnelRingDots, " POINTS");
    thenHas(Effect::Tunnel, retro::kTunnelRetroRate, " FPS");
    thenHas(Effect::Glenz, retro::kGlenzTriangles, " TRIANGLES");
    thenHas(Effect::Raytrace, retro::kRaytraceRetroRate, " FPS");
    // At 2160p every 2026 card, and the credits, sets in the left margin beside the 4:3 picture.
    for (int i = 0; i < show::chapterCount(); ++i) {
        const std::string now = nowLine(show::cues[i].effect);
        if (now.empty())
            continue;
        const render::NowCardLayout card = render::nowCardLayout(now, 3840, 2160);
        std::string lines;
        for (const std::string& line : card.lines)
            lines += (lines.empty() ? "" : " / ") + line;
        std::printf("(a) 2160p %-17s %s\n", show::cues[i].title, lines.c_str());
        CHECK(card.x1 <= 480 && card.textRight <= 480.f && card.lines.size() >= 2);
    }
    const render::NowCardLayout credits = render::nowCardLayout(show::kCreditsLine, 3840, 2160, false, true);
    CHECK(credits.lines.size() == 3 && credits.lines[0] == "CODE: JOJO" && credits.x1 <= 480);
    // A scene without a card of its own leaves the cue's line.
    CHECK(render::nowCardText(Effect::Galaxy, nullptr) == show::cues[21].nowCard);
    std::printf("(a) the texts of all %d chapters match, their numbers from the configuration\n",
                show::chapterCount());
}

// --- (b) Timing -----------------------------------------------------------------------------------------------------

// Whether the picture lets a card show at a frame (the rules, restated): the 1993 card while the 1993 picture
// fills the frame (or its part of a wipe), the 2026 card while the 2026 one does.
bool thenAllowed(const TimelineFrame& f) {
    if (render::thenCardOnCrt(f.effect))
        return false;
    if (f.transition == Transition::Wipe)
        return f.wipe < 1.f;
    if (f.transition == Transition::Portal && show::portalPhase(f).approach > 0.f)
        return false;
    return f.era < 0.5f;
}

bool nowAllowed(const TimelineFrame& f) {
    if (f.transition == Transition::Wipe)
        return f.wipe > 0.f;
    return f.transition == Transition::Portal || f.era >= 0.5f;
}

void testTiming() {
    int checkedEnds = 0;
    for (int chapter = 0; chapter < show::chapterCount(); ++chapter) {
        const double start = show::chapterStart(chapter), end = chapterEnd(chapter);
        const TimelineFrame head = at(start + 0.01);
        const show::TransitionMarks marks = show::transitionMarks(head);
        const render::CardWindow then = render::thenCardWindow(head), now = render::nowCardWindow(head);
        const Effect e = head.effect;
        // The windows from the marks.
        CHECK(then.empty() == (kLines[chapter].then[0] == '\0' || render::thenCardOnCrt(e)));
        CHECK(now.empty() == (kLines[chapter].now[0] == '\0'));
        if (!then.empty()) {
            CHECK(std::abs(then.in - (start + marks.bar)) < 1e-6);
            CHECK(then.out <= marks.accent + marks.bar + 1e-6 && then.out <= end - 2 * marks.bar + 1e-6);
            CHECK(then.hold == then.out);
        }
        if (!now.empty() && e != Effect::Raytrace) {
            CHECK(now.out <= end - 2 * marks.bar + 1e-6);
            CHECK(now.hold <= marks.accent + marks.bar + 1e-6);
            CHECK(now.in >= start + marks.bar - 1e-6);
            CHECK(now.hold >= now.in && now.out >= now.hold);
        }
        // Every frame of the chapter at 60 fps: a card shows only inside its window and where the picture
        // allows it, and does show there.
        std::optional<double> thenFirst, thenLast, nowFirst, nowLast;
        for (double t = start; t < end - 1e-9; t += kFrame) {
            const TimelineFrame f = at(t);
            if (f.chapter != chapter)
                continue;
            const render::CardFrame c = render::cardFrame(f);
            const bool inThen = !then.empty() && t >= then.in && t < then.out;
            const bool inNow = !now.empty() && t >= now.in && t < now.out;
            CHECK(c.then == (inThen && thenAllowed(f)));
            CHECK(c.now == (inNow && nowAllowed(f)));
            if (c.then) {
                thenFirst = thenFirst.value_or(t);
                thenLast = t;
            }
            if (c.now) {
                nowFirst = nowFirst.value_or(t);
                nowLast = t;
                CHECK(c.nowAlpha > 0.f && c.nowAlpha <= render::kNowCardAlpha);
            }
        }
        // In and out on the frame, wherever the picture allows the card at its ends.
        const auto ends = [&](const render::CardWindow& w, const std::optional<double>& first,
                              const std::optional<double>& last, bool (*allowed)(const TimelineFrame&)) {
            if (w.empty())
                return;
            if (allowed(at(w.in + kFrame))) {
                CHECK(first && std::abs(*first - w.in) <= kFrame + 1e-9);
                ++checkedEnds;
            }
            if (allowed(at(w.out - kFrame)) && w.out < end - 1e-6) {
                CHECK(last && std::abs(*last - w.out) <= kFrame + 1e-9);
                ++checkedEnds;
            }
        };
        ends(then, thenFirst, thenLast, &thenAllowed);
        ends(now, nowFirst, nowLast, &nowAllowed);
        std::printf("(b) %2d %-17s 1993 %7.2f..%7.2f  2026 %7.2f..%7.2f..%7.2f\n", chapter, head.title,
                    then.empty() ? 0.0 : then.in, then.empty() ? 0.0 : then.out, now.empty() ? 0.0 : now.in,
                    now.empty() ? 0.0 : now.hold, now.empty() ? 0.0 : now.out);
    }
    // The fixed points: BOOT's card at 1.70, RAYTRACE's 2026 card from the drop to the end, FLYBY's 1993
    // card gone as the approach starts.
    CHECK(std::abs(render::thenCardWindow(at(1.0)).in - 1.70) < 0.03);
    const TimelineFrame raytrace = at(show::chapterStart(7) + 1.0);
    CHECK(std::abs(render::nowCardWindow(raytrace).in - 128.07) < 0.02);
    CHECK(std::abs(render::nowCardWindow(raytrace).out - chapterEnd(7)) < 1e-6);
    const TimelineFrame flyby = at(10.0);
    const show::TransitionMarks fm = show::transitionMarks(flyby);
    CHECK(std::abs(render::thenCardWindow(flyby).out - (fm.downbeat - 2 * fm.bar)) < 1e-6);
    // The credits layer holds through CREDITS.
    const render::CardFrame credits = render::cardFrame(at(show::chapterStart(29) + 5.0));
    CHECK(credits.credits && credits.creditsAlpha == render::kCreditsCardAlpha);
    const render::CardFrame creditsEnd = render::cardFrame(at(chapterEnd(29) - 2 * kFrame));
    CHECK(creditsEnd.credits && !creditsEnd.then);
    CHECK(!render::cardFrame(at(show::chapterStart(28) + 5.0)).credits);
    std::printf("(b) %d card ends land on their frame\n", checkedEnds);
    CHECK(checkedEnds >= 40);
}

// --- (c) Wipe and BeatSwap ---------------------------------------------------------------------------------------------

void testSplitAndFlip() {
    int flips = 0, splits = 0;
    for (int chapter = 0; chapter < show::chapterCount(); ++chapter) {
        const TimelineFrame head = at(show::chapterStart(chapter) + 0.01);
        if (head.transition != Transition::Wipe && head.transition != Transition::BeatSwap)
            continue;
        const render::CardWindow then = render::thenCardWindow(head), now = render::nowCardWindow(head);
        if (then.empty() || now.empty())
            continue;
        bool previousModern = false;
        for (double t = std::max(then.in, now.in); t < std::min(then.out, now.hold); t += kFrame) {
            const TimelineFrame f = at(t);
            const render::CardFrame c = render::cardFrame(f);
            if (head.transition == Transition::Wipe) {
                CHECK(c.now == (f.wipe > 0.f) && c.then == (f.wipe < 1.f));
                if (c.now && c.then) {
                    CHECK(c.nowTo == f.wipe && c.thenFrom == f.wipe);
                    ++splits;
                }
            } else {
                // The card flips in the frame the picture does: 2026 from era 0.5, as the compositor.
                CHECK(c.now == (f.era >= 0.5f) && c.then == (f.era < 0.5f));
                if (c.now != previousModern)
                    ++flips;
                previousModern = c.now;
            }
        }
    }
    std::printf("(c) %d split frames across the wipes, %d card flips with the picture\n", splits, flips);
    CHECK(splits > 30 && flips > 10);
}

// --- (d) Typing --------------------------------------------------------------------------------------------------------

void testTyping() {
    // BOOT at 35 retro frames a second: about 30 characters a second, never ahead of it.
    const render::CardWindow w = render::thenCardWindow(at(1.0));
    for (double dt = 0; dt < 0.6; dt += 0.01) {
        const render::CardFrame c = render::cardFrame(at(w.in + dt));
        CHECK(static_cast<double>(c.typed) <= dt * render::kCardTypeRate + 1e-6);
        CHECK(static_cast<double>(c.typed) >= dt * render::kCardTypeRate - 2.0);
    }
    // RAYTRACE at 10 frames a second: three characters at a time, on the retro frames only.
    const double start = show::chapterStart(7);
    const render::CardWindow rw = render::thenCardWindow(at(start + 1.0));
    std::size_t previous = 0;
    int steps = 0;
    for (double t = rw.in; t < rw.in + 1.0; t += kFrame) {
        const TimelineFrame f = at(t);
        const render::CardFrame c = render::cardFrame(f);
        if (c.typed != previous) {
            const double frames = f.localSeconds * retro::kRaytraceRetroRate;
            CHECK(frames - std::floor(frames) < kFrame * retro::kRaytraceRetroRate + 1e-6); // a retro frame began
            CHECK(c.typed - previous == 3 || previous == 0);
            ++steps;
        }
        previous = c.typed;
        CHECK(c.cursor == (f.beatPhase < 0.5f));
    }
    std::printf("(d) typing: RAYTRACE's card steps %d times in its first second\n", steps);
    CHECK(steps >= 9 && steps <= 11);
}

// --- (e) On the GPU ------------------------------------------------------------------------------------------------------

struct Picture {
    std::uint32_t width = 0, height = 0;
    std::vector<std::uint8_t> rgb;
    const std::uint8_t* at(std::uint32_t x, std::uint32_t y) const { return &rgb[(std::size_t{y} * width + x) * 3]; }
};

class Harness {
public:
    Harness(gfx::Context& ctx, const render::RenderSettings& settings)
        : readback_(ctx, settings.width, settings.height), ring_(ctx), renderer_(ctx, ring_, settings) {}
    Picture render(const TimelineFrame& frame, bool hud) {
        renderer_.setHud(hud);
        VkCommandBuffer cmd = ring_.begin();
        renderer_.render(cmd, frame);
        readback_.record(cmd, renderer_.output());
        ring_.wait(ring_.submit());
        return {readback_.width(), readback_.height(), readback_.rgb()};
    }

private:
    gfx::Readback readback_;
    gfx::FrameRing ring_;
    render::Renderer renderer_; // last: waits for the ring before its images go
};

void save(const Picture& picture, const std::string& path) {
    std::filesystem::create_directories(std::filesystem::path(path).parent_path());
    image::writePng(path, static_cast<int>(picture.width), static_cast<int>(picture.height), picture.rgb.data());
    std::printf("wrote %s\n", path.c_str());
}

render::RenderSettings placeholderSettings(std::uint32_t width, std::uint32_t height) {
    render::RenderSettings settings;
    settings.width = width;
    settings.height = height;
    settings.sceneMap = [](Effect) { return modern::SceneId::Placeholder; }; // the cards are the subject
    return settings;
}

void testNowCardOnGpu(gfx::Context& ctx) {
    const std::uint32_t w = 1280, h = 720;
    Harness harness(ctx, placeholderSettings(w, h));
    const TimelineFrame head = at(show::chapterStart(11) + 0.01); // STUDIO NIGHT, 2026 throughout
    const render::CardWindow window = render::nowCardWindow(head);
    const TimelineFrame frame = at(0.5 * (window.in + window.hold) + 1.0);
    const render::CardFrame card = render::cardFrame(frame);
    CHECK(card.now && card.nowRule == 1.f && card.nowAlpha == render::kNowCardAlpha && !card.then);
    const Picture without = harness.render(frame, false);
    const Picture with = harness.render(frame, true);
    save(with, std::string(EVO_TEST_OUT_DIR) + "/epoch_cards/studio_night-1280x720.png");

    const render::NowCardLayout layout = render::nowCardLayout(show::cues[11].nowCard, w, h);
    const std::uint32_t rectX = (w - h * 4 / 3) / 2;
    std::size_t changed = 0;
    float most = 0.f;
    for (std::uint32_t y = 0; y < h; ++y)
        for (std::uint32_t x = 0; x < w; ++x) {
            const std::uint8_t* a = without.at(x, y);
            const std::uint8_t* b = with.at(x, y);
            if (std::memcmp(a, b, 3) == 0)
                continue;
            ++changed;
            // In the box, in the left margin, on a stroke or the rule: no shadow, no glow.
            CHECK(static_cast<int>(x) >= layout.x0 && static_cast<int>(x) < layout.x1);
            CHECK(static_cast<int>(y) >= layout.y0 && static_cast<int>(y) < layout.y1);
            CHECK(x < rectX);
            const float px = static_cast<float>(x) + 0.5f, py = static_cast<float>(y) + 0.5f;
            const bool onRule = py > layout.ruleY - 0.5f && py < layout.ruleY + layout.ruleThickness + 0.5f &&
                                px > layout.ruleX0 - 0.5f && px < layout.ruleX1 + 0.5f;
            const bool onStroke = text::strokeCoverage(layout.segments, px, py) > 0.f ||
                                  text::strokeCoverage(layout.segments, px + 0.05f, py + 0.05f) > 0.f ||
                                  text::strokeCoverage(layout.segments, px - 0.05f, py - 0.05f) > 0.f;
            CHECK(onRule || onStroke);
            // White blended at alpha: alpha = (out - below) / (255 - below), from the darkest channel.
            int c = 0;
            for (int k = 1; k < 3; ++k)
                if (a[k] < a[c])
                    c = k;
            if (a[c] < 200) {
                const float alpha = static_cast<float>(b[c] - a[c]) / static_cast<float>(255 - a[c]);
                CHECK(alpha <= render::kNowCardAlpha + 0.012f);
                most = std::max(most, alpha);
            }
        }
    std::printf("(e) 2026 card: %zu pixels changed, all in its box [%d, %d) x [%d, %d), left of the picture at "
                "%u; alpha peaks at %.3f\n",
                changed, layout.x0, layout.x1, layout.y0, layout.y1, rectX, most);
    CHECK(changed > 500);
    CHECK(std::abs(most - render::kNowCardAlpha) <= 0.012f);
}

// The 1993 card on the CPU, as card_crt.comp draws it: the colour of a lit pixel.
std::optional<std::array<float, 3>> crtCardPixel(const render::ThenCardLayout& card, std::size_t typed,
                                                 bool cursor, std::uint32_t px, std::uint32_t py) {
    const double cw = card.rectWidth, ch = card.rectHeight;
    if (px < card.rectX || py < card.rectY || px >= card.rectX + card.rectWidth || py >= card.rectY + card.rectHeight)
        return std::nullopt;
    const double u = (px - card.rectX + 0.5) / cw, v = (py - card.rectY + 0.5) / ch;
    const double cx = u * 2 - 1, cy = v * 2 - 1, r2 = cx * cx + cy * cy;
    const double k = 0.02, bulge = (1 + k * r2) / (1 + 2 * k); // crtBulge
    const double sx = cx * bulge * 0.5 + 0.5, sy = cy * bulge * 0.5 + 0.5;
    const double ax = sx * cw - card.originX, ay = sy * ch - card.originY;
    const int fx = static_cast<int>(std::floor(ax / card.scale)), fy = static_cast<int>(std::floor(ay / card.scale));
    if (fx < 0 || fy < 0 || fy >= 8)
        return std::nullopt;
    const std::size_t index = static_cast<std::size_t>(fx) / 8;
    const int x = fx % 8;
    bool lit = false;
    if (index < typed)
        lit = (text::glyph(static_cast<char>(card.codes[index]))[fy] & (0x80u >> x)) != 0;
    else if (index == typed && cursor)
        lit = x < 7;
    if (!lit)
        return std::nullopt;
    const int row = static_cast<int>(std::floor(sy * 200));
    std::array<float, 3> color{1.f, 1.f, 1.f};
    for (int c = 0; c < 3; ++c) {
        if (row % 2 == 1)
            color[c] *= 0.75f; // the scanlines
        if (c != static_cast<int>(px % 3))
            color[c] *= 1.f - 0.06f; // the mask
        color[c] *= static_cast<float>(1.0 - 0.10 * 0.5 * r2); // the vignette
    }
    return color;
}

void testThenCardOnGpu(gfx::Context& ctx) {
    const std::uint32_t w = 1280, h = 720;
    Harness harness(ctx, placeholderSettings(w, h));
    const TimelineFrame frame = at(2.5); // BOOT: the card typed, black beneath
    const render::CardFrame card = render::cardFrame(frame);
    CHECK(card.then && !card.now);
    const Picture without = harness.render(frame, false);
    const Picture with = harness.render(frame, true);
    save(with, std::string(EVO_TEST_OUT_DIR) + "/epoch_cards/boot-1280x720.png");

    const render::ThenCardLayout layout = render::thenCardLayout(render::thenCardText(Effect::Boot), w, h);
    const std::size_t typed = std::min(card.typed, layout.codes.size());
    CHECK(layout.scale == 1 && typed == layout.codes.size());
    std::size_t lit = 0, mismatched = 0, oddRows = 0;
    std::uint32_t leftmost = w;
    for (std::uint32_t y = 0; y < h; ++y)
        for (std::uint32_t x = 0; x < w; ++x) {
            const bool changed = std::memcmp(without.at(x, y), with.at(x, y), 3) != 0;
            const auto expected = crtCardPixel(layout, typed, card.cursor, x, y);
            if (changed)
                leftmost = std::min(leftmost, x);
            if (!changed && !expected)
                continue;
            if (changed != expected.has_value()) {
                ++mismatched; // a pixel centre on a font pixel's edge, where float rounding may differ
                continue;
            }
            ++lit;
            CHECK(static_cast<int>(x) >= layout.x0 && static_cast<int>(x) < layout.x1);
            CHECK(static_cast<int>(y) >= layout.y0 && static_cast<int>(y) < layout.y1);
            for (int c = 0; c < 3; ++c)
                CHECK(std::abs(static_cast<float>(with.at(x, y)[c]) - 255.f * (*expected)[c]) <= 1.5f);
            if ((*expected)[(x + 1) % 3] < 0.72f) // the scanline's 0.75 times the mask and vignette
                ++oddRows;
        }
    std::printf("(e) 1993 card: %zu pixels lit through the CRT, %zu of them on odd picture rows (25 %% darker); "
                "%zu on a font pixel's edge\n",
                lit, oddRows, mismatched);
    CHECK(lit > 400 && oddRows > 50 && oddRows < lit - 50);
    CHECK(mismatched * 100 <= lit);
    // The bulge moves the card outwards: its left end sits left of where the unbulged card starts.
    CHECK(leftmost < layout.rectX + static_cast<std::uint32_t>(layout.originX));

    // Without the CRT: plain white at the unbulged place.
    render::RenderSettings plain = placeholderSettings(w, h);
    plain.crt = false;
    Harness flat(ctx, plain);
    const Picture flatWith = flat.render(frame, true);
    const std::uint32_t x0 = layout.rectX + layout.originX, y0 = layout.rectY + layout.originY;
    std::size_t white = 0;
    for (std::uint32_t y = y0; y < y0 + 8; ++y)
        for (std::uint32_t x = x0; x < x0 + 8; ++x) {
            const bool set = (text::glyph('1')[y - y0] & (0x80u >> (x - x0))) != 0;
            const std::uint8_t* p = flatWith.at(x, y);
            CHECK(set == (p[0] == 255 && p[1] == 255 && p[2] == 255));
            white += set ? 1 : 0;
        }
    CHECK(white > 10); // the '1' of 1993, pixel for pixel
}

void look(gfx::Context& ctx, const std::string& dir) {
    render::RenderSettings settings;
    settings.width = 480;
    settings.height = 270;
    settings.spp = 4;
    settings.detail = {1.f / 16};
    Harness harness(ctx, settings);
    const auto still = [&](const char* name, double t) { save(harness.render(at(t), true), dir + "/" + name + ".png"); };
    const render::CardWindow dots = render::nowCardWindow(at(show::chapterStart(2) + 0.01));
    still("dots", dots.hold - 0.2);
    const show::TransitionMarks plasma = show::transitionMarks(at(show::chapterStart(3) + 0.01));
    still("plasma_mid_wipe", plasma.downbeat - plasma.bar);
    const show::TransitionMarks raytrace = show::transitionMarks(at(show::chapterStart(7) + 0.01));
    still("raytrace_drop_plus_bar", raytrace.downbeat + raytrace.bar);
    still("galaxy", render::nowCardWindow(at(show::chapterStart(21) + 0.01)).in + 2.0);
    still("credits", show::chapterStart(29) + 6.0);
}

} // namespace

int main(int argc, char** argv) {
    test::setEnv("EVO_SHADER_DIR", EVO_TEST_SHADER_DIR);
    try {
        if (argc >= 3 && std::string(argv[1]) == "--look") {
            auto ctx = gfx::Context::create({});
            look(ctx, argv[2]);
            return 0;
        }
        testTexts();
        testTiming();
        testSplitAndFlip();
        testTyping();
        {
            auto ctx = gfx::Context::create({.validation = true});
            if (!ctx.validationEnabled())
                std::printf("warning: validation layer unavailable; only the pixels are checked\n");
            testNowCardOnGpu(ctx);
            testThenCardOnGpu(ctx);
            CHECK(ctx.validationMessageCount() == 0);
        }
        CHECK(gfx::validationMessagesReported() == 0);
    } catch (const std::exception& e) {
        std::fprintf(stderr, "FAILED: %s (after %d checks)\n", e.what(), checksPassed);
        return 1;
    }
    std::printf("epoch_cards: all %d checks passed\n", checksPassed);
    return 0;
}
