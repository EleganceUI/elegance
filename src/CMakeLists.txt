include(GenerateExportHeader)
include(ElgShaders)

if (ELG_BUILD_SHARED)
    set(ELG_LIBRARY_TYPE SHARED)
else ()
    set(ELG_LIBRARY_TYPE STATIC)
endif ()

add_library(elegance ${ELG_LIBRARY_TYPE}
        animation/animation.cpp
        animation/frame_clock.cpp
        app.cpp
        error.cpp
        graphics_context.cpp
        geometry/path.cpp
        core/crash.cpp
        core/log.cpp
        core/ui_tasks.cpp
        gpu/atlas_texture.cpp
        gpu/blur_renderer.cpp
        gpu/buffer.cpp
        gpu/device.cpp
        gpu/instance.cpp
        gpu/path_renderer.cpp
        gpu/quad_renderer.cpp
        gpu/render_image.cpp
        gpu/swapchain.cpp
        gpu/texture_cache.cpp
        gpu/video_textures.cpp
        gpu/webgpu.cpp
        media/audio.cpp
        media/audio_stream.cpp
        media/camera.cpp
        media/image_decode.cpp
        media/miniaudio.cpp
        media/recorder.cpp
        media/gpu_image.cpp
        media/video_feed.cpp
        media/video_source.cpp
        net/http.cpp
        net/websocket.cpp
        platform/accessibility_adapter.cpp
        platform/desktop_sdl.cpp
        platform/dynamic_library.cpp
        platform/platform_sdl.cpp
        reactive/runtime.cpp
        render/display_list.cpp
        render/frame_mailbox.cpp
        render/frame_plan.cpp
        render/render_thread.cpp
        render/window_target.cpp
        text/font_library.cpp
        text/glyph_atlas.cpp
        text/glyph_cache.cpp
        text/color_glyph.cpp
        text/styled_layout.cpp
        text/text_layout.cpp
        view/controls.cpp
        view/drop.cpp
        view/element.cpp
        view/event_dispatcher.cpp
        view/events.cpp
        view/fragment.cpp
        view/grid_layout.cpp
        view/hot_reload.cpp
        view/image.cpp
        view/accessibility.cpp
        view/async_image.cpp
        view/raster_image.cpp
        view/video_view.cpp
        view/inspect.cpp
        view/inspector.cpp
        view/lazy_stack.cpp
        view/menus.cpp
        view/modifiers.cpp
        view/navigation_stack.cpp
        view/scroll.cpp
        view/shapes.cpp
        view/split_view.cpp
        view/stack_layout.cpp
        view/styles.cpp
        view/styled_text.cpp
        view/text.cpp
        view/text_field.cpp
        view/view.cpp
        view/window_controls.cpp
)
add_library(Elegance::Elegance ALIAS elegance)

target_compile_features(elegance PUBLIC cxx_std_23)

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
    # Linked into a shared library that re-exports ELG_API (the Rust bindings' prebuilt library).
    # The generated header leaves ELG_API empty for a static library unless it's already defined.
    if (MSVC)
        target_compile_definitions(elegance PUBLIC "ELG_API=__declspec(dllexport)")
    else ()
        target_compile_definitions(elegance PUBLIC "ELG_API=__attribute__((visibility(\"default\")))")
    endif ()
elseif (NOT ELG_BUILD_SHARED)
    target_compile_definitions(elegance PUBLIC ELG_STATIC_DEFINE)
    # What linking it takes, for builds outside CMake (the Rust bindings), written at the end.
    cmake_language(DEFER CALL elg_write_link_manifest elegance "${PROJECT_BINARY_DIR}/elegance-link.txt")
    include(ElgLinkManifest)
endif ()

find_package(SDL3 CONFIG REQUIRED)
find_package(Dawn CONFIG REQUIRED) # WebGPU (design.md §5)
find_package(Freetype REQUIRED)
find_package(harfbuzz CONFIG REQUIRED)
find_package(msdfgen CONFIG REQUIRED)
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
if (ELG_ACCESSIBILITY)
    include("${accesskit_c_SOURCE_DIR}/accesskit-config.cmake")
    # Its static library is a Rust staticlib with its own Rust standard library, which clashes with
    # a Rust app's own (the Rust bindings link a static Elegance with ELG_ACCESSKIT_SHARED).
    if (ELG_ACCESSKIT_SHARED)
        target_link_libraries(elegance PRIVATE accesskit-shared)
    else ()
        target_link_libraries(elegance PRIVATE accesskit-static)
    endif ()
    target_compile_definitions(elegance PRIVATE ELG_HAVE_ACCESSKIT=1)
endif ()

# miniaudio is a single header (design.md §13.3).
find_path(MINIAUDIO_INCLUDE_DIR miniaudio.h REQUIRED)
target_include_directories(elegance PRIVATE "${MINIAUDIO_INCLUDE_DIR}")

