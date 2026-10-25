// The act joins and the CRT's glass, on the CPU:
//   (a) the acts join without a cut: at 194.00 (PORTAL into STUDIO NIGHT) and 412.40 (RETURN into REBOOT)
//       the Studio shows on both sides, on the same lens, and from the frame before the join to the join the
//       eye moves at most 1 cm and the view turns at most 0.2 degrees; the steps either side of the join
//       are alike (within 1 mm and 0.02 degrees), so the move runs on through it;
//   (b) FLYBY's glass: crt.comp's view on the last 1993 frame (D - 1/60 s) is the Studio camera's framing at
//       that time, and the step from it to the Studio's first frame (D, the picture just filling the frame's
//       width, at least 98 % of it) is the pull-out's own step (within 1 %); the view is the identity from D
//       on and before D - 2 bars, and magnifies six times on D - 0.5 s;
//   (c) TUNNEL's glass: crt.comp's view magnifies from 1 at D - 2 bars to 6 on the frame before D, steadily
//       (never shrinking), centred on the tunnel's heading; the identity from D on;
//   (d) the identity everywhere outside FLYBY's and TUNNEL's approach, so crt.comp's output stays the plain
//       CRT there.

#include "modern/scene.hpp"
#include "modern/studio.hpp"
#include "render/crt_view.hpp"
#include "show/era.hpp"
#include "show/timeline.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <stdexcept>

