include(GenerateExportHeader)
include(ElgShaders)
include(ElgEmbed)

if (ELG_BUILD_SHARED)
    set(ELG_LIBRARY_TYPE SHARED)
else ()
    set(ELG_LIBRARY_TYPE STATIC)
endif ()

find_package(SDL3 CONFIG REQUIRED)
find_package(Dawn CONFIG REQUIRED) # WebGPU (design.md §5)
find_package(Freetype REQUIRED)
find_package(harfbuzz CONFIG REQUIRED)
find_package(msdfgen CONFIG REQUIRED)
# AsyncImage downloads (net/)
find_package(boost_beast CONFIG REQUIRED)
find_package(boost_url CONFIG REQUIRED)
find_package(OpenSSL REQUIRED)
# miniaudio is a single header (design.md §13.3).
find_path(MINIAUDIO_INCLUDE_DIR miniaudio.h REQUIRED)

# Accessibility (design.md §12): AccessKit's official prebuilt C library (MIT / Apache-2.0),
# linked statically, so building Elegance needs no Rust toolchain.
if (ELG_ACCESSIBILITY)
    include(FetchContent)
    FetchContent_Declare(accesskit_c
        URL https://github.com/AccessKit/accesskit-c/releases/download/0.23.1/accesskit-c-0.23.1.zip
        URL_HASH SHA256=35b7ca8a6f1e038b5da35e1e9e5a0adaed9bfcf21e1496d29598fbbadcc7043f
        DOWNLOAD_EXTRACT_TIMESTAMP TRUE
        SOURCE_SUBDIR no-cmake-here) # use the prebuilt libraries, don't build it
    FetchContent_MakeAvailable(accesskit_c)
    # Sets ACCESSKIT_LIBRARIES_DIR. Not every platform has prebuilt libraries (Linux on ARM64 doesn't).
    include("${accesskit_c_SOURCE_DIR}/accesskit.cmake")
    if (NOT IS_DIRECTORY "${ACCESSKIT_LIBRARIES_DIR}")
        message(WARNING "AccessKit has no prebuilt library for this platform (${ACCESSKIT_LIBRARIES_DIR}): "
                        "building without screen reader support. Set ELG_ACCESSIBILITY=OFF to silence this.")
        set(ELG_ACCESSIBILITY OFF)
    endif ()
endif ()
set(elg_accesskit "")
if (ELG_ACCESSIBILITY)
    include("${accesskit_c_SOURCE_DIR}/accesskit-config.cmake")
    # Its static library is a Rust staticlib with its own Rust standard library, which clashes with
    # a Rust app's own (the Rust bindings link a static Elegance with ELG_ACCESSKIT_SHARED).
    if (ELG_ACCESSKIT_SHARED)
        set(elg_accesskit accesskit-shared)
    else ()
        set(elg_accesskit accesskit-static)
    endif ()
endif ()

# The system's GStreamer plays video, and GIO talks to the notification server (Linux and other
# Unix desktops); each is optional (pkg-config), and without it that feature reports it isn't
# available.
if (UNIX AND NOT APPLE)
    find_package(PkgConfig)
    if (PkgConfig_FOUND)
        pkg_check_modules(ELG_GSTREAMER IMPORTED_TARGET gstreamer-1.0 gstreamer-app-1.0 gstreamer-video-1.0)
        pkg_check_modules(ELG_GIO IMPORTED_TARGET gio-2.0)
    endif ()
    if (NOT ELG_GSTREAMER_FOUND)
        message(STATUS "Elegance: GStreamer not found (gstreamer-1.0, -app, -video): video playback is off")
    endif ()
    if (NOT ELG_GIO_FOUND)
        message(STATUS "Elegance: GIO not found (gio-2.0): notifications are only logged")
    endif ()
endif ()
if (APPLE)
    # Video playback through AVFoundation (Objective-C++, ARC); images through ImageIO; integrated
    # title bars and trackpad gestures through AppKit.
    enable_language(OBJCXX)
endif ()

# --- What every layer compiles and links with ---

