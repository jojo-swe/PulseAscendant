#ifndef EVO_RT_LIGHT_TREE_GLSL
#define EVO_RT_LIGHT_TREE_GLSL

// The light tree (step 5, Task 12): next-event estimation's walk down a binary tree of emissive sphere
// lights, choosing a child with probability proportional to its importance at the shading point, and the
// probability of any one light, walked again along its trail (the child bits from the root). The CPU builds
// the tree (src/rt/light_tree.cpp, whose reference functions this file mirrors in its order of operations).
// lights.glsl gives the tree one top-level entry (a RectLight of kind Tree) and the raygen samples the
// chosen sphere through a uniform cone.
//
// Needs types.glsl and the includer's `treeNodes[]` and `treeLights[]` (common.glsl's bindings, or a test
// probe's); the light count is passed in (frame.treeLightCount in the path tracer).

#include "../common/finite.glsl"
#include "random.glsl" // pcgHash

const uint kTreeLeaf = 0x80000000u;
const uint kMaxTreeDepth = 32u;

// A node's importance at shading point p with normal n: power * cosBound / max(|centre - p|^2, (diag/2)^2),
// cosBound the largest max(dot(n, normalize(corner - p)), 0) over the 8 corners (1 on a transmissive surface
// or inside the box; 0 when the whole box is below the surface: cos is linear over it). 0 when not finite.
float treeNodeImportance(LightTreeNode node, vec3 p, vec3 n, bool transmissive) {
    float cosBound = 1.0;
    bool inside = all(greaterThanEqual(p, node.boundsMin)) && all(lessThanEqual(p, node.boundsMax));
    if (!transmissive && !inside) {
        cosBound = 0.0;
        for (uint i = 0u; i < 8u; ++i) {
            vec3 corner = vec3((i & 1u) != 0u ? node.boundsMax.x : node.boundsMin.x,
                               (i & 2u) != 0u ? node.boundsMax.y : node.boundsMin.y,
                               (i & 4u) != 0u ? node.boundsMax.z : node.boundsMin.z);
            vec3 v = corner - p;
            float len2 = dot(v, v);
            if (len2 > 0.0) {
                cosBound = max(cosBound, dot(n, v) / sqrt(len2));
            }
        }
        if (!(cosBound > 0.0)) {
            return 0.0;
        }
    }
    vec3 centre = 0.5 * (node.boundsMin + node.boundsMax) - p;
    vec3 halfSize = 0.5 * (node.boundsMax - node.boundsMin);
    float w = node.power * min(cosBound, 1.0) / max(dot(centre, centre), dot(halfSize, halfSize));
    return w > 0.0 && isFinite(w) ? w : 0.0;
}

// The uniform number in [0, 1) the walk chooses with at depth `level`, from one random word per pick.
float treeRandom(uint random, uint level) {
    return float(pcgHash(random + level * 0x9E3779B9u) >> 8u) * (1.0 / 16777216.0);
}

// Picks one of `count` lights at the shading point: the first child with probability I0 / (I0 + I1), the
// second with I1 / (I0 + I1), each level with its own number; 1/2 each where both are 0 (no light below can
// reach p). pdf: the product of the choices. With uniformSelection (fixed light selection, comparisons):
// uniform over the lights. False only without lights.
bool pickTreeLight(uint random, uint count, vec3 p, vec3 n, bool transmissive, bool uniformSelection,
                   out uint light, out float pdf) {
    light = 0u;
    pdf = 0.0;
    if (count == 0u) {
        return false;
    }
    if (uniformSelection) {
        light = min(uint(treeRandom(random, 0u) * float(count)), count - 1u);
        pdf = 1.0 / float(count);
        return true;
    }
    // The node's child word is read once per level, at the end (lavapipe, Mesa 25.2, crashed: a null read in the
    // JIT code of the ray tracing pipeline, on the form that read it in the loop's condition).
    uint nodeChild = treeNodes[0].child;
    float product = 1.0;
    for (uint level = 0u; level < kMaxTreeDepth; ++level) {
        if ((nodeChild & kTreeLeaf) != 0u) {
            break;
        }
        float i0 = treeNodeImportance(treeNodes[nodeChild], p, n, transmissive);
        float i1 = treeNodeImportance(treeNodes[nodeChild + 1u], p, n, transmissive);
        float total = i0 + i1;
        bool weighed = total > 0.0 && isFinite(total);
        float p0 = weighed ? i0 / total : 0.5;
        uint bit = treeRandom(random, level) < p0 ? 0u : 1u;
        product *= bit == 0u ? p0 : (weighed ? i1 / total : 0.5);
        nodeChild = treeNodes[nodeChild + bit].child;
    }
    if ((nodeChild & kTreeLeaf) == 0u) {
        return false; // deeper than any tree the CPU builds
    }
    light = nodeChild & ~kTreeLeaf;
    pdf = product;
    return pdf > 0.0;
}

