#include "gfx/context.hpp"

#include <algorithm>
#include <cstdio>
#include <stdexcept>
#include <string>

namespace {

int checksPassed = 0;

#define CHECK(x)                                                                                             \
    do {                                                                                                     \
        if (!(x))                                                                                            \
            throw std::runtime_error(#x);                                                                    \
        ++checksPassed;                                                                                      \
    } while (false)

void testCreatesRayTracingContext() {
    auto ctx = evo::gfx::Context::create({});
    std::printf("device: %s\n%s\n", ctx.deviceName().c_str(), ctx.deviceInfoJson().c_str());
    CHECK(!ctx.deviceName().empty());
    CHECK(ctx.apiVersion() >= VK_API_VERSION_1_3);
    CHECK(ctx.deviceInfoJson().find("\"rayTracing\":true") != std::string::npos);
    ctx.immediate([](VkCommandBuffer) {});
    ++checksPassed;
}

void testReportsMissingExtension() {
    bool threw = false;
    try {
        evo::gfx::Context::create({.extraDeviceExtensions = {"VK_EVO_does_not_exist"}});
    } catch (const evo::gfx::MissingRequirements& error) {
        threw = true;
        CHECK(std::find(error.missing.begin(), error.missing.end(), "VK_EVO_does_not_exist") !=
              error.missing.end());
        CHECK(std::string(error.what()).find("VK_EVO_does_not_exist") != std::string::npos);
    }
    CHECK(threw);
}

} // namespace

int main() {
    try {
        testCreatesRayTracingContext();
        testReportsMissingExtension();
    } catch (const std::exception& error) {
        std::fprintf(stderr, "FAILED: %s\n", error.what());
        return 1;
    }
    std::printf("context_test: %d checks passed\n", checksPassed);
    return 0;
}
