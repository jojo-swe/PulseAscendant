#include "app/app.hpp"

#include "app/clock.hpp"
#include "gfx/context.hpp"
#include "gfx/frame_ring.hpp"
#include "gfx/paths.hpp"
#include "gfx/readback.hpp"
#include "gfx/swapchain.hpp"
#include "gfx/vk_check.hpp"
#include "render/renderer.hpp"
#include "show/timeline.hpp"

#include <SDL3/SDL.h>
#include <SDL3/SDL_vulkan.h>

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#ifdef _WIN32
#include <fcntl.h>
#include <io.h>
#endif

namespace evo {

namespace {

constexpr Uint32 kMinimisedPollMs = 50;
// A chapter key pressed just after a jump may read the clock a hair before the chapter it jumped to; this
// much slack counts it as in that chapter, so Right, Right moves two chapters.
constexpr double kChapterSlack = 0.05;

std::runtime_error sdlError(const std::string& what) {
    return std::runtime_error(what + ": " + SDL_GetError());
}

// SDL's video subsystem for the lifetime of the windowed run.
struct SdlVideo {
    SdlVideo() {
        if (!SDL_Init(SDL_INIT_VIDEO))
            throw sdlError("SDL_Init");
    }
    ~SdlVideo() { SDL_Quit(); }
    SdlVideo(const SdlVideo&) = delete;
    SdlVideo& operator=(const SdlVideo&) = delete;
};

// The Vulkan loader, loaded by SDL before any window exists, so the Context can be created first: a machine
// that cannot run EVOLUTION exits without a window flashing up. No loader is a missing requirement.
struct VulkanLibrary {
    VulkanLibrary() {
        if (!SDL_Vulkan_LoadLibrary(nullptr))
            throw gfx::MissingRequirements({std::string("Vulkan loader (") + SDL_GetError() + ")"});
    }
    ~VulkanLibrary() { SDL_Vulkan_UnloadLibrary(); }
    VulkanLibrary(const VulkanLibrary&) = delete;
    VulkanLibrary& operator=(const VulkanLibrary&) = delete;
};

struct Window {
    explicit Window(const Options& options) {
        SDL_WindowFlags flags = SDL_WINDOW_VULKAN | SDL_WINDOW_RESIZABLE;
        if (options.fullscreen)
            flags |= SDL_WINDOW_FULLSCREEN;
        handle = SDL_CreateWindow("EVOLUTION", options.width, options.height, flags);
        if (!handle)
            throw sdlError("SDL_CreateWindow");
    }
    ~Window() { SDL_DestroyWindow(handle); }
    Window(const Window&) = delete;
    Window& operator=(const Window&) = delete;

    SDL_Window* handle = nullptr;
};

// Must be destroyed before the Context that owns the instance.
struct Surface {
    Surface(VkInstance instance, SDL_Window* window) : instance(instance) {
        if (!SDL_Vulkan_CreateSurface(window, instance, nullptr, &handle))
            throw sdlError("SDL_Vulkan_CreateSurface");
    }
    ~Surface() { SDL_Vulkan_DestroySurface(instance, handle, nullptr); }
    Surface(const Surface&) = delete;
    Surface& operator=(const Surface&) = delete;

