# This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
# If a copy of the MPL was not distributed with this file, You can obtain one at
# https://mozilla.org/MPL/2.0/.

# elegance_view_extensions(<target> <header>)
#
# Adds dot-syntax extensions to elg::View for every source file of <target> (design.md §9.4).
# <header> holds member declarations only; it's included inside the View class body:
#
#   // view_extensions.h
#   View card(float corner_radius = 16) const;
#
# and the definitions live in a normal source file of the target:
#
#   elg::View elg::View::card(float corner_radius) const { return padding(20)...; }
function(elegance_view_extensions target header)
    cmake_path(ABSOLUTE_PATH header NORMALIZE OUTPUT_VARIABLE header_path)
    target_compile_definitions(${target} PRIVATE "ELG_VIEW_EXTENSIONS=\"${header_path}\"")
endfunction()
