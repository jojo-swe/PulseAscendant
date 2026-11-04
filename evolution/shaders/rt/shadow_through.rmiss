#version 460
#extension GL_EXT_ray_tracing : require

// Miss 3: a shadow ray through thin panes (pathtrace.rgen's shadowTransmittance in fast mode, pane.rahit)
// reached its end: unoccluded, with the panes' transmission in rgb. Never traces.

layout(location = 2) rayPayloadInEXT vec4 shadowThrough;

void main() {
    shadowThrough.a = 1.0;
}
