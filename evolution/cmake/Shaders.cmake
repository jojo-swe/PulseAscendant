# Build-time GLSL -> SPIR-V compilation. There is no runtime shader compiler: every shader is compiled
# here straight into <build>/bin/shaders/<name>.spv, beside the executable that loads it.

find_program(EVO_GLSLANG glslangValidator HINTS $ENV{VULKAN_SDK}/Bin $ENV{VULKAN_SDK}/bin REQUIRED)

# The executables that load shaders live here. The "$<0:>" generator expression stops multi-config
# generators (Visual Studio) from appending a per-configuration subdirectory, so the executable and its
# shaders share one directory on every generator and a shader-only edit reaches the executable without a
# relink or a copy step.
set(EVO_RUNTIME_DIR "${CMAKE_BINARY_DIR}/bin")
set(EVO_SHADER_DIR "${EVO_RUNTIME_DIR}/shaders")
# The GLSL sources; subdirectories of it name their SPIR-V with a prefix (see evo_add_shaders).
set(EVO_SHADER_SOURCE_DIR "${CMAKE_CURRENT_LIST_DIR}/../shaders")
cmake_path(NORMAL_PATH EVO_SHADER_SOURCE_DIR)

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

# _evo_shader_rule(<outVar> <shader> <suffix> <defines>): the custom command compiling one shader into
# EVO_SHADER_DIR; sets <outVar> to its SPIR-V path. EVO_SHADER_DIR stays flat: a shader in a subdirectory of
# shaders/ is named after its path there, with "_" for "/" (shaders/retro/plasma.comp -> retro_plasma.comp.spv),
# which is the name the code loads. A non-empty suffix goes before the stage extension (rt/pbr.rchit with
# suffix "sphere" -> rt_pbr_sphere.rchit.spv), and each of <defines> (NAME or NAME=VALUE) becomes a -D. glslang
# writes a depfile, so editing a file a shader #includes recompiles it.
function(_evo_shader_rule outVar shader suffix defines)
    cmake_path(ABSOLUTE_PATH shader BASE_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}" OUTPUT_VARIABLE source)
    cmake_path(IS_PREFIX EVO_SHADER_SOURCE_DIR "${source}" NORMALIZE underShaders)
    if(underShaders)
        cmake_path(RELATIVE_PATH source BASE_DIRECTORY "${EVO_SHADER_SOURCE_DIR}" OUTPUT_VARIABLE name)
        string(REPLACE "/" "_" name "${name}")
    else()
        cmake_path(GET source FILENAME name)
    endif()
    if(suffix)
        cmake_path(GET name EXTENSION LAST_ONLY stage)
        cmake_path(REMOVE_EXTENSION name LAST_ONLY OUTPUT_VARIABLE stem)
        set(name "${stem}_${suffix}${stage}")
    endif()
    set(flags "")
    foreach(define IN LISTS defines)
        list(APPEND flags "-D${define}")
    endforeach()
    set(spirv "${EVO_SHADER_DIR}/${name}.spv")
    set(depDir "${CMAKE_CURRENT_BINARY_DIR}/shader-deps")
    set(depfile "${depDir}/${name}.d")

    add_custom_command(
        OUTPUT "${spirv}"
        COMMAND "${CMAKE_COMMAND}" -E make_directory "${EVO_SHADER_DIR}" "${depDir}"
        COMMAND "${EVO_GLSLANG}" -V --target-env vulkan1.3 ${flags} --depfile "${depfile}" -o "${spirv}"
                "${source}"
        DEPENDS "${source}"
        DEPFILE "${depfile}"
        COMMENT "Compiling shader ${name}"
        VERBATIM)
    set(${outVar} "${spirv}" PARENT_SCOPE)
endfunction()

# Makes <target> depend on <spirvFiles> through the custom target <target><tag>_shaders, places <target> in
# EVO_RUNTIME_DIR, and records the files for the stale-shader cleanup.
function(_evo_shader_target target tag spirvFiles)
    add_custom_target(${target}${tag}_shaders DEPENDS ${spirvFiles})
    add_dependencies(${target} ${target}${tag}_shaders)
    set_target_properties(${target} PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${EVO_RUNTIME_DIR}/$<0:>")

    set_property(GLOBAL APPEND PROPERTY EVO_SPIRV_FILES ${spirvFiles})
    get_property(cleanupScheduled GLOBAL PROPERTY EVO_STALE_SHADER_CLEANUP)
    if(NOT cleanupScheduled)
        set_property(GLOBAL PROPERTY EVO_STALE_SHADER_CLEANUP TRUE)
        cmake_language(DEFER DIRECTORY "${CMAKE_SOURCE_DIR}" CALL _evo_remove_stale_shaders)
    endif()
endfunction()

# evo_add_shaders(<target> <shader>...)
# Shader paths are relative to the calling CMakeLists.txt. Also places <target> in EVO_RUNTIME_DIR. Names as
# in _evo_shader_rule (shaders/retro/plasma.comp -> retro_plasma.comp.spv). Builds <target>_shaders.
function(evo_add_shaders target)
    set(spirvFiles "")
    foreach(shader IN LISTS ARGN)
        _evo_shader_rule(spirv "${shader}" "" "")
        list(APPEND spirvFiles "${spirv}")
    endforeach()
    _evo_shader_target(${target} "" "${spirvFiles}")
endfunction()

# evo_add_shader_variants(<target> SUFFIX <suffix> [DEFINES <NAME[=VALUE]>...] SHADERS <shader>...)
# Compiles each shader a second time with the defines, as <name>_<suffix>.<stage>.spv beside its plain build
# (evo_add_shader_variants(evo_modern SUFFIX sphere DEFINES GEOMETRY_SPHERES SHADERS
# shaders/rt/pbr.rchit ...) -> rt_pbr_sphere.rchit.spv). Builds <target>_<suffix>_shaders. Call it once per
# suffix and target.
function(evo_add_shader_variants target)
    cmake_parse_arguments(PARSE_ARGV 1 arg "" "SUFFIX" "DEFINES;SHADERS")
    if(NOT arg_SUFFIX OR NOT arg_SHADERS)
        message(FATAL_ERROR "evo_add_shader_variants(${target}): needs SUFFIX and SHADERS")
    endif()
    set(spirvFiles "")
    foreach(shader IN LISTS arg_SHADERS)
        _evo_shader_rule(spirv "${shader}" "${arg_SUFFIX}" "${arg_DEFINES}")
        list(APPEND spirvFiles "${spirv}")
    endforeach()
    _evo_shader_target(${target} "_${arg_SUFFIX}" "${spirvFiles}")
endfunction()
