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
    src/gfx/image.cpp
    src/gfx/paths.cpp
    src/gfx/readback.cpp
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
    src/retro/boot.cpp
    src/retro/city.cpp
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

# The run modes (windowed, render stream, capture, device info) and the show clock; SDL3 provides the
# window, the Vulkan surface and the timer.
add_library(evo_app STATIC
    src/app/app.cpp
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

# The renderer's passes at small sizes, with the validation layer on; fails on any message it reports.
add_executable(renderer_test tests/renderer_test.cpp)
target_link_libraries(renderer_test PRIVATE evo_render)
target_compile_definitions(renderer_test PRIVATE
    EVO_TEST_SHADER_DIR="${EVO_SHADER_DIR}"
    EVO_TEST_OUT_DIR="${CMAKE_BINARY_DIR}/build-out")
add_dependencies(renderer_test Evolution_shaders)
add_test(NAME renderer COMMAND renderer_test)

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

# These run the real executable, which finds its shaders in <exe dir>/shaders. With --validation any
# validation warning or error makes it exit with code 3 (without the layer it warns and runs unchecked).
add_test(NAME render_stream
    COMMAND Python3::Interpreter ${CMAKE_SOURCE_DIR}/tests/render_stream_test.py $<TARGET_FILE:Evolution>
            --validation)
set_tests_properties(render_stream PROPERTIES TIMEOUT 300)

add_test(NAME capture
    COMMAND Python3::Interpreter ${CMAKE_SOURCE_DIR}/tests/capture_test.py $<TARGET_FILE:Evolution>
            ${CMAKE_BINARY_DIR}/build-out/capture --validation)
set_tests_properties(capture PROPERTIES TIMEOUT 120)

# Seeking and export agree: in every chapter, a frame streamed after five others equals a fresh capture of the
# same time, byte for byte (Review Focus 4 of step 3).
add_test(NAME stream_capture
    COMMAND Python3::Interpreter ${CMAKE_SOURCE_DIR}/tests/stream_capture_test.py $<TARGET_FILE:Evolution>
            ${CMAKE_BINARY_DIR}/build-out/stream_capture --validation)
set_tests_properties(stream_capture PROPERTIES TIMEOUT 300)

# The storyboard tool on two chapters: one grid for Act I, of the documented size, its 1993 and 2026 cells
# forced through EVO_FORCE_ERA (needs numpy).
add_test(NAME storyboard
    COMMAND Python3::Interpreter ${CMAKE_SOURCE_DIR}/tests/storyboard_test.py $<TARGET_FILE:Evolution>
            ${CMAKE_BINARY_DIR}/build-out/storyboard)
set_tests_properties(storyboard PROPERTIES TIMEOUT 300)

# --show-info prints the show as JSON without touching SDL video or Vulkan.
add_test(NAME show_info COMMAND Evolution --show-info)
set_tests_properties(show_info PROPERTIES PASS_REGULAR_EXPRESSION "\"duration\":711\\.16[,}]")

# The process-level exit codes: 0 on success, 1 for bad usage, 2 when the GPU lacks a requirement.
add_test(NAME exit_codes
    COMMAND Python3::Interpreter ${CMAKE_SOURCE_DIR}/tests/exit_code_test.py $<TARGET_FILE:Evolution>)
set_tests_properties(exit_codes PROPERTIES TIMEOUT 120)

# The windowed loop and the renderer, text and retro code never stall the GPU per frame (Review Focus 5).
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
    # stderr and still exits 0 (Review Focus 3); on SDL's dummy driver it plays and says AUDIO.
    add_test(NAME silent_mode
        COMMAND Python3::Interpreter ${CMAKE_SOURCE_DIR}/tests/silent_mode_test.py $<TARGET_FILE:Evolution>
                ${CMAKE_BINARY_DIR}/build-out/silent_mode)
    set_tests_properties(silent_mode PROPERTIES TIMEOUT 120)
endif()
