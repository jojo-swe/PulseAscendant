# EVOLUTION Step 0: Project Skeleton Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** A standalone Vulkan 1.3 application under `evolution/` that requires ray-tracing extensions, renders a time-dependent compute test pattern headless or into a window, writes PNG captures and raw frame streams, and proves all of it with CTest on Mesa lavapipe.

**Architecture:** One CMake project in `evolution/` with pinned git dependencies (SDL3, Vulkan-Headers, volk, VMA). A `gfx::Context` owns instance, device, queue and allocator and enforces the ray-tracing requirement at startup. A `gfx::Offscreen` renders a compute shader into an RGBA8 image and reads it back; the windowed path blits that image to a swapchain, the headless paths write PNG or raw RGB to stdout. Everything is deterministic in time so later tests can compare frames.

**Tech Stack:** C++20, CMake ≥ 3.25, SDL3, Vulkan 1.3 via volk (no prototypes), Vulkan Memory Allocator, GLSL compiled with `glslangValidator` at build time, Python 3 (stdlib only) for test helpers.

**Spec:** `evolution/docs/plans/2026-10-21-evolution-design.md` (sections 1, 3.1–3.3, 3.8–3.9, 6, step 0 of section 7).

## Global Constraints

- Everything lives under `evolution/`; nothing outside it is modified. The top-level PulseAscendant build is untouched.
- Vulkan 1.3 is the floor. Required device extensions, verbatim: `VK_KHR_acceleration_structure`, `VK_KHR_ray_tracing_pipeline`, `VK_KHR_ray_query`, `VK_KHR_deferred_host_operations`. Missing any → message naming each missing one, exit code 2. No fallback.
- Required features enabled on the device: `bufferDeviceAddress`, `descriptorIndexing` + `runtimeDescriptorArray` + `shaderSampledImageArrayNonUniformIndexing`, `dynamicRendering`, `synchronization2`, `timelineSemaphore`, `scalarBlockLayout`, `maintenance4`, `accelerationStructure`, `rayTracingPipeline`, `rayQuery`.
- Dependencies via `FetchContent` with `GIT_REPOSITORY` + full commit `GIT_TAG`, `GIT_SHALLOW TRUE`: SDL3 `https://github.com/libsdl-org/SDL` @ `829a65d769d935c4852f8159e964312c0957260a` (static), Vulkan-Headers `https://github.com/KhronosGroup/Vulkan-Headers` @ `d4a196d8c84e032d27f999adcea3075517c1c97f`, volk `https://github.com/zeux/volk` @ `0b17a763ba5643e32da1b2152f8140461b3b7345`, VMA `https://github.com/GPUOpen-LibrariesAndSDKs/VulkanMemoryAllocator` @ `1d8f600fd424278486eade7ed3e877c99f0846b1`. No tarball URLs (the development proxy only serves git).
- `VK_NO_PROTOTYPES` defined project-wide; all Vulkan calls go through volk. VMA built with `VMA_STATIC_VULKAN_FUNCTIONS=0`, `VMA_DYNAMIC_VULKAN_FUNCTIONS=1`.
- Shaders are GLSL under `evolution/shaders/`, compiled by `glslangValidator -V --target-env vulkan1.3` at build time into `<build>/shaders/<name>.spv` and copied beside the executable. No runtime compiler.
- Namespace `evo`. Files snake_case, types PascalCase, functions camelCase, 4-space indent, `.clang-format` based on LLVM with `ColumnLimit: 110`. Readable code, not the dense style of the old engine.
- Executable target `Evolution`, project version `0.1.0`, C++20, MSVC static runtime on Windows (`MultiThreaded$<$<CONFIG:Debug>:Debug>`).
- Presets: `linux` (Ninja, Release, `build/linux`) and `windows` (Visual Studio 18 2026, x64, `build/windows`), mirroring the parent project's conventions.
- Tests run on lavapipe: headless tests need no display; windowed tests are behind `EVO_RUNTIME_TESTS` (default OFF) and run under `xvfb-run`.
- Commit messages: imperative subject, body explains why; end with the attribution lines the session supplies. No model names in code or commits.

## Review Focus

