#pragma once

#include "app/options.hpp"

#include <SDL3/SDL_keycode.h>

#include <string>

namespace evo::gfx {
struct MissingRequirements;
}

namespace evo {

// Each run function returns the process exit code and lets gfx::MissingRequirements escape; main() turns
// that into exit code 2 with missingRequirementsMessage() on stderr.

// Opens an SDL3 window (Vulkan, resizable) and presents the show until Esc, the window closes, or
// --duration elapses. The time comes from a ShowClock (the music, or SILENT MODE on the system timer); its
// status goes to stderr at the start. Keys: Left/Right previous/next chapter, 1-9 and 0 the first ten
// chapters, M mute, F11 fullscreen. The title shows the chapter and the show time. Returns the process exit
// code.
int runWindowed(const Options& options);

// Headless: renders frames 0..ceil(duration*fps)-1 at t = time + i/fps and writes each as raw top-down RGB
// to stdout. Progress goes to stderr.
int runRenderStream(const Options& options);

// Prints Context::deviceInfoJson() and a newline.
int runDeviceInfo(const Options& options);

// The chapter a key in the window jumps to at show time now, or -1 for other keys. Left/Right step to the
// previous/next chapter and wrap around the show; 1-9 and 0 pick chapters 0-9 in keyboard order. A time
// just before a chapter's start counts as in it, so a key pressed right after a jump steps from there.
int chapterForKey(SDL_Keycode key, double now);

// The window title at show time now: "EVOLUTION  <chapter title>  <mm:ss>".
std::string windowTitle(double now);

// "EVOLUTION needs a Vulkan 1.3 GPU with ray tracing. Missing: a, b"
std::string missingRequirementsMessage(const gfx::MissingRequirements& missing);

} // namespace evo
