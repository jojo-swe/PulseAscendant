# Build-time GLSL -> SPIR-V compilation. There is no runtime shader compiler: every shader is compiled
# here straight into <build>/bin/shaders/<name>.spv, beside the executable that loads it.

find_program(EVO_GLSLANG glslangValidator HINTS $ENV{VULKAN_SDK}/Bin $ENV{VULKAN_SDK}/bin REQUIRED)

# The executables that load shaders live here. The "$<0:>" generator expression stops multi-config
# generators (Visual Studio) from appending a per-configuration subdirectory, so the executable and its
# shaders share one directory on every generator and a shader-only edit reaches the executable without a
# relink or a copy step.
set(EVO_RUNTIME_DIR "${CMAKE_BINARY_DIR}/bin")
set(EVO_SHADER_DIR "${EVO_RUNTIME_DIR}/shaders")

# Deletes .spv files in EVO_SHADER_DIR that no evo_add_shaders() call produces any more, so a renamed or
# removed shader cannot linger beside the executable. Runs once, at the end of configuring.
function(_evo_remove_stale_shaders)
    get_property(expected GLOBAL PROPERTY EVO_SPIRV_FILES)
    file(GLOB existing "${EVO_SHADER_DIR}/*.spv")
    foreach(spirv IN LISTS existing)
        if(NOT spirv IN_LIST expected)
            message(STATUS "Removing stale shader ${spirv}")
            file(REMOVE "${spirv}")
        endif()
    endforeach()
endfunction()

# evo_add_shaders(<target> <shader>...)
# Shader paths are relative to the calling CMakeLists.txt. Also places <target> in EVO_RUNTIME_DIR.
function(evo_add_shaders target)
    set(spirvFiles "")

    foreach(shader IN LISTS ARGN)
        cmake_path(ABSOLUTE_PATH shader BASE_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}" OUTPUT_VARIABLE source)
        cmake_path(GET source FILENAME name)
        set(spirv "${EVO_SHADER_DIR}/${name}.spv")

        add_custom_command(
            OUTPUT "${spirv}"
            COMMAND "${CMAKE_COMMAND}" -E make_directory "${EVO_SHADER_DIR}"
            COMMAND "${EVO_GLSLANG}" -V --target-env vulkan1.3 -o "${spirv}" "${source}"
            DEPENDS "${source}"
            COMMENT "Compiling shader ${name}"
            VERBATIM)
        list(APPEND spirvFiles "${spirv}")
    endforeach()

    add_custom_target(${target}_shaders DEPENDS ${spirvFiles})
    add_dependencies(${target} ${target}_shaders)
    set_target_properties(${target} PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${EVO_RUNTIME_DIR}/$<0:>")

    set_property(GLOBAL APPEND PROPERTY EVO_SPIRV_FILES ${spirvFiles})
    get_property(cleanupScheduled GLOBAL PROPERTY EVO_STALE_SHADER_CLEANUP)
    if(NOT cleanupScheduled)
        set_property(GLOBAL PROPERTY EVO_STALE_SHADER_CLEANUP TRUE)
        cmake_language(DEFER DIRECTORY "${CMAKE_SOURCE_DIR}" CALL _evo_remove_stale_shaders)
    endif()
endfunction()