1. **A machine without ray tracing** (or lavapipe with `MESA_VK_...` extensions disabled): the user expects one readable message listing every missing extension and exit code 2, not a Vulkan error code dump. Pinned in Task 3 by requesting a bogus extension.
2. **Resolutions that are not multiples of the workgroup size** (e.g. `33×17`, `1919×1079`): the user expects a full image with no garbage edge and no device-lost from out-of-bounds stores. Pinned in Task 4.
3. **Non-integer `duration × fps`** in `--render-stream` (e.g. `1.05 s` at 10 fps): the export tooling expects `ceil(duration·fps)` frames and an exact byte count, otherwise ffmpeg truncates the film. Pinned in Task 5.
4. **A minimised or zero-sized window**: the user expects the app to keep running and resume when restored, not crash in swapchain creation. Pinned in Task 5 by testing `chooseExtent` with a `0×0` current extent and with `UINT32_MAX` (surface-defined) extents.
5. **A capture directory that does not exist yet**, nested two levels: the user expects it to be created. Pinned in Task 6.

---

### Task 1: CMake project, pinned dependencies, CLI options

**Suggested model:** opus.

**Files:**
- Create: `evolution/CMakeLists.txt`
- Create: `evolution/CMakePresets.json`
- Create: `evolution/.gitignore` (`build/`, `dist/`, `assets/music/*.wav`, `__pycache__/`)
- Create: `evolution/.clang-format`
- Create: `evolution/cmake/Shaders.cmake`
- Create: `evolution/src/app/options.hpp`, `evolution/src/app/options.cpp`
- Create: `evolution/src/main.cpp` (parses options, prints `--help`, exits 0; real run comes in Task 5)
- Create: `evolution/shaders/fill.comp` (the test pattern, consumed by Task 4)
- Test: `evolution/tests/options_test.cpp`

**Interfaces:**
- Produces:
  ```cpp
  namespace evo {
  struct Options {
      int width = 1280, height = 720, fps = 60;
      double time = 0.0, duration = 0.0;   // duration 0 = run until Esc
      bool headless = false, renderStream = false, validation = false, help = false,
           deviceInfo = false, fullscreen = false;
      std::filesystem::path captureDir;    // empty = no capture
  };
  Options parseOptions(const std::vector<std::string>& args); // throws std::runtime_error on bad input
  std::string helpText();
  }
  ```
  Flags: `--width N`, `--height N` (64..8192), `--time S`, `--duration S` (≥0), `--fps N` (1..240), `--headless`, `--render-stream` (implies headless), `--capture-dir PATH` (implies headless unless a window is explicitly wanted; keep simple: capture is always headless), `--validation`, `--device-info`, `--fullscreen`, `--help`/`-h`. `--render-stream` with `--capture-dir` is an error. Unknown flag is an error naming the flag.
- Produces: CMake function `evo_add_shaders(<target> <shader>...)` in `cmake/Shaders.cmake`: compiles each GLSL file with `glslangValidator -V --target-env vulkan1.3 -o <build>/shaders/<basename>.spv <src>` as a custom command, adds a `<target>_shaders` custom target, makes `<target>` depend on it, and adds a post-build copy of `<build>/shaders` to `$<TARGET_FILE_DIR:target>/shaders`. `find_program(EVO_GLSLANG glslangValidator HINTS $ENV{VULKAN_SDK}/Bin $ENV{VULKAN_SDK}/bin REQUIRED)`.
- Produces: libraries `evo_core` (options only for now; later timeline etc.), target `Evolution`; CTest enabled.

- [ ] **Step 1: Write the failing test** `evolution/tests/options_test.cpp` (plain `main`, a `CHECK(x)` macro that throws, prints the count of passed checks):
  - `parseOptions({}).width == 1280 && .height == 720 && .fps == 60`
  - `parseOptions({"--width","3840","--height","2160"})` sets both
  - `parseOptions({"--render-stream"}).headless == true`
  - `parseOptions({"--capture-dir","out/x"}).captureDir == "out/x"`
  - `parseOptions({"--fps","0"})` throws; `{"--width","32"}` throws; `{"--bogus"}` throws with message containing `--bogus`; `{"--render-stream","--capture-dir","x"}` throws
  - `helpText()` contains `--render-stream`

