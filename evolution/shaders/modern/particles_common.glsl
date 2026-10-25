#ifndef EVO_MODERN_PARTICLES_COMMON_GLSL
#define EVO_MODERN_PARTICLES_COMMON_GLSL

// The particle field's compute pass (analytic spheres; rt::ParticleField records it). A scene's
// shaders/modern/particles_<scene>.comp defines the pure function
//
//     SphereRecord particle(uint id, float seconds, vec4 params[4]);
//
// (its centre, radius, colour and emission; prevCenter and material are ignored) and includes this file
// first (after #version and GL_GOOGLE_include_directive; it declares the prototype and the types),
// whose main() runs one invocation per particle: the record at `seconds`, prevCenter = the centre at
// `previousSeconds`, material = the frame's, and the record's AABB for the BLAS build (inactive, min > max,
// for a radius <= 0 or a non-finite centre: no ray reaches it). Nothing reads what an earlier frame wrote,
// so the field is a pure function of (id, seconds, params): seeking and exports need no history.

#extension GL_GOOGLE_include_directive : require
#extension GL_EXT_scalar_block_layout : require

#include "../common/finite.glsl" // allFinite, isFinite (from the bits: robust under fast-math)
#include "../rt/types.glsl"

layout(local_size_x = 256) in; // rt::ParticleField::kLocalSize

layout(set = 0, binding = 0, scalar) writeonly buffer ParticleRecords { SphereRecord particleRecords[]; };
layout(set = 0, binding = 1, scalar) writeonly buffer ParticleAabbs { float particleAabbs[]; }; // 6 per particle

layout(push_constant, scalar) uniform ParticlePush {
    uint count;
    float seconds;
    float previousSeconds;
    uint material;
    vec4 params[4];
} particlePush;

SphereRecord particle(uint id, float seconds, vec4 params[4]);

void main() {
    uint id = gl_GlobalInvocationID.x;
    if (id >= particlePush.count) {
        return;
    }
    vec4 params[4] = particlePush.params;
    SphereRecord s = particle(id, particlePush.seconds, params);
    SphereRecord previous = particle(id, particlePush.previousSeconds, params);
    s.prevCenter = previous.center;
    s.material = particlePush.material;
    particleRecords[id] = s;
    bool live = allFinite(s.center) && isFinite(s.radius) && s.radius > 0.0;
    vec3 lo = live ? s.center - vec3(s.radius) : vec3(1.0);
    vec3 hi = live ? s.center + vec3(s.radius) : vec3(-1.0);
    uint base = 6u * id;
    particleAabbs[base + 0u] = lo.x;
    particleAabbs[base + 1u] = lo.y;
    particleAabbs[base + 2u] = lo.z;
    particleAabbs[base + 3u] = hi.x;
    particleAabbs[base + 4u] = hi.y;
    particleAabbs[base + 5u] = hi.z;
}

#endif
