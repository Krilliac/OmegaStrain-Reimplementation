cmake_minimum_required(VERSION 3.28)

if(POLICY CMP0141)
    cmake_policy(SET CMP0141 NEW)
endif()

project(OmegaStrainReimplementation VERSION 0.1.0 LANGUAGES CXX)

if(CMAKE_SIZEOF_VOID_P LESS 8)
    message(FATAL_ERROR "OpenOmega requires a 64-bit host target (x86-64 is primary; portable core may support ARM64).")
endif()

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_MSVC_DEBUG_INFORMATION_FORMAT Embedded)

option(OMEGA_BUILD_RUNTIME "Build the OpenOmega native host shell" ON)

add_library(omega_core STATIC
    native/src/archive/hog_archive.cpp
    native/src/asset/pop_terrain_index.cpp
    native/src/vfs/virtual_file_system.cpp
)

target_include_directories(omega_core
    PUBLIC
        native/include
)

target_compile_features(omega_core PUBLIC cxx_std_23)

if(MSVC)
    target_compile_options(omega_core PRIVATE
        /W4
        /WX
        /permissive-
        /Zc:__cplusplus
        /Zc:preprocessor
        /utf-8
    )
else()
    target_compile_options(omega_core PRIVATE -Wall -Wextra -Wpedantic -Werror)
endif()

add_executable(omega_tool
    native/apps/omega_tool/main.cpp
    native/apps/omega_tool/pop_commands.cpp
)
target_link_libraries(omega_tool PRIVATE omega_core)

if(MSVC)
    target_compile_options(omega_tool PRIVATE /W4 /WX /permissive- /Zc:__cplusplus /Zc:preprocessor /utf-8)
else()
    target_compile_options(omega_tool PRIVATE -Wall -Wextra -Wpedantic -Werror)
endif()

if(OMEGA_BUILD_RUNTIME)
    include(FetchContent)
    set(SDL_SHARED OFF CACHE BOOL "" FORCE)
    set(SDL_STATIC ON CACHE BOOL "" FORCE)
    set(SDL_TEST_LIBRARY OFF CACHE BOOL "" FORCE)
    set(SDL_TESTS OFF CACHE BOOL "" FORCE)
    set(SDL_EXAMPLES OFF CACHE BOOL "" FORCE)
    set(SDL_INSTALL OFF CACHE BOOL "" FORCE)
    FetchContent_Declare(SDL3
        GIT_REPOSITORY https://github.com/libsdl-org/SDL.git
        GIT_TAG 8e37db5e797b6167f3a00d697d816a684bd259c7
        GIT_SHALLOW TRUE
        GIT_PROGRESS TRUE
    )
    FetchContent_MakeAvailable(SDL3)

    add_executable(openomega
        native/apps/openomega/main.cpp
    )
    target_link_libraries(openomega PRIVATE omega_core SDL3::SDL3)
    target_compile_definitions(openomega PRIVATE
        $<$<CONFIG:Debug>:OMEGA_GPU_DEBUG=1>
    )
    if(MSVC)
        target_compile_options(openomega PRIVATE /W4 /WX /permissive- /Zc:__cplusplus /Zc:preprocessor /utf-8)
    else()
        target_compile_options(openomega PRIVATE -Wall -Wextra -Wpedantic -Werror)
    endif()
endif()

include(CTest)
if(BUILD_TESTING)
    add_executable(omega_core_tests
        native/tests/hog_archive_tests.cpp
        native/tests/pop_terrain_index_tests.cpp
        native/tests/virtual_file_system_tests.cpp
    )
    target_link_libraries(omega_core_tests PRIVATE omega_core)
    if(MSVC)
        target_compile_options(omega_core_tests PRIVATE /W4 /WX /permissive- /Zc:__cplusplus /Zc:preprocessor /utf-8)
    else()
        target_compile_options(omega_core_tests PRIVATE -Wall -Wextra -Wpedantic -Werror)
    endif()
    add_test(NAME omega_core_tests COMMAND omega_core_tests)

    file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/empty-corpus")
    add_test(NAME omega_tool_rejects_empty_hog_tree
        COMMAND omega_tool hog-verify-tree "${CMAKE_CURRENT_BINARY_DIR}/empty-corpus")
    add_test(NAME omega_tool_rejects_empty_nested_hog_tree
        COMMAND omega_tool hog-verify-nested-tree "${CMAKE_CURRENT_BINARY_DIR}/empty-corpus")
    add_test(NAME omega_tool_rejects_empty_pop_tree
        COMMAND omega_tool pop-verify-tree "${CMAKE_CURRENT_BINARY_DIR}/empty-corpus")
    set_tests_properties(
        omega_tool_rejects_empty_hog_tree
        omega_tool_rejects_empty_nested_hog_tree
        omega_tool_rejects_empty_pop_tree
        PROPERTIES WILL_FAIL TRUE
    )
endif()
