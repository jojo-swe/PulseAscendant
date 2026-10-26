#pragma once

#include <filesystem>
#include <string>
#include <string_view>

namespace evo::gfx {

// Directory holding the compiled SPIR-V: <executable dir>/shaders, or $EVO_SHADER_DIR when set (tests).
std::filesystem::path shaderDir();

// The pipeline cache file of the program's runs (ContextOptions::pipelineCacheFile): pipeline_cache.bin
// beside the shader directory, or $EVO_PIPELINE_CACHE when set ("0": none).
std::filesystem::path pipelineCachePath();

// A path from UTF-8 text: command-line arguments (UTF-8 on every platform, see main.cpp) and the paths SDL
// returns. Constructing a path from a plain std::string would read it in the Windows ANSI code page.
inline std::filesystem::path pathFromUtf8(std::string_view utf8) {
    return std::filesystem::path(
        std::u8string_view(reinterpret_cast<const char8_t*>(utf8.data()), utf8.size()));
}

// A path as UTF-8 text, for messages. path::string() can throw on Windows for names the ANSI code page
// cannot hold.
inline std::string pathToUtf8(const std::filesystem::path& path) {
    const std::u8string utf8 = path.u8string();
    return std::string(utf8.begin(), utf8.end());
}

} // namespace evo::gfx
