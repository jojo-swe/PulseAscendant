#include "app/app.hpp"

#include "app/capture.hpp"
#include "app/clock.hpp"
#include "gfx/context.hpp"
#include "gfx/frame_ring.hpp"
#include "gfx/paths.hpp"
#include "gfx/readback.hpp"
#include "gfx/swapchain.hpp"
#include "gfx/vk_check.hpp"
#include "modern/modern_scene.hpp"
#include "render/renderer.hpp"
#include "render/secret_renderer.hpp"
#include "show/music_curves.hpp"
#include "show/timeline.hpp"

#include <SDL3/SDL.h>
#include <SDL3/SDL_vulkan.h>

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <memory>
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

// The applause device. Four keys, two seconds, in order. Esc brings you back.
constexpr std::string_view kSecretWord = "CLAP";
constexpr double kSecretWordSeconds = 2.0;
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

// A letter key as its upper-case letter, anything else as 0.
char letterOf(SDL_Keycode key) {
    return key >= SDLK_A && key <= SDLK_Z ? static_cast<char>('A' + (key - SDLK_A)) : '\0';
}

// The next tracked beat after show time `now`, in show seconds (now itself past the track's last beat).
double nextBeat(double now) {
    const int track = show::trackAt(now);
    const double start = show::trackStart(track);
    const show::BeatGridView grid = show::beatGrid(track);
    const int beat = grid.beatAt(now - start) + 1;
    return beat < grid.count() ? start + grid.beatTime(beat) : now;
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

retro::KeyWord secretKeyWord() {
    return retro::KeyWord(kSecretWord, kSecretWordSeconds);
}

SecretSession::SecretSession(std::string_view answer)
    : keys_(secretKeyWord()), solvedUpFront_(retro::isPatientAnswer(answer)) {}

void SecretSession::start(double resumeAt) {
    state_ = State::Part;
    resumeAt_ = resumeAt;
    input_ = {};
    if (solvedUpFront_)
        input_.solvedAt = retro::secretMarks().puzzle;
    answer_ = {};
}

bool SecretSession::showKey(char key, double now) {
    if (state_ != State::Show || !keys_.press(key, now))
        return false;
    state_ = State::Pending;
    cutAt_ = nextBeat(now);
    return true;
}

void SecretSession::partKey(char key, double t) {
    if (state_ != State::Part)
        return;
    if (key == 'L' && input_.applauseAt < 0 && t >= retro::secretMarks().lastDevice)
        input_.applauseAt = t;
    if (answer_.press(key) && input_.solvedAt < 0)
        input_.solvedAt = t;
}

double SecretSession::escape() {
    state_ = State::Show;
    keys_.reset();
    return resumeAt_;
}

std::optional<double> SecretSession::update(double now) {
    if (state_ == State::Pending && (now >= cutAt_ || now < cutAt_ - 1.0)) { // a seek away cuts at once
        start(now);
        return 0.0;
    }
    if (state_ == State::Part && now >= retro::secretDuration(input_))
        return escape();
    return std::nullopt;
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
    // shape; only the window draws the chapter title in its HUD. Declared after the ring (its destructor
    // waits for the ring) and before the swapchain, so it is destroyed after it: ~Swapchain waits for the
    // device, so no frame can still be using it.
    gfx::FrameRing ring(ctx);
    // Interactive: the window keeps temporal history between frames, at --spp per frame (1 by default).
    render::RenderSettings settings;
    settings.width = static_cast<std::uint32_t>(width > 0 ? width : options.width);
    settings.height = static_cast<std::uint32_t>(height > 0 ? height : options.height);
    settings.crt = options.crt;
    settings.hud = options.hud;
    settings.hudTitle = true;
    settings.mode = render::RenderMode::Interactive;
    settings.spp = static_cast<std::uint32_t>(samplesPerPixel(options));
    settings.detail = modern::SceneDetail{options.detail};
    settings.traceScale = traceScaleFor(options); // 0.67 by default: traced below the window size, upscaled
    render::Renderer renderer(ctx, ring, settings);
    renderer.prepareAll(); // every scene's BLASes and the pipeline now, not as a hitch at a chapter start
    gfx::Swapchain swapchain(ctx, surface.handle, width, height);
    bool needRecreate = swapchain.extent().width == 0; // started minimised

    // The secret part: called up by its four keys, or from the start with --secret-part, when the music
    // starts at its beginning and the show resumes at --time afterwards. Its renderer is built when needed.
    SecretSession secret(options.secretAnswer);
    std::unique_ptr<render::SecretRenderer> secretRenderer;
    if (options.secretPart)
        secret.start(options.time);

    // Started last, so the music begins with the first frame rather than during the device setup.
    ShowClock clock(!options.noAudio, musicPath(options), show::showDuration(),
                    options.secretPart ? 0.0 : options.time, options.audioOffsetMs / 1000.0);
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
                if (event.key.repeat) {
                    break; // a held key acts once
                } else if (secret.state() == SecretSession::State::Part) {
                    if (event.key.key == SDLK_ESCAPE)
                        clock.seek(secret.escape());
                    else if (event.key.key == SDLK_M)
                        clock.setMuted(muted = !muted);
                    else if (event.key.key == SDLK_F11)
                        toggleFullscreen(window.handle);
                    else
                        secret.partKey(letterOf(event.key.key), clock.now());
                    break;
                } else if (secret.showKey(letterOf(event.key.key), clock.now())) {
                    renderer.setCrt(!renderer.crt()); // the word's C toggled the CRT look: undo it
                    break;
                }
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

        if (const std::optional<double> seek = secret.update(clock.now()))
            clock.seek(*seek);
        if (secret.state() == SecretSession::State::Part) {
            if (!secretRenderer)
                secretRenderer = std::make_unique<render::SecretRenderer>(
                    ctx, ring, static_cast<std::uint32_t>(width), static_cast<std::uint32_t>(height));
            secretRenderer->resize(static_cast<std::uint32_t>(width), static_cast<std::uint32_t>(height));
            if (title != "EVOLUTION") {
                title = "EVOLUTION";
                SDL_SetWindowTitle(window.handle, title.c_str());
            }
            VkCommandBuffer cmd = ring.begin();
            secretRenderer->render(cmd, clock.now(), secret.input());
            if (!swapchain.present(ring, cmd, secretRenderer->output()))
                needRecreate = true;
            continue;
        }

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

namespace {

// --render-stream --secret-part: the part from --time for --duration, as its retro frames and the CRT make
// it; headless never presses L, and --secret-part=WORD shows the answer when WORD is the patient's.
int runSecretStream(const Options& options, gfx::Context& ctx, long frames) {
    const auto width = static_cast<std::uint32_t>(options.width);
    const auto height = static_cast<std::uint32_t>(options.height);
    gfx::Readback readback(ctx, width, height);
    gfx::FrameRing ring(ctx);
    render::SecretRenderer renderer(ctx, ring, width, height);
    SecretSession session(options.secretAnswer);
    session.start(0.0);
    for (long i = 0; i < frames; ++i) {
        VkCommandBuffer cmd = ring.begin();
        renderer.render(cmd, options.time + static_cast<double>(options.firstFrame + i) / options.fps,
                        session.input(), options.crt);
        readback.record(cmd, renderer.output());
        ring.wait(ring.submit());
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

} // namespace

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
    if (options.secretPart)
        return runSecretStream(options, ctx, frames);

    const auto width = static_cast<std::uint32_t>(options.width);
    const auto height = static_cast<std::uint32_t>(options.height);
    gfx::Readback readback(ctx, width, height);
    gfx::FrameRing ring(ctx);
    // Export: every frame a pure function of its show time (and --spp, 64 by default); no title, cards only.
    render::RenderSettings settings{width, height, options.crt, options.hud};
    settings.mode = render::RenderMode::Export;
    settings.spp = static_cast<std::uint32_t>(samplesPerPixel(options));
    settings.shutterSeconds = 0.5 / options.fps; // a 180 degree shutter; the window has none
    settings.detail = modern::SceneDetail{options.detail};
    settings.submitPerSlice = true; // one submit per time slice: no frame-long submit to trip a GPU watchdog
    render::Renderer renderer(ctx, ring, settings);
    // The GPU side of every path-traced scene the range shows (BLASes, and on the first the pipeline and the
    // shared images) as setup, before the first frame.
    std::vector<double> times(static_cast<std::size_t>(frames));
    for (long i = 0; i < frames; ++i)
        times[static_cast<std::size_t>(i)] =
            options.time + static_cast<double>(options.firstFrame + i) / options.fps;
    renderer.prepare(times);
    for (long i = 0; i < frames; ++i) {
        VkCommandBuffer cmd = ring.begin();
        show::TimelineFrame frame =
            show::sampleTimeline(options.time + static_cast<double>(options.firstFrame + i) / options.fps);
        // An export forced into one era says so once, so a forced file is never mistaken for the show.
        if (applyForcedEra(frame) && i == 0)
            std::fprintf(stderr, "render-stream: EVO_FORCE_ERA=%s (%s only)\n", frame.era > .5f ? "1" : "0",
                         frame.era > .5f ? "2026" : "1993");
        renderer.render(cmd, frame);
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
