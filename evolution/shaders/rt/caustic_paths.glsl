#ifndef EVO_RT_CAUSTIC_PATHS_GLSL
#define EVO_RT_CAUSTIC_PATHS_GLSL

// The double-count exclusion of the caustic photon maps (step 5, Task 13), for pathtrace.rgen (included
// after sampleLight(); needs its payload, bounceRand, pickLight and the environment).
//
// The map holds every light path from its source light (CausticFrame::light) that leaves the light inside
// the photons' beam, meets 1..kMaxPhotonDeltaEvents delta surfaces and lands on a receiver, as the radiance
// of the receiver's Lambert lobe (caustics.glsl adds it at every receiver hit). A camera path that leaves a
// receiver by a non-delta sample and reaches that light through such a chain would add the same light again.
// So from a receiver's non-delta bounce on, while the path only meets delta surfaces (judged on the
// material's own roughness, so a chain stays a chain when regularisation roughens it), the source's light at
// the end of the chain (a BSDF hit, an escape into the disc, or next-event estimation at a link) loses the
// Lambert share of that bounce: keep = 1 - lambertLobe / bsdfEval at its (wo, wi). Everything else, and
// every path while the frame has no caustics, is untouched.

#include "caustic_common.glsl"

struct CausticPath {
    bool tracking;   // a receiver's non-delta bounce began a chain of delta surfaces
    uint links;      // the delta surfaces met since (the current vertex included)
    vec3 keep;       // the share of the source's light at the chain's end the map does not hold
    bool atLink;     // the current vertex is a link of the chain
    bool atReceiver; // the current vertex is a receiver with a Lambert lobe
};

CausticPath causticPathStart() {
    CausticPath c;
    c.tracking = false;
    c.links = 0u;
    c.keep = vec3(1.0);
    c.atLink = false;
    c.atReceiver = false;
    return c;
}

// The source record after the receivers (caustic_common.glsl).
CausticReceiver causticSource() {
    return receiverTable[frame.causticReceivers];
}

// At every hit, with the hit's own (unregularised) surface and material flags.
void causticPathHit(inout CausticPath c, Surface material, uint flags) {
    bool delta = causticDeltaSurface(material);
    c.atReceiver = frame.causticReceivers > 0u && (flags & kMaterialCausticReceiver) != 0u && !delta;
    c.atLink = false;
    if (c.tracking) {
        if (delta && c.links < kMaxPhotonDeltaEvents) {
            ++c.links;
            c.atLink = true;
        } else if (delta) {
            c.tracking = false; // longer than any photon's path
        }
    }
}

// After the BSDF sampled wi at the current vertex (s the surface it sampled with): a receiver's non-delta
// sample begins a chain; a link carries it on; anything else ends it.
void causticPathScatter(inout CausticPath c, Surface s, vec3 n, vec3 wo, vec3 wi, bool delta) {
    if (c.atLink) {
        return;
    }
    c.tracking = false;
    if (!c.atReceiver || delta) {
        return;
    }
    vec3 facing = dot(n, wo) < 0.0 ? -n : n;
    float cosO = dot(wo, facing), cosI = dot(wi, facing);
    if (!(cosO > 0.0) || !(cosI > 0.0)) {
        return;
    }
    float pdf;
    vec3 f = bsdfEval(s, n, wo, wi, pdf);
    vec3 lambert = lambertLobe(s, cosO, cosI);
    c.keep = vec3(f.r > 0.0 ? 1.0 - lambert.r / f.r : 1.0, f.g > 0.0 ? 1.0 - lambert.g / f.g : 1.0,
                  f.b > 0.0 ? 1.0 - lambert.b / f.b : 1.0);
    c.keep = clamp(c.keep, 0.0, 1.0);
    c.links = 0u;
    c.tracking = true;
}

// What an emitter hit (RectLight `light` at y, reached from the vertex x) keeps.
vec3 causticEmissionKeep(CausticPath c, uint light, vec3 x, vec3 y) {
    if (!c.tracking || c.links == 0u || light == kNoLight) {
        return vec3(1.0);
    }
    CausticReceiver source = causticSource();
    return source.layer == light && inRectBeam(source.worldToUv.rows[0], y, x) ? c.keep : vec3(1.0);
}

// What an escape from the vertex x along d keeps of the sky's radiance `sky` (the environment's lighting):
// the disc's part loses the Lambert share when the disc is the source.
vec3 causticEscape(CausticPath c, vec3 x, vec3 d, vec3 sky) {
    if (!c.tracking || c.links == 0u) {
        return sky;
    }
    CausticReceiver source = causticSource();
    if (source.layer != kNoLight || !inDiscBeam(source.worldToUv.rows[0], environment.discDirection, x, d)) {
        return sky;
    }
    vec3 disc = environment.intensity * skyDisc(normalize(d), false);
    return max(sky - (1.0 - c.keep) * disc, vec3(0.0));
}

// What next-event estimation at a link (the current vertex p, with the surface s it samples with: only a
// regularised delta surface has a lobe NEE can reach) keeps: sampleLight's own choice of light and point,
// made again from the same random numbers.
vec3 causticNeeKeep(CausticPath c, Surface s, vec3 p, vec3 n, vec3 wo, uint bounce) {
    if (!c.atLink) {
        return vec3(1.0);
    }
    CausticReceiver source = causticSource();
    vec4 sphere = source.worldToUv.rows[0];
    float choice = bounceRand(bounce, 3u);
    float environmentSelect = frame.environmentSelect;
    vec2 uv = vec2(bounceRand(bounce, 4u), bounceRand(bounce, 5u));
    if (choice < environmentSelect) {
        vec3 wi;
        vec3 le;
        float pdf;
        if (source.layer != kNoLight || !sampleEnvironment(uv, wi, le, pdf) ||
            !inDiscBeam(sphere, environment.discDirection, p, wi)) {
            return vec3(1.0);
        }
        vec3 disc = environment.intensity * skyDisc(wi, false);
        vec3 kept = le - (1.0 - c.keep) * disc;
        return vec3(le.r > 0.0 ? kept.r / le.r : 1.0, le.g > 0.0 ? kept.g / le.g : 1.0,
                    le.b > 0.0 ? kept.b / le.b : 1.0);
    }
    uint index;
    float selectPdf;
    if (source.layer == kNoLight ||
        !pickLight((choice - environmentSelect) / (1.0 - environmentSelect), p, selectionNormal(n, wo),
                   isDielectric(s), index, selectPdf) ||
        index != source.layer) {
        return vec3(1.0);
    }
    RectLight l = lights[index];
    return inRectBeam(sphere, l.corner + uv.x * l.edgeU + uv.y * l.edgeV, p) ? c.keep : vec3(1.0);
}

#endif
