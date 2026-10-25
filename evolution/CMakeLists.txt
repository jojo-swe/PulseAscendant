cmake_minimum_required(VERSION 3.25)

project(Evolution VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

include(cmake/Shaders.cmake)

# --- Dependencies, pinned to exact commits ----------------------------------------------------------------

include(FetchContent)

FetchContent_Declare(SDL3
    GIT_REPOSITORY https://github.com/libsdl-org/SDL
    GIT_TAG 829a65d769d935c4852f8159e964312c0957260a
    GIT_SHALLOW TRUE)

FetchContent_Declare(VulkanHeaders
    GIT_REPOSITORY https://github.com/KhronosGroup/Vulkan-Headers
    GIT_TAG d4a196d8c84e032d27f999adcea3075517c1c97f
    GIT_SHALLOW TRUE)

FetchContent_Declare(volk
    GIT_REPOSITORY https://github.com/zeux/volk
    GIT_TAG 0b17a763ba5643e32da1b2152f8140461b3b7345
    GIT_SHALLOW TRUE)

FetchContent_Declare(VulkanMemoryAllocator
    GIT_REPOSITORY https://github.com/GPUOpen-LibrariesAndSDKs/VulkanMemoryAllocator
    GIT_TAG 1d8f600fd424278486eade7ed3e877c99f0846b1
    GIT_SHALLOW TRUE)

set(SDL_SHARED OFF)
set(SDL_STATIC ON)
set(SDL_TESTS OFF)
set(SDL_TEST_LIBRARY OFF)
set(SDL_INSTALL OFF)

# volk gets its headers from the pinned Vulkan-Headers below, not from a system SDK.
set(VOLK_PULL_IN_VULKAN OFF)

FetchContent_MakeAvailable(SDL3 VulkanHeaders volk VulkanMemoryAllocator)

target_link_libraries(volk PUBLIC Vulkan::Headers)

# Every Vulkan call goes through volk, so nothing may use the loader's prototypes. Defined after the
# dependencies because SDL and volk already define it in their own sources.
add_compile_definitions(VK_NO_PROTOTYPES)

# --- Project targets --------------------------------------------------------------------------------------

add_library(evo_core STATIC
    src/app/options.cpp
    src/image/png.cpp)
target_include_directories(evo_core PUBLIC src)

add_library(evo_gfx STATIC
    src/gfx/buffer.cpp
    src/gfx/compute_pipeline.cpp
    src/gfx/context.cpp
    src/gfx/descriptors.cpp
    src/gfx/fill_pass.cpp
    src/gfx/frame_ring.cpp
    src/gfx/gpu_timers.cpp
    src/gfx/image.cpp
    src/gfx/paths.cpp
    src/gfx/readback.cpp
    src/gfx/rt_pipeline.cpp
    src/gfx/sbt.cpp
    src/gfx/shader.cpp
    src/gfx/swapchain.cpp
    src/gfx/sync.hpp
    src/gfx/vk_check.hpp
    src/gfx/vma_impl.cpp)
target_include_directories(evo_gfx PUBLIC src)
# SDL3 provides SDL_GetBasePath() for locating the shaders beside the executable. gfx knows nothing of the
# show: the renderer turns timeline frames into what its passes draw.
target_link_libraries(evo_gfx PUBLIC
    volk Vulkan::Headers GPUOpen::VulkanMemoryAllocator evo_core SDL3::SDL3-static)
target_compile_definitions(evo_gfx PUBLIC VK_NO_PROTOTYPES)
set_source_files_properties(src/gfx/vma_impl.cpp src/gfx/context.cpp PROPERTIES
    COMPILE_DEFINITIONS "VMA_STATIC_VULKAN_FUNCTIONS=0;VMA_DYNAMIC_VULKAN_FUNCTIONS=1")

# Music: the WAV reader, the sample-exact transport that loops it and the SDL3 player that plays it.
add_library(evo_audio STATIC
    src/audio/player.cpp
    src/audio/transport.cpp
    src/audio/wav.cpp)
target_include_directories(evo_audio PUBLIC src)
target_link_libraries(evo_audio PUBLIC SDL3::SDL3-static)

# The directed show: the cue sheet, the music curves sampled from the generated tables and the timeline that
# turns a show time into everything a frame needs. No SDL or Vulkan here.
add_library(evo_show STATIC
    src/show/era.cpp
    src/show/music_curves.cpp
    src/show/timeline.cpp)
target_include_directories(evo_show PUBLIC src)
target_compile_definitions(evo_show PRIVATE EVO_VERSION="${PROJECT_VERSION}")

# Text: the original 8x8 font, its atlas image and the compute pass that draws strings into the 1993 index
# image or blends them over an RGBA8 image.
add_library(evo_text STATIC
    src/text/atlas.cpp
    src/text/font8x8.cpp
    src/text/text_pass.cpp)
target_include_directories(evo_text PUBLIC src)
target_link_libraries(evo_text PUBLIC evo_gfx)

# Ray tracing: the geometry store (every mesh of a scene in one vertex and one index buffer), one compacted
# BLAS per mesh built at setup, and a TLAS rebuilt every frame from per-slot instance buffers.
add_library(evo_rt STATIC
    src/rt/accel.cpp
    src/rt/geometry.cpp)
target_include_directories(evo_rt PUBLIC src)
target_link_libraries(evo_rt PUBLIC evo_gfx)

# Post-processing of the 2026 side: the denoiser (demodulate, reproject and accumulate, a-trous, remodulate plus
# the primary-hit emission) and the PBR Neutral tonemap (HDR -> display-encoded RGBA8) with its CPU reference. Its
# shaders load from <build>/bin/shaders as post_<name>.comp.spv.
add_library(evo_post STATIC
    src/post/denoiser.cpp
    src/post/tonemap.cpp
    src/post/tonemap_reference.cpp
    src/post/upscale.cpp)
target_include_directories(evo_post PUBLIC src)
target_link_libraries(evo_post PUBLIC evo_gfx)
evo_add_shaders(evo_post
    shaders/post/accumulate.comp
    shaders/post/atrous.comp
    shaders/post/combine.comp
    shaders/post/firefly.comp
    shaders/post/tonemap.comp
    shaders/post/upscale.comp)

# The renderer: the 1993 stage (effect -> palette indices -> palette), the 2026 placeholder and the
# compositor (CRT pillarbox, era blend), all compute passes on the gfx foundation; plus the retro effect
# registry, the palette builders, the BOOT prompt script, the sine scroller it draws on, and the 1993 scene
# functions (projection, clipping, quantisation) whose triangles and points the scene rasteriser draws.
add_library(evo_render STATIC
    src/render/compositor.cpp
    src/render/modern_stage.cpp
    src/render/renderer.cpp
    src/render/retro_stage.cpp
    src/render/scene_raster.cpp
    src/render/stage_plan.cpp
    src/retro/boot.cpp
    src/retro/city.cpp
    src/retro/dos_idle.cpp
    src/retro/effects.cpp
    src/retro/effects.hpp
    src/retro/dots.cpp
    src/retro/finale.cpp
    src/retro/flyby.cpp
    src/retro/glenz.cpp
    src/retro/rotozoom.cpp
    src/retro/tunnel.cpp
    src/retro/palette.cpp
    src/retro/palettes.cpp
    src/retro/portal.cpp
    src/retro/raytrace.cpp
    src/retro/scene.cpp
    src/retro/scroller.cpp
    src/retro/the_end.cpp)
target_include_directories(evo_render PUBLIC src)
target_link_libraries(evo_render PUBLIC evo_gfx evo_show evo_text)
# The modern stage: the scene registry (evo_modern) and the HDR image's tonemap (evo_post).
target_link_libraries(evo_render PUBLIC evo_modern evo_post)

# The 2026 side: its procedural meshes, the scene description and registry (modern_scene.hpp, scenes.cpp), the
# Studio and the path tracer. The shared GPU structs in src/rt/gpu_types.hpp and the Mat34 helpers in
# src/modern/math.hpp are header-only. It does not use evo_post: the renderer's modern stage joins the two.
add_library(evo_modern STATIC
    src/modern/mesh_builder.cpp
    src/modern/path_tracer.cpp
    src/modern/scene.cpp
    src/modern/scene_gpu.cpp
    src/modern/scenes.cpp
    src/modern/studio.cpp)
target_include_directories(evo_modern PUBLIC src)
target_link_libraries(evo_modern PUBLIC evo_rt)
target_link_libraries(evo_modern PUBLIC evo_show) # the scene interface reads show::TimelineFrame
# The Glass scene places the 1993 glenz star and city lots (src/retro, built into evo_render, which links
# evo_modern in turn: a cycle of static libraries, which CMake repeats on the link line).
target_link_libraries(evo_modern PUBLIC evo_render)

# The path tracer's shaders (rt_<name>.<stage>.spv beside the executable), and the 2026 scenes' compute
# shaders: every shaders/modern/<scene>_<what>.comp loads as modern_<scene>_<what>.comp.spv (a scene task adds
# its file; the directory is globbed). The shared feature passes live there too and are globbed the same way,
# though they are named after the feature, not a scene (splat_resolve.comp, medium_majorant.comp,
# photons.comp, caustic_resolve.comp: Tasks 10, 11 and 13). CONFIGURE_DEPENDS re-globs on every build with
# Ninja and Makefiles; on another generator, re-run CMake when a new file is not picked up. Anything linking
# evo_modern builds them.
file(GLOB EVO_MODERN_SCENE_SHADERS CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/shaders/modern/*.comp")
evo_add_shaders(evo_modern
    shaders/rt/pathtrace.rgen
    shaders/rt/sky.rmiss
    shaders/rt/shadow.rmiss
    shaders/rt/pbr.rchit
    shaders/rt/glass.rchit
    shaders/rt/emissive.rchit
    shaders/rt/checker.rchit
    ${EVO_MODERN_SCENE_SHADERS})

# The run modes (windowed, render stream, capture, device info) and the show clock; SDL3 provides the
# window, the Vulkan surface and the timer.
add_library(evo_app STATIC
    src/app/app.cpp
    src/app/bench.cpp
    src/app/capture.cpp
    src/app/clock.cpp)
target_link_libraries(evo_app PUBLIC evo_render evo_gfx evo_show evo_audio SDL3::SDL3-static)

add_executable(Evolution
    src/main.cpp)
target_link_libraries(Evolution PRIVATE evo_app SDL3::SDL3-static)
if(WIN32)
    target_link_libraries(Evolution PRIVATE shell32) # CommandLineToArgvW, for UTF-8 arguments
endif()
# Places Evolution in <build>/bin with its compiled shaders in <build>/bin/shaders.
evo_add_shaders(Evolution
    shaders/fill.comp
    shaders/fill_hdr.comp
    shaders/crt.comp
    shaders/composite.comp
    shaders/retro/copper.comp
    shaders/retro/palette_map.comp
    shaders/retro/plasma.comp
    shaders/retro/points.comp
    shaders/retro/polys.comp
    shaders/retro/raytrace.comp
    shaders/retro/rotozoom.comp
    shaders/retro/test_pattern.comp
    shaders/text/text_index.comp
    shaders/text/text_rgba.comp)

# The show's music (git-ignored; tools/prepare_show.py writes it) goes where Evolution looks for it, so a dev
# build plays it with no flags: a symbolic link, or a copy on Windows, where links need extra privileges.
set(EVO_SHOW_WAV "${CMAKE_SOURCE_DIR}/assets/music/show.wav")
if(EXISTS "${EVO_SHOW_WAV}")
    set(runtimeMusicDir "${EVO_RUNTIME_DIR}/assets/music")
    file(MAKE_DIRECTORY "${runtimeMusicDir}")
    if(WIN32)
        file(COPY "${EVO_SHOW_WAV}" DESTINATION "${runtimeMusicDir}")
        message(STATUS "Show music copied to ${runtimeMusicDir}/show.wav")
    else()
        file(CREATE_LINK "${EVO_SHOW_WAV}" "${runtimeMusicDir}/show.wav" SYMBOLIC)
        message(STATUS "Show music linked: ${runtimeMusicDir}/show.wav -> ${EVO_SHOW_WAV}")
    endif()
endif()

# --- Tests ------------------------------------------------------------------------------------------------

enable_testing()

# The script tests run Python 3. Found rather than called as "python3", which python.org installs on
# Windows do not provide.
find_package(Python3 REQUIRED COMPONENTS Interpreter)

add_executable(options_test tests/options_test.cpp)
target_link_libraries(options_test PRIVATE evo_core)
add_test(NAME options COMMAND options_test)

add_executable(png_test tests/png_test.cpp)
target_link_libraries(png_test PRIVATE evo_core)
target_compile_definitions(png_test PRIVATE EVO_TEST_OUT_DIR="${CMAKE_BINARY_DIR}/build-out")
add_test(NAME png COMMAND png_test)
add_test(NAME png_decode
    COMMAND Python3::Interpreter ${CMAKE_SOURCE_DIR}/tests/check_png.py
            ${CMAKE_BINARY_DIR}/build-out/png/nested/dir/test.png)
set_tests_properties(png_decode PROPERTIES PASS_REGULAR_EXPRESSION "3x2 ff0000 ffffff" DEPENDS png)

add_executable(audio_transport_test tests/audio_transport_test.cpp)
target_link_libraries(audio_transport_test PRIVATE evo_audio)
target_compile_definitions(audio_transport_test PRIVATE EVO_TEST_OUT_DIR="${CMAKE_BINARY_DIR}/build-out")
add_test(NAME audio_transport COMMAND audio_transport_test)

add_executable(timeline_test tests/timeline_test.cpp)
target_link_libraries(timeline_test PRIVATE evo_show)
add_test(NAME timeline COMMAND timeline_test)

add_executable(era_test tests/era_test.cpp)
target_link_libraries(era_test PRIVATE evo_show)
add_test(NAME era COMMAND era_test)

# Also plays a short WAV through SDL's dummy audio driver, which needs no device.
add_executable(clock_test tests/clock_test.cpp)
target_link_libraries(clock_test PRIVATE evo_app)
target_compile_definitions(clock_test PRIVATE EVO_TEST_OUT_DIR="${CMAKE_BINARY_DIR}/build-out")
add_test(NAME clock COMMAND clock_test)

add_executable(context_test tests/context_test.cpp)
target_link_libraries(context_test PRIVATE evo_gfx)
add_test(NAME context COMMAND context_test)

# Runs with the validation layer and fails on any message it reports.
add_executable(gfx_foundation_test tests/gfx_foundation_test.cpp)
target_link_libraries(gfx_foundation_test PRIVATE evo_gfx evo_show) # turns show times into FillParams
target_compile_definitions(gfx_foundation_test PRIVATE EVO_TEST_SHADER_DIR="${EVO_SHADER_DIR}")
add_dependencies(gfx_foundation_test Evolution_shaders) # loads fill.comp.spv from <build>/bin/shaders
# A fixture shader with no descriptor bindings, compiled for the test only (not beside the executable).
set(EVO_TEST_SPIRV_DIR "${CMAKE_BINARY_DIR}/test-shaders")
add_custom_command(
    OUTPUT "${EVO_TEST_SPIRV_DIR}/test_no_bindings.comp.spv"
    COMMAND "${CMAKE_COMMAND}" -E make_directory "${EVO_TEST_SPIRV_DIR}"
    COMMAND "${EVO_GLSLANG}" -V --target-env vulkan1.3 -o "${EVO_TEST_SPIRV_DIR}/test_no_bindings.comp.spv"
            "${CMAKE_SOURCE_DIR}/shaders/test/no_bindings.comp"
    DEPENDS "${CMAKE_SOURCE_DIR}/shaders/test/no_bindings.comp"
    COMMENT "Compiling test shader test_no_bindings.comp"
    VERBATIM)
add_custom_target(gfx_foundation_test_shaders DEPENDS "${EVO_TEST_SPIRV_DIR}/test_no_bindings.comp.spv")
add_dependencies(gfx_foundation_test gfx_foundation_test_shaders)
target_compile_definitions(gfx_foundation_test PRIVATE EVO_TEST_SPIRV_DIR="${EVO_TEST_SPIRV_DIR}")
add_test(NAME gfx_foundation COMMAND gfx_foundation_test)

# The shared descriptor layer, ComputePipeline on it, the ray tracing properties, aligned and staged device
# buffers, and sync validation as a Context option (one hazard on purpose). The probe shader is test-only.
add_executable(descriptors_test tests/descriptors_test.cpp)
target_link_libraries(descriptors_test PRIVATE evo_gfx)
add_custom_command(
    OUTPUT "${EVO_TEST_SPIRV_DIR}/test_descriptor_probe.comp.spv"
    COMMAND "${CMAKE_COMMAND}" -E make_directory "${EVO_TEST_SPIRV_DIR}"
    COMMAND "${EVO_GLSLANG}" -V --target-env vulkan1.3 -o "${EVO_TEST_SPIRV_DIR}/test_descriptor_probe.comp.spv"
            "${CMAKE_SOURCE_DIR}/shaders/test/descriptor_probe.comp"
    DEPENDS "${CMAKE_SOURCE_DIR}/shaders/test/descriptor_probe.comp"
    COMMENT "Compiling test shader test_descriptor_probe.comp")
add_custom_target(descriptors_test_shaders DEPENDS "${EVO_TEST_SPIRV_DIR}/test_descriptor_probe.comp.spv")
add_dependencies(descriptors_test Evolution_shaders descriptors_test_shaders)
target_compile_definitions(descriptors_test PRIVATE EVO_TEST_SHADER_DIR="${EVO_SHADER_DIR}"
                                                    EVO_TEST_SPIRV_DIR="${EVO_TEST_SPIRV_DIR}")
add_test(NAME descriptors COMMAND descriptors_test)
# MALLOC_PERTURB_ fills freed heap memory, so a descriptor write chained to freed memory reads as garbage; a
# machine without synchronization validation reports the test as skipped, not passed.
set_tests_properties(descriptors PROPERTIES ENVIRONMENT "MALLOC_PERTURB_=165"
                                            SKIP_REGULAR_EXPRESSION "SKIP sync validation")

# The geometry store, compacted BLASes and the per-frame TLAS against a CPU intersection through a ray-query
# probe (test-only shader), including 64 frames with two in flight and a moving instance,
# with the validation layer and its synchronization validation on; fails on any message.
add_executable(accel_test tests/accel_test.cpp)
target_link_libraries(accel_test PRIVATE evo_rt)
add_custom_command(
    OUTPUT "${EVO_TEST_SPIRV_DIR}/test_ray_query_probe.comp.spv"
    COMMAND "${CMAKE_COMMAND}" -E make_directory "${EVO_TEST_SPIRV_DIR}"
    COMMAND "${EVO_GLSLANG}" -V --target-env vulkan1.3 -o "${EVO_TEST_SPIRV_DIR}/test_ray_query_probe.comp.spv"
            "${CMAKE_SOURCE_DIR}/shaders/test/ray_query_probe.comp"
    DEPENDS "${CMAKE_SOURCE_DIR}/shaders/test/ray_query_probe.comp"
    COMMENT "Compiling test shader test_ray_query_probe.comp"
    VERBATIM)
add_custom_target(accel_test_shaders DEPENDS "${EVO_TEST_SPIRV_DIR}/test_ray_query_probe.comp.spv")
add_dependencies(accel_test accel_test_shaders)
target_compile_definitions(accel_test PRIVATE EVO_TEST_SPIRV_DIR="${EVO_TEST_SPIRV_DIR}")
add_test(NAME accel COMMAND accel_test)

# The shader binding table layout for lavapipe's and RTX's properties, and the ray tracing
# pipeline tracing a probe (test-only shaders: one raygen, two misses and four closest-hits built from one
# source each with -DMISS_INDEX / -DHIT_GROUP) whose pixels must each name the hit group or miss they reached,
# with the validation layer on; fails on any message.
set(sbtProbeShaders "")
set(sbtProbeVariants "rgen" "miss0.rmiss:-DMISS_INDEX=0" "miss1.rmiss:-DMISS_INDEX=1")
foreach(group 0 1 2 3)
    list(APPEND sbtProbeVariants "hit${group}.rchit:-DHIT_GROUP=${group}")
endforeach()
foreach(variant IN LISTS sbtProbeVariants)
    if(variant STREQUAL "rgen")
        set(spirv "${EVO_TEST_SPIRV_DIR}/test_sbt_probe.rgen.spv")
        set(source "${CMAKE_SOURCE_DIR}/shaders/test/sbt_probe.rgen")
        set(define "")
    else()
        string(REPLACE ":" ";" parts "${variant}")
        list(GET parts 0 name)
        list(GET parts 1 define)
        cmake_path(GET name EXTENSION LAST_ONLY extension)
        set(spirv "${EVO_TEST_SPIRV_DIR}/test_sbt_probe_${name}.spv")
        set(source "${CMAKE_SOURCE_DIR}/shaders/test/sbt_probe${extension}")
    endif()
    add_custom_command(
        OUTPUT "${spirv}"
        COMMAND "${CMAKE_COMMAND}" -E make_directory "${EVO_TEST_SPIRV_DIR}"
        COMMAND "${EVO_GLSLANG}" -V --target-env vulkan1.3 ${define} -o "${spirv}" "${source}"
        DEPENDS "${source}"
        COMMENT "Compiling test shader ${spirv}"
        VERBATIM)
    list(APPEND sbtProbeShaders "${spirv}")
endforeach()
# Deliberately invalid shaders the pipeline must refuse: a closest-hit and a miss that trace (recursion depth 1)
# and a raygen that executes a callable.
foreach(offender recursive_hit.rchit recursive_miss.rmiss callable.rgen)
    set(spirv "${EVO_TEST_SPIRV_DIR}/test_${offender}.spv")
    add_custom_command(
        OUTPUT "${spirv}"
        COMMAND "${CMAKE_COMMAND}" -E make_directory "${EVO_TEST_SPIRV_DIR}"
        COMMAND "${EVO_GLSLANG}" -V --target-env vulkan1.3 -o "${spirv}"
                "${CMAKE_SOURCE_DIR}/shaders/test/${offender}"
        DEPENDS "${CMAKE_SOURCE_DIR}/shaders/test/${offender}"
        COMMENT "Compiling test shader ${spirv}"
        VERBATIM)
    list(APPEND sbtProbeShaders "${spirv}")
endforeach()
add_custom_target(rt_pipeline_test_shaders DEPENDS ${sbtProbeShaders})
add_executable(rt_pipeline_test tests/rt_pipeline_test.cpp)
target_link_libraries(rt_pipeline_test PRIVATE evo_rt)
add_dependencies(rt_pipeline_test rt_pipeline_test_shaders)
target_compile_definitions(rt_pipeline_test PRIVATE EVO_TEST_SPIRV_DIR="${EVO_TEST_SPIRV_DIR}")
add_test(NAME rt_pipeline COMMAND rt_pipeline_test)

# The renderer's passes at small sizes, with the validation layer on; fails on any message it reports.
add_executable(renderer_test tests/renderer_test.cpp)
target_link_libraries(renderer_test PRIVATE evo_render)
target_compile_definitions(renderer_test PRIVATE
    EVO_TEST_SHADER_DIR="${EVO_SHADER_DIR}"
    EVO_TEST_OUT_DIR="${CMAKE_BINARY_DIR}/build-out")
add_dependencies(renderer_test Evolution_shaders)
add_test(NAME renderer COMMAND renderer_test)

# Which stages a frame runs and may show, and when the modern stage's history resets: pure CPU.
add_executable(stage_plan_test tests/stage_plan_test.cpp)
target_link_libraries(stage_plan_test PRIVATE evo_render)
add_test(NAME stage_plan COMMAND stage_plan_test)

# BOOT: the DOS prompt script on the CPU and its index image from the retro stage, with the validation layer
# on; fails on any message it reports.
add_executable(boot_test tests/boot_test.cpp)
target_link_libraries(boot_test PRIVATE evo_render)
target_compile_definitions(boot_test PRIVATE EVO_TEST_SHADER_DIR="${EVO_SHADER_DIR}")
add_dependencies(boot_test Evolution_shaders)
add_test(NAME boot COMMAND boot_test)

# PLASMA: the cycling palette on the CPU and the plasma's index and colour images from the retro stage, with
# the validation layer on; fails on any message it reports.
add_executable(plasma_test tests/plasma_test.cpp)
target_link_libraries(plasma_test PRIVATE evo_render)
target_compile_definitions(plasma_test PRIVATE EVO_TEST_SHADER_DIR="${EVO_SHADER_DIR}")
add_dependencies(plasma_test Evolution_shaders)
add_test(NAME plasma COMMAND plasma_test)

# FLYBY: the ship's palette and scene on the CPU, and its index image from the retro stage (determinism, the
# ship's coverage and bounding box, the starfield), with the validation layer on; fails on any message it
# reports.
add_executable(flyby_test tests/flyby_test.cpp)
target_link_libraries(flyby_test PRIVATE evo_render)
target_compile_definitions(flyby_test PRIVATE EVO_TEST_SHADER_DIR="${EVO_SHADER_DIR}")
add_dependencies(flyby_test Evolution_shaders)
add_test(NAME flyby COMMAND flyby_test)

add_executable(dots_test tests/dots_test.cpp)
target_link_libraries(dots_test PRIVATE evo_render)
target_compile_definitions(dots_test PRIVATE EVO_TEST_SHADER_DIR="${EVO_SHADER_DIR}")
add_dependencies(dots_test Evolution_shaders)
add_test(NAME dots COMMAND dots_test)

# TUNNEL: the path, rings, dots and tube on the CPU (the tube's triangle budget on every frame, its painter's
# order), and its index image from the retro stage, with the validation layer on; fails on any message it
# reports.
add_executable(tunnel_test tests/tunnel_test.cpp)
target_link_libraries(tunnel_test PRIVATE evo_render)
target_compile_definitions(tunnel_test PRIVATE EVO_TEST_SHADER_DIR="${EVO_SHADER_DIR}")
add_dependencies(tunnel_test Evolution_shaders)
add_test(NAME tunnel COMMAND tunnel_test)

add_executable(city_test tests/city_test.cpp)
target_link_libraries(city_test PRIVATE evo_render)
target_compile_definitions(city_test PRIVATE EVO_TEST_SHADER_DIR="${EVO_SHADER_DIR}")
add_dependencies(city_test Evolution_shaders)
add_test(NAME city COMMAND city_test)

# GLENZ: Future Crew's mesh, the beat bounce and the silhouette on the CPU, and its index image over the copper
# bars from the retro stage (every glenz pixel against a CPU reference of the OR), with the validation layer
# on; fails on any message it reports.
add_executable(glenz_test tests/glenz_test.cpp)
target_link_libraries(glenz_test PRIVATE evo_render)
target_compile_definitions(glenz_test PRIVATE EVO_TEST_SHADER_DIR="${EVO_SHADER_DIR}")
add_dependencies(glenz_test Evolution_shaders)
add_test(NAME glenz COMMAND glenz_test)

# RAYTRACE: the fixed palette and the quantiser on the CPU, the quantiser on the GPU (a test shader running the
# shader's own quantiser, compiled for the test only) and the ray-traced index image from the retro stage, with
# the validation layer on; fails on any message it reports.
add_executable(raytrace_test tests/raytrace_test.cpp)
target_link_libraries(raytrace_test PRIVATE evo_render)
add_custom_command(
    OUTPUT "${EVO_TEST_SPIRV_DIR}/test_raytrace_quantise.comp.spv"
    COMMAND "${CMAKE_COMMAND}" -E make_directory "${EVO_TEST_SPIRV_DIR}"
    COMMAND "${EVO_GLSLANG}" -V --target-env vulkan1.3 -o "${EVO_TEST_SPIRV_DIR}/test_raytrace_quantise.comp.spv"
            "${CMAKE_SOURCE_DIR}/shaders/test/raytrace_quantise.comp"
    DEPENDS "${CMAKE_SOURCE_DIR}/shaders/test/raytrace_quantise.comp"
            "${CMAKE_SOURCE_DIR}/shaders/retro/raytrace_quantise.glsl" "${CMAKE_SOURCE_DIR}/shaders/retro/common.glsl"
    COMMENT "Compiling test shader test_raytrace_quantise.comp"
    VERBATIM)
add_custom_target(raytrace_test_shaders DEPENDS "${EVO_TEST_SPIRV_DIR}/test_raytrace_quantise.comp.spv")
add_dependencies(raytrace_test Evolution_shaders raytrace_test_shaders)
target_compile_definitions(raytrace_test PRIVATE EVO_TEST_SHADER_DIR="${EVO_SHADER_DIR}"
                                                 EVO_TEST_SPIRV_DIR="${EVO_TEST_SPIRV_DIR}")
add_test(NAME raytrace COMMAND raytrace_test)

# FINALE: the megamix's cuts on the chapter's beats and every frame's sub-effect on the CPU, and its index images
# from the retro stage against each sub-effect's own frames, with the validation layer on; fails on any message
# it reports.
add_executable(finale_test tests/finale_test.cpp)
target_link_libraries(finale_test PRIVATE evo_render)
target_compile_definitions(finale_test PRIVATE EVO_TEST_SHADER_DIR="${EVO_SHADER_DIR}")
add_dependencies(finale_test Evolution_shaders)
add_test(NAME finale COMMAND finale_test)

# PORTAL, REBOOT and THE END, the 1993 side's closing chapters: their scripts, fade, camera and bezel on the
# CPU, and their index images from the retro stage, with the validation layer on; fails on any message it
# reports.
add_executable(closing_test tests/closing_test.cpp)
target_link_libraries(closing_test PRIVATE evo_render)
target_compile_definitions(closing_test PRIVATE EVO_TEST_SHADER_DIR="${EVO_SHADER_DIR}")
add_dependencies(closing_test Evolution_shaders)
add_test(NAME closing COMMAND closing_test)

# LENS: the rotozoom texture and view on the CPU, two test builds of rotozoom.comp (unturned at 1 and at 8
# texels per pixel, the lens held still) against the tile and the refraction table, and the show's image from
# the retro stage, with the validation layer on; fails on any message it reports.
set(rotozoomTestShaders "")
foreach(texelsPerPixel 1 8)
    set(spirv "${EVO_TEST_SPIRV_DIR}/test_rotozoom_view${texelsPerPixel}.comp.spv")
    add_custom_command(
        OUTPUT "${spirv}"
        COMMAND "${CMAKE_COMMAND}" -E make_directory "${EVO_TEST_SPIRV_DIR}"
        COMMAND "${EVO_GLSLANG}" -V --target-env vulkan1.3 -DROTOZOOM_TEST_VIEW=${texelsPerPixel} -o "${spirv}"
                "${CMAKE_SOURCE_DIR}/shaders/retro/rotozoom.comp"
        DEPENDS "${CMAKE_SOURCE_DIR}/shaders/retro/rotozoom.comp" "${CMAKE_SOURCE_DIR}/shaders/retro/common.glsl"
        COMMENT "Compiling test shader test_rotozoom_view${texelsPerPixel}.comp"
        VERBATIM)
    list(APPEND rotozoomTestShaders "${spirv}")
endforeach()
add_custom_target(rotozoom_test_shaders DEPENDS ${rotozoomTestShaders})
add_executable(rotozoom_test tests/rotozoom_test.cpp)
target_link_libraries(rotozoom_test PRIVATE evo_render)
target_compile_definitions(rotozoom_test PRIVATE EVO_TEST_SHADER_DIR="${EVO_SHADER_DIR}"
                                                 EVO_TEST_SPIRV_DIR="${EVO_TEST_SPIRV_DIR}")
add_dependencies(rotozoom_test Evolution_shaders rotozoom_test_shaders)
add_test(NAME rotozoom COMMAND rotozoom_test)

# The sine scroller: its glyph positions on the CPU, and CREDITS and FLYBY's greeting from the retro stage,
# with the validation layer on; fails on any message it reports.
add_executable(scroller_test tests/scroller_test.cpp)
target_link_libraries(scroller_test PRIVATE evo_render)
target_compile_definitions(scroller_test PRIVATE EVO_TEST_SHADER_DIR="${EVO_SHADER_DIR}")
add_dependencies(scroller_test Evolution_shaders)
add_test(NAME scroller COMMAND scroller_test)

# The 1993 rasterisers: the scene functions on the CPU, and the polygon and point passes against a CPU
# reference (shared edges under rotation, primitives far off screen and behind the camera, the retro stage
# drawing an effect's scene), with the validation layer on; fails on any message it reports.
add_executable(raster_test tests/raster_test.cpp)
target_link_libraries(raster_test PRIVATE evo_render)
target_compile_definitions(raster_test PRIVATE EVO_TEST_SHADER_DIR="${EVO_SHADER_DIR}")
add_dependencies(raster_test Evolution_shaders)
add_test(NAME raster COMMAND raster_test)

# Every effect of the registry is a pure function of (chapter, retroFrame): palette, text, background and
# scene; CPU only.
add_executable(retro_registry_test tests/retro_registry_test.cpp)
target_link_libraries(retro_registry_test PRIVATE evo_render)
add_test(NAME retro_registry COMMAND retro_registry_test)

# The font's invariants, its UTF-8 mapping and the atlas layout; CPU only.
add_executable(font_test tests/font_test.cpp)
target_link_libraries(font_test PRIVATE evo_text)
add_test(NAME font COMMAND font_test)

# The text pass against a CPU rendering of the font, with the validation layer on.
add_executable(text_pass_test tests/text_pass_test.cpp)
target_link_libraries(text_pass_test PRIVATE evo_text)
target_compile_definitions(text_pass_test PRIVATE EVO_TEST_SHADER_DIR="${EVO_SHADER_DIR}")
add_dependencies(text_pass_test Evolution_shaders)
add_test(NAME text_pass COMMAND text_pass_test)

add_executable(swapchain_test tests/swapchain_test.cpp)
target_link_libraries(swapchain_test PRIVATE evo_gfx)
add_test(NAME swapchain COMMAND swapchain_test)

# The procedural meshes (watertight, wound outwards, unit normals, uvs in [0, 1]) and the Mat34 helpers; CPU
# only.
add_executable(mesh_builder_test tests/mesh_builder_test.cpp)
target_link_libraries(mesh_builder_test PRIVATE evo_modern)
add_test(NAME mesh_builder COMMAND mesh_builder_test)

# Materials and the shared GPU structs: every field of every struct read back through a test shader,
# and the BSDFs of shaders/rt/bsdf.glsl sampled and evaluated by another (white furnace, pdf
# consistency, reciprocity, finite outputs). Compute only, with the validation layer on;
# fails on any message it reports.
set(materialTestShaders "")
foreach(fixture struct_layout bsdf_probe bsdf_quadrature)
    set(spirv "${EVO_TEST_SPIRV_DIR}/test_${fixture}.comp.spv")
    add_custom_command(
        OUTPUT "${spirv}"
        COMMAND "${CMAKE_COMMAND}" -E make_directory "${EVO_TEST_SPIRV_DIR}"
        COMMAND "${EVO_GLSLANG}" -V --target-env vulkan1.3 -o "${spirv}"
                "${CMAKE_SOURCE_DIR}/shaders/test/${fixture}.comp"
        DEPENDS "${CMAKE_SOURCE_DIR}/shaders/test/${fixture}.comp" "${CMAKE_SOURCE_DIR}/shaders/rt/types.glsl"
                "${CMAKE_SOURCE_DIR}/shaders/rt/random.glsl" "${CMAKE_SOURCE_DIR}/shaders/rt/bsdf.glsl"
        COMMENT "Compiling test shader test_${fixture}.comp"
        VERBATIM)
    list(APPEND materialTestShaders "${spirv}")
endforeach()
add_custom_target(material_test_shaders DEPENDS ${materialTestShaders})
add_executable(material_test tests/material_test.cpp)
target_link_libraries(material_test PRIVATE evo_rt)
target_compile_definitions(material_test PRIVATE EVO_TEST_SPIRV_DIR="${EVO_TEST_SPIRV_DIR}")
add_dependencies(material_test material_test_shaders)
add_test(NAME material COMMAND material_test)

# The denoiser on synthetic G-buffers uploaded from the CPU: accumulation, history rejection and reset, motion
# reprojection, a-trous edges and noise, emission passthrough, non-finite input,
# determinism; with the validation layer and its synchronization validation on; fails on any message.
add_executable(denoiser_test tests/denoiser_test.cpp)
target_link_libraries(denoiser_test PRIVATE evo_post)
target_compile_definitions(denoiser_test PRIVATE EVO_TEST_SHADER_DIR="${EVO_SHADER_DIR}")
add_test(NAME denoiser COMMAND denoiser_test)
set_tests_properties(denoiser PROPERTIES SKIP_REGULAR_EXPRESSION "SKIP sync validation")

# The PBR Neutral tonemap against its CPU reference, golden anchors, black and white kept, the
# hue kept, monotonic ramps, exposure, a static dither; the scene registry; the 2026 placeholder through the HDR
# path against the old direct fill.
# With the validation layer on; fails on any message.
add_executable(tonemap_test tests/tonemap_test.cpp)
target_link_libraries(tonemap_test PRIVATE evo_render evo_post evo_modern)
target_compile_definitions(tonemap_test PRIVATE EVO_TEST_SHADER_DIR="${EVO_SHADER_DIR}")
add_dependencies(tonemap_test Evolution_shaders)
add_test(NAME tonemap COMMAND tonemap_test)

# The path tracer (Task 7 of step 4) at 64x64 on lavapipe with validation and synchronization validation:
# emission and guides, MIS against analytic and NEE-off references, the furnace, determinism per seed, the
# stress scene staying finite, dispatch batching (Windows TDR) and the retro screen as a textured light. Writes
# its pictures to build-out/step4. `path_tracer_test --look <dir> [spp]` renders a larger scene to look at.
# (i) traces the hit payload through a test-only raygen (shaders/test/payload_probe.rgen) with the real
# closest-hits.
add_custom_command(
    OUTPUT "${EVO_TEST_SPIRV_DIR}/test_payload_probe.rgen.spv"
    COMMAND "${CMAKE_COMMAND}" -E make_directory "${EVO_TEST_SPIRV_DIR}" "${CMAKE_CURRENT_BINARY_DIR}/shader-deps"
    COMMAND "${EVO_GLSLANG}" -V --target-env vulkan1.3
            --depfile "${CMAKE_CURRENT_BINARY_DIR}/shader-deps/test_payload_probe.rgen.d"
            -o "${EVO_TEST_SPIRV_DIR}/test_payload_probe.rgen.spv"
            "${CMAKE_SOURCE_DIR}/shaders/test/payload_probe.rgen"
    DEPENDS "${CMAKE_SOURCE_DIR}/shaders/test/payload_probe.rgen"
    DEPFILE "${CMAKE_CURRENT_BINARY_DIR}/shader-deps/test_payload_probe.rgen.d"
    COMMENT "Compiling test shader test_payload_probe.rgen"
    VERBATIM)
add_custom_target(path_tracer_test_shaders DEPENDS "${EVO_TEST_SPIRV_DIR}/test_payload_probe.rgen.spv")
add_executable(path_tracer_test tests/path_tracer_test.cpp)
target_link_libraries(path_tracer_test PRIVATE evo_modern evo_post) # evo_post: srgbOetf for its pictures
target_compile_definitions(path_tracer_test PRIVATE
    EVO_TEST_SHADER_DIR="${EVO_SHADER_DIR}"
    EVO_TEST_SPIRV_DIR="${EVO_TEST_SPIRV_DIR}"
    EVO_TEST_OUT_DIR="${CMAKE_BINARY_DIR}/build-out")
add_dependencies(path_tracer_test evo_modern_shaders path_tracer_test_shaders)
add_test(NAME path_tracer COMMAND path_tracer_test)
set_tests_properties(path_tracer PROPERTIES TIMEOUT 300 PROCESSORS 2)

# These run the real executable, which finds its shaders in <exe dir>/shaders. With --validation any
# validation warning or error makes it exit with code 3 (without the layer it warns and runs unchecked). The
# exports path trace at --spp 2 (the default, 64, is for real exports) and --detail 0.004 (the scenes'
# particle, splat and voxel counts at lavapipe size: Global Constraints of step 5).
add_test(NAME render_stream
    COMMAND Python3::Interpreter ${CMAKE_SOURCE_DIR}/tests/render_stream_test.py $<TARGET_FILE:Evolution>
            --validation --spp 2 --detail 0.004)
set_tests_properties(render_stream PROPERTIES TIMEOUT 300 PROCESSORS 2)

add_test(NAME capture
    COMMAND Python3::Interpreter ${CMAKE_SOURCE_DIR}/tests/capture_test.py $<TARGET_FILE:Evolution>
            ${CMAKE_BINARY_DIR}/build-out/capture --validation --spp 2 --detail 0.004)
set_tests_properties(capture PROPERTIES TIMEOUT 300 PROCESSORS 2)

# Seeking and export agree: in every chapter, a frame streamed after five others equals a fresh capture of the
# same time, byte for byte (also in the path-traced Studio
# chapters). Two parts, so CTest runs the path-traced chapters beside the others.
add_test(NAME stream_capture
    COMMAND Python3::Interpreter ${CMAKE_SOURCE_DIR}/tests/stream_capture_test.py $<TARGET_FILE:Evolution>
            ${CMAKE_BINARY_DIR}/build-out/stream_capture rest --validation --spp 2 --detail 0.004)
set_tests_properties(stream_capture PROPERTIES TIMEOUT 600 PROCESSORS 2)
add_test(NAME stream_capture_studio
    COMMAND Python3::Interpreter ${CMAKE_SOURCE_DIR}/tests/stream_capture_test.py $<TARGET_FILE:Evolution>
            ${CMAKE_BINARY_DIR}/build-out/stream_capture_studio studio --validation --spp 2 --detail 0.004)
set_tests_properties(stream_capture_studio PROPERTIES TIMEOUT 600 PROCESSORS 2)

# The storyboard tool on two chapters: one grid for Act I, of the documented size, its 1993 and 2026 cells
# forced through EVO_FORCE_ERA (needs numpy); the test passes --spp 2 --detail 0.004 to the tool.
add_test(NAME storyboard
    COMMAND Python3::Interpreter ${CMAKE_SOURCE_DIR}/tests/storyboard_test.py $<TARGET_FILE:Evolution>
            ${CMAKE_BINARY_DIR}/build-out/storyboard_test)
set_tests_properties(storyboard PROPERTIES TIMEOUT 300 PROCESSORS 2)

# --show-info prints the show as JSON without touching SDL video or Vulkan.
add_test(NAME show_info COMMAND Evolution --show-info)
set_tests_properties(show_info PROPERTIES PASS_REGULAR_EXPRESSION "\"duration\":711\\.16[,}]")

# The process-level exit codes: 0 on success, 1 for bad usage, 2 when the GPU lacks a requirement.
add_test(NAME exit_codes
    COMMAND Python3::Interpreter ${CMAKE_SOURCE_DIR}/tests/exit_code_test.py $<TARGET_FILE:Evolution>)
set_tests_properties(exit_codes PROPERTIES TIMEOUT 120)

# The windowed loop and the renderer, text and retro code never stall the GPU per frame.
add_test(NAME no_wait_idle
    COMMAND Python3::Interpreter ${CMAKE_SOURCE_DIR}/tests/no_wait_idle_test.py ${CMAKE_SOURCE_DIR})

# The Python music pipeline: beat tracker, placeholder song and the end-to-end header generation.
add_test(NAME python_tools
    COMMAND Python3::Interpreter -m unittest discover -s ${CMAKE_SOURCE_DIR}/tests -p "test_*.py")
set_tests_properties(python_tools PROPERTIES TIMEOUT 600)

# Opens a real window, so it needs an X server; off by default and run under Xvfb.
option(EVO_RUNTIME_TESTS "Run tests that open a window (needs xvfb-run)" OFF)
if(EVO_RUNTIME_TESTS)
    add_test(NAME windowed
        COMMAND xvfb-run -a $<TARGET_FILE:Evolution> --validation --duration 1 --width 320 --height 200)
    set_tests_properties(windowed PROPERTIES TIMEOUT 120)

    # A machine that cannot run EVOLUTION gets the message and exit code 2 from the window mode as well; the
    # Context is created before the window, so none opens.
    # The exit code is checked as well as the message.
    add_test(NAME windowed_unsupported
        COMMAND Python3::Interpreter ${CMAKE_SOURCE_DIR}/tests/exit_code_test.py $<TARGET_FILE:Evolution>
                --windowed)
    set_tests_properties(windowed_unsupported PROPERTIES TIMEOUT 120)

    # With no usable audio driver the windowed run falls back to the monotonic clock, says SILENT MODE on
    # stderr and still exits 0; on SDL's dummy driver it plays and says AUDIO.
    add_test(NAME silent_mode
        COMMAND Python3::Interpreter ${CMAKE_SOURCE_DIR}/tests/silent_mode_test.py $<TARGET_FILE:Evolution>
                ${CMAKE_BINARY_DIR}/build-out/silent_mode)
    set_tests_properties(silent_mode PROPERTIES TIMEOUT 120)
endif()

# The compositor's visible mask: a cleared bit shows black, a set bit its image, and the CRT pass runs only
# with the retro bit; with the validation layer on, fails on any message it reports.
add_executable(compositor_test tests/compositor_test.cpp)
target_link_libraries(compositor_test PRIVATE evo_render)
target_compile_definitions(compositor_test PRIVATE EVO_TEST_SHADER_DIR="${EVO_SHADER_DIR}")
add_dependencies(compositor_test Evolution_shaders)
add_test(NAME compositor COMMAND compositor_test)

# Images with mip levels, depth formats and sampled views, and the retro colour image as a mipmapped texture;
# with the validation layer (and its synchronization validation) on, fails on any message it reports. The
# sampled-view probe shader is test-only.
add_executable(image_test tests/image_test.cpp)
target_link_libraries(image_test PRIVATE evo_render)
add_custom_command(
    OUTPUT "${EVO_TEST_SPIRV_DIR}/test_sampled_view_probe.comp.spv"
    COMMAND "${CMAKE_COMMAND}" -E make_directory "${EVO_TEST_SPIRV_DIR}"
    COMMAND "${EVO_GLSLANG}" -V --target-env vulkan1.3 -o "${EVO_TEST_SPIRV_DIR}/test_sampled_view_probe.comp.spv"
            "${CMAKE_SOURCE_DIR}/shaders/test/sampled_view_probe.comp"
    DEPENDS "${CMAKE_SOURCE_DIR}/shaders/test/sampled_view_probe.comp"
    COMMENT "Compiling test shader test_sampled_view_probe.comp")
add_custom_target(image_test_shaders DEPENDS "${EVO_TEST_SPIRV_DIR}/test_sampled_view_probe.comp.spv")
target_compile_definitions(image_test PRIVATE EVO_TEST_SHADER_DIR="${EVO_SHADER_DIR}"
                                              EVO_TEST_SPIRV_DIR="${EVO_TEST_SPIRV_DIR}")
add_dependencies(image_test Evolution_shaders image_test_shaders)
add_test(NAME image COMMAND image_test)
# A machine without the layer's synchronization validation reports the test as skipped, not passed.
set_tests_properties(image PROPERTIES SKIP_REGULAR_EXPRESSION "SKIP sync validation")

# Export mode (Task 11 of step 4) at 160x90 in STUDIO NIGHT: the seeds the renderer hands the modern stage,
# the window's first frame after a reset equal to an export frame at equal spp and bounces,
# and export frames a pure function of the show time. With the validation layer on; fails on any message.
add_executable(export_mode_test tests/export_mode_test.cpp)
target_link_libraries(export_mode_test PRIVATE evo_render evo_modern)
target_compile_definitions(export_mode_test PRIVATE EVO_TEST_SHADER_DIR="${EVO_SHADER_DIR}")
add_dependencies(export_mode_test Evolution_shaders evo_modern_shaders)
add_test(NAME export_mode COMMAND export_mode_test)
set_tests_properties(export_mode PROPERTIES TIMEOUT 300 PROCESSORS 2)

# The Studio (Task 10 of step 4) at 320x180 in Export, 4 spp, with the validation layer on: STUDIO NIGHT lit
# but not blown out, the CRT lighting the room, the screen following the retro picture on the next window frame,
# the cameras and their cuts, the scene's instances and lights, the scene registry and the
# DOS idle screen. Writes its captures to build-out/step4. `studio_test --look <dir> [spp] [w h] [name]`
# renders stills of every shot to look at.
add_executable(studio_test tests/studio_test.cpp)
target_link_libraries(studio_test PRIVATE evo_render evo_modern)
target_compile_definitions(studio_test PRIVATE
    EVO_TEST_SHADER_DIR="${EVO_SHADER_DIR}"
    EVO_TEST_OUT_DIR="${CMAKE_BINARY_DIR}/build-out")
add_dependencies(studio_test Evolution_shaders evo_modern_shaders)
add_test(NAME studio COMMAND studio_test)
set_tests_properties(studio PROPERTIES TIMEOUT 300 PROCESSORS 2)

# Seeking with temporal accumulation on (Task 12 of step 4) at 160x90, 1 spp, Interactive:
# after any reset (a seek forwards or backwards, a chapter change, a camera cut, time running backwards) the
# window's frames equal a fresh renderer's from the same point byte for byte, in the Studio chapters, a
# 1993 chapter and across BeatSwap. With the validation layer on; fails on any message.
add_executable(seek_determinism_test tests/seek_determinism_test.cpp)
target_link_libraries(seek_determinism_test PRIVATE evo_render evo_modern)
target_compile_definitions(seek_determinism_test PRIVATE EVO_TEST_SHADER_DIR="${EVO_SHADER_DIR}")
add_dependencies(seek_determinism_test Evolution_shaders evo_modern_shaders)
add_test(NAME seek_determinism COMMAND seek_determinism_test)
set_tests_properties(seek_determinism PROPERTIES TIMEOUT 300 PROCESSORS 2)

# --- Step 5: the shared path tracer, the scene table and the scene tests -----------------------------------

# evo_gpu_test(<name> <executable> [TIMEOUT seconds] [PROCESSORS n]): a heavy lavapipe test (path tracing, many
# frames). It runs with the build's shaders (EVO_TEST_SHADER_DIR) and the test output directory
# (EVO_TEST_OUT_DIR), TIMEOUT 300 and PROCESSORS 2 by default (so CTest overlaps fewer of them), and is
# reported skipped when the layer lacks synchronization validation ("SKIP sync validation").
function(evo_gpu_test name exe)
    cmake_parse_arguments(PARSE_ARGV 2 arg "" "TIMEOUT;PROCESSORS" "")
    if(NOT arg_TIMEOUT)
        set(arg_TIMEOUT 300)
    endif()
    if(NOT arg_PROCESSORS)
        set(arg_PROCESSORS 2)
    endif()
    target_compile_definitions(${exe} PRIVATE
        EVO_TEST_SHADER_DIR="${EVO_SHADER_DIR}"
        EVO_TEST_OUT_DIR="${CMAKE_BINARY_DIR}/build-out")
    add_dependencies(${exe} Evolution_shaders evo_modern_shaders)
    add_test(NAME ${name} COMMAND ${exe})
    set_tests_properties(${name} PROPERTIES TIMEOUT ${arg_TIMEOUT} PROCESSORS ${arg_PROCESSORS}
                                            SKIP_REGULAR_EXPRESSION "SKIP sync validation")
endfunction()

# The scene test fixture (tests/scene_fixture.hpp): checkChapterPictures and checkSceneSweep, the standard
# checks every step 5 scene test runs.
add_library(evo_scene_fixture STATIC tests/scene_fixture.cpp)
target_link_libraries(evo_scene_fixture PUBLIC evo_render evo_modern)
target_include_directories(evo_scene_fixture PUBLIC tests)

# evo_scene_test(<scene>): tests/scene_<scene>_test.cpp as the CTest test scene_<scene>, linked with the
# fixture: evo_gpu_test(scene_<scene> scene_<scene>_test TIMEOUT 300 PROCESSORS 2).
function(evo_scene_test scene)
    add_executable(scene_${scene}_test tests/scene_${scene}_test.cpp)
    target_link_libraries(scene_${scene}_test PRIVATE evo_scene_fixture)
    evo_gpu_test(scene_${scene} scene_${scene}_test TIMEOUT 300 PROCESSORS 2)
endfunction()

# The scene table (CPU): a row per effect, the Studio's five chapters, names, --show-info's scene field, the
# Studio's camera() against its frames, and the fixture's sweep over the Studio's chapters.
add_executable(scene_table_test tests/scene_table_test.cpp)
target_link_libraries(scene_table_test PRIVATE evo_scene_fixture)
add_test(NAME scene_table COMMAND scene_table_test)

# One path tracer and one denoiser across scenes (Task 1 of step 5), with synchronization validation: a Studio
# frame after another scene equals a fresh one, one pipeline for two scenes, image memory independent of the
# scene count, per-frame materials, instance capacities, shotFor without frame(), and the fixture's picture
# checks on the Studio.
add_executable(shared_tracer_test tests/shared_tracer_test.cpp)
target_link_libraries(shared_tracer_test PRIVATE evo_scene_fixture)
evo_gpu_test(shared_tracer shared_tracer_test)

# Scene environments (Task 2 of step 5): EnvironmentDesc, its defaults per kind and its packing into
# rt::EnvironmentUniforms (binding EVO_BINDING_ENVIRONMENT, which the path tracer uploads per frame).
target_sources(evo_modern PRIVATE src/modern/environment.cpp)

# The environments on lavapipe with the validation layer on: shaders/rt/environment.glsl run directly by a
# test-only compute probe (disc and dome sampling, pdfs, the lighting sky without stars, filtered stars), and
# the path tracer at 64x64 (a floor under a sun and a dome against the analytic irradiance and a quadrature,
# NEE against BSDF sampling, star flux across resolutions) plus the Studio's luminance under its NightSky.
set(environmentProbeSpirv "${EVO_TEST_SPIRV_DIR}/test_environment_probe.comp.spv")
add_custom_command(
    OUTPUT "${environmentProbeSpirv}"
    COMMAND "${CMAKE_COMMAND}" -E make_directory "${EVO_TEST_SPIRV_DIR}" "${CMAKE_BINARY_DIR}/shader-deps"
    COMMAND "${EVO_GLSLANG}" -V --target-env vulkan1.3 --depfile
            "${CMAKE_BINARY_DIR}/shader-deps/test_environment_probe.comp.d" -o "${environmentProbeSpirv}"
            "${CMAKE_SOURCE_DIR}/shaders/test/environment_probe.comp"
    DEPENDS "${CMAKE_SOURCE_DIR}/shaders/test/environment_probe.comp"
    DEPFILE "${CMAKE_BINARY_DIR}/shader-deps/test_environment_probe.comp.d"
    COMMENT "Compiling test shader test_environment_probe.comp"
    VERBATIM)
add_custom_target(environment_test_shaders DEPENDS "${environmentProbeSpirv}")
add_executable(environment_test tests/environment_test.cpp)
target_link_libraries(environment_test PRIVATE evo_render evo_modern)
target_include_directories(environment_test PRIVATE tests)
target_compile_definitions(environment_test PRIVATE EVO_TEST_SPIRV_DIR="${EVO_TEST_SPIRV_DIR}")
add_dependencies(environment_test environment_test_shaders)
evo_gpu_test(environment environment_test)

# Per-point light selection (Task 3 of step 5): the CPU reference in evo_modern, and a test that holds
# shaders/rt/lights.glsl to it through a compute probe (with the path tracer's own bindings), checks pickLight's
# frequencies, MIS against BSDF sampling with three lights of very different power, and the Studio's screen and
# moon. `light_selection_test --measure <dir>` prints the Studio's 1 spp noise with per-point and fixed
# selection.
target_sources(evo_modern PRIVATE src/modern/light_select.cpp)
set(lightSelectProbe "${EVO_TEST_SPIRV_DIR}/test_light_select_probe.comp.spv")
add_custom_command(
    OUTPUT "${lightSelectProbe}"
    COMMAND "${CMAKE_COMMAND}" -E make_directory "${EVO_TEST_SPIRV_DIR}" "${CMAKE_BINARY_DIR}/shader-deps"
    COMMAND "${EVO_GLSLANG}" -V --target-env vulkan1.3 --depfile
            "${CMAKE_BINARY_DIR}/shader-deps/test_light_select_probe.comp.d" -o "${lightSelectProbe}"
            "${CMAKE_SOURCE_DIR}/shaders/test/light_select_probe.comp"
    DEPENDS "${CMAKE_SOURCE_DIR}/shaders/test/light_select_probe.comp"
    DEPFILE "${CMAKE_BINARY_DIR}/shader-deps/test_light_select_probe.comp.d"
    COMMENT "Compiling test shader test_light_select_probe.comp"
    VERBATIM)
add_custom_target(light_selection_test_shaders DEPENDS "${lightSelectProbe}")
add_executable(light_selection_test tests/light_selection_test.cpp)
target_link_libraries(light_selection_test PRIVATE evo_render evo_modern)
target_compile_definitions(light_selection_test PRIVATE EVO_TEST_SPIRV_DIR="${EVO_TEST_SPIRV_DIR}")
add_dependencies(light_selection_test light_selection_test_shaders)
evo_gpu_test(light_selection light_selection_test)

# --- Step 5 Task 4: trace resolution, temporal upscaler, GPU timers and --bench ----------------------------

# GPU timestamp scopes per ring slot: nesting and order, non-negative times, reading on slot reuse and after
# waitAll(), the disabled path (a queue without timestamps), misuse; with the validation layer on.
add_executable(gpu_timers_test tests/gpu_timers_test.cpp)
target_link_libraries(gpu_timers_test PRIVATE evo_gfx)
target_compile_definitions(gpu_timers_test PRIVATE EVO_TEST_SHADER_DIR="${EVO_SHADER_DIR}")
add_test(NAME gpu_timers COMMAND gpu_timers_test)

# The temporal upscaler on synthetic inputs, 64x36 -> 128x72: identity at equal sizes, a reset equal to the
# spatial upsample, convergence on a static pattern, no ghost behind a moving bar, non-finite inputs, the
# emission kept out of the history, the subpixel window; with synchronization validation.
add_executable(upscale_test tests/upscale_test.cpp)
target_link_libraries(upscale_test PRIVATE evo_post)
evo_gpu_test(upscale upscale_test TIMEOUT 300 PROCESSORS 1)

# The trace resolution through the renderer at 160x90 (and the tracer's subpixel window on an emitter quad):
# the trace size, the window's first frame equal to an export at the same scale, seeks equal to a fresh
# renderer below scale 1, a scale change resetting the history, and the GPU timers' passes.
add_executable(trace_scale_test tests/trace_scale_test.cpp)
target_link_libraries(trace_scale_test PRIVATE evo_render evo_modern)
evo_gpu_test(trace_scale trace_scale_test)

# --bench at 96x54: one JSON line per chapter with every key (GPU ms per pass, sizes, trace scale, device),
# STUDIO NIGHT at the default trace scale and two chapters at scale 1; with the validation layer on.
add_test(NAME bench
    COMMAND Python3::Interpreter ${CMAKE_SOURCE_DIR}/tests/bench_test.py $<TARGET_FILE:Evolution> --validation
            --detail 0.004)
set_tests_properties(bench PROPERTIES TIMEOUT 300 PROCESSORS 2)

# The seek determinism suite again at the window's default trace scale 0.67: jittered samples and the temporal
# upscaler's history must reset with everything else.
add_test(NAME seek_determinism_scaled COMMAND seek_determinism_test 0.67)
set_tests_properties(seek_determinism_scaled PROPERTIES TIMEOUT 300 PROCESSORS 2)

# --- Step 5 Task 8: analytic spheres and the particle field -------------------------------------------------

# Static sphere BLASes (one AABB BLAS per sphere range) and the per-frame particle field (a stateless compute
# pass and a BLAS rebuilt per ring slot).
target_sources(evo_rt PRIVATE src/rt/spheres.cpp src/rt/particles.cpp)
# The sphere intersection shader (rt_sphere.rint.spv) and the four closest-hits built a second time for
# spheres (rt_<class>_sphere.rchit.spv, -DGEOMETRY_SPHERES): hit groups 4-7.
_evo_shader_rule(evoSphereIntersection shaders/rt/sphere.rint "" "")
_evo_shader_target(evo_modern "_intersection" "${evoSphereIntersection}")
evo_add_shader_variants(evo_modern SUFFIX sphere DEFINES GEOMETRY_SPHERES SHADERS
    shaders/rt/pbr.rchit
    shaders/rt/glass.rchit
    shaders/rt/emissive.rchit
    shaders/rt/checker.rchit)

# Spheres and particles on lavapipe with synchronization validation: the sphere math against the CPU (a
# test-only compute probe), static sphere depth and normals, glass against a uvSphere mesh, the particle field
# over 32 frames two in flight (test-only particle shader), refusals, the material classes on spheres, a camera
# inside glass, the hit payload (path_tracer_test's payload probe) and the modern stage's one particle field.
# `spheres_test --look <dir> [spp]` renders a picture to look at.
set(spheresTestShaders "")
foreach(fixture sphere_probe particles_grid)
    set(spirv "${EVO_TEST_SPIRV_DIR}/test_${fixture}.comp.spv")
    add_custom_command(
        OUTPUT "${spirv}"
        COMMAND "${CMAKE_COMMAND}" -E make_directory "${EVO_TEST_SPIRV_DIR}" "${CMAKE_BINARY_DIR}/shader-deps"
        COMMAND "${EVO_GLSLANG}" -V --target-env vulkan1.3 --depfile
                "${CMAKE_BINARY_DIR}/shader-deps/test_${fixture}.comp.d" -o "${spirv}"
                "${CMAKE_SOURCE_DIR}/shaders/test/${fixture}.comp"
        DEPENDS "${CMAKE_SOURCE_DIR}/shaders/test/${fixture}.comp"
        DEPFILE "${CMAKE_BINARY_DIR}/shader-deps/test_${fixture}.comp.d"
        COMMENT "Compiling test shader test_${fixture}.comp"
        VERBATIM)
    list(APPEND spheresTestShaders "${spirv}")
endforeach()
add_custom_target(spheres_test_shaders DEPENDS ${spheresTestShaders})
add_executable(spheres_test tests/spheres_test.cpp)
target_link_libraries(spheres_test PRIVATE evo_render evo_modern evo_post)
target_include_directories(spheres_test PRIVATE tests)
target_compile_definitions(spheres_test PRIVATE EVO_TEST_SPIRV_DIR="${EVO_TEST_SPIRV_DIR}")
add_dependencies(spheres_test spheres_test_shaders path_tracer_test_shaders evo_modern_sphere_shaders
                 evo_modern_intersection_shaders)
evo_gpu_test(spheres spheres_test)

# --- Step 5 Task 9: object motion vectors and the export shutter --------------------------------------------

# The shutter's slice counts, offsets and chapter-clamped slice times (CPU), and the per-object motion vectors
# and shutter on lavapipe with synchronization validation (tests/motion_test.cpp).
target_sources(evo_modern PRIVATE src/modern/shutter.cpp)
add_executable(motion_test tests/motion_test.cpp)
target_link_libraries(motion_test PRIVATE evo_render evo_modern evo_post)
target_include_directories(motion_test PRIVATE tests)
target_compile_definitions(motion_test PRIVATE EVO_TEST_SPIRV_DIR="${EVO_TEST_SPIRV_DIR}")
add_dependencies(motion_test spheres_test_shaders evo_modern_sphere_shaders evo_modern_intersection_shaders)
evo_gpu_test(motion motion_test)

# --- Step 5 Task 10: emissive splats -------------------------------------------------------------------------

# SplatPass (src/modern/splats.cpp) and its shaders (shaders/modern/splat_common.glsl, splat_resolve.comp, which
# the shaders/modern glob compiles as modern_splat_resolve.comp.spv).
target_sources(evo_modern PRIVATE src/modern/splats.cpp)

# The splats with synchronization validation, through a test splat shader (shaders/test/splat_grid.comp, built
# twice: plain and with SPLAT_PERMUTE_IDS): energy across resolutions against a CPU reference, the depth test,
# order independence of 2^18 points, the fixed-point overflow, refusals, the splats through ModernStage
# (occlusion by the traced scene, projection, energy, seeks, memory) and a picture of a spiral galaxy.
set(splatGridSpirv "")
foreach(variant IN ITEMS plain permuted)
    if(variant STREQUAL "plain")
        set(spirv "${EVO_TEST_SPIRV_DIR}/test_splat_grid.comp.spv")
        set(defines "")
    else()
        set(spirv "${EVO_TEST_SPIRV_DIR}/test_splat_grid_permuted.comp.spv")
        set(defines "-DSPLAT_PERMUTE_IDS")
    endif()
    add_custom_command(
        OUTPUT "${spirv}"
        COMMAND "${CMAKE_COMMAND}" -E make_directory "${EVO_TEST_SPIRV_DIR}" "${CMAKE_BINARY_DIR}/shader-deps"
        COMMAND "${EVO_GLSLANG}" -V --target-env vulkan1.3 ${defines} --depfile
                "${CMAKE_BINARY_DIR}/shader-deps/test_splat_grid_${variant}.comp.d" -o "${spirv}"
                "${CMAKE_SOURCE_DIR}/shaders/test/splat_grid.comp"
        DEPENDS "${CMAKE_SOURCE_DIR}/shaders/test/splat_grid.comp"
        DEPFILE "${CMAKE_BINARY_DIR}/shader-deps/test_splat_grid_${variant}.comp.d"
        COMMENT "Compiling test shader test_splat_grid.comp (${variant})"
        VERBATIM)
    list(APPEND splatGridSpirv "${spirv}")
endforeach()
add_custom_target(splat_test_shaders DEPENDS ${splatGridSpirv})
add_executable(splat_test tests/splat_test.cpp)
target_link_libraries(splat_test PRIVATE evo_render evo_modern)
target_include_directories(splat_test PRIVATE tests)
target_compile_definitions(splat_test PRIVATE EVO_TEST_SPIRV_DIR="${EVO_TEST_SPIRV_DIR}")
add_dependencies(splat_test splat_test_shaders)
evo_gpu_test(splat splat_test)

# --- Step 5 Task 6: the integrator v2 -----------------------------------------------------------------------

# Path regularisation, the clamp after a rough vertex, camera-only emitters and hero-wavelength dispersion on
# lavapipe at 64x64 with the validation layer on (a mirror caustic box, chrome and a glass slab before a
# bright lamp, an LED over a mirror floor, a flint prism, slab and furnace), and shaders/rt/spectral.glsl
# against its CPU mirror src/rt/spectral.hpp through a compute probe. Pictures go to build-out/step5.
set(spectralProbeSpirv "${EVO_TEST_SPIRV_DIR}/test_spectral_probe.comp.spv")
add_custom_command(
    OUTPUT "${spectralProbeSpirv}"
    COMMAND "${CMAKE_COMMAND}" -E make_directory "${EVO_TEST_SPIRV_DIR}" "${CMAKE_BINARY_DIR}/shader-deps"
    COMMAND "${EVO_GLSLANG}" -V --target-env vulkan1.3 --depfile
            "${CMAKE_BINARY_DIR}/shader-deps/test_spectral_probe.comp.d" -o "${spectralProbeSpirv}"
            "${CMAKE_SOURCE_DIR}/shaders/test/spectral_probe.comp"
    DEPENDS "${CMAKE_SOURCE_DIR}/shaders/test/spectral_probe.comp"
    DEPFILE "${CMAKE_BINARY_DIR}/shader-deps/test_spectral_probe.comp.d"
    COMMENT "Compiling test shader test_spectral_probe.comp"
    VERBATIM)
add_custom_target(integrator_test_shaders DEPENDS "${spectralProbeSpirv}")
add_executable(integrator_test tests/integrator_test.cpp)
target_link_libraries(integrator_test PRIVATE evo_modern)
target_include_directories(integrator_test PRIVATE tests)
target_compile_definitions(integrator_test PRIVATE EVO_TEST_SPIRV_DIR="${EVO_TEST_SPIRV_DIR}")
add_dependencies(integrator_test integrator_test_shaders)
evo_gpu_test(integrator integrator_test)

# --- Step 5 Task 13: caustic photon maps ------------------------------------------------------------------

# The caustic maps (photons.comp and caustic_resolve.comp come with the shaders/modern glob above).
target_sources(evo_modern PRIVATE src/modern/caustics.cpp)

# Caustics on lavapipe with synchronization validation: a glass sphere over a receiver floor under an
# overhead disc (or a rect light): energy against the cross-section, dispersion, determinism in any photon
# order, the double-count exclusion, the photon-mapped caustic against the converged path-traced one,
# byte-identity without caustics, refusals and the modern stage. `caustics_test --look <dir> [spp]` renders
# a still to look at.
add_executable(caustics_test tests/caustics_test.cpp)
target_link_libraries(caustics_test PRIVATE evo_render evo_modern evo_post)
target_include_directories(caustics_test PRIVATE tests)
add_dependencies(caustics_test evo_modern_sphere_shaders evo_modern_intersection_shaders)
evo_gpu_test(caustics caustics_test)

# --- Step 5 Task 12: the light tree ---------------------------------------------------------------------------

# Next-event estimation over up to 65,536 emissive sphere lights: the tree is built (or refit) on the CPU per
# frame and walked in shaders/rt/light_tree.glsl.
target_sources(evo_rt PRIVATE src/rt/light_tree.cpp)

# The tree's build, refit and walk on the CPU (sums of the pdf, the pick histogram, determinism, 65,536 lights
# in budget); the walk on the GPU against them (a test-only compute probe); NEE + MIS over sphere lights
# against the analytic sum, NEE on and off, with and without blinking; the variance against uniform
# selection; 4096 lights in time; seeks; refusals. `light_tree_test --look <dir> [spp]` renders a small city
# at night to look at.
set(lightTreeProbe "${EVO_TEST_SPIRV_DIR}/test_light_tree_probe.comp.spv")
add_custom_command(
    OUTPUT "${lightTreeProbe}"
    COMMAND "${CMAKE_COMMAND}" -E make_directory "${EVO_TEST_SPIRV_DIR}" "${CMAKE_BINARY_DIR}/shader-deps"
    COMMAND "${EVO_GLSLANG}" -V --target-env vulkan1.3 --depfile
            "${CMAKE_BINARY_DIR}/shader-deps/test_light_tree_probe.comp.d" -o "${lightTreeProbe}"
            "${CMAKE_SOURCE_DIR}/shaders/test/light_tree_probe.comp"
    DEPENDS "${CMAKE_SOURCE_DIR}/shaders/test/light_tree_probe.comp"
    DEPFILE "${CMAKE_BINARY_DIR}/shader-deps/test_light_tree_probe.comp.d"
    COMMENT "Compiling test shader test_light_tree_probe.comp"
    VERBATIM)
add_custom_target(light_tree_test_shaders DEPENDS "${lightTreeProbe}")
add_executable(light_tree_test tests/light_tree_test.cpp)
target_link_libraries(light_tree_test PRIVATE evo_render evo_modern evo_post)
target_include_directories(light_tree_test PRIVATE tests)
target_compile_definitions(light_tree_test PRIVATE EVO_TEST_SPIRV_DIR="${EVO_TEST_SPIRV_DIR}")
add_dependencies(light_tree_test light_tree_test_shaders spheres_test_shaders evo_modern_sphere_shaders
                 evo_modern_intersection_shaders)
evo_gpu_test(light_tree light_tree_test)

# Step 5, Task 17: the Particles scene (DOTS, PARTICLE OCEAN; its particle shader
# shaders/modern/particles_particles.comp is globbed above) and its test (`scene_particles_test --look <dir>
# [spp] [detail]` renders 480x270 stills of both chapters; `--memory [detail]` prints the measured bytes).
target_sources(evo_modern PRIVATE src/modern/scenes/particles.cpp)
evo_scene_test(particles)

# --- Step 5 Task 22: Spheres (RAYTRACE, SPHERE STORM, RAYTRACE INFINITY) ---------------------------------------

# The scene (src/modern/scenes/spheres.cpp) and its particle shader (shaders/modern/particles_spheres.comp,
# compiled by the shaders/modern glob as modern_particles_spheres.comp.spv).
target_sources(evo_modern PRIVATE src/modern/scenes/spheres.cpp)

# The standard scene contract (chapter pictures, the CPU sweep, the cards) plus the 1993 mirror, the storm's
# particle mirror against the GPU, and the chrome sphere reflecting both checker colours. `spheres_scene_test
# --look <dir> [spp] [width height]` renders stills of the three chapters instead.
add_executable(spheres_scene_test tests/spheres_scene_test.cpp)
target_link_libraries(spheres_scene_test PRIVATE evo_scene_fixture)
evo_gpu_test(scene_spheres spheres_scene_test)

# --- Step 5 Task 11: participating media ---------------------------------------------------------------------

# Height fog and baked density grids: the CPU side (packing, the HG and fog references) and the grid's GPU
# side (MediumVolume: the bake into a 3D image and the majorant pass, shaders/modern/medium_majorant.comp).
target_sources(evo_modern PRIVATE src/modern/medium.cpp src/modern/medium_volume.cpp)

# The media on lavapipe with synchronization validation: Beer-Lambert, delta and ratio tracking, a constant
# grid against fog, emission, a furnace, NEE through a slab, exponential fog, the bake and its majorant,
# extremes, no medium, single scattering against a numeric integral, light shafts, the modern stage (seek and
# memory) and refusals, through a test-only bake shader. `medium_test --look <dir> [spp]` renders stills.
set(mediumBakeSpirv "${EVO_TEST_SPIRV_DIR}/test_medium_constant.comp.spv")
add_custom_command(
    OUTPUT "${mediumBakeSpirv}"
    COMMAND "${CMAKE_COMMAND}" -E make_directory "${EVO_TEST_SPIRV_DIR}" "${CMAKE_BINARY_DIR}/shader-deps"
    COMMAND "${EVO_GLSLANG}" -V --target-env vulkan1.3 --depfile
            "${CMAKE_BINARY_DIR}/shader-deps/test_medium_constant.comp.d" -o "${mediumBakeSpirv}"
            "${CMAKE_SOURCE_DIR}/shaders/test/medium_constant.comp"
    DEPENDS "${CMAKE_SOURCE_DIR}/shaders/test/medium_constant.comp"
    DEPFILE "${CMAKE_BINARY_DIR}/shader-deps/test_medium_constant.comp.d"
    COMMENT "Compiling test shader test_medium_constant.comp"
    VERBATIM)
add_custom_target(medium_test_shaders DEPENDS "${mediumBakeSpirv}")
add_executable(medium_test tests/medium_test.cpp)
target_link_libraries(medium_test PRIVATE evo_render evo_modern evo_post)
target_include_directories(medium_test PRIVATE tests)
target_compile_definitions(medium_test PRIVATE EVO_TEST_SPIRV_DIR="${EVO_TEST_SPIRV_DIR}")
add_dependencies(medium_test medium_test_shaders)
evo_gpu_test(medium medium_test)

# Step 5, Task 21: the Lens scene (LENS, LENS OF GOD: a glass ball and a colossal lens over the 1993 rotozoom's
# texture laid as a stone mosaic, the Checker hit group's Tesserae patterns) and its test, with a compute probe
# of shaders/rt/tesserae.glsl against the 1993 texture (`scene_lens_test --look <dir> [spp] [detail]` renders
# 480x270 stills of both chapters).
target_sources(evo_modern PRIVATE src/modern/scenes/lens.cpp)
evo_scene_test(lens)
set(tesseraeProbeSpirv "${EVO_TEST_SPIRV_DIR}/test_tesserae_probe.comp.spv")
add_custom_command(
    OUTPUT "${tesseraeProbeSpirv}"
    COMMAND "${CMAKE_COMMAND}" -E make_directory "${EVO_TEST_SPIRV_DIR}" "${CMAKE_BINARY_DIR}/shader-deps"
    COMMAND "${EVO_GLSLANG}" -V --target-env vulkan1.3 --depfile
            "${CMAKE_BINARY_DIR}/shader-deps/test_tesserae_probe.comp.d" -o "${tesseraeProbeSpirv}"
            "${CMAKE_SOURCE_DIR}/shaders/test/tesserae_probe.comp"
    DEPENDS "${CMAKE_SOURCE_DIR}/shaders/test/tesserae_probe.comp"
    DEPFILE "${CMAKE_BINARY_DIR}/shader-deps/test_tesserae_probe.comp.d"
    COMMENT "Compiling test shader test_tesserae_probe.comp"
    VERBATIM)
add_custom_target(scene_lens_test_shaders DEPENDS "${tesseraeProbeSpirv}")
target_compile_definitions(scene_lens_test PRIVATE EVO_TEST_SPIRV_DIR="${EVO_TEST_SPIRV_DIR}")
add_dependencies(scene_lens_test scene_lens_test_shaders)

# --- Step 5 Task 20: Glass (GLENZ, GLASS CATHEDRAL, CRYSTAL CITY) -------------------------------------------

# The glenz star in flint glass over rainbow caustics, a stained-glass cathedral and a city of quartz towers
# (src/modern/scenes/glass.cpp), and its scene test: the standard picture and sweep checks, the cards' numbers,
# the 1993 star's motion, the music, Beer-Lambert and thin panes against closed forms, the glenz's rainbows
# (and none without dispersion) and the cathedral's pools of coloured sunlight.
target_sources(evo_modern PRIVATE src/modern/scenes/glass.cpp)
evo_scene_test(glass)

# --- Step 5 Task 16: the Ship (WORMHOLE) -------------------------------------------------------------------

# The starship as an asset any scene can fly (src/modern/scenes/ship_asset.cpp: the hull, its plumes as a grid
# medium, the star splats; shaders/modern/ship_{plumes,splats}.comp) and the Ship scene that stages it in
# WORMHOLE (src/modern/scenes/ship.cpp), with its test: the standard picture and sweep checks, the card's
# numbers, the plumes' blackbody ramp, the rings on the beat, the crossing's motion blur and the plumes'
# volume. `scene_ship_test --look <dir> [spp] [width height]` renders stills.
target_sources(evo_modern PRIVATE src/modern/scenes/ship.cpp src/modern/scenes/ship_asset.cpp)
evo_scene_test(ship)

# --- Step 5 Task 24: Cosmos (GALAXY, ASCENDANT) ------------------------------------------------------------

# A sixteen-million-star galaxy as splats with its dust as a grid medium, and two million particles rising from
# its core into the logo (src/modern/scenes/cosmos.cpp; shaders/modern/{galaxy.glsl, cosmos_splats.comp,
# cosmos_medium.comp, cosmos_particles.comp}), with its test: the standard picture and sweep checks, the cards'
# numbers, the flight's landing and waypoint on the CPU twin, the particle shader against it, and the dust
# lanes in the volume layer. `scene_cosmos_test --look <dir> [spp] [detail]` renders one still per chapter.
target_sources(evo_modern PRIVATE src/modern/scenes/cosmos.cpp)
evo_scene_test(cosmos)

# --- Step 5 Task 25: Singularity (SINGULARITY) -------------------------------------------------------------

# The gravitational lens (src/modern/lensing.cpp packs LensingFrame; shaders/rt/lensing.glsl bends main rays
# along the null geodesic and marches the accretion disc) and the black hole's scene
# (src/modern/scenes/singularity.cpp; shaders/modern/singularity_{splats,medium}.comp), with its test: the
# standard picture and sweep checks, the card, the Einstein ring at the analytic radius, capture below the
# critical impact parameter, and the lens off. `scene_singularity_test --look <dir> [spp] [detail]` renders the
# ring still.
target_sources(evo_modern PRIVATE src/modern/lensing.cpp src/modern/scenes/singularity.cpp)
evo_scene_test(singularity)
