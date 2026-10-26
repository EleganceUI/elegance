# Release applications use the platform's GUI launcher; debug builds retain terminal output. The
# app compiles with Elegance's strict warnings (ElgWarnings.cmake).
function(elegance_application target)
    elegance_warnings(${target})
    if (WIN32)
        set_property(TARGET ${target} PROPERTY WIN32_EXECUTABLE "$<NOT:$<CONFIG:Debug>>")
        if (MSVC)
            # The examples use standard main(), including in the Windows GUI subsystem.
            target_link_options(${target} PRIVATE "$<$<NOT:$<CONFIG:Debug>>:/ENTRY:mainCRTStartup>")
        endif ()
    elseif (APPLE AND NOT CMAKE_BUILD_TYPE STREQUAL "Debug")
        set_target_properties(${target} PROPERTIES
                MACOSX_BUNDLE TRUE
                MACOSX_BUNDLE_BUNDLE_NAME "${target}"
                MACOSX_BUNDLE_GUI_IDENTIFIER "org.elegance.${target}"
                MACOSX_BUNDLE_BUNDLE_VERSION "0.1.0"
                MACOSX_BUNDLE_SHORT_VERSION_STRING "0.1.0"
                MACOSX_BUNDLE_INFO_PLIST "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/EleganceApplication.plist.in"
                BUILD_WITH_INSTALL_RPATH TRUE
                INSTALL_RPATH "@executable_path/../Frameworks")
        set_property(TARGET ${target} APPEND PROPERTY LINK_DEPENDS
                "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/EleganceBundle.cmake"
                "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/EleganceApplication.plist.in")
        set(search_dirs "$<TARGET_FILE_DIR:Elegance::Elegance>")
        if (TARGET accesskit-shared)
            list(APPEND search_dirs "$<TARGET_FILE_DIR:accesskit-shared>")
        endif ()
        add_custom_command(TARGET ${target} POST_BUILD
                COMMAND "${CMAKE_COMMAND}"
                    "-DAPP=$<TARGET_BUNDLE_DIR:${target}>"
                    "-DSEARCH_DIRS=${search_dirs}"
                    -P "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/EleganceBundle.cmake"
                VERBATIM)
    endif ()
endfunction()
