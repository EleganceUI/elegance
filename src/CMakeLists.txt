include(GenerateExportHeader)
include(ElgShaders)

if (ELG_BUILD_SHARED)
    set(ELG_LIBRARY_TYPE SHARED)
else ()
    set(ELG_LIBRARY_TYPE STATIC)
endif ()

add_library(elegence ${ELG_LIBRARY_TYPE}
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
        media/audio.cpp
        media/audio_stream.cpp
        media/camera.cpp
        media/image_decode.cpp
        media/miniaudio.cpp
        media/recorder.cpp
        media/video_feed.cpp
        media/video_source.cpp
        net/http.cpp
        net/websocket.cpp
        gpu/vk.cpp
        gpu/vma.cpp
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
add_library(Elegence::Elegence ALIAS elegence)

target_compile_features(elegence PUBLIC cxx_std_23)

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
generate_export_header(elegence
        BASE_NAME ELG
        EXPORT_MACRO_NAME ELG_API
        EXPORT_FILE_NAME ${CMAKE_CURRENT_BINARY_DIR}/generated/elg/export.h
        CUSTOM_CONTENT_FROM_VARIABLE ELG_EXPORT_EXTRA
)
if (NOT ELG_BUILD_SHARED)
    target_compile_definitions(elegence PUBLIC ELG_STATIC_DEFINE)
endif ()

find_package(SDL3 CONFIG REQUIRED)
find_package(volk CONFIG REQUIRED)
find_package(VulkanMemoryAllocator CONFIG REQUIRED)
find_package(Freetype REQUIRED)
find_package(harfbuzz CONFIG REQUIRED)
find_package(msdfgen CONFIG REQUIRED)
# Accessibility (design.md §12): AccessKit's official prebuilt C library (MIT / Apache-2.0),
# linked statically, so building Elegence needs no Rust toolchain.
if (ELG_ACCESSIBILITY)
    include(FetchContent)
    FetchContent_Declare(accesskit_c
        URL https://github.com/AccessKit/accesskit-c/releases/download/0.23.1/accesskit-c-0.23.1.zip
        URL_HASH SHA256=35b7ca8a6f1e038b5da35e1e9e5a0adaed9bfcf21e1496d29598fbbadcc7043f
        DOWNLOAD_EXTRACT_TIMESTAMP TRUE
        SOURCE_SUBDIR no-cmake-here) # use the prebuilt libraries, don't build it
    FetchContent_MakeAvailable(accesskit_c)
    include("${accesskit_c_SOURCE_DIR}/accesskit-config.cmake")
    target_link_libraries(elegence PRIVATE accesskit-static)
    target_compile_definitions(elegence PRIVATE ELG_HAVE_ACCESSKIT=1)
endif ()

# miniaudio is a single header (design.md §13.3).
find_path(MINIAUDIO_INCLUDE_DIR miniaudio.h REQUIRED)
target_include_directories(elegence PRIVATE "${MINIAUDIO_INCLUDE_DIR}")

# Private: no SDL or Vulkan types appear in public headers (design.md §4).
# Built-in icon sets (design.md §13.2), embedded into the library.
include(ElgEmbed)
if (ELG_ICONS)
    set(phosphor "${PROJECT_SOURCE_DIR}/third_party/phosphor")
    elg_embed_file(elegence phosphor_regular "${phosphor}/Phosphor.ttf")
    elg_embed_file(elegence phosphor_bold "${phosphor}/Phosphor-Bold.ttf")
    elg_embed_file(elegence phosphor_fill "${phosphor}/Phosphor-Fill.ttf")
    elg_embed_file(elegence phosphor_css "${phosphor}/phosphor.css")
    target_compile_definitions(elegence PRIVATE ELG_HAS_PHOSPHOR)
    if (ELG_ICONS_EXTRA_WEIGHTS)
        elg_embed_file(elegence phosphor_thin "${phosphor}/Phosphor-Thin.ttf")
        elg_embed_file(elegence phosphor_light "${phosphor}/Phosphor-Light.ttf")
        elg_embed_file(elegence phosphor_duotone "${phosphor}/Phosphor-Duotone.ttf")
        elg_embed_file(elegence phosphor_duotone_css "${phosphor}/phosphor-duotone.css")
        target_compile_definitions(elegence PRIVATE ELG_HAS_PHOSPHOR_EXTRA)
    endif ()
endif ()

if (WIN32)
    target_sources(elegence PRIVATE gpu/composition_win32.cpp)
    # dwmapi: rounded corners for undecorated windows; d3d11/dxgi/dcomp: transparent windows.
    target_link_libraries(elegence PRIVATE dwmapi d3d11 dxgi dcomp windowscodecs ole32 propsys mfplat mfuuid mf mfreadwrite strmiids oleaut32 winhttp shell32)
endif ()

if (APPLE)
    # Video playback through AVFoundation (Objective-C++, ARC); images through ImageIO.
    enable_language(OBJCXX)
    target_sources(elegence PRIVATE media/video_source_apple.mm)
    set_source_files_properties(media/video_source_apple.mm PROPERTIES COMPILE_OPTIONS "-fobjc-arc")
    target_link_libraries(elegence PRIVATE
            "-framework AVFoundation" "-framework CoreMedia" "-framework CoreVideo" "-framework QuartzCore"
            "-framework Foundation" "-framework ImageIO" "-framework CoreGraphics" "-framework CoreFoundation")
endif ()

target_link_libraries(elegence PRIVATE
        SDL3::SDL3
        volk::volk
        GPUOpen::VulkanMemoryAllocator
        Freetype::Freetype
        harfbuzz::harfbuzz
        msdfgen::msdfgen-core
)

# vcpkg only deploys an executable's direct DLL dependencies, so copy ours (SDL3, ...) next to
# elegence.dll, which shares the output folder with every app.
if (WIN32 AND ELG_BUILD_SHARED)
    add_custom_command(TARGET elegence POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                    $<TARGET_RUNTIME_DLLS:elegence> $<TARGET_FILE_DIR:elegence>
            COMMAND_EXPAND_LISTS
    )
endif ()

target_include_directories(elegence
        PUBLIC
            ${PROJECT_SOURCE_DIR}/include
            ${CMAKE_CURRENT_BINARY_DIR}/generated
        PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}
)

