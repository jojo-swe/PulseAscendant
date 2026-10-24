#include "gfx/swapchain.hpp"

#include <cstdint>
#include <cstdio>
#include <stdexcept>
#include <vector>

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

        // The surface format: B8G8R8A8_UNORM, then R8G8B8A8_UNORM, then any UNORM, then the first entry.
        using evo::gfx::chooseSurfaceFormat;
        const auto format = [](VkFormat f, VkColorSpaceKHR space = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) {
            return VkSurfaceFormatKHR{f, space};
        };
        const auto chosen = [&](std::vector<VkSurfaceFormatKHR> formats) {
            return chooseSurfaceFormat(formats).format;
        };
        CHECK(chosen({format(VK_FORMAT_B8G8R8A8_SRGB), format(VK_FORMAT_R8G8B8A8_UNORM),
                      format(VK_FORMAT_B8G8R8A8_UNORM)}) == VK_FORMAT_B8G8R8A8_UNORM);
        CHECK(chosen({format(VK_FORMAT_B8G8R8A8_SRGB), format(VK_FORMAT_A2B10G10R10_UNORM_PACK32),
                      format(VK_FORMAT_R8G8B8A8_UNORM)}) == VK_FORMAT_R8G8B8A8_UNORM);
        CHECK(chosen({format(VK_FORMAT_R8G8B8A8_SRGB), format(VK_FORMAT_R16G16B16A16_SFLOAT),
                      format(VK_FORMAT_A2B10G10R10_UNORM_PACK32), format(VK_FORMAT_R5G6B5_UNORM_PACK16)}) ==
              VK_FORMAT_A2B10G10R10_UNORM_PACK32);
        CHECK(chosen({format(VK_FORMAT_R8G8B8A8_SRGB), format(VK_FORMAT_R16G16B16A16_SFLOAT)}) ==
              VK_FORMAT_R8G8B8A8_SRGB);
        CHECK(chosen({format(VK_FORMAT_B8G8R8A8_SRGB)}) == VK_FORMAT_B8G8R8A8_SRGB);
        // The chosen entry keeps its colour space.
        const std::vector<VkSurfaceFormatKHR> spaces = {
            format(VK_FORMAT_B8G8R8A8_SRGB),
            format(VK_FORMAT_B8G8R8A8_UNORM, VK_COLOR_SPACE_DISPLAY_P3_NONLINEAR_EXT)};
        CHECK(chooseSurfaceFormat(spaces).colorSpace == VK_COLOR_SPACE_DISPLAY_P3_NONLINEAR_EXT);
        bool threw = false;
        try {
            chooseSurfaceFormat({});
        } catch (const std::runtime_error&) {
            threw = true;
        }
        CHECK(threw);
    } catch (const std::exception& error) {
        std::fprintf(stderr, "CHECK failed: %s\n", error.what());
        return 1;
    }
    std::printf("swapchain_test: %d checks passed\n", checksPassed);
    return 0;
}
