// The path tracer's descriptor set (set 0), written once in the subset C++ and GLSL share: C++ includes this
// file as "rt/bindings.hpp" (src/modern/path_tracer.cpp binds it) and shaders/rt/common.glsl as
// "../../src/rt/bindings.hpp" (it declares it). Plain macros only, under an include guard (GLSL has no
// #pragma once).
//
// Bindings 0-14 are the original. 15-25 and 27 are the feature resources (the ABI Tasks 1 and 1b of step 5
// froze): each is bound every frame, to the feature's resource when the frame has it (modern::TraceExtras, or
// the tracer's own per-slot buffer) and otherwise to a one-element dummy, so a scene without the feature
// binds the same layout. 26 is the G-buffer's volume layer. Each is a binding of its own, not an array
// element, so synchronization validation sees it.

#ifndef EVO_RT_BINDINGS_HPP
#define EVO_RT_BINDINGS_HPP

#define EVO_BINDING_TLAS 0
#define EVO_BINDING_LIGHTING 1             // storage image, RGBA32F
#define EVO_BINDING_EMISSION 2             // storage image, RGBA16F
#define EVO_BINDING_ALBEDO 3               // storage image, RGBA16F
#define EVO_BINDING_NORMAL 4               // storage image, RGBA16F
#define EVO_BINDING_DEPTH 5                // storage image, R32F
#define EVO_BINDING_MOTION 6               // storage image, RGBA16F
#define EVO_BINDING_FRAME 7                // FrameUniforms (per ring slot)
#define EVO_BINDING_MATERIALS 8            // Material[] (the scene's static table, or this frame's)
#define EVO_BINDING_LIGHTS 9               // RectLight[] (per ring slot)
#define EVO_BINDING_INSTANCES 10           // InstanceRecord[] (per ring slot)
#define EVO_BINDING_VERTICES 11            // Vertex[]
#define EVO_BINDING_INDICES 12             // uint[]
#define EVO_BINDING_MESH_RECORDS 13        // MeshRecord[]
#define EVO_BINDING_RETRO_SCREEN 14        // sampler2D, the live 1993 picture
#define EVO_BINDING_ENVIRONMENT 15         // EnvironmentUniforms
#define EVO_BINDING_TREE_NODES 16          // LightTreeNode[]
#define EVO_BINDING_TREE_LIGHTS 17         // TreeLight[]
#define EVO_BINDING_MEDIUM 18              // MediumUniforms
#define EVO_BINDING_DENSITY 19             // sampler3D, RGBA16F density + emission
#define EVO_BINDING_MAJORANT 20            // sampler3D, R16F majorant per cell
#define EVO_BINDING_PREVIOUS_TRANSFORMS 21 // Mat34[], the instances' previous transforms
#define EVO_BINDING_STATIC_SPHERES 22      // SphereRecord[], the scene's static spheres
#define EVO_BINDING_PARTICLE_SPHERES 23    // SphereRecord[], this frame's particle field
#define EVO_BINDING_CAUSTIC_MAPS 24        // sampler2DArray, RGBA16F irradiance per receiver
#define EVO_BINDING_LENSING 25             // LensingUniforms
#define EVO_BINDING_VOLUME 26              // storage image, RGBA16F: in-scattered radiance, transmittance
#define EVO_BINDING_CAUSTIC_RECEIVERS 27   // CausticReceiver[], Material::receiver's table
#define EVO_BINDING_COUNT 28

#endif
