# AccessKit's C library, from its release archive: the prebuilt libraries it ships for the target,
# or, where it ships none (Linux on ARM64), built from the Rust sources in the same archive, which
# takes cargo (Rust 1.87 or later). Both the static and the shared library are installed, whatever
# the triplet's linkage: a static Elegance links the shared one into Rust apps, whose own Rust
# standard library would clash with the one in the static library (ELG_ACCESSKIT_SHARED).
vcpkg_download_distfile(ARCHIVE
    URLS "https://github.com/AccessKit/accesskit-c/releases/download/${VERSION}/accesskit-c-${VERSION}.zip"
    FILENAME "accesskit-c-${VERSION}.zip"
    SHA512 53078608dd29930978e853346cda700157dc95d04c36952f815354817bf7eb8199077d3db9011bb0574315e955c9bbb332e3d1187200eaff82cfecf2ae3810ef
)
vcpkg_extract_source_archive(SOURCE_PATH ARCHIVE "${ARCHIVE}")

# Release builds only, used in every configuration: they link the release C runtime either way.
set(VCPKG_BUILD_TYPE release)

if (VCPKG_TARGET_ARCHITECTURE STREQUAL "x64")
    set(arch x86_64)
elseif (VCPKG_TARGET_ARCHITECTURE STREQUAL "arm64")
    set(arch arm64)
elseif (VCPKG_TARGET_ARCHITECTURE STREQUAL "x86")
    set(arch x86)
else ()
    message(FATAL_ERROR "accesskit-c: unsupported architecture ${VCPKG_TARGET_ARCHITECTURE}")
endif ()

if (VCPKG_TARGET_IS_WINDOWS)
    set(prebuilt "${SOURCE_PATH}/lib/windows/${arch}/msvc")
    set(static_name accesskit.lib)
    set(shared_name accesskit.dll)
    # The import library takes accesskit.lib; the static library goes beside it under another name.
    set(static_installed accesskit-static.lib)
    set(static_libraries bcrypt ntdll propsys runtimeobject uiautomationcore userenv ws2_32)
elseif (VCPKG_TARGET_IS_OSX)
    set(prebuilt "${SOURCE_PATH}/lib/macos/${arch}")
    set(static_name libaccesskit.a)
    set(shared_name libaccesskit.dylib)
    set(static_installed libaccesskit.a)
    set(static_libraries "-framework AppKit" "-framework Foundation" "-framework CoreFoundation" objc c++)
else ()
    set(prebuilt "${SOURCE_PATH}/lib/linux/${arch}")
    set(static_name libaccesskit.a)
    set(shared_name libaccesskit.so)
    set(static_installed libaccesskit.a)
    set(static_libraries -static-libgcc m)
endif ()

if (IS_DIRECTORY "${prebuilt}")
    set(static_dir "${prebuilt}/static")
    set(shared_dir "${prebuilt}/shared")
else ()
    # Built for the target's Rust triple, so a host of another architecture fails here (that
    # target isn't installed) rather than installing libraries the linker would reject later.
    if (VCPKG_TARGET_IS_LINUX AND arch STREQUAL "arm64")
        set(rust_target aarch64-unknown-linux-gnu)
    else ()
        message(FATAL_ERROR "accesskit-c: no prebuilt library for ${TARGET_TRIPLET} (${prebuilt}), "
                            "and building one from source is only set up for Linux on ARM64")
    endif ()
    find_program(CARGO cargo PATHS "$ENV{CARGO_HOME}/bin" "$ENV{HOME}/.cargo/bin")
    if (NOT CARGO)
        message(FATAL_ERROR "accesskit-c: AccessKit has no prebuilt library for ${TARGET_TRIPLET}, so it's built "
                            "from source, which needs Rust 1.87 or later (https://rustup.rs): cargo wasn't found")
    endif ()
    set(target_dir "${CURRENT_BUILDTREES_DIR}/${TARGET_TRIPLET}-rel")
    vcpkg_execute_required_process(
        COMMAND "${CARGO}" build --release --locked --target "${rust_target}" --target-dir "${target_dir}"
        WORKING_DIRECTORY "${SOURCE_PATH}"
        LOGNAME "cargo-build-${TARGET_TRIPLET}"
    )
    set(static_dir "${target_dir}/${rust_target}/release")
    set(shared_dir "${static_dir}")
endif ()

file(INSTALL "${SOURCE_PATH}/include/accesskit.h" DESTINATION "${CURRENT_PACKAGES_DIR}/include")
file(INSTALL "${static_dir}/${static_name}" DESTINATION "${CURRENT_PACKAGES_DIR}/lib" RENAME "${static_installed}")
if (VCPKG_TARGET_IS_WINDOWS)
    file(INSTALL "${shared_dir}/accesskit.lib" DESTINATION "${CURRENT_PACKAGES_DIR}/lib")
    file(INSTALL "${shared_dir}/accesskit.dll" "${shared_dir}/accesskit.pdb" DESTINATION "${CURRENT_PACKAGES_DIR}/bin")
else ()
    file(INSTALL "${shared_dir}/${shared_name}" DESTINATION "${CURRENT_PACKAGES_DIR}/lib")
endif ()

# The targets: accesskit-c::static and accesskit-c::shared.
list(JOIN static_libraries "\" \"" static_libraries)
set(static_libraries "\"${static_libraries}\"")
configure_file("${CMAKE_CURRENT_LIST_DIR}/accesskit-c-config.cmake.in"
               "${CURRENT_PACKAGES_DIR}/share/${PORT}/accesskit-c-config.cmake" @ONLY)
file(INSTALL "${CMAKE_CURRENT_LIST_DIR}/usage" DESTINATION "${CURRENT_PACKAGES_DIR}/share/${PORT}")
vcpkg_install_copyright(FILE_LIST "${SOURCE_PATH}/LICENSE-MIT" "${SOURCE_PATH}/LICENSE-APACHE"
                                  "${SOURCE_PATH}/LICENSE.chromium")

# Both libraries, whatever the linkage (see the top): a DLL in static triplets, and a static
# library beside the import library in dynamic ones. Rust's libraries link the DLL C runtime
# (/MD), in static-runtime triplets too.
set(VCPKG_POLICY_DLLS_IN_STATIC_LIBRARY enabled)
set(VCPKG_POLICY_MISMATCHED_NUMBER_OF_BINARIES enabled)
set(VCPKG_POLICY_SKIP_CRT_LINKAGE_CHECK enabled)
