# This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
# If a copy of the MPL was not distributed with this file, You can obtain one at
# https://mozilla.org/MPL/2.0/.

# WGSL shaders, embedded as source text (design.md §3.3). WebGPU compiles them when the app
# creates its pipelines, so the build needs no shader compiler.

set(ELG_SHADER_STRING_SCRIPT "${CMAKE_CURRENT_LIST_DIR}/ElgShaderString.cmake")

# elg_add_shaders(<target> <shader>...)
#
# Writes each shader as a C++ string literal at <binary dir>/generated/shaders/<file name>.inc,
# ready to embed:
#
#   constexpr char kUiWgsl[] =
#   #include "shaders/ui.wgsl.inc"
#   ;
function(elg_add_shaders target)
    set(out_dir "${CMAKE_CURRENT_BINARY_DIR}/generated/shaders")
    set(outputs)
    foreach (source IN LISTS ARGN)
        cmake_path(ABSOLUTE_PATH source NORMALIZE OUTPUT_VARIABLE source_path)
        cmake_path(GET source_path FILENAME name)
        set(output "${out_dir}/${name}.inc")
        add_custom_command(
                OUTPUT "${output}"
                COMMAND "${CMAKE_COMMAND}" -DINPUT=${source_path} -DOUTPUT=${output}
                        -P "${ELG_SHADER_STRING_SCRIPT}"
                DEPENDS "${source_path}" "${ELG_SHADER_STRING_SCRIPT}"
                COMMENT "Embedding shader ${name}"
                VERBATIM
        )
        list(APPEND outputs "${output}")
    endforeach ()
    target_sources(${target} PRIVATE ${outputs})
    target_include_directories(${target} PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/generated")
endfunction()
