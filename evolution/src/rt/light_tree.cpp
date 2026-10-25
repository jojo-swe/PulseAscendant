#include "rt/light_tree.hpp"

#include "rt/random.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <string>

namespace evo::rt {

namespace {

constexpr float kPi = 3.14159265358979f;
// Up to this many lights a node is split at the exact power-weighted median (sorted); above it, at the best
// of kBins - 1 boundaries of equal width along the axis.
constexpr std::uint32_t kExactSplit = 16, kBins = 32;

float luminance(const float e[3]) {
    return 0.2126f * e[0] + 0.7152f * e[1] + 0.0722f * e[2];
}

bool finite3(const float v[3]) {
    return std::isfinite(v[0]) && std::isfinite(v[1]) && std::isfinite(v[2]);
}

void checkLight(const TreeLight& l, std::size_t k) {
    const char* wrong = nullptr;
    if (!finite3(l.center))
        wrong = "a centre that is not finite";
    else if (!(l.radius > 0.f) || !std::isfinite(l.radius))
        wrong = "a radius that is not finite and > 0";
    else if (!(l.emission[0] >= 0.f && l.emission[1] >= 0.f && l.emission[2] >= 0.f) || !finite3(l.emission))
        wrong = "an emission that is not finite and >= 0";
    if (wrong)
        throw std::invalid_argument("buildLightTree: light " + std::to_string(k) + " has " + wrong);
}

// Bounds and powers, from the leaves up (children come after their parents, so in reverse order every
// node's children are done before it).
void computeNodes(LightTree& t) {
    for (std::size_t i = t.nodes.size(); i-- > 0;) {
        LightTreeNode& n = t.nodes[i];
        if (n.child & kTreeLeaf) {
            const TreeLight& l = t.lights[n.child & ~kTreeLeaf];
            for (int a = 0; a < 3; ++a) {
                n.boundsMin[a] = l.center[a] - l.radius;
                n.boundsMax[a] = l.center[a] + l.radius;
            }
            n.power = treeLightPower(l);
            continue;
        }
        const LightTreeNode &a = t.nodes[n.child], &b = t.nodes[n.child + 1];
        for (int c = 0; c < 3; ++c) {
            n.boundsMin[c] = std::min(a.boundsMin[c], b.boundsMin[c]);
            n.boundsMax[c] = std::max(a.boundsMax[c], b.boundsMax[c]);
        }
        n.power = a.power + b.power;
    }
}

// A light as the build sees it, compact: the build reorders these, not the lights.
struct Item {
    float centre[3];
    float power;
    std::uint32_t light;
};

class Builder {
public:
    explicit Builder(const std::vector<TreeLight>& lights)
        : items_(lights.size()), scratch_(lights.size()), bins_(lights.size()) {
        for (std::size_t k = 0; k < lights.size(); ++k)
            items_[k] = {{lights[k].center[0], lights[k].center[1], lights[k].center[2]},
                         treeLightPower(lights[k]),
                         static_cast<std::uint32_t>(k)};
    }

    std::uint32_t light(std::uint32_t i) const { return items_[i].light; }

    // Reorders items [begin, end) into the first child's lights and the second's; returns the first child's
    // count, in [1, n - 1], with neither child holding more lights than fit below it within kMaxTreeDepth
    // levels.
    std::uint32_t split(std::uint32_t begin, std::uint32_t end, std::uint32_t depth) {
        const std::uint32_t n = end - begin;
        // A child at depth + 1 holds at most 2^(kMaxTreeDepth - depth - 1) lights.
        const std::uint32_t below = kMaxTreeDepth - depth - 1;
        const std::uint32_t cap = below >= 31 ? std::numeric_limits<std::uint32_t>::max() : 1u << below;
        const std::uint32_t lo = n > cap ? n - cap : 1u, hi = std::min(n - 1, cap);

        float cmin[3], cmax[3];
        for (int a = 0; a < 3; ++a) {
            cmin[a] = std::numeric_limits<float>::infinity();
            cmax[a] = -std::numeric_limits<float>::infinity();
        }
        for (std::uint32_t i = begin; i < end; ++i)
            for (int a = 0; a < 3; ++a) {
                cmin[a] = std::min(cmin[a], items_[i].centre[a]);
                cmax[a] = std::max(cmax[a], items_[i].centre[a]);
            }
        int axis = 0;
        for (int a = 1; a < 3; ++a)
            if (cmax[a] - cmin[a] > cmax[axis] - cmin[axis])
                axis = a;
        const float lowest = cmin[axis], extent = cmax[axis] - cmin[axis];

        auto byKey = [axis](const Item& x, const Item& y) {
            return x.centre[axis] < y.centre[axis] || (x.centre[axis] == y.centre[axis] && x.light < y.light);
        };
        std::uint32_t s = n / 2;
        if (extent > 0.f && std::isfinite(extent)) {
            if (n <= kExactSplit) {
                std::sort(items_.begin() + begin, items_.begin() + end, byKey);
                s = weightedMedian(begin, end);
            } else {
                s = binnedSplit(begin, end, axis, lowest, extent);
            }
        }
        if (s < lo ||
            s > hi) { // too deep for the depth left: the count median instead (sorted, deterministic)
            std::sort(items_.begin() + begin, items_.begin() + end, byKey);
            s = std::clamp(n / 2, lo, hi);
        }
        return s;
    }

private:
    // The first s in [1, n - 1] whose prefix power is closest to half the total (by count when all are 0).
    std::uint32_t weightedMedian(std::uint32_t begin, std::uint32_t end) const {
        const std::uint32_t n = end - begin;
        double total = 0;
        for (std::uint32_t i = begin; i < end; ++i)
            total += items_[i].power;
        if (!(total > 0))
            return n / 2;
        double prefix = 0, best = std::numeric_limits<double>::infinity();
        std::uint32_t s = 1;
        for (std::uint32_t i = 1; i < n; ++i) {
            prefix += items_[begin + i - 1].power;
            const double gap = std::abs(prefix - 0.5 * total);
            if (gap < best) {
                best = gap;
                s = i;
            }
        }
        return s;
    }

