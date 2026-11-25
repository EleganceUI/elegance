if (VCPKG_CMAKE_SYSTEM_NAME STREQUAL "Mikros")
    # Mikros's fork has the build changes for Mikros, and the patch that the other branch applies, at its own
    # version (ELGSPEC-0040.R16).
    vcpkg_from_github(
        OUT_SOURCE_PATH SOURCE_PATH
        REPO GetMikros/SDL
        REF 5fc1749c4b64137e8ec340200f72c9210d4450fd
        SHA512 bb364e32055ce290b9fb05f7ddceded54ad902e18c04e13a9a932c5db1124728c167ffdcf9cb017d41bd66b7e91acbdb07bedeca924597e98e25916556fe7b6f
        HEAD_REF mikros
    )
else ()
    vcpkg_from_github(
        OUT_SOURCE_PATH SOURCE_PATH
        REPO libsdl-org/SDL
        REF "release-${VERSION}"
        SHA512 12fcb5592016b27922b08d1580d75f5f15c72e67cf04415e2bd824dc7563ac940c25bbe368b03d0a755f5832803049467e3d0ee6784ed3684792bc4d7741af50
        HEAD_REF main
        PATCHES
            fix-freebsd.patch
            100-close-file-dialogs.patch # Elegance: SDL_CloseFileDialogs() (cmake/ports/README.md)
    )
endif ()

string(COMPARE EQUAL "${VCPKG_LIBRARY_LINKAGE}" "static" SDL_STATIC)
string(COMPARE EQUAL "${VCPKG_LIBRARY_LINKAGE}" "dynamic" SDL_SHARED)
string(COMPARE EQUAL "${VCPKG_CRT_LINKAGE}" "static" FORCE_STATIC_VCRT)

vcpkg_check_features(OUT_FEATURE_OPTIONS FEATURE_OPTIONS
    FEATURES
        alsa     SDL_ALSA
        dbus     SDL_DBUS
        ibus     SDL_IBUS
        vulkan   SDL_VULKAN
        wayland  SDL_WAYLAND
        x11      SDL_X11
        libusb   SDL_HIDAPI_LIBUSB
)

if (VCPKG_TARGET_IS_EMSCRIPTEN)
    vcpkg_check_features(OUT_FEATURE_OPTIONS EMSCRIPTEN_FEATURE_OPTIONS
        FEATURES
            emscripten-pthreads     SDL_PTHREADS
    )
    vcpkg_list(APPEND FEATURE_OPTIONS "${EMSCRIPTEN_FEATURE_OPTIONS}")
endif()

if ("macos-keyboard-grab" IN_LIST FEATURES)
    # SDL only disables the system shortcuts in SDL_SetWindowKeyboardGrab() on
    # macOS if SDL_MAC_NO_SANDBOX is defined
    string(APPEND VCPKG_C_FLAGS " -DSDL_MAC_NO_SANDBOX")
    string(APPEND VCPKG_CXX_FLAGS " -DSDL_MAC_NO_SANDBOX")
endif()

if ("x11" IN_LIST FEATURES)
    message(WARNING "You will need to install Xorg dependencies to use feature x11:\nsudo apt install libx11-dev libxft-dev libxext-dev\n")
endif()
if ("wayland" IN_LIST FEATURES)
    message(WARNING "You will need to install Wayland dependencies to use feature wayland:\nsudo apt install libwayland-dev libxkbcommon-dev libegl1-mesa-dev\n")
endif()
if ("ibus" IN_LIST FEATURES)
    message(WARNING "You will need to install ibus dependencies to use feature ibus:\nsudo apt install libibus-1.0-dev\n")
endif()

if ("libusb" IN_LIST FEATURES)
    if(VCPKG_LIBRARY_LINKAGE STREQUAL "dynamic")
        vcpkg_list(APPEND FEATURE_OPTIONS "-DSDL_HIDAPI_LIBUSB_SHARED=ON")
    else()
        vcpkg_list(APPEND FEATURE_OPTIONS "-DSDL_HIDAPI_LIBUSB_SHARED=OFF")
    endif()
endif()

