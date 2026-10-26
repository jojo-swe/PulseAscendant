#version 460
#extension GL_GOOGLE_include_directive : require

// Miss 2: a shadow ray's walk through thin panes (pathtrace.rgen's shadowTransmittance) found no further pane.
// Marks the miss (t = -1) and nothing else: that walk never reads the sky's radiance, which miss 0 would
// evaluate for every such ray. Never traces.

#include "common.glsl"

layout(location = 0) rayPayloadInEXT HitPayload payload;

void main() {
    payload.t = -1.0;
}