    VkInstance instance = VK_NULL_HANDLE;
    VkSurfaceKHR handle = VK_NULL_HANDLE;
};

gfx::Context createWindowedContext(const Options& options) {
    gfx::ContextOptions contextOptions;
    contextOptions.validation = options.validation;
    contextOptions.loader =
        reinterpret_cast<PFN_vkGetInstanceProcAddr>(SDL_Vulkan_GetVkGetInstanceProcAddr());
    if (!contextOptions.loader)
        throw sdlError("SDL_Vulkan_GetVkGetInstanceProcAddr");

    Uint32 count = 0;
    const char* const* extensions = SDL_Vulkan_GetInstanceExtensions(&count);
    if (!extensions)
        throw sdlError("SDL_Vulkan_GetInstanceExtensions");
    contextOptions.instanceExtensions.assign(extensions, extensions + count);
    // Only the windowed run presents, so only it asks the device for swapchains.
    contextOptions.extraDeviceExtensions.push_back(VK_KHR_SWAPCHAIN_EXTENSION_NAME);
    return gfx::Context::create(contextOptions);
}

// --music, or the show's music beside the executable.
std::filesystem::path musicPath(const Options& options) {
    if (!options.music.empty())
        return options.music;
    const char* base = SDL_GetBasePath(); // UTF-8, owned by SDL; ends with a separator
    return base ? gfx::pathFromUtf8(base) / show::kMusicFile : std::filesystem::path(show::kMusicFile);
}

void toggleFullscreen(SDL_Window* window) {
    const bool fullscreen = (SDL_GetWindowFlags(window) & SDL_WINDOW_FULLSCREEN) != 0;
    if (!SDL_SetWindowFullscreen(window, !fullscreen))
        std::fprintf(stderr, "warning: cannot toggle fullscreen: %s\n", SDL_GetError());
}

} // namespace

int chapterForKey(SDL_Keycode key, double now) {
    const int count = show::chapterCount();
    int current = 0;
    while (current + 1 < count && show::chapterStart(current + 1) <= now + kChapterSlack)
        ++current;

    if (key == SDLK_LEFT)
        return (current + count - 1) % count;
    if (key == SDLK_RIGHT)
        return (current + 1) % count;
    if (key >= SDLK_1 && key <= SDLK_9)
        return static_cast<int>(key - SDLK_1);
    if (key == SDLK_0)
        return 9;
    return -1;
}

std::string windowTitle(const show::TimelineFrame& frame) {
    const int seconds = static_cast<int>(frame.showSeconds);
    char clock[16];
    std::snprintf(clock, sizeof clock, "%02d:%02d", seconds / 60, seconds % 60);
    return std::string("EVOLUTION  ") + frame.title + "  " + clock;
}

std::string missingRequirementsMessage(const gfx::MissingRequirements& missing) {
    std::string list;
    for (const auto& name : missing.missing)
        list += (list.empty() ? "" : ", ") + name;
    return "EVOLUTION needs a Vulkan 1.3 GPU with ray tracing. Missing: " + list;
}

int runWindowed(const Options& options) {
    SdlVideo video;
    VulkanLibrary vulkan;
    gfx::Context ctx = createWindowedContext(options); // before the window: see VulkanLibrary
    Window window(options);
    Surface surface(ctx.instance(), window.handle);

    VkBool32 presentable = VK_FALSE;
    gfx::vkCheck(vkGetPhysicalDeviceSurfaceSupportKHR(ctx.physicalDevice(), ctx.queueFamily(), surface.handle,
                                                      &presentable),
                 "vkGetPhysicalDeviceSurfaceSupportKHR");
    if (!presentable)
        throw std::runtime_error("the selected GPU (" + ctx.deviceName() +
                                 ") cannot present to this window from its graphics queue");

    int width = 0, height = 0;
    if (!SDL_GetWindowSizeInPixels(window.handle, &width, &height))
        throw sdlError("SDL_GetWindowSizeInPixels");

    // Each frame records the renderer's passes and the swapchain's blit of its output into one command
    // buffer of the ring, with up to two frames in flight; begin() waits only for the frame whose slot it
    // reuses. The renderer renders at the drawable's size, so the 4:3 picture stays undistorted in any window
    // shape. Declared after the ring (its destructor waits for the ring) and before the swapchain, so it is
    // destroyed after it: ~Swapchain waits for the device, so no frame can still be using it.
    gfx::FrameRing ring(ctx);
    render::Renderer renderer(ctx, ring,
                              {static_cast<std::uint32_t>(width > 0 ? width : options.width),
                               static_cast<std::uint32_t>(height > 0 ? height : options.height), options.crt,
                               options.hud});
    gfx::Swapchain swapchain(ctx, surface.handle, width, height);
    bool needRecreate = swapchain.extent().width == 0; // started minimised

    // Started last, so the music begins with the first frame rather than during the device setup.
    ShowClock clock(!options.noAudio, musicPath(options), show::showDuration(), options.time,
                    options.audioOffsetMs / 1000.0);
    std::fprintf(stderr, "%s\n", clock.status().c_str());
    bool silent = clock.silent();
    bool muted = options.mute;
    clock.setMuted(muted);
    std::string title;

    const Uint64 start = SDL_GetTicksNS(); // for --duration, which counts run time rather than show time
    for (bool running = true; running;) {
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            switch (event.type) {
            case SDL_EVENT_QUIT:
                running = false;
                break;
            case SDL_EVENT_KEY_DOWN:
                if (event.key.key == SDLK_ESCAPE) {
                    running = false;
                } else if (event.key.repeat) {
                    break; // a held key acts once
                } else if (event.key.key == SDLK_F11) {
                    toggleFullscreen(window.handle);
                } else if (event.key.key == SDLK_M) {
                    muted = !muted;
                    clock.setMuted(muted);
                } else if (event.key.key == SDLK_C) {
                    renderer.setCrt(!renderer.crt());
                } else if (event.key.key == SDLK_H) {
                    renderer.setHud(!renderer.hud());
                } else if (const int chapter = chapterForKey(event.key.key, clock.now()); chapter >= 0) {
                    clock.seek(show::chapterStart(chapter));
                }
                break;
            case SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED:
                needRecreate = true;
                break;
            default:
                break;
            }
        }

        const double elapsed = static_cast<double>(SDL_GetTicksNS() - start) / 1e9;
        if (!running || (options.duration > 0.0 && elapsed >= options.duration))
            break;

        if (!SDL_GetWindowSizeInPixels(window.handle, &width, &height))
            throw sdlError("SDL_GetWindowSizeInPixels");
        if (needRecreate) {
            if (!swapchain.recreate(width, height)) { // minimised: nothing to present to
                SDL_Delay(kMinimisedPollMs);
                continue;
            }
            needRecreate = false;
        }
        if (width <= 0 || height <= 0) {
            SDL_Delay(kMinimisedPollMs);
            continue;
        }
        // A no-op unless the drawable changed size; then it waits for the frames in flight first.
        renderer.resize(static_cast<std::uint32_t>(width), static_cast<std::uint32_t>(height));

        const show::TimelineFrame frame = show::sampleTimeline(clock.now());
        // The audio stream failed mid-show (or came back): say so, as at the start.
        if (clock.silent() != silent) {
            silent = clock.silent();
            std::fprintf(stderr, "%s\n", clock.status().c_str());
        }
        if (std::string newTitle = windowTitle(frame); newTitle != title) {
            title = std::move(newTitle);
            SDL_SetWindowTitle(window.handle, title.c_str());
        }
        VkCommandBuffer cmd = ring.begin();
        renderer.render(cmd, frame);
        if (!swapchain.present(ring, cmd, renderer.output())) // submits the frame in every case
            needRecreate = true;
    }

