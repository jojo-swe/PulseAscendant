#include "gfx/swapchain.hpp"

#include <cstdint>
#include <cstdio>
#include <stdexcept>

namespace {

int checksPassed = 0;

#define CHECK(x)                                                                                             \
    do {                                                                                                     \
        if (!(x))                                                                                            \
            throw std::runtime_error(#x);                                                                    \
        ++checksPassed;                                                                                      \
    } while (false)

bool operator==(VkExtent2D a, VkExtent2D b) {
    return a.width == b.width && a.height == b.height;
}

constexpr std::uint32_t kSurfaceDefined = UINT32_MAX;

VkSurfaceCapabilitiesKHR caps(VkExtent2D current) {
    return {.currentExtent = current, .minImageExtent = {1, 1}, .maxImageExtent = {4096, 4096}};
}

} // namespace

int main() {
    using evo::gfx::chooseExtent;
    try {
        // The surface lets the swapchain decide: the desired size is used as is.
        CHECK((chooseExtent(caps({kSurfaceDefined, kSurfaceDefined}), 1280, 720) == VkExtent2D{1280, 720}));
        // The surface dictates its size: the desired size is ignored.
        CHECK((chooseExtent(caps({800, 600}), 1280, 720) == VkExtent2D{800, 600}));
        // Minimised: 0x0 tells the caller to skip the frame.
        CHECK((chooseExtent(caps({0, 0}), 1280, 720) == VkExtent2D{0, 0}));
        // Clamped into the surface's limits.
        CHECK((chooseExtent(caps({kSurfaceDefined, kSurfaceDefined}), 9000, 9000) == VkExtent2D{4096, 4096}));
        // A zero-sized drawable on a surface-defined extent is also "minimised", not clamped up to 1x1.
        CHECK((chooseExtent(caps({kSurfaceDefined, kSurfaceDefined}), 0, 0) == VkExtent2D{0, 0}));
    } catch (const std::exception& error) {
        std::fprintf(stderr, "CHECK failed: %s\n", error.what());
        return 1;
    }
    std::printf("swapchain_test: %d checks passed\n", checksPassed);
    return 0;
}
