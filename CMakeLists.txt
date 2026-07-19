cmake_minimum_required(VERSION 3.28)

if(POLICY CMP0141)
    cmake_policy(SET CMP0141 NEW)
endif()

project(OmegaStrainReimplementation VERSION 0.1.0 LANGUAGES CXX)

find_package(Threads REQUIRED)

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

add_library(omega_simulation STATIC
    native/src/simulation/entity_registry.cpp
    native/src/simulation/simulation_world.cpp
)
target_include_directories(omega_simulation PUBLIC native/include)
target_compile_features(omega_simulation PUBLIC cxx_std_23)

add_library(omega_retail_formats STATIC
    native/src/retail/col_spatial_mesh_decoder.cpp
    native/src/retail/container_descriptors.cpp
    native/src/retail/pop_level_manifest_decoder.cpp
    native/src/retail/pop_post_terrain_hypothesis_descriptor.cpp
    native/src/retail/ska_container_descriptor.cpp
    native/src/retail/skm_container_descriptor.cpp
    native/src/retail/skl_container_descriptor.cpp
    native/src/retail/tdx_texture_storage_decoder.cpp
    native/src/retail/vum_layout_internal.cpp
    native/src/retail/vum_material_catalog_decoder.cpp
    native/src/retail/vum_render_payload_descriptor.cpp
)
target_include_directories(omega_retail_formats PUBLIC native/include)
target_link_libraries(omega_retail_formats PUBLIC omega_assets omega_core)
target_compile_features(omega_retail_formats PUBLIC cxx_std_23)

add_library(omega_content STATIC
    native/src/content/game_data_service.cpp
    native/src/content/level_texture_store.cpp
)
target_include_directories(omega_content PUBLIC native/include)
target_link_libraries(omega_content PUBLIC omega_assets PRIVATE omega_core omega_retail_formats)
target_compile_features(omega_content PUBLIC cxx_std_23)

add_library(omega_runtime STATIC
    native/src/runtime/asset_service.cpp
    native/src/runtime/config_service.cpp
    native/src/runtime/content_startup.cpp
    native/src/runtime/frame_scheduler.cpp
    native/src/runtime/input_tracker.cpp
    native/src/runtime/job_service.cpp
    native/src/runtime/launch_options.cpp
    native/src/runtime/log_service.cpp
    native/src/runtime/spatial_debug_image.cpp
    native/src/runtime/runtime_settings.cpp
)
target_include_directories(omega_runtime PUBLIC native/include)
target_link_libraries(omega_runtime
    PUBLIC
        omega_assets
        omega_content
    PRIVATE
        Threads::Threads
)
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
    target_compile_options(omega_core PRIVATE -Wall -Wextra -Wpedantic -Werror -Wno-missing-field-initializers)
endif()

if(MSVC)
    target_compile_options(omega_simulation PRIVATE
        /W4
        /WX
        /permissive-
        /Zc:__cplusplus
        /Zc:preprocessor
        /utf-8
    )
else()
    target_compile_options(omega_simulation PRIVATE -Wall -Wextra -Wpedantic -Werror -Wno-missing-field-initializers)
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
    target_compile_options(omega_runtime PRIVATE -Wall -Wextra -Wpedantic -Werror -Wno-missing-field-initializers)
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
    target_compile_options(omega_content PRIVATE -Wall -Wextra -Wpedantic -Werror -Wno-missing-field-initializers)
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
    target_compile_options(omega_retail_formats PRIVATE -Wall -Wextra -Wpedantic -Werror -Wno-missing-field-initializers)
endif()

add_executable(omega_tool
    native/apps/omega_tool/asset_commands.cpp
    native/apps/omega_tool/level_texture_commands.cpp
    native/apps/omega_tool/main.cpp
    native/apps/omega_tool/pop_post_terrain_commands.cpp
    native/apps/omega_tool/pop_commands.cpp
)
target_link_libraries(omega_tool PRIVATE
    omega_core
    omega_retail_formats
    omega_content
    omega_runtime
    Threads::Threads
)

if(MSVC)
    target_compile_options(omega_tool PRIVATE /W4 /WX /permissive- /Zc:__cplusplus /Zc:preprocessor /utf-8)
