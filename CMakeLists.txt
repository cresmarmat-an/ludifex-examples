cmake_minimum_required(VERSION 3.22)

project(ludifex_examples
    DESCRIPTION "Example programs for ludifex"
    LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# The library, downloaded from GitHub at the release these examples are
# written for, along with its dependencies. To build against your own copy
# instead, configure with
#   -DFETCHCONTENT_SOURCE_DIR_LUDIFEX=path/to/ludifex
include(FetchContent)
FetchContent_Declare(ludifex
    GIT_REPOSITORY https://github.com/cresmarmat-an/ludifex.git
    GIT_TAG v0.0.1
    GIT_SHALLOW TRUE)
FetchContent_MakeAvailable(ludifex)

function(add_example name)
    # Every example can report where it crashed; see common/CrashReport.h.
    add_executable(${name} ${name}/main.cpp ${CMAKE_CURRENT_SOURCE_DIR}/common/CrashReport.cpp)
    target_link_libraries(${name} PRIVATE ${ARGN})

    # Lets an example find its own shader files, and lets hot reload watch the
    # real source rather than a copy.
    target_compile_definitions(${name} PRIVATE
        EXAMPLE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/${name}")

    set_target_properties(${name} PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
        RUNTIME_OUTPUT_DIRECTORY_DEBUG ${CMAKE_BINARY_DIR}/bin
        RUNTIME_OUTPUT_DIRECTORY_RELEASE ${CMAKE_BINARY_DIR}/bin)

    # SDL3 builds as a shared library, so its DLL has to sit next to the
    # executable. An example that links no shared library produces an empty
    # list, and copy_if_different with only a destination is an error, so the
    # command degrades to a no-op in that case.
    if(WIN32)
        add_custom_command(TARGET ${name} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E
                    "$<IF:$<BOOL:$<TARGET_RUNTIME_DLLS:${name}>>,copy_if_different,true>"
                    "$<TARGET_RUNTIME_DLLS:${name}>" "$<TARGET_FILE_DIR:${name}>"
            COMMAND_EXPAND_LISTS
            VERBATIM)
    endif()
endfunction()

# Automated checks of the library, run without a window.
add_example(01-checks ludifex::ludifex)

# world.Run() without an interface library, in the world's own window.
add_example(02-run-standalone ludifex::ludifex)

# Counts heap allocations while stepping, by replacing new and delete, and
# measures how busy each worker thread is.
add_example(03-benchmark ludifex::ludifex)
