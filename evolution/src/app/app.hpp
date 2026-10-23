#pragma once

#include "app/options.hpp"

#include <string>

namespace evo::gfx {
struct MissingRequirements;
}

namespace evo {

// Opens an SDL3 window (Vulkan, resizable) and presents the test pattern until Esc, the window closes, or
// --duration elapses. F11 toggles fullscreen. Returns the process exit code.
int runWindowed(const Options& options);

// Headless: renders frames 0..ceil(duration*fps)-1 at t = time + i/fps and writes each as raw top-down RGB
// to stdout. Progress goes to stderr.
int runRenderStream(const Options& options);

// Prints Context::deviceInfoJson() and a newline. Returns 2 (with the message on stderr) when the GPU
// lacks a requirement.
int runDeviceInfo(const Options& options);

// "EVOLUTION needs a Vulkan 1.3 GPU with ray tracing. Missing: a, b"
std::string missingRequirementsMessage(const gfx::MissingRequirements& missing);

} // namespace evo