- [ ] **Step 2: Write `CMakeLists.txt`, presets, `.gitignore`, `.clang-format`, `cmake/Shaders.cmake`, `shaders/fill.comp`, `options.*`, `main.cpp`.**
  `fill.comp`: `#version 460`, `layout(local_size_x=8, local_size_y=8)`, `layout(set=0, binding=0, rgba8) uniform writeonly image2D outImage`, push constants `{ uint width; uint height; float time; float pad; }`. Early-return when `gl_GlobalInvocationID.xy >= (width,height)`. Pattern: a 32-pixel checkerboard whose two colours are a horizontal gradient (red = x/width, green = y/height) and its inverse, plus a filled circle of radius `0.2·height` centred at `(0.5 + 0.3·sin(time), 0.5 + 0.3·cos(time))·(width,height)` drawn in white. This is the frame every later test looks at.
  CMake: FetchContent declarations per Global Constraints; `set(SDL_SHARED OFF) set(SDL_STATIC ON) set(SDL_TESTS OFF) set(SDL_TEST_LIBRARY OFF) set(SDL_INSTALL OFF)`; `set(VOLK_PULL_IN_VULKAN OFF)` and link `volk` with `Vulkan::Headers`; VMA via `GPUOpen::VulkanMemoryAllocator`; `add_compile_definitions(VK_NO_PROTOTYPES)`. `evo_core` = `src/app/options.cpp`. `Evolution` = `src/main.cpp` for now. `add_test(NAME options COMMAND options_test)`.
  Do not yet link SDL/Vulkan into `Evolution` beyond what `main.cpp` needs; Tasks 3–5 add `evo_gfx`.

- [ ] **Step 3: Configure and build, verify the test fails before `options.cpp` compiles (or passes once implemented)**
  Run: `cd evolution && cmake --preset linux && cmake --build --preset linux && ctest --preset linux`
  Expected: `options` test passes (`N checks passed`), `glslangValidator` produced `build/linux/shaders/fill.comp.spv`, `Evolution --help` prints usage and exits 0.

- [ ] **Step 4: Commit**
  `git add evolution && git commit -m "Scaffold the EVOLUTION project with pinned dependencies and CLI options"`

---

### Task 2: Dependency-free PNG writer

**Suggested model:** opus.

**Files:**
- Create: `evolution/src/image/png.hpp`, `evolution/src/image/png.cpp`
- Create: `evolution/tests/check_png.py` (stdlib `zlib`, `struct`; decodes an 8-bit RGB PNG with stored deflate and filter type 0 and prints `WIDTHxHEIGHT` and the hex of the first and last pixel)
- Test: `evolution/tests/png_test.cpp` + CTest entry that runs `png_test` then `python3 tests/check_png.py <file>`

**Interfaces:**
- Produces:
  ```cpp
  namespace evo::image {
  // rgb is row-major top-down, 3 bytes per pixel, size == width*height*3. Creates parent directories.
  void writePng(const std::filesystem::path& path, int width, int height, const std::uint8_t* rgb);
  }
  ```
  Implementation decisions: IHDR 8-bit colour type 2; one IDAT; zlib stream with **stored** deflate blocks of at most 65535 bytes (no compression), Adler-32 trailer; CRC-32 (table-driven) over chunk type + data; filter byte 0 per scanline. Big-endian lengths.

- [ ] **Step 1: Write the failing test** `png_test.cpp`: builds a 3×2 image with pixels `(255,0,0) (0,255,0) (0,0,255) / (0,0,0) (128,128,128) (255,255,255)`, calls `writePng("build-out/png/nested/dir/test.png", ...)`, then `CHECK`s the file exists, starts with the 8-byte PNG signature, and is `8 + (12+13) + (12 + 2 + 5 + (2*(1+9)) + 4) + 12` bytes long (signature + IHDR + IDAT{zlib header 2, one stored block header 5, data 2 rows × (1 filter + 9), Adler 4} + IEND).
  CTest: `add_test(NAME png COMMAND png_test)` and `add_test(NAME png_decode COMMAND python3 ${CMAKE_SOURCE_DIR}/tests/check_png.py ${CMAKE_BINARY_DIR}/build-out/png/nested/dir/test.png)` with `PASS_REGULAR_EXPRESSION "3x2 ff0000 ffffff"` and `DEPENDS png`.

- [ ] **Step 2: Run tests to verify they fail** (`png_test` does not link yet / file missing). Expected: build error or FAIL.

- [ ] **Step 3: Implement `writePng` in `png.cpp`** per the decisions above; add `src/image/png.cpp` to `evo_core`.

- [ ] **Step 4: Run tests** `ctest --preset linux -R png` Expected: both PASS; `check_png.py` prints `3x2 ff0000 ffffff`.

- [ ] **Step 5: Commit** `git commit -m "Add a dependency-free PNG writer for captures"`