elg_add_shaders(elegence
        ${PROJECT_SOURCE_DIR}/shaders/ui.vert
        ${PROJECT_SOURCE_DIR}/shaders/ui.frag
        ${PROJECT_SOURCE_DIR}/shaders/blur.vert
        ${PROJECT_SOURCE_DIR}/shaders/blur.frag
        ${PROJECT_SOURCE_DIR}/shaders/path.vert
        ${PROJECT_SOURCE_DIR}/shaders/path_cover.vert
        ${PROJECT_SOURCE_DIR}/shaders/path.frag
        ${PROJECT_SOURCE_DIR}/shaders/video_convert.frag
)

target_compile_definitions(elegence PRIVATE
        ELG_VERSION_STRING="${PROJECT_VERSION}"
        VK_NO_PROTOTYPES # every Vulkan function comes from volk
)
if (WIN32)
    target_compile_definitions(elegence PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
endif ()

# std::stacktrace lives in a separate library on GCC.
if (CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    target_link_libraries(elegence PRIVATE stdc++exp)
endif ()

set_target_properties(elegence PROPERTIES
        CXX_VISIBILITY_PRESET hidden
        VISIBILITY_INLINES_HIDDEN ON
)

if (MSVC)
    target_compile_options(elegence PRIVATE /W4 /permissive- /utf-8)
else ()
    target_compile_options(elegence PRIVATE -Wall -Wextra -Wpedantic)
endif ()
