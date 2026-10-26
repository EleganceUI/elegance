# Strict compiler warnings: the usual set for C++ projects, for Elegance itself and for apps built
# with it, so the compiler points out the mistakes that would otherwise crash an app at run time
# (narrowing conversions, shadowed variables, missing overrides, ...).
#
#   elegance_warnings(my_target)
#
# elegance_application() and elegance_views_module() apply them already. ELG_WARNINGS_AS_ERRORS
# turns them into errors (for CI).

option(ELG_WARNINGS_AS_ERRORS "Treat compiler warnings in Elegance's own code as errors" OFF)

function(elegance_warnings target)
    if (MSVC)
        set(warnings
            /W4
            /permissive-
            /w14242 # conversion, possible loss of data
            /w14254 # larger bit field to smaller, possible loss of data
            /w14263 # member function doesn't override any base virtual function
            /w14265 # class with virtual functions has a non-virtual destructor
            /w14287 # unsigned/negative constant mismatch
            /we4289 # loop variable used outside the loop
            /w14296 # expression is always true/false
            /w14311 # pointer truncation
            /w14545 /w14546 /w14547 /w14549 # suspicious comma expressions
            /w14555 # expression has no effect
            /w14619 # unknown warning number in #pragma warning
            /w14640 # thread-unsafe static member initialization
            /w14826 # sign-extending conversion
            /w14905 /w14906 # string literal cast to the wrong string type
            /w14928 # more than one user-defined conversion applied implicitly
        )
        if (ELG_WARNINGS_AS_ERRORS)
            list(APPEND warnings /WX)
        endif ()
    else ()
        set(warnings
            -Wall
            -Wextra
            -Wpedantic
            -Wnon-virtual-dtor
            -Wold-style-cast
            -Wcast-align
            -Wcast-qual
            -Wunused
            -Woverloaded-virtual
            -Wconversion
            -Wsign-conversion
            -Wdouble-promotion
            -Wformat=2
            -Wimplicit-fallthrough
            -Wextra-semi
            -Wmissing-declarations
            -Wundef
            -Wzero-as-null-pointer-constant
            -Wsuggest-override
        )
        if (CMAKE_CXX_COMPILER_ID MATCHES "Clang")
            list(APPEND warnings
                -Wshadow # with the field checks, about what GCC's -Wshadow covers
                -Wshadow-field
                -Wshadow-field-in-constructor
                -Wmissing-prototypes
                -Wcomma
                -Wdeprecated # e.g. implicit copies of classes with a user-declared destructor
                -Wunreachable-code-aggressive
            )
        else ()
            list(APPEND warnings
                -Wshadow
                -Wnull-dereference
                -Wduplicated-cond
                -Wduplicated-branches
                -Wlogical-op
                -Wdeprecated-copy-dtor
                # Level 1, as Clang's: overriding one overload of a virtual function (a
                # CachedLayout's cached size_that_fits) and so hiding the others is intended.
                -Woverloaded-virtual=1
                # GCC also flags designated initializers that leave fields at their defaults,
                # Elegance's idiom ({.title = "Notes"}); Clang doesn't.
                -Wno-missing-field-initializers
            )
        endif ()
        if (ELG_WARNINGS_AS_ERRORS)
            list(APPEND warnings -Werror)
        endif ()
    endif ()
    target_compile_options(${target} PRIVATE ${warnings})
endfunction()
