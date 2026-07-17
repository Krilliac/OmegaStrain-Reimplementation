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

add_library(omega_assets INTERFACE)
target_include_directories(omega_assets INTERFACE native/include)
target_compile_features(omega_assets INTERFACE cxx_std_23)

add_library(omega_retail_formats STATIC
    native/src/retail/col_spatial_mesh_decoder.cpp
    native/src/retail/container_descriptors.cpp
    native/src/retail/pop_level_manifest_decoder.cpp
)
target_include_directories(omega_retail_formats PUBLIC native/include)
target_link_libraries(omega_retail_formats PUBLIC omega_assets omega_core)
target_compile_features(omega_retail_formats PUBLIC cxx_std_23)

add_library(omega_content STATIC
    native/src/content/game_data_service.cpp
)
target_include_directories(omega_content PUBLIC native/include)
target_link_libraries(omega_content PUBLIC omega_assets PRIVATE omega_core omega_retail_formats)
target_compile_features(omega_content PUBLIC cxx_std_23)

add_library(omega_runtime STATIC
    native/src/runtime/content_startup.cpp
    native/src/runtime/launch_options.cpp
    native/src/runtime/manifest_debug_image.cpp
)
target_include_directories(omega_runtime PUBLIC native/include)
target_link_libraries(omega_runtime PUBLIC omega_assets omega_content)
target_compile_features(omega_runtime PUBLIC cxx_std_23)

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

if(MSVC)
    target_compile_options(omega_runtime PRIVATE
        /W4
        /WX
        /permissive-
        /Zc:__cplusplus
        /Zc:preprocessor
        /utf-8
    )
else()
    target_compile_options(omega_runtime PRIVATE -Wall -Wextra -Wpedantic -Werror)
endif()

if(MSVC)
    target_compile_options(omega_content PRIVATE
        /W4
        /WX
        /permissive-
        /Zc:__cplusplus
        /Zc:preprocessor
        /utf-8
    )
else()
    target_compile_options(omega_content PRIVATE -Wall -Wextra -Wpedantic -Werror)
endif()

if(MSVC)
    target_compile_options(omega_retail_formats PRIVATE
        /W4
        /WX
        /permissive-
        /Zc:__cplusplus
        /Zc:preprocessor
        /utf-8
    )
else()
    target_compile_options(omega_retail_formats PRIVATE -Wall -Wextra -Wpedantic -Werror)
endif()

add_executable(omega_tool
    native/apps/omega_tool/asset_commands.cpp
    native/apps/omega_tool/main.cpp
    native/apps/omega_tool/pop_commands.cpp
)
target_link_libraries(omega_tool PRIVATE omega_core omega_retail_formats)

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
        native/apps/openomega/sdl_gpu_host.cpp
    )
    target_link_libraries(openomega PRIVATE omega_runtime SDL3::SDL3)
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
        native/tests/col_spatial_mesh_decoder_tests.cpp
        native/tests/container_descriptor_tests.cpp
        native/tests/game_data_service_tests.cpp
        native/tests/hog_archive_tests.cpp
        native/tests/launch_options_tests.cpp
        native/tests/manifest_debug_image_tests.cpp
        native/tests/pop_level_manifest_decoder_tests.cpp
        native/tests/pop_terrain_index_tests.cpp
        native/tests/virtual_file_system_tests.cpp
    )
    target_link_libraries(omega_core_tests PRIVATE
        omega_core
        omega_retail_formats
        omega_content
        omega_runtime
    )
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
    add_test(NAME omega_tool_rejects_empty_level_manifest_tree
        COMMAND omega_tool level-manifest-verify-tree "${CMAKE_CURRENT_BINARY_DIR}/empty-corpus")
    add_test(NAME omega_tool_rejects_empty_asset_metadata_tree
        COMMAND omega_tool asset-metadata-verify-tree "${CMAKE_CURRENT_BINARY_DIR}/empty-corpus")
    set_tests_properties(
        omega_tool_rejects_empty_hog_tree
        omega_tool_rejects_empty_nested_hog_tree
        omega_tool_rejects_empty_pop_tree
        omega_tool_rejects_empty_level_manifest_tree
        omega_tool_rejects_empty_asset_metadata_tree
        PROPERTIES WILL_FAIL TRUE
    )
endif()