namespace {

namespace modern = evo::modern;
namespace render = evo::render;
namespace show = evo::show;

int checksPassed = 0;

#define CHECK(x)                                                                                             \
    do {                                                                                                     \
        if (!(x))                                                                                            \
            throw std::runtime_error(#x);                                                                    \
        ++checksPassed;                                                                                      \
    } while (false)

constexpr double kFrame = 1.0 / 60;

double degreesBetween(modern::Vec3 a, modern::Vec3 b) {
    const float c = std::clamp(modern::dot(modern::normalize(a), modern::normalize(b)), -1.f, 1.f);
    return std::acos(static_cast<double>(c)) * 180.0 / 3.14159265358979;
}

// How far the camera moves and turns from one time to another.
struct Step {
    double metres, degrees;
};
Step step(double from, double to) {
    const modern::Camera a = modern::studioCamera(show::sampleTimeline(from));
    const modern::Camera b = modern::studioCamera(show::sampleTimeline(to));
    const modern::CameraBasis ba = modern::cameraBasis(a), bb = modern::cameraBasis(b);
    return {modern::length(b.eye - a.eye),
            std::max(degreesBetween(ba.forward, bb.forward), degreesBetween(ba.up, bb.up))};
}

void checkJoin(double join, show::Effect before, show::Effect after) {
    const show::TimelineFrame last = show::sampleTimeline(join - kFrame), first = show::sampleTimeline(join);
    CHECK(last.effect == before && first.effect == after);
    CHECK(modern::sceneFor(before) == modern::SceneId::Studio &&
          modern::sceneFor(after) == modern::SceneId::Studio);
    const modern::Camera a = modern::studioCamera(last), b = modern::studioCamera(first);
    CHECK(a.fovYDegrees == b.fovYDegrees);
    const Step s = step(join - kFrame, join);
    CHECK(s.metres <= 0.01 && s.degrees <= 0.2);
    // The move runs on through the join: the steps just before and just after it are alike.
    const Step earlier = step(join - 2 * kFrame, join - kFrame), later = step(join, join + kFrame);
    CHECK(std::abs(later.metres - earlier.metres) <= 0.001 &&
          std::abs(later.degrees - earlier.degrees) <= 0.02);
    std::printf("(a) the join at %.2f: %.4f m and %.3f degrees from the frame before (%.4f m, %.3f degrees "
                "the frame before that, %.4f m, %.3f degrees the frame after)\n",
                join, s.metres, s.degrees, earlier.metres, earlier.degrees, later.metres, later.degrees);
}

int chapterOf(show::Effect effect) {
    for (int c = 0; c < show::chapterCount(); ++c)
        if (show::sampleTimeline(show::chapterStart(c)).effect == effect)
            return c;
    throw std::runtime_error("no chapter");
}

show::TransitionMarks marksOf(show::Effect effect) {
    return show::transitionMarks(show::sampleTimeline(show::chapterStart(chapterOf(effect))));
}

void checkFlyby() {
    const show::TransitionMarks m = marksOf(show::Effect::Flyby);
    const double d = m.downbeat;
    const render::CrtView last = render::crtView(show::sampleTimeline(d - kFrame));
    const render::CrtView before = render::crtView(show::sampleTimeline(d - 2 * kFrame));
    const float span = modern::studioScreenSpan(show::sampleTimeline(d));
    // crt.comp frames the picture as the Studio's camera would at the same time, and the step from its last
    // frame to the Studio's first is the pull-out's own step from the frame before (no jump on the glass).
    CHECK(last.fill == 1.f && last.zoom == modern::studioScreenSpan(show::sampleTimeline(d - kFrame)));
    const float stepIn = last.zoom / before.zoom, stepOver = span / last.zoom;
    CHECK(stepOver < 1.f && std::abs(stepOver - stepIn) < 0.01f);
    CHECK(span >= 0.98f);
    CHECK(render::isIdentity(render::crtView(show::sampleTimeline(d))));
    CHECK(render::isIdentity(render::crtView(show::sampleTimeline(d + 3.0))));
    CHECK(render::isIdentity(render::crtView(show::sampleTimeline(d - 2 * m.bar - kFrame))));
    // Six times the pillarbox (where the picture spans 3/4 of the 16:9 frame) half a second before the glass.
    const render::CrtView closest = render::crtView(show::sampleTimeline(d - 0.5));
    CHECK(std::abs(closest.zoom / 0.75f - 6.f) < 0.05f);
    std::printf(
        "(b) FLYBY: crt.comp at D - 1/60 s frames the picture at %.4f of the width, the Studio on D at "
        "%.4f (steps %.4f, %.4f); x%.2f at D - 0.5 s\n",
        last.zoom, span, stepIn, stepOver, closest.zoom / 0.75f);
}

void checkTunnel() {
    const show::TransitionMarks m = marksOf(show::Effect::Tunnel);
    const double d = m.downbeat, from = d - 2 * m.bar;
    CHECK(std::abs(render::crtView(show::sampleTimeline(from + 1e-6)).zoom - 1.f) < 1e-3f);
    float previous = 1.f;
    for (double t = from; t < d; t += kFrame) {
        const render::CrtView v = render::crtView(show::sampleTimeline(t));
        CHECK(v.fill == 0.f && v.zoom >= previous - 1e-6f && v.zoom <= 6.f);
        CHECK(v.centre[0] > 0.f && v.centre[0] < 1.f && v.centre[1] > 0.f && v.centre[1] < 1.f);
        previous = v.zoom;
    }
    CHECK(previous > 5.9f);
    CHECK(render::isIdentity(render::crtView(show::sampleTimeline(d))));
    std::printf("(c) TUNNEL: x1 at D - 2 bars to x%.3f on the frame before D\n", previous);
}

void checkIdentityElsewhere() {
    int frames = 0, magnified = 0;
    for (double t = 0; t < show::showDuration(); t += 0.05) {
        const show::TimelineFrame f = show::sampleTimeline(t);
        const bool approach = (f.effect == show::Effect::Flyby || f.effect == show::Effect::Tunnel) && [&] {
            const show::TransitionMarks m = show::transitionMarks(f);
            return t >= m.downbeat - 2 * m.bar && t < m.downbeat;
        }();
        const bool identity = render::isIdentity(render::crtView(f));
        CHECK(identity || approach);
        magnified += identity ? 0 : 1;
        ++frames;
    }
    CHECK(magnified > 0);
    std::printf("(d) crt.comp's view is the identity at %d of %d times, magnified only on the approaches\n",
                frames - magnified, frames);
}

} // namespace

int main() {
    try {
        checkJoin(show::chapterStart(chapterOf(show::Effect::StudioNight)), show::Effect::Portal,
                  show::Effect::StudioNight);
        checkJoin(show::chapterStart(chapterOf(show::Effect::Reboot)), show::Effect::Return,
                  show::Effect::Reboot);
        checkFlyby();
        checkTunnel();
        checkIdentityElsewhere();
    } catch (const std::exception& e) {
        std::fprintf(stderr, "FAILED: %s\n", e.what());
        return 1;
    }
    std::printf("act_join_test: %d checks passed\n", checksPassed);
    return 0;
}
