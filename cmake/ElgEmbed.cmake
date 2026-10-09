# This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
# If a copy of the MPL was not distributed with this file, You can obtain one at
# https://mozilla.org/MPL/2.0/.

# elg_embed_file(<target> <symbol> <file>)
#
# Compiles <file>'s bytes into <target> as elg::detail::embedded::<symbol> (const unsigned
# char[]) and <symbol>_size (std::size_t). Regenerated when the file changes. Declare them with
#   namespace elg::detail::embedded { extern const unsigned char <symbol>[]; extern const std::size_t <symbol>_size; }
function(elg_embed_file target symbol file)
    get_filename_component(input "${file}" ABSOLUTE)
    set(output "${CMAKE_CURRENT_BINARY_DIR}/embedded/${symbol}.cpp")
    add_custom_command(
        OUTPUT "${output}"
        COMMAND "${CMAKE_COMMAND}" -DINPUT=${input} -DOUTPUT=${output} -DSYMBOL=${symbol}
                -P "${ELG_EMBED_SCRIPT}"
        DEPENDS "${input}" "${ELG_EMBED_SCRIPT}"
        COMMENT "Embedding ${file}"
        VERBATIM)
    target_sources(${target} PRIVATE "${output}")
endfunction()

set(ELG_EMBED_SCRIPT "${CMAKE_CURRENT_LIST_DIR}/ElgEmbedFile.cmake")