    std::uint32_t binnedSplit(std::uint32_t begin, std::uint32_t end, int axis, float lowest, float extent) {
        const std::uint32_t n = end - begin;
        double binPower[kBins] = {};
        std::uint32_t binCount[kBins] = {};
        double total = 0;
        const float perUnit = float(kBins) / extent;
        for (std::uint32_t i = begin; i < end; ++i) {
            const float x = (items_[i].centre[axis] - lowest) * perUnit;
            const std::uint32_t b = std::min(kBins - 1, static_cast<std::uint32_t>(std::max(x, 0.f)));
            bins_[i] = static_cast<std::uint8_t>(b);
            binPower[b] += items_[i].power;
            binCount[b] += 1;
            total += items_[i].power;
        }
        // The boundary (the first child: the bins below it) closest to half the power, or half the count.
        double prefix = 0, best = std::numeric_limits<double>::infinity();
        std::uint32_t count = 0, boundary = 0, s = n / 2;
        for (std::uint32_t b = 1; b < kBins; ++b) {
            prefix += binPower[b - 1];
            count += binCount[b - 1];
            if (count == 0 || count == n)
                continue;
            const double gap = total > 0 ? std::abs(prefix - 0.5 * total) : std::abs(double(count) - 0.5 * n);
            if (gap < best) {
                best = gap;
                boundary = b;
                s = count;
            }
        }
        if (boundary == 0)
            return n / 2; // cannot happen: the extremes fall in bins 0 and kBins - 1
        // A stable partition through the scratch buffer.
        std::uint32_t left = begin, right = begin + s;
        for (std::uint32_t i = begin; i < end; ++i)
            scratch_[bins_[i] < boundary ? left++ : right++] = items_[i];
        std::copy(scratch_.begin() + begin, scratch_.begin() + end, items_.begin() + begin);
        return s;
    }