add_library(elegance_settings INTERFACE)
target_compile_features(elegance_settings INTERFACE cxx_std_23)
target_include_directories(elegance_settings INTERFACE
        ${PROJECT_SOURCE_DIR}/include
        ${CMAKE_CURRENT_BINARY_DIR}/generated
        ${CMAKE_CURRENT_SOURCE_DIR}
)

# <elg/headers_hash.h>: a hash of the public headers, which a views module's entry carries and
# HotReload() compares with the one Elegance was compiled with. The build writes it again when a
# header changes (only if the hash did), before anything that links Elegance is compiled.
file(GLOB_RECURSE elg_public_headers CONFIGURE_DEPENDS "${PROJECT_SOURCE_DIR}/include/elg/*.h")
set(elg_headers_hash_stamp "${CMAKE_CURRENT_BINARY_DIR}/generated/headers_hash.stamp")
add_custom_command(
        OUTPUT "${elg_headers_hash_stamp}"
        BYPRODUCTS "${CMAKE_CURRENT_BINARY_DIR}/generated/elg/headers_hash.h"
        COMMAND ${CMAKE_COMMAND} "-DROOT=${PROJECT_SOURCE_DIR}"
                "-DHEADER=${CMAKE_CURRENT_BINARY_DIR}/generated/elg/headers_hash.h" "-DSTAMP=${elg_headers_hash_stamp}"
                -P "${PROJECT_SOURCE_DIR}/cmake/ElgHeadersHash.cmake"
        DEPENDS ${elg_public_headers} "${PROJECT_SOURCE_DIR}/cmake/ElgHeadersHash.cmake"
        COMMENT "Hashing Elegance's public headers"
        VERBATIM)
add_custom_target(elegance_headers_hash DEPENDS "${elg_headers_hash_stamp}")
add_dependencies(elegance_settings elegance_headers_hash)
if (ELG_BUILD_SHARED)
    # What generate_export_header() tests for the library's own sources: ELG_API exports.
    target_compile_definitions(elegance_settings INTERFACE elegance_EXPORTS)
elseif (ELG_STATIC_EXPORTS)
    # Linked into a shared library that re-exports ELG_API (the Rust bindings' prebuilt library).
    # The generated header leaves ELG_API empty for a static library unless it's already defined.
    if (MSVC)
        set(elg_api_definition "ELG_API=__declspec(dllexport)")
    else ()
        set(elg_api_definition "ELG_API=__attribute__((visibility(\"default\")))")
    endif ()
    target_compile_definitions(elegance_settings INTERFACE "${elg_api_definition}")
else ()
    target_compile_definitions(elegance_settings INTERFACE ELG_STATIC_DEFINE)
endif ()
if (ELG_ACCESSIBILITY)
    target_compile_definitions(elegance_settings INTERFACE ELG_HAVE_ACCESSKIT=1)
endif ()
if (ELG_GSTREAMER_FOUND)
    target_compile_definitions(elegance_settings INTERFACE ELG_HAVE_GSTREAMER)
endif ()
if (ELG_GIO_FOUND)
    target_compile_definitions(elegance_settings INTERFACE ELG_HAVE_GIO)
endif ()
if (WIN32)
    target_compile_definitions(elegance_settings INTERFACE NOMINMAX WIN32_LEAN_AND_MEAN)
endif ()
if (MSVC)
    # Boost.Asio coroutines (net/fetch.cpp) exceed the default 2^16 sections on arm64.
    target_compile_options(elegance_settings INTERFACE /utf-8 /bigobj)