else()
    target_compile_options(omega_tool PRIVATE -Wall -Wextra -Wpedantic -Werror -Wno-missing-field-initializers)
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

    add_library(omega_sdl_backend STATIC
        native/apps/openomega/sdl_audio_service.cpp
        native/apps/openomega/sdl_gpu_host.cpp
        native/apps/openomega/sdl_input_service.cpp
        native/apps/openomega/sdl_platform_service.cpp
    )
    target_include_directories(omega_sdl_backend PUBLIC native/apps/openomega)
    target_link_libraries(omega_sdl_backend
        PUBLIC
            SDL3::SDL3
        PRIVATE
            omega_runtime
    )
    target_compile_features(omega_sdl_backend PUBLIC cxx_std_23)
    if(MSVC)
        target_compile_options(omega_sdl_backend PRIVATE /W4 /WX /permissive- /Zc:__cplusplus /Zc:preprocessor /utf-8)
    else()
        target_compile_options(omega_sdl_backend PRIVATE -Wall -Wextra -Wpedantic -Werror -Wno-missing-field-initializers)
    endif()

    add_executable(openomega
        native/apps/openomega/main.cpp
        native/apps/openomega/omega_app.cpp
    )
    target_link_libraries(openomega PRIVATE omega_runtime omega_simulation omega_sdl_backend)
    target_compile_definitions(openomega PRIVATE
        $<$<CONFIG:Debug>:OMEGA_GPU_DEBUG=1>
    )
    if(MSVC)
        target_compile_options(openomega PRIVATE /W4 /WX /permissive- /Zc:__cplusplus /Zc:preprocessor /utf-8)
    else()
        target_compile_options(openomega PRIVATE -Wall -Wextra -Wpedantic -Werror -Wno-missing-field-initializers)
    endif()
endif()

