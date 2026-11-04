#version 460
#extension GL_EXT_ray_tracing : require

// Miss 1: a shadow ray (traced with SkipClosestHitShader | TerminateOnFirstHit) reached its end unoccluded.
// Never traces.

layout(location = 1) rayPayloadInEXT uint shadowVisible;

void main() {
    shadowVisible = 1u;
}
