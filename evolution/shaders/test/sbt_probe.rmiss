#version 460
#extension GL_EXT_ray_tracing : require

// Miss shader MISS_INDEX of the shader binding table probe (sbt_probe.rgen): writes 100 + MISS_INDEX.

#ifndef MISS_INDEX
#error "compile with -DMISS_INDEX=<index>"
#endif

layout(location = 0) rayPayloadInEXT uint payload;

void main() {
    payload = 100u + uint(MISS_INDEX);
}
