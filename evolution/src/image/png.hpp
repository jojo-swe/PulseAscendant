#pragma once

#include <cstdint>
#include <filesystem>

namespace evo::image {

// Writes an 8-bit RGB PNG without compression. rgb is row-major top-down, 3 bytes per pixel,
// size == width*height*3. Creates parent directories. Throws std::runtime_error on failure.
void writePng(const std::filesystem::path& path, int width, int height, const std::uint8_t* rgb);

} // namespace evo::image
