#pragma once

// The epoch cards: the show's recurring graphic, one card per era stating what that era's picture is.
//
//   1993  the demo's 8x8 font at an integer scale, max(1, floor(height / 480)) (3 at 1440p, 4 at 2160p), in
//         phosphor white with a block cursor that blinks on the beat. It types in at 30 characters a second,
//         quantised to the chapter's retro frame lock (RAYTRACE's 10 frames a second make it stutter). It sits
//         bottom-left in the 4:3 picture and is drawn through the CRT: crtBulge, the scanlines (odd picture
//         rows 25 % darker), the phosphor mask and the vignette (shaders/text/card_crt.comp). Shown only while
//         the 1993 picture fills the frame: never from a Portal's approach on, and not at all in FINALE, PORTAL
//         and REBOOT, where the Studio's CRT shows the 1993 picture and the retro stage types the card into it.
//   2026  the same glyph skeletons as rounded distance-field strokes (text/stroke_font.hpp), pure white at 0.85
//         alpha, no shadow, glow or outline (shaders/text/card_sdf.comp). The year is set at twice the body's
//         cap height, the feat under it wraps at " · " (then at spaces) to fit the 16:9 frame's left margin,
//         bottom-left, outside the 4:3 picture. A hairline rule above the year draws left to right over one
//         beat (smoothstep) and the text is revealed behind its head; it fades out over one beat.
//
// When: a card appears 1 bar after the chapter start, holds until 1 bar after the accent and is gone by the
// chapter's last 2 bars; RAYTRACE's 2026 card appears on the drop (D) and holds to the end; a Portal's 2026 card
// appears as the camera clears the glass (D + 0.5 s). Within that window a Cut or BeatSwap chapter shows the
// 1993 card while era < 0.5 and the 2026 card from 0.5 (so a BeatSwap flips the card in the frame the picture
// flips); a Wipe shows both, split at the wipe line (2026 left of it, 1993 right of it). Bars, beats, D and the
// accent are show::transitionMarks'; every card is a pure function of the show time.
//
// CREDITS adds a quiet typographic layer: show::kCreditsLine, one credit per line in the 2026 face at 0.6 alpha,
// bottom-left in the left margin, from 1 bar in to the end of the chapter.
//
// The texts: the 1993 cards' numbers come from the retro configuration (thenCardText), the 2026 cards' from
// each scene's ModernScene::nowCard, the cue sheet's line where a scene has none (nowCardText).

#include "gfx/buffer.hpp"
#include "gfx/compute_pipeline.hpp"
#include "gfx/context.hpp"
#include "gfx/frame_ring.hpp"
#include "gfx/image.hpp"
#include "modern/modern_scene.hpp"
#include "show/timeline.hpp"
#include "text/stroke_font.hpp"

#include <volk.h>

#include <array>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace evo::render {

// --- The texts ---------------------------------------------------------------------------------------------------

// The 1993 card of an effect, its numbers from the retro constants; "" for an effect without one.
std::string thenCardText(show::Effect effect);
// The 2026 card: the scene's card for the effect, or the cue sheet's line when the scene gives none (or there
// is no scene).
std::string nowCardText(show::Effect effect, const modern::ModernScene* scene);
// FINALE, PORTAL and REBOOT: the retro stage types their 1993 card into the Studio's CRT.
bool thenCardOnCrt(show::Effect effect);

// --- When ----------------------------------------------------------------------------------------------------------

inline constexpr double kCardTypeRate = 30;      // 1993: characters per second
inline constexpr float kNowCardAlpha = 0.85f;    // 2026
inline constexpr float kCreditsCardAlpha = 0.6f; // the credits layer

// A card's span in show seconds: it appears at `in`, is held to `hold`, then fades and is gone at `out`
// (the 1993 card goes at once: hold == out). Empty (out <= in) for a chapter without the card.
struct CardWindow {
    double in = 0, hold = 0, out = 0;
    bool empty() const { return out <= in; }
};
CardWindow thenCardWindow(const show::TimelineFrame& frame);
CardWindow nowCardWindow(const show::TimelineFrame& frame);
CardWindow creditsCardWindow(const show::TimelineFrame& frame);

