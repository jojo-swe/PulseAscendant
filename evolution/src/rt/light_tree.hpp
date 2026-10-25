#pragma once

// The light tree (step 5, Task 12): next-event estimation over up to 65,536 emissive sphere lights (a night
// city's lamps and windows) picks one by walking down a binary tree of their bounds and powers, choosing a
// child with probability proportional to its importance at the shading point, instead of looking at every
// light (Decision 14: deterministic, no temporal reuse, unlike ReSTIR).
//
// The tree is built on the CPU from the frame's lights (binary, split along the longest axis of the light
// centres at the power-weighted median) and refit for per-frame powers (blinking) in O(n); the path tracer
// uploads `nodes` and `lights` (bindings EVO_BINDING_TREE_NODES and EVO_BINDING_TREE_LIGHTS) and walks it in
// shaders/rt/light_tree.glsl. The functions below the build are the CPU reference of that walk, bit for bit
// in its order of operations (tests hold the shader to them).
//
// A light's probability is reproducible from the light alone: TreeLight::trail holds the child choices from
// the root to its leaf (bit l the choice at depth l, 1 = the second child), depth their count, so the pdf of
// the light a BSDF ray happens to hit is the product of the same choice probabilities pickTreeLight()
// multiplied, evaluated with the same function at the same shading point (Review Focus 2).

#include "rt/gpu_types.hpp"

#include <array>
#include <cstdint>
#include <span>
#include <vector>

namespace evo::rt {

using Float3 = std::array<float, 3>;

inline constexpr std::uint32_t kMaxTreeLights = 65536;  // Decision 14
inline constexpr std::uint32_t kTreeLeaf = 0x80000000u; // LightTreeNode::child of a leaf: kTreeLeaf | light
inline constexpr std::uint32_t kMaxTreeDepth = 32;      // the bits of TreeLight::trail

// nodes: the root at 0; an inner node's children at child and child + 1 (after their parent), a leaf's child
// kTreeLeaf | its light. Bounds: the union of the lights' spheres (centre +- radius); power: the sum of the
// lights' treeLightPower. lights: in the order given to buildLightTree (light k stays k), with trail and
// depth filled in and emission = the built emission times the last refit's scale. built: the emissions as
// built (3 per light), what refitLightTree scales.
struct LightTree {
    std::vector<LightTreeNode> nodes;
    std::vector<TreeLight> lights;
    std::vector<float> built;
};

// What the tree weighs a sphere light by: luminance(emission) * pi * radius^2 (the radiance times the area it
// shows; a rect light's lightPower is its luminance times its area, so the two compare).
float treeLightPower(const TreeLight& light);

// Builds the tree over `lights` (their trail, depth and pads are ignored): deterministic, a pure function of
// the input. Each node is split along the longest axis of its lights' centres at the power-weighted median
// (exact for 16 lights or fewer, else to 1/32 of the axis), by count where the centres coincide or the
// powers are all 0; no leaf deeper than kMaxTreeDepth. An empty span gives an empty tree. Throws
// std::invalid_argument for more than kMaxTreeLights lights, a radius that is not finite and > 0, a centre
// that is not finite, or an emission that is not finite and >= 0.
LightTree buildLightTree(std::span<const TreeLight> lights);

// This frame's powers (blinking), O(n): light k's emission becomes its built emission times scale[k], and
// every node's power the sum of its lights' (as buildLightTree computes them: refit with all scales 1 gives
// the built tree bit for bit). Bounds and topology are unchanged. Throws std::invalid_argument unless there
// is one scale per light, each finite and >= 0.
void refitLightTree(LightTree& tree, std::span<const float> scale);

// --- The CPU reference of shaders/rt/light_tree.glsl
// ---------------------------------------------------------

// A node's importance at shading point p with normal n: power * cosBound / max(|centre - p|^2, (diag / 2)^2),
// where cosBound is 1 on a transmissive surface or with p inside the bounds, else the largest of
// max(dot(n, normalize(corner - p)), 0) over the 8 corners (0: the whole box is below the surface; cos is
// linear over the box, so no light of it can reach p). 0 when not finite.
float treeNodeImportance(const LightTreeNode& node, Float3 p, Float3 n, bool transmissive);

// The uniform number in [0, 1) with which the walk chooses at depth `level`, from one random word per pick:
// the top 24 bits of pcgHash(random + level * 0x9E3779B9).
float treeRandom(std::uint32_t random, std::uint32_t level);

// Picks a light: from the root, the first child with probability I0 / (I0 + I1) (treeNodeImportance), the
// second with I1 / (I0 + I1), until a leaf; pdf is the product of those probabilities. Where both children
// have importance 0 (both boxes below the surface, though their parent's box reaches above it: its corners
// are not theirs), each gets 1/2: no light below such a node can reach p, so this costs nothing over giving
// up, and the pdf stays a distribution (it sums to 1 over the lights). False only for an empty tree. With
// `uniform` (the path tracer's fixed light selection, for comparisons) light = floor(treeRandom(random, 0) *
// count), pdf 1 / count.
bool pickTreeLight(const LightTree& tree, std::uint32_t random, Float3 p, Float3 n, bool transmissive,
                   bool uniform, std::uint32_t& light, float& pdf);

// The probability that pickTreeLight at (p, n, transmissive) returns `light`: the same choice probabilities,
// walked along its trail.
float treeLightPdf(const LightTree& tree, std::uint32_t light, Float3 p, Float3 n, bool transmissive,
                   bool uniform);

} // namespace evo::rt
