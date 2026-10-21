# Build-time GLSL -> SPIR-V compilation. There is no runtime shader compiler: every shader is compiled
# here into <build>/shaders/<name>.spv and copied next to the executable that uses it.

find_program(EVO_GLSLANG glslangValidator HINTS $ENV{VULKAN_SDK}/Bin $ENV{VULKAN_SDK}/bin REQUIRED)

# evo_add_shaders(<target> <shader>...)
# Shader paths are relative to the calling CMakeLists.txt.
function(evo_add_shaders target)
    set(outputDir "${CMAKE_BINARY_DIR}/shaders")
    set(spirvFiles "")

    foreach(shader IN LISTS ARGN)
        cmake_path(ABSOLUTE_PATH shader BASE_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}" OUTPUT_VARIABLE source)
        cmake_path(GET source FILENAME name)
        set(spirv "${outputDir}/${name}.spv")

        add_custom_command(
            OUTPUT "${spirv}"
            COMMAND "${CMAKE_COMMAND}" -E make_directory "${outputDir}"
            COMMAND "${EVO_GLSLANG}" -V --target-env vulkan1.3 -o "${spirv}" "${source}"
            DEPENDS "${source}"
            COMMENT "Compiling shader ${name}"
            VERBATIM)
        list(APPEND spirvFiles "${spirv}")
    endforeach()

    add_custom_target(${target}_shaders DEPENDS ${spirvFiles})
    add_dependencies(${target} ${target}_shaders)

    add_custom_command(
        TARGET ${target} POST_BUILD
        COMMAND "${CMAKE_COMMAND}" -E copy_directory "${outputDir}" "$<TARGET_FILE_DIR:${target}>/shaders"
        VERBATIM)
endfunction()