---

### Task 3: Vulkan context with the ray-tracing requirement

**Suggested model:** default (Vulkan setup, feature chains).

**Files:**
- Create: `evolution/src/gfx/context.hpp`, `evolution/src/gfx/context.cpp`
- Create: `evolution/src/gfx/vk_check.hpp` (`void vkCheck(VkResult, const char* what)` throws `std::runtime_error("<what>: VkResult <n>")`)
- Create: `evolution/src/gfx/vma_impl.cpp` (`#define VMA_IMPLEMENTATION` + the two VMA function macros, includes volk first)
- Test: `evolution/tests/context_test.cpp`

**Interfaces:**
- Produces:
  ```cpp
  namespace evo::gfx {
  struct ContextOptions {
      bool validation = false;                 // enable VK_LAYER_KHRONOS_validation if present, else warn to stderr
      std::vector<const char*> instanceExtensions;   // from SDL when windowed; empty headless
      std::vector<const char*> extraDeviceExtensions; // test hook; appended to the required list
  };
  struct MissingRequirements : std::runtime_error { std::vector<std::string> missing; /* ctor builds message */ };
  class Context {
  public:
      static Context create(const ContextOptions&);   // throws MissingRequirements or std::runtime_error
      ~Context(); Context(Context&&) noexcept; Context& operator=(Context&&) noexcept; // non-copyable
      VkInstance instance() const; VkPhysicalDevice physicalDevice() const; VkDevice device() const;
      VkQueue queue() const; uint32_t queueFamily() const;   // one graphics+compute queue family
      VmaAllocator allocator() const;
      std::string deviceName() const; uint32_t apiVersion() const;
      std::string deviceInfoJson() const; // {"device":...,"driver":...,"api":"1.4.318","rayTracing":true,"validation":false}
      VkCommandPool commandPool() const;  // resettable, on queueFamily()
      // Records on a fresh primary command buffer, submits, waits idle, frees.
      void immediate(const std::function<void(VkCommandBuffer)>&) const;
  };
  }
  ```
  Decisions: volk initialised with `volkInitialize()` headless or `volkInitializeCustom(SDL_Vulkan_GetVkGetInstanceProcAddr())` when the caller passes SDL extensions (Task 5 decides which by whether `instanceExtensions` is non-empty, so `Context` takes a `PFN_vkGetInstanceProcAddr loader = nullptr` field in `ContextOptions`; `nullptr` → `volkInitialize`). Instance `apiVersion = VK_API_VERSION_1_3`. Physical device choice: first device whose `apiVersion ≥ 1.3`, supports every required extension, has a queue family with GRAPHICS|COMPUTE; prefer `DISCRETE_GPU`, then `INTEGRATED_GPU`, then `CPU`. If no device qualifies, collect the missing extension names of the best candidate (fewest missing) into `MissingRequirements`. Feature chain: `VkPhysicalDeviceFeatures2` → `Vulkan11Features` → `Vulkan12Features` (bufferDeviceAddress, descriptorIndexing, runtimeDescriptorArray, shaderSampledImageArrayNonUniformIndexing, scalarBlockLayout, timelineSemaphore) → `Vulkan13Features` (dynamicRendering, synchronization2, maintenance4) → `AccelerationStructureFeaturesKHR` → `RayTracingPipelineFeaturesKHR` → `RayQueryFeaturesKHR`; query with `vkGetPhysicalDeviceFeatures2`, verify each required flag is supported (unsupported flags also go to `MissingRequirements` as `feature:<name>`), then enable exactly those. After `vkCreateDevice`, `volkLoadDevice`. VMA: `vulkanApiVersion = VK_API_VERSION_1_3`, `flags = VMA_ALLOCATOR_CREATE_BUFFER_DEVICE_ADDRESS_BIT`, `VmaVulkanFunctions{vkGetInstanceProcAddr, vkGetDeviceProcAddr}` via `vmaImportVulkanFunctionsFromVolk`.

- [ ] **Step 1: Write the failing test** `context_test.cpp`:
  - `auto ctx = Context::create({});` → `CHECK(!ctx.deviceName().empty())`, `CHECK(ctx.apiVersion() >= VK_API_VERSION_1_3)`, `CHECK(ctx.deviceInfoJson().find("\"rayTracing\":true") != npos)`.
  - `ctx.immediate([](VkCommandBuffer){})` does not throw.
  - `Context::create({.extraDeviceExtensions = {"VK_EVO_does_not_exist"}})` throws `MissingRequirements` whose `missing` contains `"VK_EVO_does_not_exist"` and whose `what()` contains it too.
  CTest: `add_test(NAME context COMMAND context_test)`.

