#include "gfx/context.hpp"

#include <algorithm>
#include <cstdio>
#include <stdexcept>
#include <string>
#include <utility>

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

// volk's function pointers are process-wide, so a second live Context is refused.
void testRefusesSecondLiveContext() {
    auto ctx = evo::gfx::Context::create({});
    auto moved = std::move(ctx); // the slot moves with the Context
    bool threw = false;
    try {
        evo::gfx::Context::create({});
    } catch (const std::logic_error&) {
        threw = true;
    }
    CHECK(threw);
}

// --validation can only fail a run if invalid usage is counted. Runs after the tests above, so it also
// shows that a Context which threw or was refused gave its slot back.
void testCountsValidationMessages() {
    auto ctx = evo::gfx::Context::create({.validation = true});
    if (!ctx.validationEnabled()) {
        std::printf("SKIP testCountsValidationMessages: validation layer unavailable\n");
        return;
    }
    const unsigned before = evo::gfx::validationMessagesReported();

    VkBufferCreateInfo info{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
    info.size = 0; // invalid: VUID-VkBufferCreateInfo-size-00912
    info.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
    std::printf("expecting one validation error about a zero-sized buffer:\n");
    std::fflush(stdout);
    VkBuffer buffer = VK_NULL_HANDLE;
    if (vkCreateBuffer(ctx.device(), &info, nullptr, &buffer) == VK_SUCCESS)
        vkDestroyBuffer(ctx.device(), buffer, nullptr);

    CHECK(ctx.validationMessageCount() >= 1);
    CHECK(evo::gfx::validationMessagesReported() > before);
}

} // namespace

int main() {
    try {
        testCreatesRayTracingContext();
        testReportsMissingExtension();
        testRefusesSecondLiveContext();
        testCountsValidationMessages();
    } catch (const std::exception& error) {
        std::fprintf(stderr, "FAILED: %s\n", error.what());
        return 1;
    }
    std::printf("context_test: %d checks passed\n", checksPassed);
    return 0;
}
