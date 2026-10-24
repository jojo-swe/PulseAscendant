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
    src/gfx/context.cpp
    src/gfx/offscreen.cpp
    src/gfx/paths.cpp
    src/gfx/shader.cpp
    src/gfx/swapchain.cpp
    src/gfx/vk_check.hpp
    src/gfx/vma_impl.cpp)
target_include_directories(evo_gfx PUBLIC src)
# SDL3 provides SDL_GetBasePath() for locating the shaders beside the executable; evo_show the timeline
# frame that Offscreen renders.
target_link_libraries(evo_gfx PUBLIC
    volk Vulkan::Headers GPUOpen::VulkanMemoryAllocator evo_core evo_show SDL3::SDL3-static)
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

# The run modes (windowed, render stream, capture, device info) and the show clock; SDL3 provides the
# window, the Vulkan surface and the timer.
add_library(evo_app STATIC
    src/app/app.cpp
    src/app/capture.cpp
    src/app/clock.cpp)
target_link_libraries(evo_app PUBLIC evo_gfx evo_show evo_audio SDL3::SDL3-static)

add_executable(Evolution
    src/main.cpp)
target_link_libraries(Evolution PRIVATE evo_app SDL3::SDL3-static)
# Places Evolution in <build>/bin with its compiled shaders in <build>/bin/shaders.
evo_add_shaders(Evolution shaders/fill.comp)

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

add_executable(offscreen_test tests/offscreen_test.cpp)
target_link_libraries(offscreen_test PRIVATE evo_gfx)
target_compile_definitions(offscreen_test PRIVATE
    EVO_TEST_SHADER_DIR="${EVO_SHADER_DIR}"
    EVO_TEST_OUT_DIR="${CMAKE_BINARY_DIR}/build-out")
add_dependencies(offscreen_test Evolution_shaders) # the test loads fill.comp.spv from <build>/bin/shaders
add_test(NAME offscreen COMMAND offscreen_test)

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

# --show-info prints the show as JSON without touching SDL video or Vulkan.
add_test(NAME show_info COMMAND Evolution --show-info)
set_tests_properties(show_info PROPERTIES PASS_REGULAR_EXPRESSION "\"duration\":711\\.16[,}]")

# The process-level exit codes: 0 on success, 1 for bad usage, 2 when the GPU lacks a requirement.
add_test(NAME exit_codes
    COMMAND Python3::Interpreter ${CMAKE_SOURCE_DIR}/tests/exit_code_test.py $<TARGET_FILE:Evolution>)
set_tests_properties(exit_codes PROPERTIES TIMEOUT 120)

# The Python music pipeline: beat tracker, placeholder song and the end-to-end header generation.
add_test(NAME python_tools
    COMMAND Python3::Interpreter -m unittest discover -s ${CMAKE_SOURCE_DIR}/tests -p "test_*.py")

# Opens a real window, so it needs an X server; off by default and run under Xvfb.
option(EVO_RUNTIME_TESTS "Run tests that open a window (needs xvfb-run)" OFF)
if(EVO_RUNTIME_TESTS)
    add_test(NAME windowed
        COMMAND xvfb-run -a $<TARGET_FILE:Evolution> --validation --duration 1 --width 320 --height 200)
    set_tests_properties(windowed PROPERTIES TIMEOUT 120)

    # With no usable audio driver the windowed run falls back to the monotonic clock, says SILENT MODE on
    # stderr and still exits 0 (Review Focus 3); on SDL's dummy driver it plays and says AUDIO.
    add_test(NAME silent_mode
        COMMAND Python3::Interpreter ${CMAKE_SOURCE_DIR}/tests/silent_mode_test.py $<TARGET_FILE:Evolution>
                ${CMAKE_BINARY_DIR}/build-out/silent_mode)
    set_tests_properties(silent_mode PROPERTIES TIMEOUT 120)
endif()
