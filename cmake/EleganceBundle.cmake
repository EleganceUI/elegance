if (NOT DEFINED APP OR NOT IS_DIRECTORY "${APP}")
    message(FATAL_ERROR "APP must identify an existing application bundle")
endif ()

include(BundleUtilities)
fixup_bundle("${APP}" "" "${SEARCH_DIRS}")

# install_name_tool changes invalidate Mach-O signatures, including the prebuilt AccessKit one.
# Ad-hoc signing makes local Apple Silicon builds runnable; distribution still needs signing
# with the developer's identity and notarization.
find_program(codesign codesign REQUIRED)
execute_process(COMMAND "${codesign}" --force --deep --sign - "${APP}"
                COMMAND_ERROR_IS_FATAL ANY)
