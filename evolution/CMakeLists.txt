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
    src/app/options.cpp)
target_include_directories(evo_core PUBLIC src)

add_executable(Evolution
    src/main.cpp)
target_link_libraries(Evolution PRIVATE evo_core)
evo_add_shaders(Evolution shaders/fill.comp)

# --- Tests ------------------------------------------------------------------------------------------------

enable_testing()

add_executable(options_test tests/options_test.cpp)
target_link_libraries(options_test PRIVATE evo_core)
add_test(NAME options COMMAND options_test)
