// FINALE's 1993 side: the 3x3 grid of every effect, each tile at a frame of its home chapter, round the
// EVOLUTION scroller. Everything here is a function of the chapter's fixed facts and the retro frame.

#include "retro/finale.hpp"

#include "retro/boot.hpp"
#include "retro/dots.hpp"
#include "retro/tunnel.hpp"

#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <string>

namespace evo::retro {

namespace {

// Chapter starts lie on tracked beats; converting them to track seconds rounds by far less than this.
constexpr double kChapterEdge = 1e-6;

std::uint32_t framesOf(double seconds, std::uint32_t rate) {
    return static_cast<std::uint32_t>(std::floor(std::max(seconds, 0.0) * rate));
}

// The home chapter's fixed facts (its frame at its start).
show::TimelineFrame homeFacts(show::Effect effect) {
    return show::sampleTimeline(show::chapterStart(finaleHomeChapter(effect)));
}

// The last whole retro frame of a tile's home chapter.
std::uint32_t homeLastFrame(show::Effect effect) {
    const std::uint32_t frames = framesOf(homeFacts(effect).chapterDuration, effectFor(effect).retroRate);
    return frames > 0 ? frames - 1 : 0;
}

constexpr std::uint32_t indexColour(std::uint8_t index) {
    return 0xFF000000u | index;
}

} // namespace

PixelRect finaleCell(int cell) {
    return {(cell % 3) * (kFinaleCellWidth + 1), (cell / 3) * (kFinaleCellHeight + 1), kFinaleCellWidth,
            kFinaleCellHeight};
}

int finaleTileCell(std::size_t tile) {
    const int cell = static_cast<int>(tile);
    return cell < kFinaleCentre ? cell : cell + 1;
}

std::uint32_t finaleElapsed(const show::TimelineFrame& frame, std::uint32_t retroFrame) {
    if (frame.effect != show::Effect::Portal)
        return retroFrame;
    // PORTAL follows FINALE: its frame 0 is the frame after FINALE's last.
    const int finale = finaleHomeChapter(show::Effect::Finale);
    const double duration = show::chapterStart(finale + 1) - show::chapterStart(finale);
    return framesOf(duration - kChapterEdge, kFinaleRetroRate) + 1 + retroFrame;
}

int finaleHomeChapter(show::Effect effect) {
    for (std::size_t chapter = 0; chapter < show::cues.size(); ++chapter)
        if (show::cues[chapter].effect == effect)
            return static_cast<int>(chapter);
    throw std::invalid_argument("no chapter shows this effect");
}

show::TimelineFrame finaleHomeFrame(show::Effect effect, std::uint32_t homeRetroFrame) {
    const double start = show::chapterStart(finaleHomeChapter(effect));
    const double duration = homeFacts(effect).chapterDuration;
    const double local = (homeRetroFrame + 0.5) / effectFor(effect).retroRate;
    return show::sampleTimeline(start + std::min(local, duration - kChapterEdge));
}

std::uint32_t finaleHomeStart(show::Effect effect) {
    const show::TimelineFrame home = homeFacts(effect);
    const std::uint32_t rate = effectFor(effect).retroRate;
    switch (effect) {
    case show::Effect::Flyby: // past the ship's right side
        return framesOf(0.6 * home.chapterDuration, rate);
    case show::Effect::Dots: { // the last 0.2 s of the morph into the year, which then holds
        const std::uint32_t arrival = dotsSchedule(home)[2].arrival;
        return arrival > 7 ? arrival - 7 : 0;
    }
    case show::Effect::Plasma:
        return framesOf(home.accentSeconds, rate);
    case show::Effect::Tunnel: // the tube, once its front has swept in
        return tunnelTubeFrame(home) + framesOf(kTunnelTubeSweepSeconds, rate) + 1;
    case show::Effect::Glenz:
        return framesOf(home.accentSeconds, rate);
    case show::Effect::Lens: // the lens high at the right end of its run (rotozoomView), over the logo
        return framesOf(kFinaleLensStartSeconds, rate);
    case show::Effect::Raytrace: // mid-orbit
        return framesOf(0.5 * home.chapterDuration, rate);
    case show::Effect::City:
        return framesOf(0.4 * home.chapterDuration, rate);
    default:
        throw std::invalid_argument("not a FINALE tile");
    }
}

EffectFrame finaleTileSource(show::Effect effect, std::uint32_t elapsed) {
    const std::uint32_t start = finaleHomeStart(effect);
    const std::uint32_t span = homeLastFrame(effect) - start + 1;
    // n FINALE frames are floor(n * rate / 35) frames at the tile's rate, round the rest of its chapter.
    const std::uint32_t home = start + (elapsed * effectFor(effect).retroRate / kFinaleRetroRate) % span;
    return {effect, finaleHomeFrame(effect, home), home};
}

std::vector<text::Item> finaleScroller(std::uint32_t elapsed) {
    const PixelRect cell = finaleCell(kFinaleCentre);
    const std::string text = kFinaleScrollText;
    const double t = static_cast<double>(elapsed) / kFinaleRetroRate;
    const double loop = 8.0 * static_cast<double>(text.size());
    const double scrolled = std::fmod(kFinaleScrollSpeed * t, loop);
    std::vector<text::Item> items;
    // Two copies of the text one loop apart cover the cell however far it has scrolled.
    for (int copy = 0; copy < 3; ++copy)
        for (std::size_t i = 0; i < text.size(); ++i) {
            if (text[i] == ' ')
                continue;
            const double x = std::round(cell.x + 2 - scrolled + loop * copy + 8.0 * static_cast<double>(i));
            if (x <= cell.x - 8 || x >= cell.x + cell.width)
                continue;
            const double wave = std::sin((x - cell.x + 2.0 * kFinaleScrollSpeed * t) / 30.0);
            const double y = std::round(cell.y + (cell.height - 8) / 2.0 + 14.0 * wave);
            const auto shade = static_cast<std::uint8_t>(
                kFinaleRamp + (static_cast<std::uint32_t>(i) + elapsed / 3) % kFinaleRampSize);
            items.push_back({static_cast<float>(x), static_cast<float>(y), 1, indexColour(shade),
                             std::string(1, text[i])});
        }
    return items;
}

std::vector<GridLayer> finaleGrid(const show::TimelineFrame& frame, std::uint32_t retroFrame) {
    const std::uint32_t elapsed = finaleElapsed(frame, retroFrame);
    std::vector<GridLayer> layers;
    for (std::size_t tile = 0; tile < kFinaleTiles.size(); ++tile)
        layers.push_back(
            {finaleTileSource(kFinaleTiles[tile], elapsed), kFinaleSource, finaleCell(finaleTileCell(tile))});
    const EffectFrame own{frame.effect, frame, retroFrame};
    layers.push_back({own, finaleCell(kFinaleCentre), finaleCell(kFinaleCentre)});
    const PixelRect card = cardRect(frame, retroFrame, kFinaleRetroRate);
    if (card.width > 0)
        layers.push_back({own, card, card});
    return layers;
}

void finalePalette(Palette& palette, const show::TimelineFrame&, std::uint32_t) {
    palette.fill(rgba(0, 0, 0));
    palette[kFinaleWhite] = rgba(255, 255, 255);
    // White hot to deep amber and back: the scroller's colour cycle.
    constexpr std::uint8_t kRamp[kFinaleRampSize][3] = {{255, 255, 255}, {255, 240, 205}, {255, 218, 150},
                                                        {248, 188, 96},  {236, 150, 58},  {248, 188, 96},
                                                        {255, 218, 150}, {255, 240, 205}};
    for (int i = 0; i < kFinaleRampSize; ++i)
        palette[kFinaleRamp + i] = rgba(kRamp[i][0], kRamp[i][1], kRamp[i][2]);
}

void finaleScene(RetroScene& scene, const show::TimelineFrame& frame, std::uint32_t retroFrame) {
    cardCursor(scene, frame, retroFrame, kFinaleRetroRate, kFinaleWhite);
}

std::vector<text::Item> finaleText(const show::TimelineFrame& frame, std::uint32_t retroFrame) {
    std::vector<text::Item> items = finaleScroller(finaleElapsed(frame, retroFrame));
    const std::vector<text::Item> card = cardText(frame, retroFrame, kFinaleRetroRate, kFinaleWhite);
    items.insert(items.end(), card.begin(), card.end());
    return items;
}

} // namespace evo::retro