# Private: no SDL or WebGPU types appear in public headers (design.md §4).
# Built-in icon sets (design.md §13.2), embedded into the library.
include(ElgEmbed)
if (ELG_ICONS)
    set(phosphor "${PROJECT_SOURCE_DIR}/third_party/phosphor")
    elg_embed_file(elegance phosphor_regular "${phosphor}/Phosphor.ttf")
    elg_embed_file(elegance phosphor_bold "${phosphor}/Phosphor-Bold.ttf")
    elg_embed_file(elegance phosphor_fill "${phosphor}/Phosphor-Fill.ttf")
    elg_embed_file(elegance phosphor_css "${phosphor}/phosphor.css")
    target_compile_definitions(elegance PRIVATE ELG_HAS_PHOSPHOR)
    if (ELG_ICONS_EXTRA_WEIGHTS)
        elg_embed_file(elegance phosphor_thin "${phosphor}/Phosphor-Thin.ttf")
        elg_embed_file(elegance phosphor_light "${phosphor}/Phosphor-Light.ttf")
        elg_embed_file(elegance phosphor_duotone "${phosphor}/Phosphor-Duotone.ttf")
        elg_embed_file(elegance phosphor_duotone_css "${phosphor}/phosphor-duotone.css")
        target_compile_definitions(elegance PRIVATE ELG_HAS_PHOSPHOR_EXTRA)
    endif ()
endif ()

if (WIN32)
    target_sources(elegance PRIVATE gpu/composition_win32.cpp)
    # dwmapi: rounded corners for undecorated windows; d3d11/dxgi/dcomp: transparent windows.
    target_link_libraries(elegance PRIVATE dwmapi d3d11 dxgi dcomp windowscodecs ole32 propsys mfplat mfuuid mf mfreadwrite strmiids oleaut32 winhttp shell32)
endif ()

if (APPLE)
    # Video playback through AVFoundation (Objective-C++, ARC); images through ImageIO.
    enable_language(OBJCXX)
    # Integrated title bars and trackpad gestures through AppKit.
    set(elg_apple_sources media/video_source_apple.mm platform/input_apple.mm platform/title_bar_apple.mm)
    target_sources(elegance PRIVATE ${elg_apple_sources})
    set_source_files_properties(${elg_apple_sources} PROPERTIES COMPILE_OPTIONS "-fobjc-arc")
    target_link_libraries(elegance PRIVATE
            "-framework AppKit" "-framework AVFoundation" "-framework CoreMedia" "-framework CoreVideo" "-framework QuartzCore"
            "-framework Foundation" "-framework ImageIO" "-framework CoreGraphics" "-framework CoreFoundation"
            curl)
endif ()

# HTTP (net/http.cpp) uses the system's libcurl where there's no native HTTP API; without it,
# requests fail with an error.
if (NOT WIN32 AND NOT APPLE)
    find_package(CURL)
    if (CURL_FOUND)
        target_link_libraries(elegance PRIVATE CURL::libcurl)
        target_compile_definitions(elegance PRIVATE ELG_HAVE_CURL=1)
    else ()
        message(WARNING "libcurl not found (install libcurl4-openssl-dev or libcurl-devel): "
                        "building without HTTP support")
    endif ()
endif ()

target_link_libraries(elegance PRIVATE
        SDL3::SDL3
        dawn::webgpu_dawn
        Freetype::Freetype
        harfbuzz::harfbuzz
        msdfgen::msdfgen-core
)

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
# device uses the older, much slower FXC: see dxc_available() in gpu/device.cpp), so it isn't
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

target_include_directories(elegance
        PUBLIC
            ${PROJECT_SOURCE_DIR}/include
            ${CMAKE_CURRENT_BINARY_DIR}/generated
        PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}
)

elg_add_shaders(elegance
        ${PROJECT_SOURCE_DIR}/shaders/ui.wgsl
        ${PROJECT_SOURCE_DIR}/shaders/blur.wgsl
        ${PROJECT_SOURCE_DIR}/shaders/path.wgsl
        ${PROJECT_SOURCE_DIR}/shaders/mipmap.wgsl
        ${PROJECT_SOURCE_DIR}/shaders/video_convert.wgsl
)

target_compile_definitions(elegance PRIVATE
        ELG_VERSION_STRING="${PROJECT_VERSION}"
)
if (WIN32)
    target_compile_definitions(elegance PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
endif ()

# std::stacktrace lives in a separate library on GCC. Linked by path, so a link outside CMake (the
# Rust bindings, linking with the system's default cc) gets this compiler's, not an older GCC's.
if (CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    execute_process(COMMAND "${CMAKE_CXX_COMPILER}" -print-file-name=libstdc++exp.a
                    OUTPUT_VARIABLE elg_stdcxxexp OUTPUT_STRIP_TRAILING_WHITESPACE)
    if (IS_ABSOLUTE "${elg_stdcxxexp}")
        target_link_libraries(elegance PRIVATE "${elg_stdcxxexp}")
    else ()
        target_link_libraries(elegance PRIVATE stdc++exp)
    endif ()
endif ()

set_target_properties(elegance PROPERTIES
        CXX_VISIBILITY_PRESET hidden
        VISIBILITY_INLINES_HIDDEN ON
)

if (MSVC)
    target_compile_options(elegance PRIVATE /W4 /permissive- /utf-8)
else ()
    target_compile_options(elegance PRIVATE -Wall -Wextra -Wpedantic)
endif ()
