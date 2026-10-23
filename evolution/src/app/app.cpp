#include "app/app.hpp"

#include "gfx/context.hpp"
#include "gfx/offscreen.hpp"
#include "gfx/swapchain.hpp"
#include "gfx/vk_check.hpp"

#include <SDL3/SDL.h>
#include <SDL3/SDL_vulkan.h>

#include <cmath>
#include <cstdio>
#include <memory>
#include <stdexcept>
#include <string>

#ifdef _WIN32
#include <fcntl.h>
#include <io.h>
#endif

namespace evo {

namespace {

constexpr Uint32 kMinimisedPollMs = 50;

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

void toggleFullscreen(SDL_Window* window) {
    const bool fullscreen = (SDL_GetWindowFlags(window) & SDL_WINDOW_FULLSCREEN) != 0;
    if (!SDL_SetWindowFullscreen(window, !fullscreen))
        std::fprintf(stderr, "warning: cannot toggle fullscreen: %s\n", SDL_GetError());
}

} // namespace

std::string missingRequirementsMessage(const gfx::MissingRequirements& missing) {
    std::string list;
    for (const auto& name : missing.missing)
        list += (list.empty() ? "" : ", ") + name;
    return "EVOLUTION needs a Vulkan 1.3 GPU with ray tracing. Missing: " + list;
}

int runWindowed(const Options& options) {
    SdlVideo video;
    Window window(options);
    gfx::Context ctx = createWindowedContext(options);
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

    // Declared before the swapchain so it is destroyed after it: ~Swapchain waits for the device, so no
    // blit can still be reading the offscreen image when it goes.
    std::unique_ptr<gfx::Offscreen> offscreen; // sized to the drawable; created on the first visible frame
    gfx::Swapchain swapchain(ctx, surface.handle, width, height);
    bool needRecreate = swapchain.extent().width == 0; // started minimised

    const Uint64 start = SDL_GetTicksNS();
    for (bool running = true; running;) {
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            switch (event.type) {
            case SDL_EVENT_QUIT:
                running = false;
                break;
            case SDL_EVENT_KEY_DOWN:
                if (event.key.key == SDLK_ESCAPE)
                    running = false;
                else if (event.key.key == SDLK_F11 && !event.key.repeat)
                    toggleFullscreen(window.handle);
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
        if (!offscreen || offscreen->width() != width || offscreen->height() != height) {
            // The previous frame's blit may still be reading the old image.
            gfx::vkCheck(vkDeviceWaitIdle(ctx.device()), "vkDeviceWaitIdle");
            offscreen.reset();
            offscreen = std::make_unique<gfx::Offscreen>(ctx, width, height);
        }

        offscreen->render(options.time + elapsed);
        if (!swapchain.present(offscreen->image(), offscreen->width(), offscreen->height()))
            needRecreate = true;
    }

    gfx::vkCheck(vkDeviceWaitIdle(ctx.device()), "vkDeviceWaitIdle");
    return 0;
}

int runRenderStream(const Options& options) {
#ifdef _WIN32
    _setmode(_fileno(stdout), _O_BINARY); // raw bytes: no CRLF translation
#endif
    gfx::ContextOptions contextOptions;
    contextOptions.validation = options.validation;
    gfx::Context ctx = gfx::Context::create(contextOptions);
    gfx::Offscreen offscreen(ctx, options.width, options.height);

    // The epsilon keeps products like 0.7 * 10 = 7.000000000000001 from adding a frame.
    const long frames = static_cast<long>(std::ceil(options.duration * options.fps - 1e-9));
    if (frames < 1)
        throw std::runtime_error("--duration is shorter than one frame"); // parseOptions rejects only 0
    for (long i = 0; i < frames; ++i) {
        offscreen.render(options.time + static_cast<double>(i) / options.fps);
        const gfx::Frame frame = offscreen.readback();
        if (std::fwrite(frame.rgb.data(), 1, frame.rgb.size(), stdout) != frame.rgb.size())
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