endif ()
# std::stacktrace lives in a separate library on GCC. Linked by path, so a link outside CMake (the
# Rust bindings, linking with the system's default cc) gets this compiler's, not an older GCC's.
if (CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    execute_process(COMMAND "${CMAKE_CXX_COMPILER}" -print-file-name=libstdc++exp.a
                    OUTPUT_VARIABLE elg_stdcxxexp OUTPUT_STRIP_TRAILING_WHITESPACE)
    if (IS_ABSOLUTE "${elg_stdcxxexp}")
        target_link_libraries(elegance_settings INTERFACE "${elg_stdcxxexp}")
    else ()
        target_link_libraries(elegance_settings INTERFACE stdc++exp)
    endif ()
endif ()
# libc++ 19's std::jthread needs -fexperimental-library (the top-level CMakeLists.txt finds out).
if (ELG_LIBCXX_EXPERIMENTAL)
    target_compile_options(elegance_settings INTERFACE $<$<COMPILE_LANGUAGE:CXX>:-fexperimental-library>)
    target_link_options(elegance_settings INTERFACE -fexperimental-library)
endif ()
# The OS's own libraries, which the platform folders' code calls (each layer's backend is in them).
if (WIN32)
    # dwmapi: rounded corners and backdrops; comctl32: window subclassing; d3d11/dxgi/dcomp:
    # transparent windows; the rest: images, video, cameras, certificates, proxies, the shell.
    target_link_libraries(elegance_settings INTERFACE dwmapi comctl32 d3d11 dxgi dcomp windowscodecs ole32 propsys
            mfplat mfuuid mf mfreadwrite strmiids oleaut32 shell32 crypt32 imm32 winhttp)
endif ()
if (APPLE)
    target_link_libraries(elegance_settings INTERFACE
            "-framework AppKit" "-framework AVFoundation" "-framework CoreMedia" "-framework CoreVideo" "-framework QuartzCore"
            "-framework Foundation" "-framework ImageIO" "-framework CoreGraphics" "-framework CoreFoundation"
            "-framework Security" "-framework Carbon" "-framework CFNetwork" "-framework UserNotifications" "-framework Metal" "-framework IOSurface")
endif ()
if (ELG_GSTREAMER_FOUND)
    target_link_libraries(elegance_settings INTERFACE PkgConfig::ELG_GSTREAMER)
    # GStreamer needs libunwind.so.8, which on x86-64 defines _Unwind_RaiseException and the rest
    # of GCC's unwinder interface too: whichever library the loader finds first supplies them to
    # the whole process, and libstdc++ must throw with libgcc_s's. What links GStreamer through
    # this (a program linking a static Elegance, each layer's tests) puts libgcc_s first on its
    # link line, before anything that can bring libunwind in (tests/unwinder_tests.cpp).
    if (CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
        target_link_options(elegance_settings INTERFACE "LINKER:--push-state,--no-as-needed,-lgcc_s,--pop-state")
    endif ()
endif ()
if (ELG_GIO_FOUND)
    target_link_libraries(elegance_settings INTERFACE PkgConfig::ELG_GIO)
endif ()

# --- The layers (ELGSPEC-0001.R1) ---

# elg_layer(<name> SOURCES <file>... [WINDOWS|APPLE|LINUX <file>...] [DEPENDS <layer>...]
#           [LIBRARIES <library>...])
#
# A layer, or the implementation of a layer's backend (ELGSPEC-0001.R4), as an object library,
# elegance_<name>, that links only the layers it may depend on (ELGSPEC-0001.R2): it sees their
# headers and libraries, not those of the layers above it. Its ELG_OBJECTS property lists it and
# every layer below it, for an executable that links the layer on its own (its tests,
# ELGSPEC-0001.R9): object libraries add their objects only where they're linked directly.
function(elg_layer name)
    cmake_parse_arguments(PARSE_ARGV 1 arg "" "" "SOURCES;WINDOWS;APPLE;LINUX;DEPENDS;LIBRARIES")
    set(target elegance_${name})
    set(sources ${arg_SOURCES})
    if (WIN32)
        list(APPEND sources ${arg_WINDOWS})
    elseif (APPLE)
        list(APPEND sources ${arg_APPLE})
    else ()
        list(APPEND sources ${arg_LINUX})
    endif ()
    add_library(${target} OBJECT ${sources})
    if (APPLE)
        set(objective_c ${sources})
        list(FILTER objective_c INCLUDE REGEX "\\.mm$")
        if (objective_c)
            set_source_files_properties(${objective_c} PROPERTIES COMPILE_OPTIONS "-fobjc-arc")
        endif ()
    endif ()
    set(objects ${target})
    foreach (dependency IN LISTS arg_DEPENDS)
        target_link_libraries(${target} PUBLIC elegance_${dependency})
        get_target_property(below elegance_${dependency} ELG_OBJECTS)
        list(APPEND objects ${below})
    endforeach ()
    list(REMOVE_DUPLICATES objects)
    set_target_properties(${target} PROPERTIES ELG_OBJECTS "${objects}")
    target_link_libraries(${target} PUBLIC elegance_settings ${arg_LIBRARIES})
    if (ELG_BUILD_SHARED)
        set_target_properties(${target} PROPERTIES POSITION_INDEPENDENT_CODE ON)
    endif ()
    # A shared library exports only the API (ELG_API), except in a build with the unit tests, which
    # test the internals (elg::detail) against a shared library too (ELGSPEC-0006.R4). Shipped
    # builds don't build the tests.
    if (ELG_BUILD_SHARED AND ELG_BUILD_TESTS)
        set_target_properties(${target} PROPERTIES CXX_VISIBILITY_PRESET default VISIBILITY_INLINES_HIDDEN OFF)
    else ()
        set_target_properties(${target} PROPERTIES CXX_VISIBILITY_PRESET hidden VISIBILITY_INLINES_HIDDEN ON)
    endif ()
    elegance_warnings(${target})
    set_property(GLOBAL APPEND PROPERTY ELG_LAYERS ${target})
    set_property(GLOBAL APPEND PROPERTY ELG_LAYER_LIBRARIES ${arg_LIBRARIES})
endfunction()

# The backends' implementations (ELGSPEC-0001.R4), each with the layer whose backend it is.
elg_layer(core_os
        SOURCES platform/sdl/os.cpp
        WINDOWS platform/windows/os.cpp
        APPLE platform/macos/os.cpp
        LINUX platform/linux/os.cpp
        LIBRARIES SDL3::SDL3)
elg_layer(core
        SOURCES core/app_errors.cpp core/child_process.cpp core/crash.cpp core/dynamic_library.cpp core/error.cpp
                core/log.cpp
                core/ui_tasks.cpp core/utf8.cpp core/version.cpp
        DEPENDS core_os)
target_compile_definitions(elegance_core PRIVATE ELG_VERSION_STRING="${PROJECT_VERSION}")
elg_layer(geometry SOURCES geometry/path.cpp DEPENDS core)
elg_layer(reactive SOURCES reactive/runtime.cpp reactive/timers.cpp DEPENDS core)
elg_layer(animation SOURCES animation/animation.cpp animation/frame_clock.cpp DEPENDS core geometry reactive)

elg_layer(text_os
        WINDOWS platform/windows/fonts.cpp
        APPLE platform/macos/fonts.cpp
        LINUX platform/linux/fonts.cpp)
elg_layer(text
        SOURCES text/color_glyph.cpp text/font_library.cpp text/glyph_atlas.cpp text/glyph_cache.cpp
                text/styled_layout.cpp text/text_layout.cpp
        DEPENDS core geometry reactive text_os
        LIBRARIES Freetype::Freetype harfbuzz::harfbuzz msdfgen::msdfgen-core)

elg_layer(media_os
        WINDOWS platform/windows/camera.cpp platform/windows/image_codec.cpp platform/windows/video_source.cpp
        APPLE platform/macos/camera.mm platform/macos/image_codec.cpp platform/macos/pixel_buffer.cpp
              platform/macos/video_source.mm
        LINUX platform/linux/camera.cpp platform/linux/image_codec.cpp platform/linux/video_source.cpp)
elg_layer(media
        SOURCES media/audio.cpp media/audio_stream.cpp media/camera.cpp media/frame_source.cpp media/image_decode.cpp
                media/miniaudio.cpp media/recorder.cpp media/video_feed.cpp media/video_source.cpp
        DEPENDS core geometry reactive media_os)
target_include_directories(elegance_media PRIVATE "${MINIAUDIO_INCLUDE_DIR}")

elg_layer(net_os
        WINDOWS platform/windows/net.cpp platform/windows/proxy.cpp
        APPLE platform/macos/net.cpp platform/macos/proxy.cpp
        LINUX platform/linux/net.cpp platform/linux/proxy.cpp)
elg_layer(net
        SOURCES net/fetch.cpp net/http_cache.cpp net/proxy.cpp
        DEPENDS core net_os
        LIBRARIES Boost::beast Boost::url OpenSSL::SSL OpenSSL::Crypto)

elg_layer(display SOURCES display/display_list.cpp DEPENDS core geometry)

elg_layer(gpu
        SOURCES gpu/atlas_texture.cpp gpu/blur_renderer.cpp gpu/buffer.cpp gpu/device.cpp gpu/external_image.cpp
                gpu/instance.cpp gpu/path_renderer.cpp gpu/quad_renderer.cpp gpu/render_image.cpp gpu/swapchain.cpp
                gpu/texture_cache.cpp gpu/webgpu.cpp
        # Each OS's interop with its graphics APIs (gpu/<os>), and what stands in for it elsewhere.
        WINDOWS gpu/native_image.cpp gpu/windows/composition.cpp gpu/windows/system.cpp
        APPLE gpu/macos/native_image.mm gpu/presenter.cpp gpu/system.cpp
        LINUX gpu/native_image.cpp gpu/presenter.cpp gpu/system.cpp
        DEPENDS core geometry
        LIBRARIES dawn::webgpu_dawn)
elg_add_shaders(elegance_gpu
        ${PROJECT_SOURCE_DIR}/shaders/ui.wgsl
        ${PROJECT_SOURCE_DIR}/shaders/blur.wgsl
        ${PROJECT_SOURCE_DIR}/shaders/path.wgsl
        ${PROJECT_SOURCE_DIR}/shaders/mipmap.wgsl
)

elg_layer(render
        SOURCES render/frame_mailbox.cpp render/frame_plan.cpp render/frame_sources.cpp render/gpu_image.cpp
                render/render_thread.cpp render/video_textures.cpp render/window_target.cpp
        DEPENDS core geometry display gpu text media
        LIBRARIES dawn::webgpu_dawn)
elg_add_shaders(elegance_render ${PROJECT_SOURCE_DIR}/shaders/video_convert.wgsl)

# The platform implementation (platform/sdl and each OS's): what the platform interface
# (platform/*.h) declares.
elg_layer(platform
        SOURCES platform/sdl/accessibility_adapter.cpp platform/sdl/desktop_sdl.cpp platform/sdl/menu_bar.cpp
                platform/sdl/platform_sdl.cpp platform/sdl/system_text.cpp
        WINDOWS platform/windows/appearance.cpp platform/windows/dock.cpp platform/windows/menu_bar.cpp
                platform/windows/native.cpp platform/windows/notifications.cpp
        APPLE platform/macos/appearance.mm platform/macos/dock.mm platform/macos/input.mm platform/macos/menu_bar.mm
              platform/macos/native.cpp platform/macos/notifications.mm platform/macos/title_bar.mm
        LINUX platform/linux/appearance.cpp platform/linux/dock.cpp platform/linux/glib_thread.cpp
              platform/linux/menu_bar.cpp platform/linux/native.cpp platform/linux/notifications.cpp
        DEPENDS core geometry
        LIBRARIES SDL3::SDL3 ${elg_accesskit})

elg_layer(view
        SOURCES view/accessibility.cpp view/async_image.cpp view/controls.cpp view/drop.cpp view/element.cpp
                view/event_dispatcher.cpp view/events.cpp view/floating.cpp view/fragment.cpp view/gestures.cpp
                view/graphics_context.cpp view/grid_layout.cpp view/image.cpp view/inspect.cpp view/lazy_stack.cpp
                view/menu_bar_view.cpp view/menus.cpp view/modifiers.cpp view/navigation_stack.cpp view/number_input.cpp
                view/path_cache.cpp view/progress.cpp view/raster_image.cpp view/scroll.cpp view/selection.cpp
                view/separator.cpp view/shapes.cpp view/split_view.cpp view/stack_layout.cpp view/styled_draw.cpp
                view/styled_text.cpp view/styles.cpp view/text.cpp view/text_field.cpp view/tray_icon.cpp
                view/video_view.cpp view/view.cpp view/window_controls.cpp
        # The platform interface's implementation comes along: View calls what it declares.
        DEPENDS core geometry reactive animation text media net display platform)
# Built-in icon sets (design.md §13.2), embedded into the library.
if (ELG_ICONS)
    set(phosphor "${PROJECT_SOURCE_DIR}/third_party/phosphor")
    elg_embed_file(elegance_view phosphor_regular "${phosphor}/Phosphor.ttf")
    elg_embed_file(elegance_view phosphor_bold "${phosphor}/Phosphor-Bold.ttf")
    elg_embed_file(elegance_view phosphor_fill "${phosphor}/Phosphor-Fill.ttf")
    elg_embed_file(elegance_view phosphor_css "${phosphor}/phosphor.css")
    target_compile_definitions(elegance_view PRIVATE ELG_HAS_PHOSPHOR)
    if (ELG_ICONS_EXTRA_WEIGHTS)
        elg_embed_file(elegance_view phosphor_thin "${phosphor}/Phosphor-Thin.ttf")
        elg_embed_file(elegance_view phosphor_light "${phosphor}/Phosphor-Light.ttf")
        elg_embed_file(elegance_view phosphor_duotone "${phosphor}/Phosphor-Duotone.ttf")
        elg_embed_file(elegance_view phosphor_duotone_css "${phosphor}/phosphor-duotone.css")
        target_compile_definitions(elegance_view PRIVATE ELG_HAS_PHOSPHOR_EXTRA)
    endif ()
endif ()

elg_layer(hot_reload SOURCES hot_reload/hot_reload.cpp DEPENDS core reactive view platform)
elg_layer(app
        SOURCES app/app.cpp app/inspector.cpp
        DEPENDS core geometry reactive animation text media net display gpu render platform view hot_reload)

# --- The library: every layer ---

get_property(elg_layers GLOBAL PROPERTY ELG_LAYERS)
set(elg_objects "")
foreach (layer IN LISTS elg_layers)
    list(APPEND elg_objects $<TARGET_OBJECTS:${layer}>)
endforeach ()
add_library(elegance ${ELG_LIBRARY_TYPE} ${elg_objects})
add_library(Elegance::Elegance ALIAS elegance)
# What the layers link: the library takes it along, as the layers' objects don't.
get_property(elg_layer_libraries GLOBAL PROPERTY ELG_LAYER_LIBRARIES)
list(REMOVE_DUPLICATES elg_layer_libraries)
target_link_libraries(elegance PRIVATE elegance_settings ${elg_layer_libraries})
set_target_properties(elegance PROPERTIES LINKER_LANGUAGE CXX)

target_compile_features(elegance PUBLIC cxx_std_23)
target_include_directories(elegance PUBLIC
        ${PROJECT_SOURCE_DIR}/include
        ${CMAKE_CURRENT_BINARY_DIR}/generated
)

# Generates <elg/export.h> with the ELG_API macro, plus macros for wrapping private data members
# of exported classes whose types aren't exported themselves (std types, View). Clients never
# touch those members, so MSVC's C4251 warning about them is noise; this scopes it away without
# disabling it in app code.
set(ELG_EXPORT_EXTRA [=[
#if defined(_MSC_VER)
#define ELG_PRIVATE_MEMBERS_BEGIN __pragma(warning(push)) __pragma(warning(disable : 4251))
#define ELG_PRIVATE_MEMBERS_END __pragma(warning(pop))
#else
#define ELG_PRIVATE_MEMBERS_BEGIN
#define ELG_PRIVATE_MEMBERS_END
#endif
]=])
generate_export_header(elegance
        BASE_NAME ELG
        EXPORT_MACRO_NAME ELG_API
        EXPORT_FILE_NAME ${CMAKE_CURRENT_BINARY_DIR}/generated/elg/export.h
        CUSTOM_CONTENT_FROM_VARIABLE ELG_EXPORT_EXTRA
)
if (NOT ELG_BUILD_SHARED AND ELG_STATIC_EXPORTS)
    target_compile_definitions(elegance PUBLIC "${elg_api_definition}")
elseif (NOT ELG_BUILD_SHARED)
    target_compile_definitions(elegance PUBLIC ELG_STATIC_DEFINE)
    # What linking it takes, for builds outside CMake (the Rust bindings), written at the end.
    cmake_language(DEFER CALL elg_write_link_manifest elegance "${PROJECT_BINARY_DIR}/elegance-link.txt")
    include(ElgLinkManifest)
endif ()

# vcpkg only deploys an executable's direct DLL dependencies, so copy ours (SDL3, ...) next to
# elegance.dll, which shares the output folder with every app.
if (WIN32 AND ELG_BUILD_SHARED)
    add_custom_command(TARGET elegance POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                    $<TARGET_RUNTIME_DLLS:elegance> $<TARGET_FILE_DIR:elegance>
            COMMAND_EXPAND_LISTS
    )
endif ()

# Dawn's Direct3D 12 backend loads the DirectX shader compiler at run time (without it, the
# device uses the older, much slower FXC: see dxc_available() in gpu/windows/system.cpp), so it isn't
# among the linked DLLs above. It goes next to
# the apps: beside elegance.dll, or with a static Elegance, into the runtime output folder (set it,
# or copy these two DLLs next to your app yourself).
if (WIN32 AND DEFINED VCPKG_INSTALLED_DIR)
    set(elg_dxc_dir "${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/$<$<CONFIG:Debug>:debug/>bin")
    if (ELG_BUILD_SHARED)
        set(elg_runtime_dir $<TARGET_FILE_DIR:elegance>)
    elseif (CMAKE_RUNTIME_OUTPUT_DIRECTORY)
        set(elg_runtime_dir "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}")
    else ()
        message(STATUS "Elegance: copy dxcompiler.dll and dxil.dll from ${VCPKG_INSTALLED_DIR}/"
                       "${VCPKG_TARGET_TRIPLET}/bin next to your app (CMAKE_RUNTIME_OUTPUT_DIRECTORY isn't set)")
    endif ()
    if (DEFINED elg_runtime_dir)
        add_custom_command(TARGET elegance POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E make_directory "${elg_runtime_dir}"
                COMMAND ${CMAKE_COMMAND} -E copy_if_different
                        "${elg_dxc_dir}/dxcompiler.dll" "${elg_dxc_dir}/dxil.dll" "${elg_runtime_dir}"
        )
    endif ()
endif ()

# libc++ 19's std::jthread needs -fexperimental-library, in what links Elegance too.
if (ELG_LIBCXX_EXPERIMENTAL)
    target_compile_options(elegance INTERFACE $<$<COMPILE_LANGUAGE:CXX>:-fexperimental-library>)
    target_link_options(elegance INTERFACE -fexperimental-library)
endif ()

# A shared library exports only the API (ELG_API), except in a build with the unit tests, which
# test the internals (elg::detail) against a shared library too (ELGSPEC-0006.R4). Shipped builds
# don't build the tests.
if (ELG_BUILD_SHARED AND ELG_BUILD_TESTS)
    set_target_properties(elegance PROPERTIES
            CXX_VISIBILITY_PRESET default
            VISIBILITY_INLINES_HIDDEN OFF
            WINDOWS_EXPORT_ALL_SYMBOLS ON
    )
else ()
    set_target_properties(elegance PROPERTIES
            CXX_VISIBILITY_PRESET hidden
            VISIBILITY_INLINES_HIDDEN ON
    )
endif ()
