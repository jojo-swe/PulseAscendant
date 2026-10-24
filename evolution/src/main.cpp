#include "app/app.hpp"
#include "app/capture.hpp"
#include "app/options.hpp"
#include "gfx/context.hpp"
#include "modern/modern_scene.hpp"
#include "show/timeline.hpp"

#include <cstdio>
#include <exception>
#include <string>
#include <utility>
#include <vector>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

#include <shellapi.h>
#endif

namespace {

// The arguments after the program name, as UTF-8. On Windows argv is in the ANSI code page, which cannot
// hold every file name, so the arguments are read again from the UTF-16 command line.
std::vector<std::string> utf8Arguments(int argc, char** argv) {
#ifdef _WIN32
    int count = 0;
    if (LPWSTR* wide = CommandLineToArgvW(GetCommandLineW(), &count)) {
        std::vector<std::string> args;
        for (int i = 1; i < count; ++i) {
            const int size = WideCharToMultiByte(CP_UTF8, 0, wide[i], -1, nullptr, 0, nullptr, nullptr);
            std::string arg(size > 0 ? size - 1 : 0, '\0'); // size counts the terminating null
            if (size > 1)
                WideCharToMultiByte(CP_UTF8, 0, wide[i], -1, arg.data(), size, nullptr, nullptr);
            args.push_back(std::move(arg));
        }
        LocalFree(wide);
        return args;
    }
#endif
    return std::vector<std::string>(argv + 1, argv + argc);
}

// Exit codes: 0 success, 1 error, 2 the machine cannot run EVOLUTION. main() adds 3.
int run(const evo::Options& options) {
    try {
        if (options.deviceInfo)
            return evo::runDeviceInfo(options);
        if (options.renderStream)
            return evo::runRenderStream(options);
        if (!options.captureDir.empty())
            return evo::runCapture(options);
        if (options.headless) {
            std::fputs("Evolution: --headless needs an output: --render-stream or --capture-dir.\n", stderr);
            return 1;
        }
        return evo::runWindowed(options);
    } catch (const evo::gfx::MissingRequirements& missing) {
        std::fprintf(stderr, "%s\n", evo::missingRequirementsMessage(missing).c_str());
        return 2;
    } catch (const std::exception& error) {
        std::fprintf(stderr, "Evolution: %s\n", error.what());
        return 1;
    }
}

} // namespace

int main(int argc, char** argv) {
    evo::Options options;
    try {
        options = evo::parseOptions(utf8Arguments(argc, argv));
    } catch (const std::exception& error) {
        std::fprintf(stderr, "Evolution: %s\nRun 'Evolution --help' for usage.\n", error.what());
        return 1;
    }

    if (options.help) {
        std::fputs(evo::helpText().c_str(), stdout);
        return 0;
    }
    // Needs neither SDL video nor Vulkan: the show is compiled in.
    if (options.showInfo) {
        std::printf("%s\n", evo::show::showInfoJson(evo::modern::effectSceneName).c_str());
        return 0;
    }

    int code = run(options);

    // Read after the Context is gone, so problems found while destroying it count too. With --validation
    // any warning or error fails an otherwise successful run (exit code 3), so tests cannot miss one.
    if (options.validation) {
        const unsigned messages = evo::gfx::validationMessagesReported();
        if (messages > 0) {
            std::fprintf(stderr, "Evolution: %u validation messages\n", messages);
            if (code == 0)
                code = 3;
        }
    }
    return code;
}