- [ ] **Step 2: Run to verify it fails** (link error / no implementation).

- [ ] **Step 3: Implement `Context`** per the decisions; add library `evo_gfx` (`context.cpp`, `vk_check.hpp`, `vma_impl.cpp`) linking `volk`, `Vulkan::Headers`, `GPUOpen::VulkanMemoryAllocator`, `evo_core`. Validation layer: enumerate instance layers, add `VK_LAYER_KHRONOS_validation` only if present; when requested but absent print `validation layer unavailable` to stderr and continue.

- [ ] **Step 4: Run** `ctest --preset linux -R context` Expected: PASS on lavapipe (device name contains `llvmpipe`).

- [ ] **Step 5: Commit** `git commit -m "Create the Vulkan context and enforce the ray-tracing requirement"`

---

### Task 4: Offscreen compute render and readback

**Suggested model:** default.

**Files:**
- Create: `evolution/src/gfx/offscreen.hpp`, `evolution/src/gfx/offscreen.cpp`
- Create: `evolution/src/gfx/shader.hpp`, `evolution/src/gfx/shader.cpp` (`VkShaderModule loadShaderModule(VkDevice, const std::filesystem::path& spv)`; throws naming the path when missing)
- Create: `evolution/src/gfx/paths.hpp`, `evolution/src/gfx/paths.cpp` (`std::filesystem::path shaderDir()` = `<executable dir>/shaders`, via `SDL_GetBasePath()`; overridable with env `EVO_SHADER_DIR` for tests)
- Test: `evolution/tests/offscreen_test.cpp`

**Interfaces:**
- Consumes: `Context` (Task 3), `writePng` (Task 2).
- Produces:
  ```cpp
  namespace evo::gfx {
  struct Frame { int width = 0, height = 0; std::vector<std::uint8_t> rgb; }; // top-down RGB
  class Offscreen {
  public:
      Offscreen(Context& ctx, int width, int height);  // RGBA8 UNORM storage image (STORAGE|TRANSFER_SRC), readback buffer, pipeline from shaderDir()/"fill.comp.spv"
      ~Offscreen();
      void render(double seconds);          // record+submit+wait: barrier → dispatch ceil(w/8)×ceil(h/8) → barrier → copy to buffer
      Frame readback() const;               // RGBA→RGB conversion on the host
      VkImage image() const; VkImageLayout layout() const; // after render(): TRANSFER_SRC_OPTIMAL (Task 5 blits from it)
      int width() const; int height() const;
  };
  }
  ```
  Decisions: descriptor set layout with one `STORAGE_IMAGE`, a pool of one set; push constant range 16 bytes compute stage; layout transitions via `vkCmdPipelineBarrier2` (synchronization2). Readback buffer host-visible, `VMA_MEMORY_USAGE_AUTO_PREFER_HOST` + `HOST_ACCESS_RANDOM_BIT`, mapped once.