    std::vector<Item> items_, scratch_;
    std::vector<std::uint8_t> bins_;
};

} // namespace

float treeLightPower(const TreeLight& light) {
    return luminance(light.emission) * (kPi * light.radius * light.radius);
}

LightTree buildLightTree(std::span<const TreeLight> lights) {
    if (lights.size() > kMaxTreeLights)
        throw std::invalid_argument("buildLightTree: " + std::to_string(lights.size()) +
                                    " lights, more than " + std::to_string(kMaxTreeLights));
    for (std::size_t k = 0; k < lights.size(); ++k)
        checkLight(lights[k], k);
    LightTree t;
    t.lights.assign(lights.begin(), lights.end());
    t.built.reserve(3 * lights.size());
    for (TreeLight& l : t.lights) {
        l.trail = l.depth = l.pad0 = l.pad1 = 0;
        t.built.insert(t.built.end(), {l.emission[0], l.emission[1], l.emission[2]});
    }
    const auto n = static_cast<std::uint32_t>(lights.size());
    if (n == 0)
        return t;

    Builder builder(t.lights);
    struct Work {
        std::uint32_t node, begin, end, depth, trail;
    };
    std::vector<Work> stack{{0, 0, n, 0, 0}};
    t.nodes.reserve(2 * std::size_t(n) - 1);
    t.nodes.push_back({});
    while (!stack.empty()) {
        const Work w = stack.back();
        stack.pop_back();
        if (w.end - w.begin == 1) {
            const std::uint32_t k = builder.light(w.begin);
            t.nodes[w.node].child = kTreeLeaf | k;
            t.lights[k].trail = w.trail;
            t.lights[k].depth = w.depth;
            continue;
        }
        const std::uint32_t s = builder.split(w.begin, w.end, w.depth);
        const auto child = static_cast<std::uint32_t>(t.nodes.size());
        t.nodes.push_back({});
        t.nodes.push_back({});
        t.nodes[w.node].child = child;
        stack.push_back({child + 1, w.begin + s, w.end, w.depth + 1, w.trail | (1u << w.depth)});
        stack.push_back({child, w.begin, w.begin + s, w.depth + 1, w.trail});
    }
    computeNodes(t);
    return t;
}

void refitLightTree(LightTree& tree, std::span<const float> scale) {
    if (scale.size() != tree.lights.size())
        throw std::invalid_argument("refitLightTree: " + std::to_string(scale.size()) + " scales for " +
                                    std::to_string(tree.lights.size()) + " lights");
    for (std::size_t k = 0; k < scale.size(); ++k)
        if (!(scale[k] >= 0.f) || !std::isfinite(scale[k]))
            throw std::invalid_argument("refitLightTree: scale " + std::to_string(k) +
                                        " is not finite and >= 0");
    for (std::size_t k = 0; k < scale.size(); ++k)
        for (int c = 0; c < 3; ++c)
            tree.lights[k].emission[c] = tree.built[3 * k + c] * scale[k];
    computeNodes(tree);
}

// --- The CPU reference of shaders/rt/light_tree.glsl
// ---------------------------------------------------------

float treeNodeImportance(const LightTreeNode& node, Float3 p, Float3 n, bool transmissive) {
    float cosBound = 1.f;
    const bool inside = p[0] >= node.boundsMin[0] && p[0] <= node.boundsMax[0] && p[1] >= node.boundsMin[1] &&
                        p[1] <= node.boundsMax[1] && p[2] >= node.boundsMin[2] && p[2] <= node.boundsMax[2];
    if (!transmissive && !inside) {
        cosBound = 0.f;
        for (int i = 0; i < 8; ++i) {
            const float v[3] = {((i & 1) ? node.boundsMax[0] : node.boundsMin[0]) - p[0],
                                ((i & 2) ? node.boundsMax[1] : node.boundsMin[1]) - p[1],
                                ((i & 4) ? node.boundsMax[2] : node.boundsMin[2]) - p[2]};
            const float len2 = v[0] * v[0] + v[1] * v[1] + v[2] * v[2];
            if (len2 > 0.f)
                cosBound = std::max(cosBound, (n[0] * v[0] + n[1] * v[1] + n[2] * v[2]) / std::sqrt(len2));
        }
        if (!(cosBound > 0.f))
            return 0.f;
    }
    float d2 = 0.f, r2 = 0.f;
    for (int a = 0; a < 3; ++a) {
        const float centre = 0.5f * (node.boundsMin[a] + node.boundsMax[a]) - p[a];
        const float half = 0.5f * (node.boundsMax[a] - node.boundsMin[a]);
        d2 += centre * centre;
        r2 += half * half;
    }
    const float w = node.power * std::min(cosBound, 1.f) / std::max(d2, r2);
    return w > 0.f && std::isfinite(w) ? w : 0.f;
}

float treeRandom(std::uint32_t random, std::uint32_t level) {
    return float(pcgHash(random + level * 0x9E3779B9u) >> 8u) * (1.f / 16777216.f);
}

bool pickTreeLight(const LightTree& tree, std::uint32_t random, Float3 p, Float3 n, bool transmissive,
                   bool uniform, std::uint32_t& light, float& pdf) {
    light = 0;
    pdf = 0.f;
    if (tree.nodes.empty())
        return false;
    if (uniform) {
        const auto count = static_cast<std::uint32_t>(tree.lights.size());
        light = std::min(static_cast<std::uint32_t>(treeRandom(random, 0) * float(count)), count - 1);
        pdf = 1.f / float(count);
        return true;
    }
    std::uint32_t node = 0;
    float product = 1.f;
    for (std::uint32_t level = 0; !(tree.nodes[node].child & kTreeLeaf); ++level) {
        const std::uint32_t child = tree.nodes[node].child;
        const float i0 = treeNodeImportance(tree.nodes[child], p, n, transmissive);
        const float i1 = treeNodeImportance(tree.nodes[child + 1], p, n, transmissive);
        const float total = i0 + i1;
        const bool weighed = total > 0.f && std::isfinite(total);
        const float p0 = weighed ? i0 / total : 0.5f;
        if (treeRandom(random, level) < p0) {
            node = child;
            product *= p0;
        } else {
            node = child + 1;
            product *= weighed ? i1 / total : 0.5f;
        }
    }
    light = tree.nodes[node].child & ~kTreeLeaf;
    pdf = product;
    return pdf > 0.f;
}

float treeLightPdf(const LightTree& tree, std::uint32_t light, Float3 p, Float3 n, bool transmissive,
                   bool uniform) {
    if (light >= tree.lights.size() || tree.nodes.empty())
        return 0.f;
    if (uniform)
        return 1.f / float(tree.lights.size());
    const TreeLight& l = tree.lights[light];
    std::uint32_t node = 0;
    float product = 1.f;
    for (std::uint32_t level = 0; level < l.depth; ++level) {
        const std::uint32_t child = tree.nodes[node].child;
        const float i0 = treeNodeImportance(tree.nodes[child], p, n, transmissive);
        const float i1 = treeNodeImportance(tree.nodes[child + 1], p, n, transmissive);
        const float total = i0 + i1;
        const bool weighed = total > 0.f && std::isfinite(total);
        const std::uint32_t bit = (l.trail >> level) & 1u;
        product *= weighed ? (bit ? i1 : i0) / total : 0.5f;
        node = child + bit;
    }
    return product;
}

} // namespace evo::rt
