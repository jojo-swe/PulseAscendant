#include "render/epoch_cards.hpp"

#include "gfx/paths.hpp"
#include "modern/card_format.hpp"
#include "retro/city.hpp"
#include "retro/dots.hpp"
#include "retro/dots_schedule.hpp"
#include "retro/effects.hpp"
#include "retro/glenz.hpp"
#include "retro/scene.hpp"
#include "retro/tunnel.hpp"
#include "show/director.hpp"
#include "show/era.hpp"
#include "text/font8x8.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <stdexcept>
#include <tuple>

namespace evo::render {

namespace {

using show::Effect;
using show::Transition;

// --- Texts ------------------------------------------------------------------------------------------------------

std::string n(std::uint64_t value) {
    return modern::groupThousands(value);
}

const show::Cue& cueOf(Effect effect) {
    return show::cues[static_cast<std::size_t>(effect)];
}

// --- Time ---------------------------------------------------------------------------------------------------------

struct Span {
    double start, end, accent, downbeat, bar, beat;
};

Span spanOf(const show::TimelineFrame& frame) {
    const show::TransitionMarks marks = show::transitionMarks(frame);
    const double end = frame.showSeconds - frame.localSeconds + frame.chapterDuration;
    return {marks.start, end, marks.accent, marks.downbeat, marks.bar, marks.bar / 4.0};
}

float smooth01(double x) {
    const double t = std::clamp(x, 0.0, 1.0);
    return static_cast<float>(t * t * (3.0 - 2.0 * t));
}

// --- Layout -------------------------------------------------------------------------------------------------------

struct Rect {
    std::uint32_t x, y, width, height;
};

// crtContentRect (shaders/crt_common.glsl): the centred 4:3 rectangle.
Rect contentRect(std::uint32_t w, std::uint32_t h) {
    std::uint32_t cw = w, ch = h;
    if (w * 3u >= h * 4u)
        cw = h * 4u / 3u;
    else
        ch = w * 3u / 4u;
    return {(w - cw) / 2u, (h - ch) / 2u, cw, ch};
}

std::vector<std::string> split(std::string_view text, std::string_view separator) {
    std::vector<std::string> parts;
    std::size_t from = 0;
    while (true) {
        const std::size_t at = text.find(separator, from);
        parts.emplace_back(text.substr(from, at == std::string_view::npos ? std::string_view::npos : at - from));
        if (at == std::string_view::npos)
            break;
        from = at + separator.size();
    }
    return parts;
}

// Line spacing and gaps, in body cap heights.
constexpr float kBodyPerHeight = 1.f / 96.f; // the body's cap height per output pixel of height
constexpr float kLinePitch = 1.9f;
constexpr float kYearGap = 0.9f;  // from the year's baseline to the first feat line's cap line
constexpr float kRuleGap = 0.8f;  // from the rule to the first line's cap line
constexpr float kDescender = 1.25f / text::kStrokeCapUnits; // the comma's tail below the baseline
constexpr float kMarginPerHeight = 0.02f;

// Shader data: must match card_sdf.comp's Segment (32 bytes).
struct GpuSegment {
    float ax, ay, bx, by;
    float radius, pad0, pad1, pad2;
};
static_assert(sizeof(GpuSegment) == 32);

// Must match card_sdf.comp's push constants (56 bytes).
struct SdfPush {
    std::int32_t boxX, boxY, boxWidth, boxHeight;
    std::uint32_t first, count;
    float alpha, revealX, revealSoft, clipX;
    float ruleX0, ruleY, ruleX1, ruleThickness;
};
static_assert(sizeof(SdfPush) == 56);

// Must match card_crt.comp's push constants (56 bytes).
struct CrtPush {
    std::uint32_t width, height;
    std::int32_t originX, originY;
    std::uint32_t scale, count, cursor, crt;
    std::int32_t boxX, boxY, boxWidth, boxHeight;
    float fromX, pad;
};
static_assert(sizeof(CrtPush) == 56);

constexpr std::uint32_t kFontWords = text::kGlyphCount * 2; // eight rows of a glyph in two words

constexpr std::array<gfx::Binding, 2> kBindings = {
    gfx::Binding{0, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE},
    gfx::Binding{1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER},
};

// A slot's buffer with room for `bytes` (a new one when it has not): the slot's previous frame is done (the
// ring's begin() waited for it), so the old buffer is free to go.
void reserve(gfx::Context& ctx, gfx::Buffer& buffer, VkDeviceSize bytes, const char* name) {
    if (buffer.handle() != VK_NULL_HANDLE && buffer.size() >= bytes)
        return;
    VkDeviceSize size = 4096;
    while (size < bytes)
        size *= 2;
    buffer = gfx::Buffer(ctx, gfx::BufferDesc{size, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, true, name});
}

} // namespace

// --- Texts ----------------------------------------------------------------------------------------------------------

std::string thenCardText(Effect effect) {
    const std::string dot = modern::kCardDot;
    const std::string year = "1993";
    const std::string screen = n(retro::kScreenWidth) + "×" + n(retro::kScreenHeight);
    const std::string colours = n(std::tuple_size_v<retro::Palette>) + " COLORS";
    switch (effect) {
    case Effect::Boot:
        return year + dot + "MS-DOS" + dot + screen;
    case Effect::Flyby:
        return year + dot + screen + dot + colours + dot + "FLAT SHADING";
    case Effect::Dots:
        return year + dot + n(retro::kDotsPoints) + " POINTS" + dot + n(retro::kDotsRetroRate) + " FPS";
    case Effect::Plasma:
        return year + dot + "PALETTE CYCLING" + dot + colours;
    case Effect::Tunnel:
        return year + dot + screen + dot + n(retro::kTunnelRetroRate) + " FPS" + dot +
               n(retro::kTunnelRings * retro::kTunnelRingDots) + " POINTS";
    case Effect::Glenz:
        return year + dot + n(retro::kGlenzTriangles) + " TRIANGLES" + dot + "TRANSPARENT VECTORS";
    case Effect::Raytrace:
        return year + dot + "PRECALCULATED" + dot + n(retro::kRaytraceRetroRate) + " FPS";
    case Effect::City:
        return year + dot + n(retro::kCityPolygons) + " POLYGONS" + dot + "GOURAUD";
    default:
        // The rest state no number of the retro configuration.
        return cueOf(effect).thenCard;
    }
}

std::string nowCardText(Effect effect, const modern::ModernScene* scene) {
    std::string text = scene != nullptr ? scene->nowCard(effect) : std::string();
    return text.empty() ? std::string(cueOf(effect).nowCard) : text;
}

bool thenCardOnCrt(Effect effect) {
    return effect == Effect::Finale || effect == Effect::Portal || effect == Effect::Reboot;
}

// --- When -------------------------------------------------------------------------------------------------------------

CardWindow thenCardWindow(const show::TimelineFrame& frame) {
    if (*cueOf(frame.effect).thenCard == '\0' || thenCardOnCrt(frame.effect))
        return {};
    const Span s = spanOf(frame);
    double out = std::min(s.accent + s.bar, s.end - 2.0 * s.bar);
    if (frame.transition == Transition::Portal) // gone as the camera starts for the glass
        out = std::min(out, s.downbeat - show::kPortalBars * s.bar);
    return {s.start + s.bar, out, out};
}

CardWindow nowCardWindow(const show::TimelineFrame& frame) {
    if (*cueOf(frame.effect).nowCard == '\0')
        return {};
    const Span s = spanOf(frame);
    if (frame.effect == Effect::Raytrace) // from the drop to the end
        return {s.downbeat, s.end, s.end};
    double in = s.start + s.bar;
    if (frame.transition == Transition::Portal) // once the camera is through the glass
        in = std::max(in, s.downbeat + show::kPortalGlassHalfWidth);
    const double out = std::min(s.accent + s.bar + s.beat, s.end - 2.0 * s.bar);
    return {in, std::max(in, out - s.beat), out};
}

CardWindow creditsCardWindow(const show::TimelineFrame& frame) {
    if (frame.effect != Effect::Credits)
        return {};
    const Span s = spanOf(frame);
    return {s.start + s.bar, s.end, s.end};
}

CardFrame cardFrame(const show::TimelineFrame& frame) {
    CardFrame card;
    const double t = frame.showSeconds;
    const bool wipe = frame.transition == Transition::Wipe;
    const bool modern = frame.era >= 0.5f;
    const auto inside = [t](const CardWindow& w) { return !w.empty() && t >= w.in && t < w.out; };

    const CardWindow then = thenCardWindow(frame);
    if (inside(then)) {
        bool shown = !modern;
        if (wipe)
            shown = frame.wipe < 1.f;
        else if (frame.transition == Transition::Portal)
            shown = !modern && show::portalPhase(frame).approach <= 0.f;
        if (shown) {
            card.then = true;
            card.thenFrom = wipe ? frame.wipe : 0.f;
            // Typed at the chapter's retro frame lock: the time is the start of the retro frame.
            const double rate = retro::effectFor(frame.effect).retroRate;
            const double local = std::floor(frame.localSeconds * rate + 1e-9) / rate;
            const double typing = local - (then.in - (frame.showSeconds - frame.localSeconds));
            card.typed = typing > 0 ? static_cast<std::size_t>(std::floor(typing * kCardTypeRate + 1e-6)) : 0;
            card.cursor = frame.beatPhase < 0.5f;
        }
    }

    const CardWindow now = nowCardWindow(frame);
    if (inside(now)) {
        const bool shown = wipe ? frame.wipe > 0.f : frame.transition == Transition::Portal || modern;
        if (shown) {
            const Span s = spanOf(frame);
            card.now = true;
            card.nowTo = wipe ? frame.wipe : 1.f;
            const float fade = t < now.hold ? 1.f : 1.f - smooth01((t - now.hold) / (now.out - now.hold));
            card.nowAlpha = kNowCardAlpha * fade;
            card.nowRule = smooth01((t - now.in) / s.beat);
        }
    }

    const CardWindow credits = creditsCardWindow(frame);
    if (inside(credits)) {
        card.credits = true;
        card.creditsAlpha = kCreditsCardAlpha;
        card.creditsRule = smooth01((t - credits.in) / spanOf(frame).beat);
    }
    return card;
}

// --- Where ------------------------------------------------------------------------------------------------------------

NowCardLayout nowCardLayout(std::string_view text, std::uint32_t width, std::uint32_t height, bool withYear,
                            bool itemPerLine) {
    NowCardLayout card;
    const auto h = static_cast<float>(height);
    card.bodyCap = std::max(4.f, h * kBodyPerHeight);
    card.yearCap = 2.f * card.bodyCap;
    const float rBody = 0.5f * text::strokeWidthPx(card.bodyCap);
    const float rYear = 0.5f * text::strokeWidthPx(card.yearCap);
    const float margin = std::max(4.f, std::round(h * kMarginPerHeight));
    // The left margin of the 16:9 frame beside the 4:3 picture; on an output too narrow for one, the frame.
    const Rect rect = contentRect(width, height);
    float available = static_cast<float>(rect.x) - 2.f * margin - 2.f * rBody;
    if (available < 10.f * card.bodyCap)
        available = static_cast<float>(width) - 2.f * margin - 2.f * rBody;

    const std::string dot = modern::kCardDot;
    std::vector<std::string> items = split(text, dot);
    std::string year;
    if (withYear && !items.empty()) {
        year = items.front();
        items.erase(items.begin());
    }
    const auto fits = [&](const std::string& line) { return text::strokeTextWidth(line, card.bodyCap) <= available; };
    std::vector<std::string> lines;
    std::string line;
    const auto flush = [&] {
        if (!line.empty())
            lines.push_back(line);
        line.clear();
    };
    for (const std::string& item : items) {
        if (!itemPerLine && !line.empty() && fits(line + dot + item)) {
            line += dot + item;
            continue;
        }
        flush();
        if (fits(item)) {
            line = item;
            continue;
        }
        // An item too wide for the margin wraps at spaces: into two balanced lines where it can (the break
        // that leaves the longer line shortest, the later of two about as short), else greedily; the next item
        // starts a line of its own.
        const std::vector<std::string> words = split(item, " ");
        const auto join = [&](std::size_t from, std::size_t to) {
            std::string out;
            for (std::size_t w = from; w < to; ++w)
                out += (w > from ? " " : "") + words[w];
            return out;
        };
        float best = 1e30f;
        std::size_t split2 = 0;
        for (std::size_t k = 1; k < words.size(); ++k) {
            const std::string first = join(0, k), second = join(k, words.size());
            const float longer = std::max(text::strokeTextWidth(first, card.bodyCap),
                                          text::strokeTextWidth(second, card.bodyCap));
            if (fits(first) && fits(second) && longer <= best + 0.25f * card.bodyCap) { // a tie: the later break
                best = longer;
                split2 = k;
            }
        }
        if (split2 > 0) {
            lines.push_back(join(0, split2));
            lines.push_back(join(split2, words.size()));
            continue;
        }
        for (const std::string& word : words) {
            if (line.empty())
                line = word;
            else if (fits(line + " " + word))
                line += " " + word;
            else {
                flush();
                line = word;
            }
        }
        flush();
    }
    flush();

    // From the bottom up: the last feat line's baseline, the feat lines, the year, the rule.
    const float unit = card.bodyCap / text::kStrokeCapUnits;
    const float last = h - margin - kDescender * text::kStrokeCapUnits * unit - rBody;
    const float pitch = kLinePitch * card.bodyCap;
    const auto count = static_cast<float>(lines.size());
    const float firstFeat = last - (count - 1.f) * pitch;
    float top = firstFeat - card.bodyCap - rBody;
    float widest = 0.f;
    const float left = margin;
    if (!year.empty()) {
        const float baseline = lines.empty() ? last : firstFeat - card.bodyCap - rBody - kYearGap * card.bodyCap - rYear;
        const text::StrokeRun run = text::layoutStrokes(year, card.yearCap, left + rYear, baseline);
        card.segments.insert(card.segments.end(), run.segments.begin(), run.segments.end());
        widest = std::max(widest, run.width + 2.f * rYear);
        top = baseline - card.yearCap - rYear;
        card.lines.push_back(year);
    }
    for (std::size_t i = 0; i < lines.size(); ++i) {
        const float baseline = firstFeat + static_cast<float>(i) * pitch;
        const text::StrokeRun run = text::layoutStrokes(lines[i], card.bodyCap, left + rBody, baseline);
        card.segments.insert(card.segments.end(), run.segments.begin(), run.segments.end());
        widest = std::max(widest, run.width + 2.f * rBody);
        card.lines.push_back(lines[i]);
    }
    card.textLeft = left;
    card.textRight = left + widest;
    card.ruleThickness = std::max(1.f, std::round(h / 1080.f));
    card.ruleY = std::floor(top - kRuleGap * card.bodyCap - card.ruleThickness);
    card.ruleX0 = left;
    card.ruleX1 = left + widest;

    const auto clampTo = [](float v, std::uint32_t hi) {
        return static_cast<int>(std::clamp(v, 0.f, static_cast<float>(hi)));
    };
    card.x0 = clampTo(std::floor(left - 1.f), width);
    card.x1 = clampTo(std::ceil(left + widest + 1.f), width);
    card.y0 = clampTo(card.ruleY - 1.f, height);
    card.y1 = clampTo(std::ceil(last + kDescender * text::kStrokeCapUnits * unit + rBody + 1.f), height);
    return card;
}

ThenCardLayout thenCardLayout(std::string_view text, std::uint32_t width, std::uint32_t height) {
    ThenCardLayout card;
    card.scale = std::max(1, static_cast<int>(height / 480u));
    card.codes = text::glyphCodes(text);
    const Rect rect = contentRect(width, height);
    card.rectX = rect.x, card.rectY = rect.y, card.rectWidth = rect.width, card.rectHeight = rect.height;
    const int cell = 8 * card.scale;
    card.originX = 2 * cell;
    card.originY = static_cast<int>(rect.height) - 2 * cell - cell;
    // The bulge moves a point by at most about 1 % of the picture's size: pad the card's box by 2 %.
    const int pad = static_cast<int>(std::ceil(0.02 * std::max(rect.width, rect.height))) + 2;
    const int cardWidth = cell * static_cast<int>(card.codes.size() + 1); // the cursor's cell too
    const auto clampX = [&](int v) { return std::clamp(v, static_cast<int>(rect.x), static_cast<int>(rect.x + rect.width)); };
    const auto clampY = [&](int v) { return std::clamp(v, static_cast<int>(rect.y), static_cast<int>(rect.y + rect.height)); };
    card.x0 = clampX(static_cast<int>(rect.x) + card.originX - pad);
    card.x1 = clampX(static_cast<int>(rect.x) + card.originX + cardWidth + pad);
    card.y0 = clampY(static_cast<int>(rect.y) + card.originY - pad);
    card.y1 = clampY(static_cast<int>(rect.y) + card.originY + cell + pad);
    return card;
}

// --- The pass -----------------------------------------------------------------------------------------------------------

EpochCards::EpochCards(gfx::Context& ctx, gfx::FrameRing& ring, NowCardSource nowCard)
    : ctx_(ctx), ring_(ring), source_(std::move(nowCard)),
      sdf_(ctx, gfx::shaderDir() / "text_card_sdf.comp.spv", kBindings, sizeof(SdfPush)),
      crt_(ctx, gfx::shaderDir() / "text_card_crt.comp.spv", kBindings, sizeof(CrtPush)) {}

EpochCards::~EpochCards() {
    // Frames in flight may still read the slots' buffers. Best effort, as in ~Renderer.
    try {
        ring_.waitAll(); // shutdown
    } catch (const std::exception&) {
    }
}

const std::string& EpochCards::nowText(Effect effect) {
    std::optional<std::string>& text = nowTexts_[static_cast<std::size_t>(effect)];
    if (!text) {
        const std::string scene = source_ ? source_(effect) : std::string();
        text = scene.empty() ? std::string(cueOf(effect).nowCard) : scene;
    }
    return *text;
}

void EpochCards::drawStrokes(VkCommandBuffer cmd, gfx::Image& output, const NowCardLayout& layout,
                             std::uint32_t first, float alpha, float rule, float clipX) {
    if (layout.x1 <= layout.x0 || layout.y1 <= layout.y0 || layout.segments.empty())
        return;
    const float head = layout.ruleX0 + rule * (layout.ruleX1 - layout.ruleX0);
    const SdfPush push{layout.x0,
                       layout.y0,
                       layout.x1 - layout.x0,
                       layout.y1 - layout.y0,
                       first,
                       static_cast<std::uint32_t>(layout.segments.size()),
                       alpha,
                       rule >= 1.f ? 1e9f : head,
                       std::max(2.f, layout.bodyCap),
                       clipX,
                       layout.ruleX0,
                       layout.ruleY,
                       head,
                       layout.ruleThickness};
    output.transition(cmd, VK_IMAGE_LAYOUT_GENERAL, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                      VK_ACCESS_2_SHADER_STORAGE_READ_BIT | VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT);
    const Slot& slot = slots_[ring_.slot()];
    const std::array<VkDescriptorImageInfo, 1> images = {
        VkDescriptorImageInfo{VK_NULL_HANDLE, output.view(), VK_IMAGE_LAYOUT_GENERAL}};
    const VkDescriptorBufferInfo segments{slot.segments.handle(), 0, VK_WHOLE_SIZE};
    sdf_.bind(cmd, ring_, images, {&segments, 1});
    sdf_.push(cmd, &push, sizeof push);
    sdf_.dispatch(cmd, static_cast<std::uint32_t>(push.boxWidth), static_cast<std::uint32_t>(push.boxHeight));
}

void EpochCards::record(VkCommandBuffer cmd, gfx::Image& output, const show::TimelineFrame& frame, bool crt) {
    if (!ring_.recording())
        throw std::logic_error("EpochCards: record outside FrameRing::begin()/submit()");
    const CardFrame card = cardFrame(frame);
    if (!card.then && !card.now && !card.credits)
        return;
    const VkExtent2D size = output.extent();
    Slot& slot = slots_[ring_.slot()];

    // The 2026 card and the credits layer: their strokes, one after the other in the slot's buffer.
    std::optional<NowCardLayout> now, credits;
    if (card.now)
        now = nowCardLayout(nowText(frame.effect), size.width, size.height);
    if (card.credits)
        credits = nowCardLayout(show::kCreditsLine, size.width, size.height, false, true);
    std::vector<GpuSegment> segments;
    for (const auto* layout : {&now, &credits})
        if (*layout)
            for (const text::StrokeSegment& s : (*layout)->segments)
                segments.push_back({s.ax, s.ay, s.bx, s.by, s.radius, 0.f, 0.f, 0.f});
    if (!segments.empty()) {
        reserve(ctx_, slot.segments, segments.size() * sizeof(GpuSegment), "epoch card strokes");
        std::memcpy(slot.segments.mapped(), segments.data(), segments.size() * sizeof(GpuSegment));
        slot.segments.flush();
    }
    const auto width = static_cast<float>(size.width);
    if (now)
        drawStrokes(cmd, output, *now, 0, card.nowAlpha, card.nowRule, card.nowTo * width);
    if (credits)
        drawStrokes(cmd, output, *credits, now ? static_cast<std::uint32_t>(now->segments.size()) : 0u,
                    card.creditsAlpha, card.creditsRule, width);

    if (card.then) {
        const ThenCardLayout layout = thenCardLayout(thenCardText(frame.effect), size.width, size.height);
        const auto typed = static_cast<std::uint32_t>(std::min(card.typed, layout.codes.size()));
        std::vector<std::uint32_t> words(kFontWords + layout.codes.size(), 0u);
        for (int g = 0; g < text::kGlyphCount; ++g) {
            const text::GlyphRows& rows = text::glyph(static_cast<char>(text::kFirstGlyph + g));
            for (int r = 0; r < 8; ++r)
                words[g * 2 + r / 4] |= std::uint32_t{rows[r]} << (8 * (r % 4));
        }
        for (std::size_t i = 0; i < layout.codes.size(); ++i)
            words[kFontWords + i] = layout.codes[i];
        reserve(ctx_, slot.glyphs, words.size() * sizeof(std::uint32_t), "epoch card glyphs");
        std::memcpy(slot.glyphs.mapped(), words.data(), words.size() * sizeof(std::uint32_t));
        slot.glyphs.flush();

        const CrtPush push{size.width,
                           size.height,
                           layout.originX,
                           layout.originY,
                           static_cast<std::uint32_t>(layout.scale),
                           typed,
                           card.cursor ? 1u : 0u,
                           crt ? 1u : 0u,
                           layout.x0,
                           layout.y0,
                           layout.x1 - layout.x0,
                           layout.y1 - layout.y0,
                           card.thenFrom * width,
                           0.f};
        if (push.boxWidth > 0 && push.boxHeight > 0) {
            output.transition(cmd, VK_IMAGE_LAYOUT_GENERAL, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                              VK_ACCESS_2_SHADER_STORAGE_READ_BIT | VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT);
            const std::array<VkDescriptorImageInfo, 1> images = {
                VkDescriptorImageInfo{VK_NULL_HANDLE, output.view(), VK_IMAGE_LAYOUT_GENERAL}};
            const VkDescriptorBufferInfo glyphs{slot.glyphs.handle(), 0, VK_WHOLE_SIZE};
            crt_.bind(cmd, ring_, images, {&glyphs, 1});
            crt_.push(cmd, &push, sizeof push);
            crt_.dispatch(cmd, static_cast<std::uint32_t>(push.boxWidth), static_cast<std::uint32_t>(push.boxHeight));
        }
    }
}

} // namespace evo::render