- [ ] **Step 1: Write the failing test** `offscreen_test.cpp` (sets `EVO_SHADER_DIR` to the build's `shaders/` via a compile definition `EVO_TEST_SHADER_DIR`):
  - `Offscreen off(ctx, 64, 64); off.render(0.0); auto a = off.readback();` → `a.rgb.size() == 64*64*3`, at least one byte > 0.
  - `off.render(1.0); auto b = off.readback();` → `a.rgb != b.rgb` (circle moved).
  - `off.render(0.5); auto c = off.readback(); off.render(0.5); auto d = off.readback();` → `c.rgb == d.rgb` (determinism).
  - **Review Focus 2:** `Offscreen odd(ctx, 33, 17); odd.render(0.0);` → `readback().rgb.size() == 33*17*3`, and the pixel at `(32,16)` equals the pattern's expected checker/gradient value computed on the host: `(red = 255·32/33, green = 255·16/17)` rounded as the shader rounds (`uint8(v*255+0.5)`), within ±1 per channel.
  - Writes `a` to `<build>/build-out/offscreen/t0.png` with `writePng` (for the reviewer to look at).

- [ ] **Step 2: Run to verify it fails.**

- [ ] **Step 3: Implement `Offscreen`, `loadShaderModule`, `shaderDir()`**; add to `evo_gfx`; `evo_add_shaders(Evolution shaders/fill.comp)` already places `fill.comp.spv` under `<build>/shaders`.

- [ ] **Step 4: Run** `ctest --preset linux -R offscreen` Expected: PASS; open `t0.png` and confirm checker + gradient + white circle.

- [ ] **Step 5: Commit** `git commit -m "Render the compute test pattern offscreen with deterministic readback"`

---

### Task 5: Window, swapchain, main loop and render stream

**Suggested model:** default.

**Files:**
- Create: `evolution/src/gfx/swapchain.hpp`, `evolution/src/gfx/swapchain.cpp`
- Create: `evolution/src/app/app.hpp`, `evolution/src/app/app.cpp`
- Modify: `evolution/src/main.cpp` (dispatch: `--help`, `--device-info`, `--render-stream`, `--capture-dir` (Task 6), else windowed)
- Test: `evolution/tests/swapchain_test.cpp` (pure function test), `evolution/tests/render_stream_test.py`, optional runtime test behind `EVO_RUNTIME_TESTS`

**Interfaces:**
- Consumes: `Context`, `Offscreen`.
- Produces:
  ```cpp
  namespace evo::gfx {
  // Pure: clamps a desired size into the surface capabilities; handles currentExtent == UINT32_MAX (surface-defined)
  // and 0×0 (minimised → returns {0,0}, caller skips the frame).
  VkExtent2D chooseExtent(const VkSurfaceCapabilitiesKHR&, int desiredWidth, int desiredHeight);
  class Swapchain {  // FIFO present mode; prefers B8G8R8A8_UNORM, else first format; 3 images where allowed
  public:
      Swapchain(Context&, VkSurfaceKHR, int width, int height);
      ~Swapchain();
      bool recreate(int width, int height);           // false when extent is 0×0
      // Acquires, blits `src` (TRANSFER_SRC_OPTIMAL) to the swapchain image with linear filter, presents.
      // Returns false when the swapchain is out of date (caller recreates).
      bool present(VkImage src, int srcWidth, int srcHeight);
      VkExtent2D extent() const;
  };
  }
  namespace evo {
  int runWindowed(const Options&);     // SDL3 window with SDL_WINDOW_VULKAN|RESIZABLE; Esc quits, F11 toggles fullscreen; --duration auto-quits
  int runRenderStream(const Options&); // headless: frames 0..ceil(duration*fps)-1 at t = time + i/fps, raw RGB top-down to stdout (binary mode on Windows), progress to stderr
  int runDeviceInfo(const Options&);   // prints Context::deviceInfoJson() + '\n', returns 0; MissingRequirements → message on stderr, returns 2
  }
  ```
  Decisions: time source in the windowed loop is `SDL_GetTicksNS()` from start plus `--time`; one `Offscreen` sized to the window's drawable size, recreated on resize; `present()` uses one semaphore pair and a fence per frame-in-flight (2 in flight); minimised window (`chooseExtent` → 0×0) sleeps 50 ms and polls events. `main.cpp` catches `MissingRequirements` → prints `EVOLUTION needs a Vulkan 1.3 GPU with ray tracing. Missing: <list>` to stderr, returns 2; other exceptions → message, return 1.

- [ ] **Step 1: Write the failing tests**
  - `swapchain_test.cpp`: `chooseExtent({.currentExtent={UINT32_MAX,UINT32_MAX}, .minImageExtent={1,1}, .maxImageExtent={4096,4096}}, 1280, 720) == {1280,720}`; `chooseExtent({.currentExtent={800,600},...}, 1280, 720) == {800,600}`; `chooseExtent({.currentExtent={0,0},...}, 1280, 720) == {0,0}`; desired `9000×9000` with max `4096` → `{4096,4096}`.
  - `render_stream_test.py <exe>`: runs `<exe> --render-stream --width 64 --height 36 --fps 10 --duration 1.05` and asserts `len(stdout) == 11*64*36*3` and that frame 0 ≠ frame 10 (**Review Focus 3**); runs `<exe> --device-info` and asserts the JSON parses and `rayTracing is True`.
  - CTest: `add_test(NAME swapchain COMMAND swapchain_test)`, `add_test(NAME render_stream COMMAND python3 .../render_stream_test.py $<TARGET_FILE:Evolution>)`; behind `EVO_RUNTIME_TESTS`: `add_test(NAME windowed COMMAND xvfb-run -a $<TARGET_FILE:Evolution> --duration 1 --width 320 --height 200)` with `TIMEOUT 120`.

- [ ] **Step 2: Run to verify they fail.**

- [ ] **Step 3: Implement `chooseExtent`, `Swapchain`, `runWindowed`, `runRenderStream`, `runDeviceInfo`, wire `main.cpp`.** Link `SDL3::SDL3-static` into `evo_gfx`'s consumer `Evolution` (and `evo_app` library holding `app.cpp`).

- [ ] **Step 4: Run** `ctest --preset linux` (all), then `cmake --preset linux -DEVO_RUNTIME_TESTS=ON && ctest --preset linux -R windowed` Expected: all PASS; windowed exits 0 after one second under Xvfb.

- [ ] **Step 5: Commit** `git commit -m "Present frames in a window and stream raw frames headless"`

---

### Task 6: PNG capture mode, README and Windows preset check

**Suggested model:** opus.

**Files:**
- Create: `evolution/src/app/capture.hpp`, `evolution/src/app/capture.cpp`
- Modify: `evolution/src/main.cpp` (route `--capture-dir`)
- Create: `evolution/README.md`
- Test: `evolution/tests/capture_test.py`

**Interfaces:**
- Consumes: `Offscreen`, `writePng`, `Options`.
- Produces: `int evo::runCapture(const Options&)` — headless; renders at `t = time`, writes `<captureDir>/frame-<seconds with 3 decimals>.png` (e.g. `frame-001.500.png`), creates missing directories (**Review Focus 5**), overwrites existing files (captures are diagnostics), prints the written path to stdout, returns 0.

- [ ] **Step 1: Write the failing test** `capture_test.py <exe> <tmpdir>`: runs `<exe> --capture-dir <tmpdir>/a/b --time 1.5 --width 96 --height 54`, asserts `<tmpdir>/a/b/frame-001.500.png` exists and `check_png.py` reports `96x54`; runs it again and asserts exit 0 (overwrite). CTest `add_test(NAME capture COMMAND python3 .../capture_test.py $<TARGET_FILE:Evolution> ${CMAKE_BINARY_DIR}/build-out/capture)`.

- [ ] **Step 2: Run to verify it fails.**

- [ ] **Step 3: Implement `runCapture`, route it in `main.cpp`.**

- [ ] **Step 4: Write `evolution/README.md`**: what EVOLUTION is (two sentences, link to the design spec), requirements (Vulkan 1.3 + RT GPU; Windows 10/11 or Linux), build on Linux (`cmake --preset linux && cmake --build --preset linux && ctest --preset linux`, apt line: `build-essential cmake ninja-build glslang-tools libx11-dev libxext-dev libxrandr-dev libxcursor-dev libxi-dev libxfixes-dev libxss-dev libxtst-dev libwayland-dev libxkbcommon-dev libegl1-mesa-dev libgl1-mesa-dev libasound2-dev libpulse-dev libvulkan-dev`, plus `mesa-vulkan-drivers` for software testing), build on Windows (Visual Studio 2026, Vulkan SDK for `glslangValidator`, `cmake --preset windows`), CLI reference from `helpText()`, and the note that the current build renders only a test pattern (step 0 of the plan).

- [ ] **Step 5: Run the whole suite** `ctest --preset linux` Expected: options, png, png_decode, context, offscreen, swapchain, render_stream, capture all PASS.

- [ ] **Step 6: Commit** `git commit -m "Capture PNG frames headless and document the build"`

---

## Self-review notes

- Spec coverage for step 0: repo/CMake/deps (T1), Vulkan device with RT requirement (T3), swapchain (T5), headless mode (T4/T5/T6), compute fill shader (T1/T4), PNG capture (T2/T6), CTest (all). `--show-info`, timeline and audio belong to step 1 and are deliberately absent.
- Names used across tasks: `Options`, `parseOptions`, `helpText`, `writePng`, `Context`, `ContextOptions`, `MissingRequirements`, `Offscreen`, `Frame`, `chooseExtent`, `Swapchain`, `runWindowed`, `runRenderStream`, `runDeviceInfo`, `runCapture`, `shaderDir`, `loadShaderModule`, `evo_add_shaders` — consistent above.
- Review Focus 1 → T3 bogus extension test; 2 → T4 `33×17`; 3 → T5 `1.05 s`; 4 → T5 `chooseExtent`; 5 → T6 nested capture dir.
