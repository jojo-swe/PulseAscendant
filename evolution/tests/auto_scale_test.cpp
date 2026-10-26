// The window's adaptive trace scale (app/auto_scale.hpp): the table's scale per chapter and output size, and
// the controller's moves (down after a sustained miss, up after a long stretch of headroom, the chapter's
// last scale when it comes back, nothing without a GPU time).

#include "app/auto_scale.hpp"
#include "app/options.hpp"

#include <cmath>
#include <cstdio>
#include <stdexcept>

namespace {

int checksPassed = 0;

#define CHECK(x)                                                                                             \
    do {                                                                                                     \
        if (!(x))                                                                                            \
            throw std::runtime_error(#x);                                                                    \
        ++checksPassed;                                                                                      \
    } while (false)

bool near(float a, float b) {
    return std::abs(a - b) < 1e-5f;
}

// Feeds `frames` frames of `chapter` at `gpuMs` and returns the last scale.
float run(evo::TraceScaleController& c, int chapter, double gpuMs, int frames) {
    float scale = 0.f;
    for (int i = 0; i < frames; ++i)
        scale = c.update(chapter, gpuMs);
    return scale;
}

} // namespace

int main() {
    try {
        using evo::kAutoMaxTraceScale;
        using evo::kAutoMinTraceScale;
        // The table at 2560x1440 as it stands, coarser outputs scaled up by sqrt of the pixel ratio, clamped.
        CHECK(near(evo::kAutoMaxTraceScale, evo::kWindowTraceScale));
        for (int chapter = 0; chapter < 31; ++chapter) {
            const float s = evo::autoTraceScale(chapter, 2560, 1440);
            CHECK(s >= kAutoMinTraceScale && s <= kAutoMaxTraceScale);
            CHECK(evo::autoTraceScale(chapter, 1280, 720) >= s);
        }
        CHECK(near(evo::autoTraceScale(15, 2560, 1440), 0.45f));              // GLASS CATHEDRAL
        CHECK(near(evo::autoTraceScale(15, 1280, 720), kAutoMaxTraceScale));  // 0.9, clamped
        CHECK(near(evo::autoTraceScale(15, 3840, 2160), kAutoMinTraceScale)); // 0.3, clamped
        CHECK(near(evo::autoTraceScale(11, 2560, 1440), kAutoMaxTraceScale));
        CHECK(near(evo::autoTraceScale(99, 2560, 1440), kAutoMaxTraceScale));

        // A chapter starts at its table scale; without a GPU time nothing moves.
        evo::TraceScaleController c(2560, 1440);
        CHECK(near(c.update(15, 0.0), 0.45f));
        CHECK(near(run(c, 15, 0.0, 500), 0.45f));
        // Within budget: stays.
        CHECK(near(run(c, 15, 12.0, 500), 0.45f));
        // A sustained miss (twice the budget): down to the scale whose pixels fit, at least one step.
        const float down = run(c, 15, 30.0, evo::TraceScaleController::kSettleFrames + 60);
        CHECK(down < 0.45f && down >= kAutoMinTraceScale);
        CHECK(near(down, kAutoMinTraceScale)); // 0.45 * sqrt(15 / 30) * 0.95 = 0.30, clamped to 0.4
        // A brief spike does not move it.
        evo::TraceScaleController d(2560, 1440);
        d.update(8, 0.0);
        run(d, 8, 10.0, 20);
        CHECK(near(run(d, 8, 40.0, 5), 0.5f) && near(run(d, 8, 10.0, 100), 0.5f));
        // Long headroom: up one step at a time, never above the window's scale.
        const float up = run(d, 8, 5.0, evo::TraceScaleController::kSettleFrames + 200);
        CHECK(near(up, 0.55f));
        CHECK(near(run(d, 8, 5.0, 5000), kAutoMaxTraceScale));
        // A chapter comes back at the scale it last settled at; another starts from its table entry.
        CHECK(near(c.update(13, 0.0), 0.45f));
        CHECK(near(c.update(15, 0.0), kAutoMinTraceScale));
        // A resize starts every chapter over from the table.
        c.resize(1280, 720);
        CHECK(near(c.update(15, 0.0), kAutoMaxTraceScale));

        // The options: the window adapts by default, --bench only with --trace-scale auto, a fixed scale
        // never.
        CHECK(evo::adaptiveTraceScale(evo::parseOptions({})));
        CHECK(!evo::adaptiveTraceScale(evo::parseOptions({"--trace-scale", "0.5"})));
        CHECK(!evo::adaptiveTraceScale(evo::parseOptions({"--bench"})));
        CHECK(evo::adaptiveTraceScale(evo::parseOptions({"--bench", "--trace-scale", "auto"})));
        CHECK(!evo::adaptiveTraceScale(evo::parseOptions({"--render-stream", "--duration", "1"})));
        CHECK(near(evo::traceScaleFor(evo::parseOptions({"--bench"})), 0.67f));
    } catch (const std::exception& error) {
        std::fprintf(stderr, "CHECK failed: %s\n", error.what());
        return 1;
    }
    std::printf("%d checks passed\n", checksPassed);
    return 0;
}