// The probability that pickTreeLight at (p, n, transmissive) returns `light`: the same choices along its
// trail (Review Focus 2: the pdf of the light a BSDF ray hits, evaluated at the vertex the ray left).
float treeLightPdf(uint light, uint count, vec3 p, vec3 n, bool transmissive, bool uniformSelection) {
    if (light >= count) {
        return 0.0;
    }
    if (uniformSelection) {
        return 1.0 / float(count);
    }
    uint trail = treeLights[light].trail;
    uint depth = min(treeLights[light].depth, kMaxTreeDepth);
    uint nodeChild = treeNodes[0].child;
    float product = 1.0;
    for (uint level = 0u; level < depth; ++level) {
        if ((nodeChild & kTreeLeaf) != 0u) {
            return 0.0; // a trail longer than the tree: not this tree's light
        }
        float i0 = treeNodeImportance(treeNodes[nodeChild], p, n, transmissive);
        float i1 = treeNodeImportance(treeNodes[nodeChild + 1u], p, n, transmissive);
        float total = i0 + i1;
        bool weighed = total > 0.0 && isFinite(total);
        uint bit = (trail >> level) & 1u;
        product *= weighed ? (bit != 0u ? i1 : i0) / total : 0.5;
        nodeChild = treeNodes[nodeChild + bit].child;
    }
    return product;
}

// --- Sampling a sphere light: a uniform cone of the directions that see it ----------------------------------

// 1 - cos of the half-angle the sphere (c, r) subtends at p, without cancellation for a small, far sphere:
// sin^2 / (1 + cos); 0 when p is inside it.
float sphereConeOneMinusCos(vec3 p, vec3 c, float r) {
    vec3 toCentre = c - p;
    float d2 = dot(toCentre, toCentre);
    float r2 = r * r;
    if (!(d2 > r2)) {
        return 0.0;
    }
    float sin2 = r2 / d2;
    return sin2 / (1.0 + sqrt(1.0 - sin2));
}

// The solid angle density of a direction from p towards the sphere (c, r) under uniform cone sampling (0
// from inside it).
float sphereConeDensity(vec3 p, vec3 c, float r) {
    float oneMinusCos = sphereConeOneMinusCos(p, c, r);
    return oneMinusCos > 0.0 ? 1.0 / (2.0 * 3.14159265358979 * oneMinusCos) : 0.0;
}

// A direction wi from p uniformly in the cone of the sphere (c, r), the distance to the sphere's near
// surface along it and its density; false from inside the sphere.
bool sampleSphereCone(vec3 p, vec3 c, float r, vec2 u, out vec3 wi, out float dist, out float pdf) {
    wi = vec3(0.0, 1.0, 0.0);
    dist = 0.0;
    pdf = 0.0;
    float oneMinusCos = sphereConeOneMinusCos(p, c, r);
    if (!(oneMinusCos > 0.0)) {
        return false;
    }
    vec3 toCentre = c - p;
    float d = length(toCentre);
    vec3 w = toCentre / d;
    float a = u.x * oneMinusCos; // 1 - cos theta
    float cosTheta = 1.0 - a;
    float sinTheta = sqrt(max(a * (2.0 - a), 0.0));
    float phi = 2.0 * 3.14159265358979 * u.y;
    vec3 t = normalize(abs(w.x) > 0.5 ? cross(w, vec3(0.0, 1.0, 0.0)) : cross(w, vec3(1.0, 0.0, 0.0)));
    vec3 b = cross(w, t);
    wi = normalize(sinTheta * cos(phi) * t + sinTheta * sin(phi) * b + cosTheta * w);
    // The near hit along wi: d cos - sqrt(r^2 - d^2 sin^2), with sin^2 = a (2 - a) (no cancellation).
    dist = max(d * cosTheta - sqrt(max(r * r - d * d * (a * (2.0 - a)), 0.0)), 0.0);
    pdf = 1.0 / (2.0 * 3.14159265358979 * oneMinusCos);
    return dist > 0.0 && isFinite(pdf);
}

#endif