    gfx::vkCheck(vkDeviceWaitIdle(ctx.device()), "vkDeviceWaitIdle"); // shutdown: presents included
    return 0;
}

int runRenderStream(const Options& options) {
#ifdef _WIN32
    _setmode(_fileno(stdout), _O_BINARY); // raw bytes: no CRLF translation
#endif
    gfx::ContextOptions contextOptions;
    contextOptions.validation = options.validation;
    gfx::Context ctx = gfx::Context::create(contextOptions);

    // The epsilon keeps products like 0.7 * 10 = 7.000000000000001 from adding a frame.
    const long frames = static_cast<long>(std::ceil(options.duration * options.fps - 1e-9));
    if (frames < 1)
        throw std::runtime_error("--duration is shorter than one frame"); // parseOptions rejects only 0

    const auto width = static_cast<std::uint32_t>(options.width);
    const auto height = static_cast<std::uint32_t>(options.height);
    gfx::Readback readback(ctx, width, height);
    gfx::FrameRing ring(ctx);
    render::Renderer renderer(ctx, ring, {width, height, options.crt, options.hud});
    for (long i = 0; i < frames; ++i) {
        VkCommandBuffer cmd = ring.begin();
        renderer.render(cmd, show::sampleTimeline(options.time + static_cast<double>(i) / options.fps));
        readback.record(cmd, renderer.output());
        ring.wait(ring.submit()); // the readback is complete: rgb() may read it
        const std::vector<std::uint8_t> rgb = readback.rgb();
        if (std::fwrite(rgb.data(), 1, rgb.size(), stdout) != rgb.size())
            throw std::runtime_error("writing frame " + std::to_string(i) + " to stdout failed");
        if ((i + 1) % 60 == 0 || i + 1 == frames)
            std::fprintf(stderr, "render-stream: frame %ld/%ld\n", i + 1, frames);
    }
    if (std::fflush(stdout) != 0)
        throw std::runtime_error("flushing stdout failed");
    return 0;
}

int runDeviceInfo(const Options& options) {
    gfx::ContextOptions contextOptions;
    contextOptions.validation = options.validation;
    const gfx::Context ctx = gfx::Context::create(contextOptions);
    std::printf("%s\n", ctx.deviceInfoJson().c_str());
    return 0;
}

} // namespace evo