include(CTest)
if(BUILD_TESTING)
    add_executable(omega_pop_post_terrain_commands_tests
        native/tests/pop_post_terrain_commands_tests.cpp
        native/apps/omega_tool/pop_post_terrain_commands.cpp
    )
    target_include_directories(omega_pop_post_terrain_commands_tests PRIVATE
        native/apps/omega_tool
    )
    target_link_libraries(omega_pop_post_terrain_commands_tests PRIVATE omega_retail_formats)
    if(MSVC)
        target_compile_options(omega_pop_post_terrain_commands_tests PRIVATE
            /W4
            /WX
            /permissive-
            /Zc:__cplusplus
            /Zc:preprocessor
            /utf-8
        )
    else()
        target_compile_options(omega_pop_post_terrain_commands_tests PRIVATE -Wall -Wextra -Wpedantic -Werror -Wno-missing-field-initializers)
    endif()
    add_test(NAME omega_pop_post_terrain_commands_tests COMMAND omega_pop_post_terrain_commands_tests)
    set_tests_properties(omega_pop_post_terrain_commands_tests PROPERTIES TIMEOUT 10)

    add_executable(omega_level_texture_commands_tests
        native/tests/level_texture_commands_tests.cpp
        native/apps/omega_tool/level_texture_commands.cpp
    )
    target_include_directories(omega_level_texture_commands_tests PRIVATE
        native/apps/omega_tool
    )
    target_link_libraries(omega_level_texture_commands_tests PRIVATE
        omega_runtime
        omega_content
        Threads::Threads
    )
    if(MSVC)
        target_compile_options(omega_level_texture_commands_tests PRIVATE
            /W4
            /WX
            /permissive-
            /Zc:__cplusplus
            /Zc:preprocessor
            /utf-8
        )
    else()
        target_compile_options(omega_level_texture_commands_tests PRIVATE -Wall -Wextra -Wpedantic -Werror -Wno-missing-field-initializers)
    endif()
    add_test(NAME omega_level_texture_commands_tests COMMAND omega_level_texture_commands_tests)
    set_tests_properties(omega_level_texture_commands_tests PROPERTIES TIMEOUT 10)

    add_executable(omega_level_texture_store_tests
        native/tests/level_texture_store_tests.cpp
    )
    target_link_libraries(omega_level_texture_store_tests PRIVATE
        omega_content
        omega_retail_formats
        Threads::Threads
    )
    if(MSVC)
        target_compile_options(omega_level_texture_store_tests PRIVATE
            /W4
            /WX
            /permissive-
            /Zc:__cplusplus
            /Zc:preprocessor
            /utf-8
        )
    else()
        target_compile_options(omega_level_texture_store_tests PRIVATE -Wall -Wextra -Wpedantic -Werror -Wno-missing-field-initializers)
    endif()
    add_test(NAME omega_level_texture_store_tests COMMAND omega_level_texture_store_tests)
    set_tests_properties(omega_level_texture_store_tests PROPERTIES TIMEOUT 10)

    add_executable(omega_asset_service_tests
        native/tests/asset_service_tests.cpp
    )
    target_link_libraries(omega_asset_service_tests PRIVATE
        omega_runtime
        omega_content
        omega_retail_formats
        Threads::Threads
    )
    if(MSVC)
        target_compile_options(omega_asset_service_tests PRIVATE
            /W4
            /WX
            /permissive-
            /Zc:__cplusplus
            /Zc:preprocessor
            /utf-8
        )
    else()
        target_compile_options(omega_asset_service_tests PRIVATE -Wall -Wextra -Wpedantic -Werror -Wno-missing-field-initializers)
    endif()
    add_test(NAME omega_asset_service_tests COMMAND omega_asset_service_tests)
    set_tests_properties(omega_asset_service_tests PROPERTIES TIMEOUT 10)

    if(TARGET omega_sdl_backend)
        add_executable(omega_sdl_audio_tests
            native/tests/sdl_audio_service_tests.cpp
        )
        target_link_libraries(omega_sdl_audio_tests PRIVATE omega_sdl_backend Threads::Threads)
        if(MSVC)
            target_compile_options(omega_sdl_audio_tests PRIVATE /W4 /WX /permissive- /Zc:__cplusplus /Zc:preprocessor /utf-8)
        else()
            target_compile_options(omega_sdl_audio_tests PRIVATE -Wall -Wextra -Wpedantic -Werror -Wno-missing-field-initializers)
        endif()
        add_test(NAME omega_sdl_audio_tests COMMAND omega_sdl_audio_tests)
        set_tests_properties(omega_sdl_audio_tests PROPERTIES
            ENVIRONMENT "SDL_AUDIO_DRIVER=dummy"
            TIMEOUT 5
        )

        add_executable(omega_sdl_input_tests
            native/tests/sdl_input_service_tests.cpp
        )
        target_link_libraries(omega_sdl_input_tests
            PRIVATE
                omega_sdl_backend
                omega_runtime
                Threads::Threads
        )
        if(MSVC)
            target_compile_options(omega_sdl_input_tests PRIVATE /W4 /WX /permissive- /Zc:__cplusplus /Zc:preprocessor /utf-8)
        else()
            target_compile_options(omega_sdl_input_tests PRIVATE -Wall -Wextra -Wpedantic -Werror -Wno-missing-field-initializers)
        endif()
        add_test(NAME omega_sdl_input_tests COMMAND omega_sdl_input_tests)
        set_tests_properties(omega_sdl_input_tests PROPERTIES TIMEOUT 5)
    endif()

    add_executable(omega_core_tests
        native/tests/col_spatial_mesh_decoder_tests.cpp
        native/tests/component_store_tests.cpp
        native/tests/config_service_tests.cpp
        native/tests/container_descriptor_tests.cpp
        native/tests/entity_registry_tests.cpp
        native/tests/frame_scheduler_tests.cpp
        native/tests/game_data_service_tests.cpp
        native/apps/omega_tool/pop_commands.cpp
        native/tests/hog_archive_tests.cpp
        native/tests/input_tracker_tests.cpp
        native/tests/job_service_tests.cpp
        native/tests/launch_options_tests.cpp
        native/tests/log_service_tests.cpp
        native/tests/spatial_debug_image_tests.cpp
        native/tests/pop_level_manifest_decoder_tests.cpp
        native/tests/pop_post_terrain_hypothesis_descriptor_tests.cpp
        native/tests/pop_terrain_index_tests.cpp
        native/tests/runtime_settings_tests.cpp
        native/tests/simulation_world_tests.cpp
        native/tests/ska_container_descriptor_tests.cpp
        native/tests/skm_container_descriptor_tests.cpp
        native/tests/skl_container_descriptor_tests.cpp
        native/tests/tdx_texture_storage_decoder_tests.cpp
        native/tests/virtual_file_system_tests.cpp
        native/tests/vum_material_catalog_decoder_tests.cpp
        native/tests/vum_render_payload_descriptor_tests.cpp
    )
    target_link_libraries(omega_core_tests PRIVATE
        omega_core
        omega_retail_formats
        omega_content
        omega_runtime
        omega_simulation
    )
    target_include_directories(omega_core_tests PRIVATE native/apps/omega_tool)
    if(MSVC)
        target_compile_options(omega_core_tests PRIVATE /W4 /WX /permissive- /Zc:__cplusplus /Zc:preprocessor /utf-8)
    else()
        target_compile_options(omega_core_tests PRIVATE -Wall -Wextra -Wpedantic -Werror -Wno-missing-field-initializers)
    endif()
    add_test(NAME omega_core_tests COMMAND omega_core_tests)

    file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/empty-corpus")
    add_test(NAME omega_tool_rejects_empty_hog_tree
        COMMAND omega_tool hog-verify-tree "${CMAKE_CURRENT_BINARY_DIR}/empty-corpus")
    add_test(NAME omega_tool_rejects_empty_nested_hog_tree
        COMMAND omega_tool hog-verify-nested-tree "${CMAKE_CURRENT_BINARY_DIR}/empty-corpus")
    add_test(NAME omega_tool_rejects_empty_pop_tree
        COMMAND omega_tool pop-verify-tree "${CMAKE_CURRENT_BINARY_DIR}/empty-corpus")
    add_test(NAME omega_tool_rejects_empty_pop_post_terrain_hypotheses_tree
        COMMAND omega_tool pop-post-terrain-hypotheses-verify-tree
            "${CMAKE_CURRENT_BINARY_DIR}/empty-corpus")
    add_test(NAME omega_tool_rejects_empty_level_manifest_tree
        COMMAND omega_tool level-manifest-verify-tree "${CMAKE_CURRENT_BINARY_DIR}/empty-corpus")
    add_test(NAME omega_tool_rejects_empty_level_spatial_tree
        COMMAND omega_tool level-spatial-verify-tree "${CMAKE_CURRENT_BINARY_DIR}/empty-corpus")
    add_test(NAME omega_tool_rejects_empty_level_material_catalogs_tree
        COMMAND omega_tool level-material-catalogs-verify-tree
            "${CMAKE_CURRENT_BINARY_DIR}/empty-corpus")
    add_test(NAME omega_tool_rejects_empty_level_texture_store_tree
        COMMAND omega_tool level-texture-store-verify-tree
            "${CMAKE_CURRENT_BINARY_DIR}/empty-corpus")
    add_test(NAME omega_tool_rejects_empty_asset_service_tree
        COMMAND omega_tool asset-service-verify-tree
            "${CMAKE_CURRENT_BINARY_DIR}/empty-corpus")
    add_test(NAME omega_tool_empty_level_material_catalogs_signature
        COMMAND omega_tool level-material-catalogs-verify-tree
            "${CMAKE_CURRENT_BINARY_DIR}/empty-corpus")
    add_test(NAME omega_tool_rejects_empty_asset_metadata_tree
        COMMAND omega_tool asset-metadata-verify-tree "${CMAKE_CURRENT_BINARY_DIR}/empty-corpus")
    set_tests_properties(
        omega_tool_rejects_empty_hog_tree
        omega_tool_rejects_empty_nested_hog_tree
        omega_tool_rejects_empty_pop_tree
        omega_tool_rejects_empty_pop_post_terrain_hypotheses_tree
        omega_tool_rejects_empty_level_manifest_tree
        omega_tool_rejects_empty_level_spatial_tree
        omega_tool_rejects_empty_level_material_catalogs_tree
        omega_tool_rejects_empty_level_texture_store_tree
        omega_tool_rejects_empty_asset_service_tree
        omega_tool_rejects_empty_asset_metadata_tree
        PROPERTIES WILL_FAIL TRUE
    )
    set_tests_properties(
        omega_tool_empty_level_material_catalogs_signature
        PROPERTIES PASS_REGULAR_EXPRESSION "\"catalogs\":0"
    )
endif()
