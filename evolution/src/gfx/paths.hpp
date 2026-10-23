#pragma once

#include <filesystem>

namespace evo::gfx {

// Directory holding the compiled SPIR-V: <executable dir>/shaders, or $EVO_SHADER_DIR when set (tests).
std::filesystem::path shaderDir();

} // namespace evo::gfx
