// The secret part: its timeline and text on the CPU (exact strings at three times, the numbers from the
// cards' accessors, the applause and the answer), the window's keys and its way back to the show, and its
// picture from its own retro stage and compositor on the GPU (pure in the retro frame, black after the
// power-off). Runs with the validation layer on and fails on any message it reports. With EVO_PATIENT_ANSWER
// set it also checks that word against the stored digest.

#include "app/app.hpp"
#include "gfx/buffer.hpp"
#include "gfx/context.hpp"
#include "gfx/frame_ring.hpp"
#include "gfx/readback.hpp"
#include "gfx/sync.hpp"
#include "modern/scenes/cosmos.hpp"
#include "modern/scenes/glass.hpp"
#include "modern/scenes/particles.hpp"
#include "render/retro_stage.hpp"
#include "render/secret_renderer.hpp"
#include "retro/secret_part.hpp"
#include "show/timeline.hpp"
#include "test_env.hpp"
#include "text/text_pass.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

using evo::retro::SecretInput;

int checksPassed = 0;

#define CHECK(x)                                                                                             \
    do {                                                                                                     \
        if (!(x))                                                                                            \
            throw std::runtime_error(#x);                                                                    \
        ++checksPassed;                                                                                      \
    } while (false)

std::vector<std::string> textAt(double t, const SecretInput& input = {}) {
    return evo::retro::secretScreenText(t, input);
}

bool contains(const std::vector<std::string>& lines, const std::string& line) {
    return std::find(lines.begin(), lines.end(), line) != lines.end();
}

void testTimeline() {
    const auto& marks = evo::retro::secretMarks();
    // On track 0's beats: screen 2 on the fifth downbeat, screen 3 and the puzzle on later ones.
    const auto grid = evo::show::beatGrid(evo::retro::kSecretTrack);
    const auto onDownbeat = [&](double t) {
        const int beat = grid.beatAt(t + 1e-9);
        return std::abs(grid.beatTime(beat) - t) < 1e-9 && grid.bar(beat) == 0;
    };
    CHECK(onDownbeat(marks.screen2) && onDownbeat(marks.screen3) && onDownbeat(marks.puzzle));
    CHECK(marks.screen2 > 7 && marks.screen2 < 9);
    CHECK(marks.screen3 > 18 && marks.screen3 < 19.5);
    CHECK(marks.puzzle > marks.lastDevice - 1.0 / 35 && marks.puzzle < 24); // on the frame the typing ends
    CHECK(std::is_sorted(marks.list.begin(), marks.list.end()));
    CHECK(marks.list.front() == marks.screen2 && marks.list.back() < marks.screen3 - 3);
    CHECK(evo::retro::secretDuration({}) == evo::retro::kSecretSeconds);
    CHECK(evo::retro::secretScreen(2, {}) == 1 && evo::retro::secretScreen(10, {}) == 2 &&
          evo::retro::secretScreen(20, {}) == 3 && evo::retro::secretScreen(27.9, {}) == 0);
}

void testText() {
    using evo::retro::groupDigits;
    const evo::retro::SecretStats stats = evo::retro::secretStats();
    // The numbers are the cards' accessors and the show's length, never typed.
    CHECK(stats.photons == evo::modern::glassCard(evo::show::Effect::Cathedral).exportPhotons);
    CHECK(stats.stars == evo::modern::kGalaxyStars && stats.stars == 16777216);
    CHECK(stats.particles == evo::modern::kDotsParticles);
    CHECK(stats.frames == static_cast<std::uint64_t>(std::ceil(60 * evo::show::showDuration() - 1e-9)));
    CHECK(groupDigits(4194304) == "4,194,304" && groupDigits(42670) == "42,670" && groupDigits(7) == "7");

    // t = 2: the box is drawn, its title typed, the first line typing.
    const std::vector<std::string> one = textAt(2);
    CHECK(one.size() == 2);
    CHECK(one[0] == "EVOLUTION \xC2\xB7 SECRET PART");
    CHECK(one[1] == "CONGRATULATIONS");
    const std::vector<std::string> later = textAt(6);
    CHECK(later.size() == 5);
    CHECK(later[1] == "CONGRATULATIONS. ROUGHLY 0.3% OF" && later[2] == "VIEWERS FIND THIS." &&
          later[3] == "WE ARE NOT SMARTER THAN YOU." && later[4] == "WE JUST HAD MORE TIME.");

    // t = 10: screen 1 cleared, the heading and two numbers listed.
    const std::vector<std::string> two = textAt(10);
    CHECK(two.size() == 3);
    CHECK(two[0] == "TONIGHT, FOR YOU:");
    CHECK(two[1] == " " + groupDigits(stats.photons) + " PHOTONS");
    CHECK(two[2].find(groupDigits(stats.stars) + " STARS") != std::string::npos);
    const std::vector<std::string> full = textAt(18);
    CHECK(full.size() == 8);
    CHECK(full[3].find(groupDigits(stats.particles) + " PARTICLES") != std::string::npos);
    CHECK(full[4].find(groupDigits(stats.frames) + " FRAMES") != std::string::npos);
    CHECK(full[5] == "ALL OF IT TO SAY HELLO." && full[6] == "A LIKE WOULD BE NICE." &&
          full[7] == "A SMILE IS ENOUGH.");

    // t = 20: screen 3, the first device typed, the second typing.
    const std::vector<std::string> three = textAt(20);
    CHECK(three.size() == 2);
    CHECK(three[0] == "Sound Blaster 16 at 220h ........ found");
    CHECK(three[1] == "Ray trac");
    // The last device waits; the patient's row shows dim from the puzzle on.
    const std::vector<std::string> waiting = textAt(25);
    CHECK(waiting.size() == 5);
    CHECK(waiting[1] == "Ray tracing hardware ............ found");
    CHECK(waiting[2] == "Applause device ............... press L");
    CHECK(waiting[3] == "FOR THE PATIENT:");
    std::string row;
    for (const std::uint32_t n : evo::retro::patientNumbers())
        row += (row.empty() ? "" : " ") + std::to_string(n);
    CHECK(waiting[4] == row);
    // Every line fits BOOT's 39 columns.
    for (const double t : {1.0, 3.0, 6.0, 9.0, 12.0, 17.0, 19.5, 21.0, 23.0, 26.0})
        for (const std::string& line : textAt(t))
            CHECK(line.size() <= 39);
    CHECK(textAt(27.9).empty());
}

void testApplause() {
    const auto& marks = evo::retro::secretMarks();
    // Before the last device is on screen, L counts for nothing.
    const SecretInput early{marks.screen3 + 0.5, -1};
    CHECK(evo::retro::settledInput(early).applauseAt < 0);
    CHECK(textAt(25, early) == textAt(25));

    const SecretInput clap{marks.lastDevice + 0.2, -1};
    const std::vector<std::string> answered = textAt(26.5, clap);
    CHECK(answered[2] == "Applause device ..... found. thank you.");
    CHECK(answered[3] == "WE SAW THAT.");
    // One extra-high bounce, on the beat after the press, and only that one.
    double highest = 0, highestPlain = 0;
    for (double t = marks.lastDevice; t < 27.3; t += 1.0 / 35) {
        highest = std::max(highest, evo::retro::secretGlenzLift(t, clap));
        highestPlain = std::max(highestPlain, evo::retro::secretGlenzLift(t, {}));
    }
    CHECK(highestPlain <= 1.0 + 1e-9 && highest > 1.5);
    int high = 0;
    for (double t = marks.lastDevice; t < 27.3; t += 1.0 / 35)
        high += evo::retro::secretGlenzLift(t, clap) > 1.0 + 1e-9;
    CHECK(high > 0 && high < 35);
}

void testAnswer() {
    using evo::retro::sha256Hex;
    CHECK(sha256Hex("") == "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
    CHECK(sha256Hex("abc") == "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
    CHECK(sha256Hex(std::string(1000, 'a')) ==
          "41edece42d63e8d9bf515a9ba6932e1c20cbc9f5a5d134645adb5db1b9737ea3");
    for (const char* wrong : {"", "CLAP", "STAR IT", "STARIT", "PATIENT", "1993", "2026", "EVOLUTION"})
        CHECK(!evo::retro::isPatientAnswer(wrong));
    evo::retro::AnswerKeys keys;
    for (const char c : std::string("CLAPSTARITPATIENT"))
        CHECK(!keys.press(c));

    // The reward replaces the patient's row and keeps the part at its length when answered on time.
    const auto& marks = evo::retro::secretMarks();
    const SecretInput solved{-1, 3.0};
    CHECK(evo::retro::settledInput(solved).solvedAt <= marks.puzzle + 1e-9);
    CHECK(evo::retro::secretDuration(solved) == evo::retro::kSecretSeconds);
    const std::vector<std::string> reward = textAt(26, solved);
    CHECK(contains(reward, "YOU SOLVED IT."));
    CHECK(contains(reward, "THAT TOOK LONGER THAN THE DEMO."));
    CHECK(contains(reward, "RESPECT. NOW GO MAKE SOMETHING."));
    CHECK(!contains(reward, "FOR THE PATIENT:"));
    // Answered late, the part waits for the words to be read.
    CHECK(evo::retro::secretDuration({-1, 27.0}) > 30);
    // A wrong word on the command line leaves the part as it is.
    evo::SecretSession wrong("NOTIT");
    wrong.start(5);
    CHECK(wrong.input().solvedAt < 0);

    if (const char* word = std::getenv("EVO_PATIENT_ANSWER")) {
        CHECK(evo::retro::isPatientAnswer(word));
        evo::retro::AnswerKeys typed;
        bool done = false;
        for (const char c : "XY" + std::string(word))
            done = typed.press(static_cast<char>(std::toupper(static_cast<unsigned char>(c))));
        CHECK(done);
        evo::SecretSession session(word);
        session.start(5);
        CHECK(session.input().solvedAt == marks.puzzle);
        std::printf("secret_part_test: the patient's answer checked\n");
    }
}

void testKeys() {
    evo::retro::KeyWord word = evo::secretKeyWord();
    const auto typed = [&](const std::string& keys, double step) {
        word.reset();
        bool done = false;
        double t = 100;
        for (const char c : keys) {
            done = word.press(c, t);
            t += step;
        }
        return done;
    };
    CHECK(typed("CLAP", 0.3));
    CHECK(typed("CLAP", 0.66)); // 1.98 s from C to P
    CHECK(!typed("CLAP", 0.7)); // 2.1 s
    CHECK(!typed("CALP", 0.1));
    CHECK(!typed("CLXAP", 0.1));
    CHECK(typed("CCLAP", 0.1));
    CHECK(typed("XCLAP", 0.1));
    CHECK(!typed("CLA", 0.1));

    // The window's session: the cut waits for the next beat; Esc and the end return to the show time.
    evo::SecretSession session;
    for (const char c : std::string("CLA"))
        CHECK(!session.showKey(c, 100.0));
    CHECK(session.showKey('P', 100.2));
    CHECK(session.state() == evo::SecretSession::State::Pending);
    const double cut = session.cutAt();
    CHECK(cut > 100.2 && cut < 101.0);
    CHECK(!session.update(cut - 0.01));
    const auto toPart = session.update(cut + 0.004);
    CHECK(toPart && *toPart == 0.0 && session.state() == evo::SecretSession::State::Part);
    CHECK(session.resumeAt() == cut + 0.004);
    CHECK(!session.update(5.0));
    CHECK(session.escape() == cut + 0.004);
    CHECK(session.state() == evo::SecretSession::State::Show);

    evo::SecretSession flag;
    flag.start(42.5);
    flag.partKey('L', flag.input().applauseAt >= 0 ? 0 : 1.0); // too early: nothing
    CHECK(flag.input().applauseAt < 0);
    flag.partKey('L', evo::retro::secretMarks().lastDevice + 0.1);
    CHECK(flag.input().applauseAt > 0);
    const auto back = flag.update(evo::retro::kSecretSeconds + 0.01);
    CHECK(back && *back == 42.5 && flag.state() == evo::SecretSession::State::Show);
}

// --- GPU
// ----------------------------------------------------------------------------------------------------

const evo::retro::EffectInfo& secretRegistry(evo::show::Effect) {
    return evo::retro::secretEffect();
}

std::vector<std::uint8_t> renderIndices(evo::gfx::Context& ctx, evo::gfx::FrameRing& ring,
                                        evo::render::RetroStage& stage,
                                        const evo::show::TimelineFrame& frame) {
    constexpr std::uint32_t w = evo::render::RetroStage::kWidth, h = evo::render::RetroStage::kHeight;
    evo::gfx::Buffer buffer(ctx,
                            {VkDeviceSize{w} * h, VK_BUFFER_USAGE_TRANSFER_DST_BIT, true, "secret readback"});
    VkCommandBuffer cmd = ring.begin();
    stage.render(cmd, frame);
    evo::gfx::Image& index = stage.indexImage();
    index.transition(cmd, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_PIPELINE_STAGE_2_COPY_BIT,
                     VK_ACCESS_2_TRANSFER_READ_BIT);
    VkBufferImageCopy region{};
    region.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
    region.imageExtent = {w, h, 1};
    vkCmdCopyImageToBuffer(cmd, index.handle(), VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, buffer.handle(), 1,
                           &region);
    evo::gfx::bufferBarrier2(cmd, buffer.handle(), VK_PIPELINE_STAGE_2_COPY_BIT,
                             VK_ACCESS_2_TRANSFER_WRITE_BIT, VK_PIPELINE_STAGE_2_HOST_BIT,
                             VK_ACCESS_2_HOST_READ_BIT);
    ring.wait(ring.submit());
    buffer.invalidate();
    const auto* data = static_cast<const std::uint8_t*>(buffer.mapped());
    return {data, data + buffer.size()};
}

void testPictures(evo::gfx::Context& ctx) {
    constexpr int w = evo::render::RetroStage::kWidth;
    {
        evo::gfx::FrameRing ring(ctx);
        evo::text::TextPass text(ctx, ring);
        evo::render::RetroStage stage(ctx, ring, text, secretRegistry);
        const auto count = [](const std::vector<std::uint8_t>& p, auto pred) {
            return std::count_if(p.begin(), p.end(), pred);
        };
        const auto glass = [](std::uint8_t i) { return i >= 0x10; };
        // t = 2: the box and white text, no glenz.
        const auto one = renderIndices(ctx, ring, stage, evo::retro::secretFrame(2, {}));
        CHECK(count(one, [](std::uint8_t i) { return i == 15; }) > 500);
        CHECK(count(one, glass) == 0);
        CHECK(one[36 * w + 160] == 15 && one[39 * w + 160] == 15 &&
              one[37 * w + 160] == 0); // the double rule
        // t = 10: the list and the glenz in the right third.
        const auto two = renderIndices(ctx, ring, stage, evo::retro::secretFrame(10, {}));
        CHECK(count(two, glass) > 300);
        for (int y = 0; y < 200; ++y)
            for (int x = 0; x < 200; ++x)
                CHECK(!glass(two[static_cast<std::size_t>(y * w + x)]) || x >= 200);
        // t = 20: grey device lines at the top.
        const auto three = renderIndices(ctx, ring, stage, evo::retro::secretFrame(20, {}));
        CHECK(count(three, [](std::uint8_t i) { return i == 7; }) > 100);
        // Pure in the retro frame: the same frame again, after another, renders the same indices.
        const auto again = renderIndices(ctx, ring, stage, evo::retro::secretFrame(10 + 0.1 / 35, {}));
        CHECK(again == two);
        // The applause and the answer change the key, so a cached frame never shows the wrong input.
        const auto& marks = evo::retro::secretMarks();
        const auto plain = renderIndices(ctx, ring, stage, evo::retro::secretFrame(26, {}));
        const auto clapped =
            renderIndices(ctx, ring, stage, evo::retro::secretFrame(26, {marks.lastDevice + 0.1, -1}));
        CHECK(plain != clapped);
        // The power-off: a bright line, then nothing but a dot.
        const auto line = renderIndices(ctx, ring, stage, evo::retro::secretFrame(27.6, {}));
        CHECK(count(line, [](std::uint8_t i) { return i == 14; }) >= 2 * 4);
        const auto end = renderIndices(ctx, ring, stage, evo::retro::secretFrame(27.99, {}));
        CHECK(count(end, [](std::uint8_t i) { return i != 0; }) <= 4);
    }
    {
        // The renderer: CRT-composited RGB at an output size, black around the 4:3 picture.
        constexpr std::uint32_t W = 128, H = 72;
        evo::gfx::FrameRing ring(ctx);
        evo::gfx::Readback readback(ctx, W, H);
        evo::render::SecretRenderer renderer(ctx, ring, W, H);
        const auto frame = [&](double t) {
            VkCommandBuffer cmd = ring.begin();
            renderer.render(cmd, t, {});
            readback.record(cmd, renderer.output());
            ring.wait(ring.submit());
            return readback.rgb();
        };
        const std::vector<std::uint8_t> two = frame(10);
        CHECK(two.size() == std::size_t{W} * H * 3);
        CHECK(std::count_if(two.begin(), two.end(), [](std::uint8_t c) { return c > 128; }) > 30);
        CHECK(two[0] == 0 && two[1] == 0 && two[2] == 0); // the pillarbox
        CHECK(frame(10) == two);
    }
}

} // namespace

int main() {
    try {
        evo::test::setEnv("EVO_SHADER_DIR", EVO_TEST_SHADER_DIR);
        testTimeline();
        testText();
        testApplause();
        testAnswer();
        testKeys();
        {
            auto ctx = evo::gfx::Context::create({.validation = true});
            if (!ctx.validationEnabled())
                std::printf("warning: validation layer unavailable; only the pixels are checked\n");
            testPictures(ctx);
            CHECK(ctx.validationMessageCount() == 0);
        }
        CHECK(evo::gfx::validationMessagesReported() == 0);
    } catch (const std::exception& error) {
        std::fprintf(stderr, "FAILED: %s\n", error.what());
        return 1;
    }
    std::printf("secret_part_test: %d checks passed\n", checksPassed);
    return 0;
}