// What a frame shows of the cards.
struct CardFrame {
    bool then = false;
    std::size_t typed = 0; // glyphs of the 1993 card typed so far (may exceed the card's length)
    bool cursor = false;   // the block cursor lit (the first half of every beat)
    float thenFrom = 0.f;  // the 1993 card shows right of this fraction of the width (the wipe line)
    bool now = false;
    float nowAlpha = 0.f; // kNowCardAlpha, fading out over the window's last beat
    float nowRule = 0.f;  // the rule's progress, 0 -> 1 over the first beat (smoothstep)
    float nowTo = 1.f;    // the 2026 card shows left of this fraction of the width (the wipe line)
    bool credits = false;
    float creditsAlpha = 0.f;
    float creditsRule = 0.f;
};
CardFrame cardFrame(const show::TimelineFrame& frame);

// --- Where -----------------------------------------------------------------------------------------------------------

// The 2026 card (or the credits layer) laid out on a width x height output.
struct NowCardLayout {
    std::vector<std::string> lines; // the year (if any) first, then the feat lines
    std::vector<text::StrokeSegment> segments;
    float yearCap = 0, bodyCap = 0; // cap heights in pixels
    float textLeft = 0, textRight = 0; // the ink's extent across
    float ruleX0 = 0, ruleX1 = 0, ruleY = 0, ruleThickness = 0;
    int x0 = 0, y0 = 0, x1 = 0, y1 = 0; // the box (half-open) every pixel the card may touch lies in
};
// `text` is "YEAR · FEAT · FEAT ..."; withYear = false sets every item as feat (the credits), and
// itemPerLine puts each " · " item on a line of its own.
NowCardLayout nowCardLayout(std::string_view text, std::uint32_t width, std::uint32_t height, bool withYear = true,
                            bool itemPerLine = false);

// The 1993 card laid out on a width x height output.
struct ThenCardLayout {
    int scale = 1;
    std::vector<std::uint8_t> codes; // the 8x8 font's glyph codes (text::glyphCodes)
    // The centred 4:3 rectangle (crtContentRect) and the card's top-left in it, in its unbulged pixels.
    std::uint32_t rectX = 0, rectY = 0, rectWidth = 0, rectHeight = 0;
    int originX = 0, originY = 0;
    int x0 = 0, y0 = 0, x1 = 0, y1 = 0; // the output box (half-open) the bulged card lies in
};
ThenCardLayout thenCardLayout(std::string_view text, std::uint32_t width, std::uint32_t height);

// --- The pass ----------------------------------------------------------------------------------------------------------

// Draws a frame's cards over the renderer's RGBA8 output, after the compositor: card_crt.comp for the 1993
// card, card_sdf.comp for the 2026 card and the credits layer, each dispatched over its own box only, so no
// other pixel is touched. The 2026 texts come from `nowCard` (the modern stage's scenes in the show), asked
// once per effect, when its card first shows. Records into the ring's current command buffer; the per-frame
// data goes into host-visible buffers of the slot being recorded.
class EpochCards {
public:
    using NowCardSource = std::function<std::string(show::Effect)>;

    EpochCards(gfx::Context& ctx, gfx::FrameRing& ring, NowCardSource nowCard);
    ~EpochCards();
    EpochCards(const EpochCards&) = delete;
    EpochCards& operator=(const EpochCards&) = delete;

    // `crt` draws the 1993 card through the CRT (as the renderer's CRT setting). Leaves output in GENERAL.
    void record(VkCommandBuffer cmd, gfx::Image& output, const show::TimelineFrame& frame, bool crt);

    // The 2026 card text of an effect, as drawn (cached).
    const std::string& nowText(show::Effect effect);

private:
    struct Slot {
        gfx::Buffer segments;
        gfx::Buffer glyphs;
    };
    void drawStrokes(VkCommandBuffer cmd, gfx::Image& output, const NowCardLayout& layout, std::uint32_t first,
                     float alpha, float rule, float clipX);

    gfx::Context& ctx_;
    gfx::FrameRing& ring_;
    NowCardSource source_;
    gfx::ComputePipeline sdf_;
    gfx::ComputePipeline crt_;
    std::array<Slot, gfx::FrameRing::kFramesInFlight> slots_;
    std::array<std::optional<std::string>, show::kEffectCount> nowTexts_;
};

} // namespace evo::render
