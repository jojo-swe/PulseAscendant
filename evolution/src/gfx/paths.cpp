#include "gfx/paths.hpp"

#include <SDL3/SDL_error.h>
#include <SDL3/SDL_filesystem.h>

#include <cstdlib>
#include <stdexcept>
#include <string>

namespace evo::gfx {

std::filesystem::path shaderDir() {
    if (const char* overridden = std::getenv("EVO_SHADER_DIR"); overridden && *overridden)
        return overridden;

    // SDL owns the returned string; it ends with a path separator.
    const char* base = SDL_GetBasePath();
    if (!base)
        throw std::runtime_error(std::string("cannot locate the executable directory: ") + SDL_GetError());
    return std::filesystem::path(base) / "shaders";
}

} // namespace evo::gfx
