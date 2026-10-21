#include "app/options.hpp"

#include <cstdio>
#include <exception>
#include <string>
#include <vector>

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

    std::fputs("Evolution: rendering is not implemented yet. Run 'Evolution --help' for usage.\n", stderr);
    return 0;
}
