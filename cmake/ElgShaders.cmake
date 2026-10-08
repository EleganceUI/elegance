# This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
# If a copy of the MPL was not distributed with this file, You can obtain one at
# https://mozilla.org/MPL/2.0/.

# Offline GLSL -> SPIR-V compilation with glslc from the Vulkan SDK (design.md §3.3).
find_program(ELG_GLSLC glslc HINTS "$ENV{VULKAN_SDK}/Bin" "$ENV{VULKAN_SDK}/bin" REQUIRED)

# elg_add_shaders(<target> <shader>...)
#
# Compiles each shader to SPIR-V words as text (glslc -mfmt=num) at
# <binary dir>/generated/shaders/<file name>.inc, ready to embed:
#
#   constexpr std::uint32_t kSpirv[] = {
#   #include "shaders/rect.vert.inc"
#   };
function(elg_add_shaders target)
    set(out_dir "${CMAKE_CURRENT_BINARY_DIR}/generated/shaders")
    set(outputs)
    foreach (source IN LISTS ARGN)
        cmake_path(ABSOLUTE_PATH source NORMALIZE OUTPUT_VARIABLE source_path)
        cmake_path(GET source_path FILENAME name)
        set(output "${out_dir}/${name}.inc")
        add_custom_command(
                OUTPUT "${output}"
                COMMAND "${CMAKE_COMMAND}" -E make_directory "${out_dir}"
                COMMAND "${ELG_GLSLC}" --target-env=vulkan1.3 -O -mfmt=num -o "${output}" "${source_path}"
                DEPENDS "${source_path}"
                COMMENT "Compiling shader ${name}"
                VERBATIM
        )
        list(APPEND outputs "${output}")
    endforeach ()
    target_sources(${target} PRIVATE ${outputs})
    target_include_directories(${target} PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/generated")
endfunction()
