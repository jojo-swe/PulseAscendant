#include "text/stroke_font.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdlib>
#include <cstddef>
#include <map>
#include <mutex>
#include <utility>

namespace evo::text {

namespace {

// The skeletons: polylines separated by '|', points "x,y" in grid units separated by spaces. Each follows
// the 8x8 glyph of the same character with columns (0,1) -> 0, 2 -> 1, 3 -> 2, 4 -> 3, (5,6) -> 4, rows as
// they are (the derivation test checks every point against the bitmap). Digits all span 0..4.
struct Skeleton {
    char32_t code;
    const char* strokes;
    bool fromBitmap = true;
};

// clang-format off
constexpr std::array kSkeletons = {
    Skeleton{U'A', "0,6 0,2 1,0 3,0 4,2 4,6 | 0,4 4,4"},
    Skeleton{U'B', "0,3 3,3 4,2 4,1 3,0 0,0 0,6 3,6 4,5 4,4 3,3"},
    Skeleton{U'C', "4,1 3,0 1,0 0,1 0,5 1,6 3,6 4,5"},
    Skeleton{U'D', "0,0 2.5,0 4,1.5 4,4.5 2.5,6 0,6 0,0"},
    Skeleton{U'E', "4,0 0,0 0,6 4,6 | 0,3 3,3"},
    Skeleton{U'F', "4,0 0,0 0,6 | 0,3 3,3"},
    Skeleton{U'G', "4,1 3,0 1,0 0,1 0,5 1,6 4,6 4,3 2,3"},
    Skeleton{U'H', "0,0 0,6 | 4,0 4,6 | 0,3 4,3"},
    Skeleton{U'I', "1,0 3,0 | 2,0 2,6 | 1,6 3,6"},
    Skeleton{U'J', "1,0 4,0 | 3,0 3,5 2,6 1,6 0,5 0,4"},
    Skeleton{U'K', "0,0 0,6 | 4,0 1,3 0,3 | 1,3 4,6"},
    Skeleton{U'L', "0,0 0,6 4,6"},
    Skeleton{U'M', "0,6 0,0 2,3 4,0 4,6"},
    Skeleton{U'N', "0,6 0,0 4,6 4,0"},
    Skeleton{U'O', "1,0 3,0 4,1 4,5 3,6 1,6 0,5 0,1 1,0"},
    Skeleton{U'P', "0,6 0,0 3,0 4,1 4,2 3,3 0,3"},
    Skeleton{U'Q', "4,4 4,1 3,0 1,0 0,1 0,5 1,6 2.5,6 | 2,4 4,6"},
    Skeleton{U'R', "0,6 0,0 3,0 4,1 4,2 3,3 0,3 | 2,3 4,6"},
    Skeleton{U'S', "4,1 3,0 1,0 0,1 0,2 1,3 3,3 4,4 4,5 3,6 1,6 0,5"},
    Skeleton{U'T', "0,0 4,0 | 2,0 2,6"},
    Skeleton{U'U', "0,0 0,5 1,6 3,6 4,5 4,0"},
    Skeleton{U'V', "0,0 0,3 2,6 4,3 4,0"},
    Skeleton{U'W', "0,0 0,6 2,3 4,6 4,0"},
    Skeleton{U'X', "0,0 0,1 4,5 4,6 | 4,0 4,1 0,5 0,6"},
    Skeleton{U'Y', "0,0 0,2 2,3.5 4,2 4,0 | 2,3.5 2,6"},
    Skeleton{U'Z', "0,0 4,0 4,1 0,5 0,6 4,6"},
    Skeleton{U'0', "1,0 3,0 4,1 4,5 3,6 1,6 0,5 0,1 1,0 | 3,2 1,4"},
    Skeleton{U'1', "1,1 2,0 2,6 | 1,6 3,6"},
    Skeleton{U'2', "0,1 1,0 3,0 4,1 4,2 3,3 1,3 0,4 0,6 4,6"},
    Skeleton{U'3', "0,1 1,0 3,0 4,1 4,2 3,3 4,4 4,5 3,6 1,6 0,5 | 1,3 3,3"},
    Skeleton{U'4', "3,6 3,0 0,4 4,4"},
    Skeleton{U'5', "4,0 0,0 0,2 3,2 4,3 4,5 3,6 1,6 0,5"},
    Skeleton{U'6', "4,0 2,0 0,2 0,5 1,6 3,6 4,5 4,4 3,3 0,3"},
    Skeleton{U'7', "0,0 4,0 4,1 1.5,4 1.5,6"},
    Skeleton{U'8', "1,3 0,2 0,1 1,0 3,0 4,1 4,2 3,3 1,3 0,4 0,5 1,6 3,6 4,5 4,4 3,3"},
    Skeleton{U'9', "4,3 1,3 0,2 0,1 1,0 3,0 4,1 4,4 2,6 1,6"},
    Skeleton{U'?', "0,1 1,0 3,0 4,1 4,2 2,3.5 2,4 | 2,6"},
    Skeleton{U',', "1,5.5 1,6.2 0.4,7.2"},
    Skeleton{U'.', "1.5,5.6"},
    Skeleton{U':', "1.5,2 | 1.5,5.5"},
    Skeleton{U'-', "0,3 3,3"},
    Skeleton{U'·', "1.5,3.5"},                              // the middle dot
    Skeleton{U'×', "0.8,2 3.2,5 | 3.2,2 0.8,5"},             // x: the bitmap's 'x' on the math axis
    Skeleton{U'→', "0,3.5 5,3.5 | 3,1.8 5,3.5 3,5.2", false}, // the arrow: drawn as "->" by the bitmap
};
// clang-format on

bool isDigit(char32_t c) {
    return c >= U'0' && c <= U'9';
}

// The 45-degree chamfers of the bitmap become curves: a corner that turns by less than this is replaced by a
// quadratic arc; sharper corners (E, V, the arrow's head) stay corners, rounded only by the stroke itself.
constexpr float kRoundTurn = 1.2f; // radians, about 69 degrees
constexpr float kRoundReach = 0.55f;
constexpr int kArcSteps = 4;

using Polyline = std::vector<StrokePoint>;

StrokePoint lerp(StrokePoint a, StrokePoint b, float t) {
    return {a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t};
}

float distance(StrokePoint a, StrokePoint b) {
    return std::hypot(b.x - a.x, b.y - a.y);
}

Polyline roundCorners(const Polyline& line) {
    if (line.size() < 3)
        return line;
    const bool closed = distance(line.front(), line.back()) < 1e-4f;
    // The vertices; a closed line drops its repeated last point and wraps.
    Polyline v(line.begin(), closed ? line.end() - 1 : line.end());
    const std::size_t n = v.size();
    Polyline out;
    for (std::size_t i = 0; i < n; ++i) {
        const bool end = !closed && (i == 0 || i == n - 1);
        if (end) {
            out.push_back(v[i]);
            continue;
        }
        const StrokePoint prev = v[(i + n - 1) % n], cur = v[i], next = v[(i + 1) % n];
        const float lin = distance(prev, cur), lout = distance(cur, next);
        const float dix = (cur.x - prev.x) / lin, diy = (cur.y - prev.y) / lin;
        const float dox = (next.x - cur.x) / lout, doy = (next.y - cur.y) / lout;
        const float turn = std::acos(std::clamp(dix * dox + diy * doy, -1.f, 1.f));
        if (turn < 1e-3f || turn > kRoundTurn) {
            out.push_back(cur);
            continue;
        }
        const float reach = std::min({kRoundReach, 0.45f * lin, 0.45f * lout});
        const StrokePoint p0{cur.x - dix * reach, cur.y - diy * reach};
        const StrokePoint p2{cur.x + dox * reach, cur.y + doy * reach};
        for (int s = 0; s <= kArcSteps; ++s) {
            const float t = static_cast<float>(s) / kArcSteps;
            out.push_back(lerp(lerp(p0, cur, t), lerp(cur, p2, t), t));
        }
    }
    if (closed)
        out.push_back(out.front());
    return out;
}

std::vector<Polyline> parse(const char* text) {
    std::vector<Polyline> lines(1);
    const char* p = text;
    while (*p != '\0') {
        if (*p == ' ') {
            ++p;
        } else if (*p == '|') {
            lines.emplace_back();
            ++p;
        } else {
            char* after = nullptr;
            const float x = std::strtof(p, &after);
            const float y = std::strtof(after + 1, &after); // past the comma
            lines.back().push_back({x, y});
            p = after;
        }
    }
    return lines;
}

std::vector<StrokeGlyph> buildFace() {
    std::vector<StrokeGlyph> face;
    for (const Skeleton& s : kSkeletons) {
        StrokeGlyph g;
        g.code = s.code;
        g.fromBitmap = s.fromBitmap;
        g.skeleton = parse(s.strokes);
        float lo = 1e9f, hi = -1e9f;
        for (const Polyline& line : g.skeleton) {
            g.strokes.push_back(roundCorners(line));
            for (const StrokePoint& point : line) {
                lo = std::min(lo, point.x);
                hi = std::max(hi, point.x);
            }
        }
        g.left = isDigit(s.code) ? 0.f : lo;
        g.right = isDigit(s.code) ? 4.f : hi;
        face.push_back(std::move(g));
    }
    return face;
}

const std::vector<StrokeGlyph>& face() {
    static const std::vector<StrokeGlyph> glyphs = buildFace();
    return glyphs;
}

// --- Kerning from the profiles ---------------------------------------------------------------------------------

constexpr int kProfileRows = 33; // y from -0.5 to 7.5 in quarter units
constexpr float kProfileTop = -0.5f, kProfileStep = 0.25f;
constexpr float kProfileRadius = 0.5f; // a typical stroke half-width in grid units
// The normal white between two upright stems at the tracking the cards are set with (strokeTrackingUnits at
// about 16 px), and how close a kerned pair may come at its nearest row, as a fraction of it.
constexpr float kNominalGap = 1.6f - 2.f * kProfileRadius;
constexpr float kClosestFraction = 0.35f;

struct Profile {
    std::array<float, kProfileRows> left, right; // ink extent per row; left > right where there is none
};

Profile profileOf(const StrokeGlyph& g) {
    Profile p;
    p.left.fill(1e9f);
    p.right.fill(-1e9f);
    const auto mark = [&](StrokePoint q) {
        for (int i = 0; i < kProfileRows; ++i) {
            const float dy = kProfileTop + kProfileStep * static_cast<float>(i) - q.y;
            if (std::abs(dy) > 0.5f * kProfileStep) // the centre line's rows; the round caps don't count
                continue;
            p.left[i] = std::min(p.left[i], q.x - kProfileRadius);
            p.right[i] = std::max(p.right[i], q.x + kProfileRadius);
        }
    };
    for (const Polyline& line : g.strokes) {
        mark(line.front());
        for (std::size_t k = 1; k < line.size(); ++k) {
            const int steps = std::max(1, static_cast<int>(std::ceil(distance(line[k - 1], line[k]) / 0.05f)));
            for (int s = 1; s <= steps; ++s)
                mark(lerp(line[k - 1], line[k], static_cast<float>(s) / static_cast<float>(steps)));
        }
    }
    return p;
}

float computeKerning(const StrokeGlyph& a, const StrokeGlyph& b) {
    const Profile pa = profileOf(a), pb = profileOf(b);
    double sum = 0;
    float least = 1e9f;
    int rows = 0;
    for (int i = 0; i < kProfileRows; ++i) {
        if (pa.right[i] < pa.left[i] || pb.right[i] < pb.left[i])
            continue;
        // The white beyond two stems' at this row: a's ink short of its box's right edge, b's of its left.
        const float excess = (a.right + kProfileRadius - pa.right[i]) + (pb.left[i] - (b.left - kProfileRadius));
        sum += excess;
        least = std::min(least, excess);
        ++rows;
    }
    if (rows == 0)
        return 0.f;
    float kern = -0.5f * static_cast<float>(sum / rows);
    kern = std::max(kern, -(least + (1.f - kClosestFraction) * kNominalGap));
    return std::clamp(kern, -kMaxKerningUnits, 0.f);
}

const StrokeGlyph& glyphOrQuestion(char32_t code) {
    const StrokeGlyph* g = strokeGlyph(code);
    return g != nullptr ? *g : *strokeGlyph(U'?');
}

} // namespace

const StrokeGlyph* strokeGlyph(char32_t code) {
    for (const StrokeGlyph& g : face())
        if (g.code == code)
            return &g;
    return nullptr;
}

std::vector<char32_t> strokeFaceCodes() {
    std::vector<char32_t> codes;
    for (const StrokeGlyph& g : face())
        codes.push_back(g.code);
    return codes;
}

std::u32string decodeUtf8(std::string_view utf8) {
    std::u32string out;
    for (std::size_t i = 0; i < utf8.size();) {
        const auto lead = static_cast<unsigned char>(utf8[i]);
        int length = lead < 0x80 ? 1 : (lead & 0xE0) == 0xC0 ? 2 : (lead & 0xF0) == 0xE0 ? 3 : (lead & 0xF8) == 0xF0 ? 4 : 0;
        if (length == 0 || i + length > utf8.size()) {
            out += U'?';
            ++i;
            continue;
        }
        char32_t code = length == 1 ? lead : lead & (0x7F >> length);
        bool ok = true;
        for (int k = 1; k < length; ++k) {
            const auto c = static_cast<unsigned char>(utf8[i + k]);
            ok = ok && (c & 0xC0) == 0x80;
            code = code << 6 | (c & 0x3F);
        }
        if (!ok) {
            out += U'?';
            ++i;
            continue;
        }
        out += code;
        i += length;
    }
    return out;
}

float strokeWidthPx(float capPx) {
    return 0.11f * capPx + 0.9f;
}

float strokeTrackingUnits(float capPx) {
    return 1.35f + 4.f / std::max(capPx, 1.f);
}

float strokeKerningUnits(char32_t left, char32_t right) {
    if (left == U' ' || right == U' ' || (isDigit(left) && isDigit(right)))
        return 0.f;
    static std::mutex mutex;
    static std::map<std::pair<char32_t, char32_t>, float> cache;
    const std::lock_guard lock(mutex);
    const auto key = std::make_pair(left, right);
    const auto found = cache.find(key);
    if (found != cache.end())
        return found->second;
    const float kern = computeKerning(glyphOrQuestion(left), glyphOrQuestion(right));
    cache.emplace(key, kern);
    return kern;
}

namespace {

// Walks the glyphs of a line, calling place(glyph, penUnits) for every glyph with ink; returns the run's
// width in grid units.
template <typename Place>
float walk(std::string_view utf8, float capPx, Place&& place) {
    const std::u32string codes = decodeUtf8(utf8);
    const float tracking = strokeTrackingUnits(capPx);
    float pen = 0.f, inkEnd = 0.f;
    for (std::size_t i = 0; i < codes.size(); ++i) {
        const char32_t code = codes[i];
        if (code == U' ') {
            pen += kStrokeSpaceUnits + tracking;
            continue;
        }
        if (i > 0)
            pen += strokeKerningUnits(codes[i - 1], code);
        const StrokeGlyph& g = glyphOrQuestion(code);
        place(g, pen);
        inkEnd = pen + (g.right - g.left);
        pen = inkEnd + tracking;
    }
    return inkEnd;
}

} // namespace

StrokeRun layoutStrokes(std::string_view utf8, float capPx, float x, float baselineY) {
    StrokeRun run;
    const float unit = capPx / kStrokeCapUnits;
    const float radius = 0.5f * strokeWidthPx(capPx);
    const float width = walk(utf8, capPx, [&](const StrokeGlyph& g, float pen) {
        const auto px = [&](StrokePoint p) {
            return std::pair{x + (pen + p.x - g.left) * unit, baselineY - (kStrokeCapUnits - p.y) * unit};
        };
        for (const Polyline& line : g.strokes) {
            if (line.size() == 1) {
                const auto [ax, ay] = px(line[0]);
                run.segments.push_back({ax, ay, ax, ay, radius});
            }
            for (std::size_t k = 1; k < line.size(); ++k) {
                const auto [ax, ay] = px(line[k - 1]);
                const auto [bx, by] = px(line[k]);
                run.segments.push_back({ax, ay, bx, by, radius});
            }
        }
    });
    run.width = width * unit;
    return run;
}

float strokeTextWidth(std::string_view utf8, float capPx) {
    return walk(utf8, capPx, [](const StrokeGlyph&, float) {}) * capPx / kStrokeCapUnits;
}

float strokeCoverage(std::span<const StrokeSegment> segments, float x, float y) {
    float nearest = 1e30f;
    for (const StrokeSegment& s : segments) {
        const float bax = s.bx - s.ax, bay = s.by - s.ay;
        const float pax = x - s.ax, pay = y - s.ay;
        const float len2 = bax * bax + bay * bay;
        const float h = len2 > 0.f ? std::clamp((pax * bax + pay * bay) / len2, 0.f, 1.f) : 0.f;
        const float d = std::hypot(pax - bax * h, pay - bay * h) - s.radius;
        nearest = std::min(nearest, d);
    }
    return std::clamp(0.5f - nearest, 0.f, 1.f);
}

} // namespace evo::text