if (VCPKG_CMAKE_SYSTEM_NAME STREQUAL "Mikros")
    # vcpkg's default features follow its own list of systems, which has no Mikros, so this turns Wayland and
    # Vulkan on and X11, D-Bus, IBus and ALSA off itself (ELGSPEC-0040.R3).
    vcpkg_list(APPEND FEATURE_OPTIONS -DSDL_WAYLAND=ON -DSDL_VULKAN=ON -DSDL_X11=OFF -DSDL_DBUS=OFF -DSDL_IBUS=OFF
               -DSDL_ALSA=OFF)
endif ()

# option for not need to show windows
list(APPEND FEATURE_OPTIONS -DSDL_UNIX_CONSOLE_BUILD=ON)
if (VCPKG_TARGET_IS_LINUX AND NOT "x11" IN_LIST FEATURES AND NOT "wayland" IN_LIST FEATURES)
    message(WARNING "The selected features don't allow sdl3 to create windows, which is usually unintentional. You can get windowing support by installing the x11 and/or wayland features.")
endif()

vcpkg_cmake_configure(
    SOURCE_PATH "${SOURCE_PATH}"
    OPTIONS
        ${FEATURE_OPTIONS}
        -DSDL_STATIC=${SDL_STATIC}
        -DSDL_SHARED=${SDL_SHARED}
        -DSDL_FORCE_STATIC_VCRT=${FORCE_STATIC_VCRT}
        -DSDL_LIBC=ON
        # Prevent host-installed Unix libraries from silently changing SDL's capabilities.
        -DSDL_FRIBIDI=OFF
        -DSDL_JACK=OFF
        -DSDL_KMSDRM=OFF
        -DSDL_LIBTHAI=OFF
        -DSDL_LIBUDEV=OFF
        -DSDL_LIBURING=OFF
        -DSDL_PIPEWIRE=OFF
        -DSDL_PULSEAUDIO=OFF
        -DSDL_ROCKCHIP=OFF
        -DSDL_RPI=OFF
        -DSDL_SNDIO=OFF
        -DSDL_WAYLAND_LIBDECOR=OFF
        -DSDL_TEST_LIBRARY=OFF
        -DSDL_TESTS=OFF
        -DSDL_X11_XCURSOR=OFF
        -DSDL_X11_XDBE=OFF
        -DSDL_X11_XFIXES=OFF
        -DSDL_X11_XINPUT=OFF
        -DSDL_X11_XRANDR=OFF
        -DSDL_X11_XSHAPE=OFF
        -DSDL_X11_XSCRNSAVER=OFF
        -DSDL_X11_XSYNC=OFF
        -DSDL_X11_XTEST=OFF
        -DSDL_INSTALL_CMAKEDIR_ROOT=share/${PORT}
        # Specifying the revision skips the need to use git to determine a version
        -DSDL_REVISION=vcpkg
    MAYBE_UNUSED_VARIABLES
        SDL_FORCE_STATIC_VCRT
)

vcpkg_cmake_install()
vcpkg_cmake_config_fixup()

file(REMOVE_RECURSE
    "${CURRENT_PACKAGES_DIR}/debug/include"
    "${CURRENT_PACKAGES_DIR}/debug/share"
)

vcpkg_copy_pdbs()
vcpkg_fixup_pkgconfig()

file(INSTALL "${CMAKE_CURRENT_LIST_DIR}/usage" DESTINATION "${CURRENT_PACKAGES_DIR}/share/${PORT}")
vcpkg_install_copyright(
    FILE_LIST
        "${SOURCE_PATH}/LICENSE.txt"
        "${SOURCE_PATH}/src/hidapi/LICENSE-bsd.txt"
        "${SOURCE_PATH}/src/video/stb_image.h"
        "${SOURCE_PATH}/src/video/yuv2rgb/LICENSE"
        "${SOURCE_PATH}/include/SDL3/SDL_opengles2_gl2.h"
        "${SOURCE_PATH}/include/SDL3/SDL_opengles2_gl2platform.h"
    COMMENT "SDL_opengles2_gl2platform.h is licensed under Apache-2.0; see https://www.apache.org/licenses/LICENSE-2.0."
)
