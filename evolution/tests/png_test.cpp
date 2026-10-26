#include "image/png.hpp"

#include <array>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
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

} // namespace

int main() {
    try {
        // 3x2 image, rows top-down.
        const std::array<std::uint8_t, 3 * 2 * 3> rgb = {
            255, 0, 0, 0, 255, 0, 0,   0,   255, // row 0: red, green, blue
            0,   0, 0, 128, 128, 128, 255, 255, 255, // row 1: black, grey, white
        };
        const std::filesystem::path path =
            std::filesystem::path(EVO_TEST_OUT_DIR) / "png" / "nested" / "dir" / "test.png";
        std::filesystem::remove(path);

        evo::image::writePng(path, 3, 2, rgb.data());

        CHECK(std::filesystem::exists(path));

        std::ifstream file(path, std::ios::binary);
        const std::vector<std::uint8_t> bytes{std::istreambuf_iterator<char>(file),
                                              std::istreambuf_iterator<char>()};
        const std::array<std::uint8_t, 8> signature = {0x89, 'P', 'N', 'G', '\r', '\n', 0x1a, '\n'};
        CHECK(bytes.size() >= signature.size());
        CHECK(std::equal(signature.begin(), signature.end(), bytes.begin()));

        // signature + IHDR + IDAT{zlib header, one stored block header, 2 rows of (filter + 9), Adler} + IEND
        const std::size_t expectedSize = 8 + (12 + 13) + (12 + 2 + 5 + (2 * (1 + 9)) + 4) + 12;
        CHECK(bytes.size() == expectedSize);
    } catch (const std::exception& error) {
        std::fprintf(stderr, "CHECK failed: %s\n", error.what());
        return 1;
    }
    std::printf("%d checks passed\n", checksPassed);
    return 0;
}
