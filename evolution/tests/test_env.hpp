// Setting an environment variable in a test (tests set EVO_SHADER_DIR before they make any pipeline), the
// same way on POSIX and Windows.

#pragma once

#include <cstdlib>

namespace evo::test {

inline void setEnv(const char* name, const char* value) {
#ifdef _WIN32
    _putenv_s(name, value);
#else
    setenv(name, value, 1);
#endif
}

} // namespace evo::test
