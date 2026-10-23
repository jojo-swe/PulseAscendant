#include "app/app.hpp"
#include "app/capture.hpp"
#include "app/options.hpp"
#include "gfx/context.hpp"

#include <cstdio>
#include <exception>
#include <string>
#include <vector>

namespace {

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
        options = evo::parseOptions(std::vector<std::string>(argv + 1, argv + argc));
    } catch (const std::exception& error) {
        std::fprintf(stderr, "Evolution: %s\nRun 'Evolution --help' for usage.\n", error.what());
        return 1;
    }

    if (options.help) {
        std::fputs(evo::helpText().c_str(), stdout);
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
