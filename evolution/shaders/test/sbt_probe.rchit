#version 460
#extension GL_EXT_ray_tracing : require

// The closest-hit shader of hit group HIT_GROUP of the shader binding table probe (sbt_probe.rgen): writes
// HIT_GROUP + 1.

#ifndef HIT_GROUP
#error "compile with -DHIT_GROUP=<index>"
#endif

layout(location = 0) rayPayloadInEXT uint payload;

void main() {
    payload = uint(HIT_GROUP) + 1u;
}
