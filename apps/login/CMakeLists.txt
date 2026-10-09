find_package(unofficial-nayuki-qr-code-generator CONFIG)
if (NOT unofficial-nayuki-qr-code-generator_FOUND)
    message(WARNING "Skipping the login example: enable the vcpkg \"examples\" feature for its QR code encoder")
    return()
endif ()

add_executable(login
        main.cpp
        qr_code.cpp
)

target_link_libraries(login PRIVATE
        Elegence::Elegence
        unofficial::nayuki-qr-code-generator::nayuki-qr-code-generator
)

if (MSVC)
    target_compile_options(login PRIVATE /W4 /permissive- /utf-8)
else ()
    target_compile_options(login PRIVATE -Wall -Wextra -Wpedantic)
endif ()
