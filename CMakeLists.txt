cmake_minimum_required(VERSION 3.28)

if(POLICY CMP0141)
    cmake_policy(SET CMP0141 NEW)
endif()

project(OmegaStrainReimplementation VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_MSVC_RUNTIME_LIBRARY
    "MultiThreaded$<$<CONFIG:Debug>:Debug>$<$<NOT:$<CONFIG:Release>>:DLL>"
)
if(CMAKE_CXX_COMPILER_ID STREQUAL "MSVC")
    file(TO_NATIVE_PATH "${CMAKE_BINARY_DIR}" OMEGA_NATIVE_BINARY_DIRECTORY)
    file(TO_NATIVE_PATH "${CMAKE_SOURCE_DIR}" OMEGA_NATIVE_SOURCE_DIRECTORY)
    add_compile_options(
        "$<$<CONFIG:Release>:/experimental:deterministic>"
        "$<$<CONFIG:Release>:/pathmap:${OMEGA_NATIVE_BINARY_DIRECTORY}=.build>"
        "$<$<CONFIG:Release>:/pathmap:${OMEGA_NATIVE_SOURCE_DIRECTORY}=.>"
    )
endif()

find_package(Threads REQUIRED)

if(CMAKE_SIZEOF_VOID_P LESS 8)
    message(FATAL_ERROR "OpenOmega requires a 64-bit host target (x86-64 is primary; portable core may support ARM64).")
endif()

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_MSVC_DEBUG_INFORMATION_FORMAT Embedded)

option(OMEGA_BUILD_RUNTIME "Build the OpenOmega native host shell" ON)
option(OMEGA_RUN_GPU_SMOKE_TEST
    "Register the hardware/display-dependent SDL GPU smoke tests with CTest"
    OFF
)

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

add_library(omega_persistence STATIC
    native/src/persistence/native_save_path.cpp
    native/src/persistence/save_database.cpp
)
target_include_directories(omega_persistence PUBLIC native/include)
target_compile_features(omega_persistence PUBLIC cxx_std_23)

add_library(omega_profiles STATIC
    native/src/profiles/character_catalog.cpp
    native/src/profiles/profile_catalog.cpp
)
target_include_directories(omega_profiles PUBLIC native/include)
target_link_libraries(omega_profiles PUBLIC omega_persistence)
target_compile_features(omega_profiles PUBLIC cxx_std_23)

add_library(omega_ps2_compat STATIC
    native/src/compat/ps2_memory_card_export.cpp
    native/src/compat/ps2_memory_card_filesystem.cpp
    native/src/compat/ps2_memory_card_image.cpp
)
target_include_directories(omega_ps2_compat PUBLIC native/include)
target_compile_features(omega_ps2_compat PUBLIC cxx_std_23)

add_library(omega_assets INTERFACE)
target_include_directories(omega_assets INTERFACE native/include)
target_compile_features(omega_assets INTERFACE cxx_std_23)

add_library(omega_media STATIC
    native/src/media/media_foundation_h262_decoder.cpp
    native/src/media/mpeg_program_stream_descriptor.cpp
    native/src/media/mpeg_video_elementary_stream.cpp
    native/src/media/nv12_to_rgba8.cpp
    native/src/media/pss_pcm_audio_stream.cpp
)
target_include_directories(omega_media PUBLIC native/include)
target_link_libraries(omega_media PUBLIC omega_assets)
target_compile_features(omega_media PUBLIC cxx_std_23)
if(WIN32)
    target_link_libraries(omega_media PRIVATE mfplat mfuuid ole32)
endif()

add_library(omega_simulation STATIC
    native/src/simulation/entity_registry.cpp
    native/src/simulation/simulation_world.cpp
)
target_include_directories(omega_simulation PUBLIC native/include)
target_compile_features(omega_simulation PUBLIC cxx_std_23)

add_library(omega_gameplay STATIC
    native/src/gameplay/debug_locomotion.cpp
)
target_include_directories(omega_gameplay PUBLIC native/include)
target_link_libraries(omega_gameplay PUBLIC omega_simulation)
target_compile_features(omega_gameplay PUBLIC cxx_std_23)

add_library(omega_retail_formats STATIC
    native/src/retail/col_spatial_mesh_decoder.cpp
    native/src/retail/container_descriptors.cpp
    native/src/retail/fnt_envelope_descriptor.cpp
    native/src/retail/gui_envelope_descriptor.cpp
    native/src/retail/ie_envelope_descriptor.cpp
    native/src/retail/lpd_envelope_decoder.cpp
    native/src/retail/par_text_envelope_decoder.cpp
    native/src/retail/pop_level_manifest_decoder.cpp
    native/src/retail/pop_post_terrain_hypothesis_descriptor.cpp
    native/src/retail/ska_container_descriptor.cpp
    native/src/retail/skas_text_envelope_decoder.cpp
    native/src/retail/skm_container_descriptor.cpp
    native/src/retail/skl_container_descriptor.cpp
    native/src/retail/so_module_descriptor.cpp
    native/src/retail/tbl_envelope_descriptor.cpp
    native/src/retail/tdx_texture_storage_decoder.cpp
    native/src/retail/vag_adpcm_decoder.cpp
    native/src/retail/vpk_wrapper_envelope_decoder.cpp
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
    native/src/runtime/level_texture_topology_preview.cpp
    native/src/runtime/config_service.cpp
    native/src/runtime/content_startup.cpp
    native/src/runtime/content_startup_diagnostic.cpp
    native/src/runtime/frame_scheduler.cpp
    native/src/runtime/input_trace.cpp
    native/src/runtime/input_tracker.cpp
    native/src/runtime/job_service.cpp
    native/src/runtime/launch_options.cpp
    native/src/runtime/log_service.cpp
    native/src/runtime/packed24_transfer_debug_image.cpp
    native/src/runtime/render_draw_list.cpp
    native/src/runtime/render_mesh_draw_list.cpp
    native/src/runtime/render_mesh_pool.cpp
    native/src/runtime/render_texture_pool.cpp
    native/src/runtime/runtime_config_discovery.cpp
    native/src/runtime/run_capture_replay.cpp
    native/src/runtime/run_capture_session.cpp
    native/src/runtime/scheduler_elapsed_trace.cpp
    native/src/runtime/spatial_diagnostic_scene.cpp
    native/src/runtime/spatial_debug_image.cpp
    native/src/runtime/tdx_indexed8_candidate_debug_image.cpp
    native/src/runtime/texture_storage_topology_debug_image.cpp
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
    target_compile_options(omega_media PRIVATE
        /W4
        /WX
        /permissive-
        /Zc:__cplusplus
        /Zc:preprocessor
        /utf-8
    )
else()
    target_compile_options(omega_media PRIVATE
        -Wall
        -Wextra
        -Wpedantic
        -Werror
        -Wno-missing-field-initializers
    )
endif()

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
    target_compile_options(omega_persistence PRIVATE
        /W4
        /WX
        /permissive-
        /Zc:__cplusplus
        /Zc:preprocessor
        /utf-8
    )
else()
    target_compile_options(omega_persistence PRIVATE
        -Wall
        -Wextra
        -Wpedantic
        -Werror
        -Wno-missing-field-initializers
    )
endif()

if(MSVC)
    target_compile_options(omega_profiles PRIVATE
        /W4
        /WX
        /permissive-
        /Zc:__cplusplus
        /Zc:preprocessor
        /utf-8
    )
    target_compile_options(omega_ps2_compat PRIVATE
        /W4
        /WX
        /permissive-
        /Zc:__cplusplus
        /Zc:preprocessor
        /utf-8
    )
else()
    target_compile_options(omega_profiles PRIVATE
        -Wall
        -Wextra
        -Wpedantic
        -Werror
        -Wno-missing-field-initializers
    )
    target_compile_options(omega_ps2_compat PRIVATE
        -Wall
        -Wextra
        -Wpedantic
        -Werror
        -Wno-missing-field-initializers
    )
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
    target_compile_options(omega_gameplay PRIVATE
        /W4
        /WX
        /permissive-
        /Zc:__cplusplus
        /Zc:preprocessor
        /utf-8
    )
else()
    target_compile_options(omega_gameplay PRIVATE -Wall -Wextra -Wpedantic -Werror -Wno-missing-field-initializers)
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

add_library(omega_app_core STATIC
    native/apps/openomega/front_end.cpp
    native/apps/openomega/run_capture.cpp
    native/apps/openomega/run_replay_session.cpp
)
target_include_directories(omega_app_core PUBLIC native/apps/openomega)
target_link_libraries(omega_app_core
    PUBLIC omega_profiles omega_runtime omega_simulation omega_gameplay)
target_compile_features(omega_app_core PUBLIC cxx_std_23)
if(MSVC)
    target_compile_options(omega_app_core PRIVATE
        /W4
        /WX
        /permissive-
        /Zc:__cplusplus
        /Zc:preprocessor
        /utf-8
    )
else()
    target_compile_options(omega_app_core PRIVATE
        -Wall
        -Wextra
        -Wpedantic
        -Werror
        -Wno-missing-field-initializers
    )
endif()

add_executable(omega_tool
    native/apps/omega_tool/asset_commands.cpp
    native/apps/omega_tool/frontend_envelope_commands.cpp
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
    if(WIN32 AND MSVC AND CMAKE_CXX_COMPILER_ID STREQUAL "MSVC")
        if(NOT CMAKE_CXX_COMPILER_ARCHITECTURE_ID STREQUAL "x64")
            message(FATAL_ERROR
                "OpenOmega Windows portable packaging requires the MSVC x64 target architecture"
            )
        endif()

        set(OMEGA_WINDOWS_PORTABLE_PACKAGE_ENABLED ON)
        set(OMEGA_WINDOWS_PORTABLE_PACKAGE_ROOT
            "OpenOmega-${PROJECT_VERSION}-windows-x86_64"
        )

        # This must remain the first install rule. Unsupported configurations fail before
        # FetchContent subdirectories or project payloads can mutate the install prefix.
        install(CODE [=[
            if(NOT CMAKE_INSTALL_CONFIG_NAME STREQUAL "Release")
                message(FATAL_ERROR
                    "OpenOmega Windows portable installation requires Release configuration"
                )
            endif()
        ]=])
    endif()

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
        native/apps/openomega/startup_failure_dialog.cpp
    )
    target_include_directories(omega_sdl_backend
        PUBLIC native/apps/openomega
        # The pinned SDL source includes portable compiled forms of its permissively licensed
        # testgpu position/color shader. OpenOmega consumes those build-time headers directly so
        # the executable needs no runtime shader compiler or external shader files.
        PRIVATE "${SDL3_SOURCE_DIR}/test"
    )
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

    add_library(omega_native_persistence STATIC
        native/apps/openomega/native_persistence.cpp
    )
    target_include_directories(omega_native_persistence PUBLIC native/apps/openomega)
    target_link_libraries(omega_native_persistence PUBLIC omega_profiles)
    target_compile_features(omega_native_persistence PUBLIC cxx_std_23)
    if(MSVC)
        target_compile_options(omega_native_persistence PRIVATE
            /W4
            /WX
            /permissive-
            /Zc:__cplusplus
            /Zc:preprocessor
            /utf-8
        )
    else()
        target_compile_options(omega_native_persistence PRIVATE
            -Wall
            -Wextra
            -Wpedantic
            -Werror
            -Wno-missing-field-initializers
        )
    endif()

    add_library(omega_app_host STATIC
        native/apps/openomega/opening_movie_safety.cpp
        native/apps/openomega/opening_movie_player.cpp
        native/apps/openomega/omega_app.cpp
    )
    target_include_directories(omega_app_host PUBLIC native/apps/openomega)
    target_link_libraries(omega_app_host
        PUBLIC
            omega_app_core
            omega_media
            omega_native_persistence
            omega_runtime
            omega_simulation
            omega_sdl_backend
    )
    target_compile_features(omega_app_host PUBLIC cxx_std_23)
    if(MSVC)
        target_compile_options(omega_app_host PRIVATE
            /W4
            /WX
            /permissive-
            /Zc:__cplusplus
            /Zc:preprocessor
            /utf-8
        )
    else()
        target_compile_options(omega_app_host PRIVATE
            -Wall
            -Wextra
            -Wpedantic
            -Werror
            -Wno-missing-field-initializers
        )
    endif()

    add_executable(openomega
        native/apps/openomega/main.cpp
    )
    target_link_libraries(openomega PRIVATE omega_app_host)
    target_compile_definitions(openomega PRIVATE
        $<$<CONFIG:Debug>:OMEGA_GPU_DEBUG=1>
    )
    if(MSVC)
        target_compile_options(openomega PRIVATE /W4 /WX /permissive- /Zc:__cplusplus /Zc:preprocessor /utf-8)
    else()
        target_compile_options(openomega PRIVATE -Wall -Wextra -Wpedantic -Werror -Wno-missing-field-initializers)
    endif()

    if(OMEGA_WINDOWS_PORTABLE_PACKAGE_ENABLED)
        install(TARGETS openomega
            RUNTIME DESTINATION "${OMEGA_WINDOWS_PORTABLE_PACKAGE_ROOT}"
        )
        install(FILES
            "${CMAKE_CURRENT_SOURCE_DIR}/packaging/windows/launch-openomega.cmd"
            "${CMAKE_CURRENT_SOURCE_DIR}/packaging/windows/README-WINDOWS.md"
            "${CMAKE_CURRENT_SOURCE_DIR}/LICENSE"
            "${CMAKE_CURRENT_SOURCE_DIR}/NOTICE"
            "${CMAKE_CURRENT_SOURCE_DIR}/TRADEMARKS.md"
            "${CMAKE_CURRENT_SOURCE_DIR}/THIRD_PARTY_NOTICES.md"
            DESTINATION "${OMEGA_WINDOWS_PORTABLE_PACKAGE_ROOT}"
        )
        install(FILES
            "${CMAKE_CURRENT_SOURCE_DIR}/LICENSES/SDL3.txt"
            DESTINATION "${OMEGA_WINDOWS_PORTABLE_PACKAGE_ROOT}/LICENSES"
        )
    endif()
endif()

include(CTest)
if(OMEGA_RUN_GPU_SMOKE_TEST AND NOT BUILD_TESTING)
    message(FATAL_ERROR "OMEGA_RUN_GPU_SMOKE_TEST requires BUILD_TESTING=ON")
endif()
if(OMEGA_RUN_GPU_SMOKE_TEST AND NOT TARGET omega_sdl_backend)
    message(FATAL_ERROR "OMEGA_RUN_GPU_SMOKE_TEST requires OMEGA_BUILD_RUNTIME=ON and SDL GPU support")
endif()

if(BUILD_TESTING)
    function(set_openomega_synthetic_profile_environment test_name)
        set(profile_root
            "${CMAKE_CURRENT_BINARY_DIR}/synthetic-runtime-profiles/${test_name}")
        file(REMOVE_RECURSE "${profile_root}")
        file(MAKE_DIRECTORY
            "${profile_root}/local-app-data"
            "${profile_root}/xdg-config-home"
            "${profile_root}/xdg-data-home"
            "${profile_root}/home"
        )
        set_property(TEST "${test_name}" APPEND PROPERTY ENVIRONMENT
            "OPENOMEGA_TEST_PROFILE_ROOT=${profile_root}"
            "LOCALAPPDATA=${profile_root}/local-app-data"
            "XDG_CONFIG_HOME=${profile_root}/xdg-config-home"
            "XDG_DATA_HOME=${profile_root}/xdg-data-home"
            "HOME=${profile_root}/home"
            "OPENOMEGA_DISABLE_STARTUP_DIALOG=1"
        )
    endfunction()

    if(WIN32)
        unset(OMEGA_LAUNCH_TEST_POWERSHELL CACHE)
        unset(OMEGA_LAUNCH_TEST_POWERSHELL)
        find_program(OMEGA_LAUNCH_TEST_POWERSHELL
            NAMES powershell.exe pwsh.exe
            REQUIRED
            NO_CACHE
        )
        if(NOT IS_ABSOLUTE "${OMEGA_LAUNCH_TEST_POWERSHELL}" OR
           NOT EXISTS "${OMEGA_LAUNCH_TEST_POWERSHELL}")
            message(FATAL_ERROR
                "OpenOmega launcher tests require an absolute existing PowerShell host"
            )
        endif()

        add_executable(omega_windows_argv_capture
            native/tests/windows_argv_capture.cpp
        )
        target_compile_features(omega_windows_argv_capture PRIVATE cxx_std_23)
        if(MSVC)
            target_compile_options(omega_windows_argv_capture PRIVATE
                /W4
                /WX
                /permissive-
                /Zc:__cplusplus
                /Zc:preprocessor
                /utf-8
            )
        else()
            target_compile_options(omega_windows_argv_capture PRIVATE
                -Wall
                -Wextra
                -Wpedantic
                -Werror
            )
            if(MINGW)
                target_link_options(omega_windows_argv_capture PRIVATE -municode)
            endif()
        endif()

        add_test(NAME omega_launch_quoting_tests
            COMMAND "${OMEGA_LAUNCH_TEST_POWERSHELL}"
                -NoProfile
                -NonInteractive
                -File "${CMAKE_CURRENT_SOURCE_DIR}/scripts/tests/test-launch-omega-quoting.ps1"
                -ArgvCaptureExecutable "$<TARGET_FILE:omega_windows_argv_capture>"
        )
        set_tests_properties(omega_launch_quoting_tests PROPERTIES
            LABELS "integration;windows"
            RUN_SERIAL TRUE
            TIMEOUT 30
        )
    endif()

    add_executable(omega_boot_sequence_tests
        native/tests/boot_sequence_tests.cpp
    )
    target_include_directories(omega_boot_sequence_tests PRIVATE native/apps/openomega)
    if(MSVC)
        target_compile_options(omega_boot_sequence_tests PRIVATE
            /W4
            /WX
            /permissive-
            /Zc:__cplusplus
            /Zc:preprocessor
            /utf-8
        )
    else()
        target_compile_options(omega_boot_sequence_tests PRIVATE
            -Wall
            -Wextra
            -Wpedantic
            -Werror
            -Wno-missing-field-initializers
        )
    endif()
    add_test(NAME omega_boot_sequence_tests COMMAND omega_boot_sequence_tests)
    set_tests_properties(omega_boot_sequence_tests PROPERTIES TIMEOUT 10)

    add_executable(omega_opening_movie_audio_clock_tests
        native/tests/opening_movie_audio_clock_tests.cpp
    )
    target_include_directories(omega_opening_movie_audio_clock_tests PRIVATE
        native/apps/openomega
    )
    if(MSVC)
        target_compile_options(omega_opening_movie_audio_clock_tests PRIVATE
            /W4
            /WX
            /permissive-
            /Zc:__cplusplus
            /Zc:preprocessor
            /utf-8
        )
    else()
        target_compile_options(omega_opening_movie_audio_clock_tests PRIVATE
            -Wall
            -Wextra
            -Wpedantic
            -Werror
            -Wno-missing-field-initializers
        )
    endif()
    add_test(NAME omega_opening_movie_audio_clock_tests
        COMMAND omega_opening_movie_audio_clock_tests)
    set_tests_properties(omega_opening_movie_audio_clock_tests PROPERTIES TIMEOUT 10)

    add_executable(omega_opening_movie_audio_fault_tests
        native/tests/opening_movie_audio_fault_tests.cpp
    )
    target_include_directories(omega_opening_movie_audio_fault_tests PRIVATE
        native/apps/openomega
    )
    if(MSVC)
        target_compile_options(omega_opening_movie_audio_fault_tests PRIVATE
            /W4
            /WX
            /permissive-
            /Zc:__cplusplus
            /Zc:preprocessor
            /utf-8
        )
    else()
        target_compile_options(omega_opening_movie_audio_fault_tests PRIVATE
            -Wall
            -Wextra
            -Wpedantic
            -Werror
            -Wno-missing-field-initializers
        )
    endif()
    add_test(NAME omega_opening_movie_audio_fault_tests
        COMMAND omega_opening_movie_audio_fault_tests)
    set_tests_properties(omega_opening_movie_audio_fault_tests PROPERTIES TIMEOUT 10)

    add_executable(omega_sdl_gpu_exception_boundary_tests
        native/tests/sdl_gpu_exception_boundary_tests.cpp
    )
    target_include_directories(omega_sdl_gpu_exception_boundary_tests PRIVATE
        native/apps/openomega
    )
    if(MSVC)
        target_compile_options(omega_sdl_gpu_exception_boundary_tests PRIVATE
            /W4
            /WX
            /permissive-
            /Zc:__cplusplus
            /Zc:preprocessor
            /utf-8
        )
    else()
        target_compile_options(omega_sdl_gpu_exception_boundary_tests PRIVATE
            -Wall
            -Wextra
            -Wpedantic
            -Werror
            -Wno-missing-field-initializers
        )
    endif()
    add_test(NAME omega_sdl_gpu_exception_boundary_tests
        COMMAND omega_sdl_gpu_exception_boundary_tests)
    set_tests_properties(omega_sdl_gpu_exception_boundary_tests PROPERTIES TIMEOUT 5)

    add_executable(omega_opening_movie_player_error_mapping_tests
        native/tests/opening_movie_player_error_mapping_tests.cpp
    )
    target_include_directories(omega_opening_movie_player_error_mapping_tests PRIVATE
        native/apps/openomega
        native/include
    )
    if(MSVC)
        target_compile_options(omega_opening_movie_player_error_mapping_tests PRIVATE
            /W4
            /WX
            /permissive-
            /Zc:__cplusplus
            /Zc:preprocessor
            /utf-8
        )
    else()
        target_compile_options(omega_opening_movie_player_error_mapping_tests PRIVATE
            -Wall
            -Wextra
            -Wpedantic
            -Werror
            -Wno-missing-field-initializers
        )
    endif()
    add_test(NAME omega_opening_movie_player_error_mapping_tests
        COMMAND omega_opening_movie_player_error_mapping_tests)
    set_tests_properties(omega_opening_movie_player_error_mapping_tests PROPERTIES
        TIMEOUT 5)

    if(TARGET omega_app_host)
        add_executable(omega_opening_movie_player_boundary_tests
            native/tests/opening_movie_player_boundary_tests.cpp
        )
        target_link_libraries(omega_opening_movie_player_boundary_tests PRIVATE omega_app_host)
        if(MSVC)
            target_compile_options(omega_opening_movie_player_boundary_tests PRIVATE
                /W4
                /WX
                /permissive-
                /Zc:__cplusplus
                /Zc:preprocessor
                /utf-8
            )
        else()
            target_compile_options(omega_opening_movie_player_boundary_tests PRIVATE
                -Wall
                -Wextra
                -Wpedantic
                -Werror
                -Wno-missing-field-initializers
            )
        endif()
        add_test(NAME omega_opening_movie_player_boundary_tests
            COMMAND omega_opening_movie_player_boundary_tests)
        set_tests_properties(omega_opening_movie_player_boundary_tests PROPERTIES TIMEOUT 10)
    endif()

    add_executable(omega_save_database_tests
        native/tests/save_database_tests.cpp
    )
    target_link_libraries(omega_save_database_tests PRIVATE omega_persistence)
    if(MSVC)
        target_compile_options(omega_save_database_tests PRIVATE
            /W4
            /WX
            /permissive-
            /Zc:__cplusplus
            /Zc:preprocessor
            /utf-8
        )
    else()
        target_compile_options(omega_save_database_tests PRIVATE
            -Wall
            -Wextra
            -Wpedantic
            -Werror
            -Wno-missing-field-initializers
        )
    endif()
    add_test(NAME omega_save_database_tests COMMAND omega_save_database_tests)
    set_tests_properties(omega_save_database_tests PROPERTIES TIMEOUT 10)

    add_executable(omega_native_save_path_tests
        native/tests/native_save_path_tests.cpp
    )
    target_link_libraries(omega_native_save_path_tests PRIVATE omega_persistence)
    if(MSVC)
        target_compile_options(omega_native_save_path_tests PRIVATE
            /W4
            /WX
            /permissive-
            /Zc:__cplusplus
            /Zc:preprocessor
            /utf-8
        )
    else()
        target_compile_options(omega_native_save_path_tests PRIVATE
            -Wall
            -Wextra
            -Wpedantic
            -Werror
            -Wno-missing-field-initializers
        )
    endif()
    add_test(NAME omega_native_save_path_tests COMMAND omega_native_save_path_tests)
    set_tests_properties(omega_native_save_path_tests PROPERTIES TIMEOUT 5)

    add_executable(omega_vag_adpcm_decoder_tests
        native/tests/vag_adpcm_decoder_tests.cpp
    )
    target_link_libraries(omega_vag_adpcm_decoder_tests PRIVATE omega_retail_formats)
    if(MSVC)
        target_compile_options(omega_vag_adpcm_decoder_tests PRIVATE
            /W4
            /WX
            /permissive-
            /Zc:__cplusplus
            /Zc:preprocessor
            /utf-8
        )
    else()
        target_compile_options(omega_vag_adpcm_decoder_tests PRIVATE
            -Wall
            -Wextra
            -Wpedantic
            -Werror
            -Wno-missing-field-initializers
        )
    endif()
    add_test(NAME omega_vag_adpcm_decoder_tests COMMAND omega_vag_adpcm_decoder_tests)
    set_tests_properties(omega_vag_adpcm_decoder_tests PROPERTIES TIMEOUT 10)

    add_executable(omega_skas_text_envelope_decoder_tests
        native/tests/skas_text_envelope_decoder_tests.cpp
    )
    target_link_libraries(omega_skas_text_envelope_decoder_tests PRIVATE omega_retail_formats)
    if(MSVC)
        target_compile_options(omega_skas_text_envelope_decoder_tests PRIVATE
            /W4
            /WX
            /permissive-
            /Zc:__cplusplus
            /Zc:preprocessor
            /utf-8
        )
    else()
        target_compile_options(omega_skas_text_envelope_decoder_tests PRIVATE
            -Wall
            -Wextra
            -Wpedantic
            -Werror
            -Wno-missing-field-initializers
        )
    endif()
    add_test(NAME omega_skas_text_envelope_decoder_tests
        COMMAND omega_skas_text_envelope_decoder_tests)
    set_tests_properties(omega_skas_text_envelope_decoder_tests PROPERTIES TIMEOUT 10)

    add_executable(omega_vpk_wrapper_envelope_decoder_tests
        native/tests/vpk_wrapper_envelope_decoder_tests.cpp
    )
    target_link_libraries(omega_vpk_wrapper_envelope_decoder_tests PRIVATE omega_retail_formats)
    if(MSVC)
        target_compile_options(omega_vpk_wrapper_envelope_decoder_tests PRIVATE
            /W4
            /WX
            /permissive-
            /Zc:__cplusplus
            /Zc:preprocessor
            /utf-8
        )
    else()
        target_compile_options(omega_vpk_wrapper_envelope_decoder_tests PRIVATE
            -Wall
            -Wextra
            -Wpedantic
            -Werror
            -Wno-missing-field-initializers
        )
    endif()
    add_test(NAME omega_vpk_wrapper_envelope_decoder_tests
        COMMAND omega_vpk_wrapper_envelope_decoder_tests)
    set_tests_properties(omega_vpk_wrapper_envelope_decoder_tests PROPERTIES TIMEOUT 10)

    add_executable(omega_fnt_envelope_descriptor_tests
        native/tests/fnt_envelope_descriptor_tests.cpp
    )
    target_link_libraries(omega_fnt_envelope_descriptor_tests PRIVATE omega_retail_formats)
    if(MSVC)
        target_compile_options(omega_fnt_envelope_descriptor_tests PRIVATE
            /W4
            /WX
            /permissive-
            /Zc:__cplusplus
            /Zc:preprocessor
            /utf-8
        )
    else()
        target_compile_options(omega_fnt_envelope_descriptor_tests PRIVATE
            -Wall
            -Wextra
            -Wpedantic
            -Werror
            -Wno-missing-field-initializers
        )
    endif()
    add_test(NAME omega_fnt_envelope_descriptor_tests
        COMMAND omega_fnt_envelope_descriptor_tests)
    set_tests_properties(omega_fnt_envelope_descriptor_tests PROPERTIES TIMEOUT 10)

    add_executable(omega_gui_envelope_descriptor_tests
        native/tests/gui_envelope_descriptor_tests.cpp
    )
    target_link_libraries(omega_gui_envelope_descriptor_tests PRIVATE omega_retail_formats)
    if(MSVC)
        target_compile_options(omega_gui_envelope_descriptor_tests PRIVATE
            /W4
            /WX
            /permissive-
            /Zc:__cplusplus
            /Zc:preprocessor
            /utf-8
        )
    else()
        target_compile_options(omega_gui_envelope_descriptor_tests PRIVATE
            -Wall
            -Wextra
            -Wpedantic
            -Werror
            -Wno-missing-field-initializers
        )
    endif()
    add_test(NAME omega_gui_envelope_descriptor_tests
        COMMAND omega_gui_envelope_descriptor_tests)
    set_tests_properties(omega_gui_envelope_descriptor_tests PROPERTIES TIMEOUT 10)

    add_executable(omega_ie_envelope_descriptor_tests
        native/tests/ie_envelope_descriptor_tests.cpp
    )
    target_link_libraries(omega_ie_envelope_descriptor_tests PRIVATE omega_retail_formats)
    if(MSVC)
        target_compile_options(omega_ie_envelope_descriptor_tests PRIVATE
            /W4
            /WX
            /permissive-
            /Zc:__cplusplus
            /Zc:preprocessor
            /utf-8
        )
    else()
        target_compile_options(omega_ie_envelope_descriptor_tests PRIVATE
            -Wall
            -Wextra
            -Wpedantic
            -Werror
            -Wno-missing-field-initializers
        )
    endif()
    add_test(NAME omega_ie_envelope_descriptor_tests
        COMMAND omega_ie_envelope_descriptor_tests)
    set_tests_properties(omega_ie_envelope_descriptor_tests PROPERTIES TIMEOUT 10)

    add_executable(omega_tbl_envelope_descriptor_tests
        native/tests/tbl_envelope_descriptor_tests.cpp
    )
    target_link_libraries(omega_tbl_envelope_descriptor_tests PRIVATE omega_retail_formats)
    if(MSVC)
        target_compile_options(omega_tbl_envelope_descriptor_tests PRIVATE
            /W4
            /WX
            /permissive-
            /Zc:__cplusplus
            /Zc:preprocessor
            /utf-8
        )
    else()
        target_compile_options(omega_tbl_envelope_descriptor_tests PRIVATE
            -Wall
            -Wextra
            -Wpedantic
            -Werror
            -Wno-missing-field-initializers
        )
    endif()
    add_test(NAME omega_tbl_envelope_descriptor_tests
        COMMAND omega_tbl_envelope_descriptor_tests)
    set_tests_properties(omega_tbl_envelope_descriptor_tests PROPERTIES TIMEOUT 10)

    add_executable(omega_mpeg_program_stream_descriptor_tests
        native/tests/mpeg_program_stream_descriptor_tests.cpp
    )
    target_link_libraries(omega_mpeg_program_stream_descriptor_tests PRIVATE omega_media)
    if(MSVC)
        target_compile_options(omega_mpeg_program_stream_descriptor_tests PRIVATE
            /W4
            /WX
            /permissive-
            /Zc:__cplusplus
            /Zc:preprocessor
            /utf-8
        )
    else()
        target_compile_options(omega_mpeg_program_stream_descriptor_tests PRIVATE
            -Wall
            -Wextra
            -Wpedantic
            -Werror
            -Wno-missing-field-initializers
        )
    endif()
    add_test(NAME omega_mpeg_program_stream_descriptor_tests
        COMMAND omega_mpeg_program_stream_descriptor_tests)
    set_tests_properties(omega_mpeg_program_stream_descriptor_tests PROPERTIES TIMEOUT 10)

    add_executable(omega_mpeg_video_elementary_stream_tests
        native/tests/mpeg_video_elementary_stream_tests.cpp
    )
    target_link_libraries(omega_mpeg_video_elementary_stream_tests PRIVATE omega_media)
    if(MSVC)
        target_compile_options(omega_mpeg_video_elementary_stream_tests PRIVATE
            /W4
            /WX
            /permissive-
            /Zc:__cplusplus
            /Zc:preprocessor
            /utf-8
        )
    else()
        target_compile_options(omega_mpeg_video_elementary_stream_tests PRIVATE
            -Wall
            -Wextra
            -Wpedantic
            -Werror
            -Wno-missing-field-initializers
        )
    endif()
    add_test(NAME omega_mpeg_video_elementary_stream_tests
        COMMAND omega_mpeg_video_elementary_stream_tests)
    set_tests_properties(omega_mpeg_video_elementary_stream_tests PROPERTIES TIMEOUT 10)

    add_executable(omega_nv12_to_rgba8_tests
        native/tests/nv12_to_rgba8_tests.cpp
    )
    target_link_libraries(omega_nv12_to_rgba8_tests PRIVATE omega_media)
    if(MSVC)
        target_compile_options(omega_nv12_to_rgba8_tests PRIVATE
            /W4
            /WX
            /permissive-
            /Zc:__cplusplus
            /Zc:preprocessor
            /utf-8
        )
    else()
        target_compile_options(omega_nv12_to_rgba8_tests PRIVATE
            -Wall
            -Wextra
            -Wpedantic
            -Werror
            -Wno-missing-field-initializers
        )
    endif()
    add_test(NAME omega_nv12_to_rgba8_tests
        COMMAND omega_nv12_to_rgba8_tests)
    set_tests_properties(omega_nv12_to_rgba8_tests PROPERTIES TIMEOUT 10)

    add_executable(omega_media_foundation_h262_decoder_tests
        native/tests/media_foundation_h262_decoder_tests.cpp
    )
    target_link_libraries(omega_media_foundation_h262_decoder_tests PRIVATE omega_media)
    if(MSVC)
        target_compile_options(omega_media_foundation_h262_decoder_tests PRIVATE
            /W4
            /WX
            /permissive-
            /Zc:__cplusplus
            /Zc:preprocessor
            /utf-8
        )
    else()
        target_compile_options(omega_media_foundation_h262_decoder_tests PRIVATE
            -Wall
            -Wextra
            -Wpedantic
            -Werror
            -Wno-missing-field-initializers
        )
    endif()
    add_test(NAME omega_media_foundation_h262_decoder_tests
        COMMAND omega_media_foundation_h262_decoder_tests)
    set_tests_properties(omega_media_foundation_h262_decoder_tests PROPERTIES TIMEOUT 10)

    add_executable(omega_pss_pcm_audio_stream_tests
        native/tests/pss_pcm_audio_stream_tests.cpp
    )
    target_link_libraries(omega_pss_pcm_audio_stream_tests PRIVATE omega_media)
    if(MSVC)
        target_compile_options(omega_pss_pcm_audio_stream_tests PRIVATE
            /W4
            /WX
            /permissive-
            /Zc:__cplusplus
            /Zc:preprocessor
            /utf-8
        )
    else()
        target_compile_options(omega_pss_pcm_audio_stream_tests PRIVATE
            -Wall
            -Wextra
            -Wpedantic
            -Werror
            -Wno-missing-field-initializers
        )
    endif()
    add_test(NAME omega_pss_pcm_audio_stream_tests
        COMMAND omega_pss_pcm_audio_stream_tests)
    set_tests_properties(omega_pss_pcm_audio_stream_tests PROPERTIES TIMEOUT 10)

    add_executable(omega_ps2_memory_card_image_tests
        native/tests/ps2_memory_card_image_tests.cpp
    )
    target_link_libraries(omega_ps2_memory_card_image_tests PRIVATE omega_ps2_compat)
    if(MSVC)
        target_compile_options(omega_ps2_memory_card_image_tests PRIVATE
            /W4
            /WX
            /permissive-
            /Zc:__cplusplus
            /Zc:preprocessor
            /utf-8
        )
    else()
        target_compile_options(omega_ps2_memory_card_image_tests PRIVATE
            -Wall
            -Wextra
            -Wpedantic
            -Werror
            -Wno-missing-field-initializers
        )
    endif()
    add_test(NAME omega_ps2_memory_card_image_tests
        COMMAND omega_ps2_memory_card_image_tests)
    set_tests_properties(omega_ps2_memory_card_image_tests PROPERTIES TIMEOUT 15)

    add_executable(omega_ps2_memory_card_filesystem_tests
        native/tests/ps2_memory_card_filesystem_tests.cpp
    )
    target_link_libraries(omega_ps2_memory_card_filesystem_tests PRIVATE omega_ps2_compat)
    if(MSVC)
        target_compile_options(omega_ps2_memory_card_filesystem_tests PRIVATE
            /W4
            /WX
            /permissive-
            /Zc:__cplusplus
            /Zc:preprocessor
            /utf-8
        )
    else()
        target_compile_options(omega_ps2_memory_card_filesystem_tests PRIVATE
            -Wall
            -Wextra
            -Wpedantic
            -Werror
            -Wno-missing-field-initializers
        )
    endif()
    add_test(NAME omega_ps2_memory_card_filesystem_tests
        COMMAND omega_ps2_memory_card_filesystem_tests)
    set_tests_properties(omega_ps2_memory_card_filesystem_tests PROPERTIES TIMEOUT 20)

    add_executable(omega_ps2_memory_card_export_tests
        native/tests/ps2_memory_card_export_tests.cpp
    )
    target_link_libraries(omega_ps2_memory_card_export_tests PRIVATE omega_ps2_compat)
    if(MSVC)
        target_compile_options(omega_ps2_memory_card_export_tests PRIVATE
            /W4
            /WX
            /permissive-
            /Zc:__cplusplus
            /Zc:preprocessor
            /utf-8
        )
    else()
        target_compile_options(omega_ps2_memory_card_export_tests PRIVATE
            -Wall
            -Wextra
            -Wpedantic
            -Werror
            -Wno-missing-field-initializers
        )
    endif()
    add_test(NAME omega_ps2_memory_card_export_tests
        COMMAND omega_ps2_memory_card_export_tests)
    set_tests_properties(omega_ps2_memory_card_export_tests PROPERTIES TIMEOUT 30)

    add_executable(omega_profile_catalog_tests
        native/tests/profile_catalog_tests.cpp
    )
    target_link_libraries(omega_profile_catalog_tests PRIVATE omega_profiles)
    if(MSVC)
        target_compile_options(omega_profile_catalog_tests PRIVATE
            /W4
            /WX
            /permissive-
            /Zc:__cplusplus
            /Zc:preprocessor
            /utf-8
        )
    else()
        target_compile_options(omega_profile_catalog_tests PRIVATE
            -Wall
            -Wextra
            -Wpedantic
            -Werror
            -Wno-missing-field-initializers
        )
    endif()
    add_test(NAME omega_profile_catalog_tests COMMAND omega_profile_catalog_tests)
    # The 65-profile durability fixture performs synchronous write-through and
    # directory flushes whose Windows Debug latency varies substantially on hosted runners.
    set_tests_properties(omega_profile_catalog_tests PROPERTIES TIMEOUT 30)

    add_executable(omega_character_catalog_tests
        native/tests/character_catalog_tests.cpp
    )
    target_link_libraries(omega_character_catalog_tests PRIVATE omega_profiles)
    if(MSVC)
        target_compile_options(omega_character_catalog_tests PRIVATE
            /W4
            /WX
            /permissive-
            /Zc:__cplusplus
            /Zc:preprocessor
            /utf-8
        )
    else()
        target_compile_options(omega_character_catalog_tests PRIVATE
            -Wall
            -Wextra
            -Wpedantic
            -Werror
            -Wno-missing-field-initializers
        )
    endif()
    add_test(NAME omega_character_catalog_tests
        COMMAND omega_character_catalog_tests)
    set_tests_properties(omega_character_catalog_tests PROPERTIES TIMEOUT 30)

    add_executable(omega_par_text_envelope_tests
        native/tests/par_text_envelope_decoder_tests.cpp
    )
    target_link_libraries(omega_par_text_envelope_tests PRIVATE omega_retail_formats)
    if(MSVC)
        target_compile_options(omega_par_text_envelope_tests PRIVATE
            /W4
            /WX
            /permissive-
            /Zc:__cplusplus
            /Zc:preprocessor
            /utf-8
        )
    else()
        target_compile_options(omega_par_text_envelope_tests PRIVATE
            -Wall
            -Wextra
            -Wpedantic
            -Werror
            -Wno-missing-field-initializers
        )
    endif()
    add_test(NAME omega_par_text_envelope_tests COMMAND omega_par_text_envelope_tests)
    set_tests_properties(omega_par_text_envelope_tests PROPERTIES TIMEOUT 5)

    if(TARGET omega_native_persistence)
        add_executable(omega_native_persistence_fixture_writer
            native/tests/native_persistence_fixture_writer.cpp
        )
        target_link_libraries(omega_native_persistence_fixture_writer
            PRIVATE omega_native_persistence)
        if(MSVC)
            target_compile_options(omega_native_persistence_fixture_writer PRIVATE
                /W4
                /WX
                /permissive-
                /Zc:__cplusplus
                /Zc:preprocessor
                /utf-8
            )
        else()
            target_compile_options(omega_native_persistence_fixture_writer PRIVATE
                -Wall
                -Wextra
                -Wpedantic
                -Werror
                -Wno-missing-field-initializers
            )
        endif()

        add_executable(omega_native_persistence_tests
            native/tests/native_persistence_tests.cpp
        )
        target_link_libraries(omega_native_persistence_tests
            PRIVATE omega_native_persistence)
        if(MSVC)
            target_compile_options(omega_native_persistence_tests PRIVATE
                /W4
                /WX
                /permissive-
                /Zc:__cplusplus
                /Zc:preprocessor
                /utf-8
            )
        else()
            target_compile_options(omega_native_persistence_tests PRIVATE
                -Wall
                -Wextra
                -Wpedantic
                -Werror
                -Wno-missing-field-initializers
            )
        endif()
        add_test(NAME omega_native_persistence_tests
            COMMAND omega_native_persistence_tests)
        # The generated rollback/reopen matrix performs many intentionally durable
        # snapshot publications; hosted Windows write-through latency can transiently
        # exceed 30 seconds.
        set_tests_properties(omega_native_persistence_tests PROPERTIES TIMEOUT 60)

        add_executable(omega_native_character_session_tests
            native/tests/native_character_session_tests.cpp
        )
        target_link_libraries(omega_native_character_session_tests
            PRIVATE omega_native_persistence omega_profiles)
        if(MSVC)
            target_compile_options(omega_native_character_session_tests PRIVATE
                /W4
                /WX
                /permissive-
                /Zc:__cplusplus
                /Zc:preprocessor
                /utf-8
            )
        else()
            target_compile_options(omega_native_character_session_tests PRIVATE
                -Wall
                -Wextra
                -Wpedantic
                -Werror
                -Wno-missing-field-initializers
            )
        endif()
        add_test(NAME omega_native_character_session_tests
            COMMAND omega_native_character_session_tests)
        set_tests_properties(omega_native_character_session_tests
            PROPERTIES TIMEOUT 30)
    endif()

    if(TARGET openomega)
        add_test(NAME openomega_process_contract
            COMMAND "${CMAKE_COMMAND}"
                "-DOPENOMEGA_EXECUTABLE=$<TARGET_FILE:openomega>"
                "-DOPENOMEGA_NATIVE_PERSISTENCE_FIXTURE_WRITER=$<TARGET_FILE:omega_native_persistence_fixture_writer>"
                -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/openomega_process_contract.cmake"
        )
        set_tests_properties(openomega_process_contract PROPERTIES
            TIMEOUT 20
            LABELS "integration"
        )
        set_openomega_synthetic_profile_environment(openomega_process_contract)
    endif()

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

    add_executable(omega_frontend_envelope_commands_tests
        native/tests/frontend_envelope_commands_tests.cpp
        native/apps/omega_tool/frontend_envelope_commands.cpp
    )
    target_include_directories(omega_frontend_envelope_commands_tests PRIVATE
        native/apps/omega_tool
    )
    target_link_libraries(omega_frontend_envelope_commands_tests PRIVATE
        omega_core
        omega_retail_formats
    )
    if(MSVC)
        target_compile_options(omega_frontend_envelope_commands_tests PRIVATE
            /W4
            /WX
            /permissive-
            /Zc:__cplusplus
            /Zc:preprocessor
            /utf-8
        )
    else()
        target_compile_options(omega_frontend_envelope_commands_tests PRIVATE
            -Wall
            -Wextra
            -Wpedantic
            -Werror
            -Wno-missing-field-initializers
        )
    endif()
    add_test(NAME omega_frontend_envelope_commands_tests
        COMMAND omega_frontend_envelope_commands_tests)
    set_tests_properties(omega_frontend_envelope_commands_tests PROPERTIES TIMEOUT 10)

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

    add_executable(omega_input_trace_tests
        native/tests/input_trace_tests.cpp
    )
    target_link_libraries(omega_input_trace_tests PRIVATE omega_runtime)
    if(MSVC)
        target_compile_options(omega_input_trace_tests PRIVATE
            /W4
            /WX
            /permissive-
            /Zc:__cplusplus
            /Zc:preprocessor
            /utf-8
        )
    else()
        target_compile_options(omega_input_trace_tests PRIVATE
            -Wall
            -Wextra
            -Wpedantic
            -Werror
            -Wno-missing-field-initializers
        )
    endif()
    add_test(NAME omega_input_trace_tests COMMAND omega_input_trace_tests)
    set_tests_properties(omega_input_trace_tests PROPERTIES TIMEOUT 5)

    add_executable(omega_scheduler_elapsed_trace_tests
        native/tests/scheduler_elapsed_trace_tests.cpp
    )
    target_link_libraries(omega_scheduler_elapsed_trace_tests PRIVATE omega_runtime)
    if(MSVC)
        target_compile_options(omega_scheduler_elapsed_trace_tests PRIVATE
            /W4
            /WX
            /permissive-
            /Zc:__cplusplus
            /Zc:preprocessor
            /utf-8
        )
    else()
        target_compile_options(omega_scheduler_elapsed_trace_tests PRIVATE
            -Wall
            -Wextra
            -Wpedantic
            -Werror
            -Wno-missing-field-initializers
        )
    endif()
    add_test(NAME omega_scheduler_elapsed_trace_tests COMMAND omega_scheduler_elapsed_trace_tests)
    set_tests_properties(omega_scheduler_elapsed_trace_tests PROPERTIES TIMEOUT 5)

    add_executable(omega_run_capture_session_tests
        native/tests/run_capture_session_tests.cpp
    )
    target_link_libraries(omega_run_capture_session_tests PRIVATE omega_runtime)
    if(MSVC)
        target_compile_options(omega_run_capture_session_tests PRIVATE
            /W4
            /WX
            /permissive-
            /Zc:__cplusplus
            /Zc:preprocessor
            /utf-8
        )
    else()
        target_compile_options(omega_run_capture_session_tests PRIVATE
            -Wall
            -Wextra
            -Wpedantic
            -Werror
            -Wno-missing-field-initializers
        )
    endif()
    add_test(NAME omega_run_capture_session_tests COMMAND omega_run_capture_session_tests)
    set_tests_properties(omega_run_capture_session_tests PROPERTIES TIMEOUT 5)

    add_executable(omega_run_capture_replay_tests
        native/tests/run_capture_replay_tests.cpp
    )
    target_link_libraries(omega_run_capture_replay_tests PRIVATE
        omega_runtime
        omega_simulation
    )
    if(MSVC)
        target_compile_options(omega_run_capture_replay_tests PRIVATE
            /W4
            /WX
            /permissive-
            /Zc:__cplusplus
            /Zc:preprocessor
            /utf-8
        )
    else()
        target_compile_options(omega_run_capture_replay_tests PRIVATE
            -Wall
            -Wextra
            -Wpedantic
            -Werror
            -Wno-missing-field-initializers
        )
    endif()
    add_test(NAME omega_run_capture_replay_tests COMMAND omega_run_capture_replay_tests)
    set_tests_properties(omega_run_capture_replay_tests PROPERTIES TIMEOUT 5)

    add_executable(omega_run_capture_tests
        native/tests/run_capture_tests.cpp
    )
    target_link_libraries(omega_run_capture_tests PRIVATE omega_app_core)
    if(MSVC)
        target_compile_options(omega_run_capture_tests PRIVATE
            /W4
            /WX
            /permissive-
            /Zc:__cplusplus
            /Zc:preprocessor
            /utf-8
        )
    else()
        target_compile_options(omega_run_capture_tests PRIVATE
            -Wall
            -Wextra
            -Wpedantic
            -Werror
            -Wno-missing-field-initializers
        )
    endif()
    add_test(NAME omega_run_capture_tests COMMAND omega_run_capture_tests)
    set_tests_properties(omega_run_capture_tests PROPERTIES TIMEOUT 5)

    add_executable(omega_run_replay_session_tests
        native/tests/run_replay_session_tests.cpp
    )
    target_link_libraries(omega_run_replay_session_tests PRIVATE omega_app_core)
    if(MSVC)
        target_compile_options(omega_run_replay_session_tests PRIVATE
            /W4
            /WX
            /permissive-
            /Zc:__cplusplus
            /Zc:preprocessor
            /utf-8
        )
    else()
        target_compile_options(omega_run_replay_session_tests PRIVATE
            -Wall
            -Wextra
            -Wpedantic
            -Werror
            -Wno-missing-field-initializers
        )
    endif()
    add_test(NAME omega_run_replay_session_tests COMMAND omega_run_replay_session_tests)
    set_tests_properties(omega_run_replay_session_tests PROPERTIES TIMEOUT 5)

    add_executable(omega_gameplay_tests
        native/tests/debug_locomotion_tests.cpp
    )
    target_link_libraries(omega_gameplay_tests PRIVATE omega_gameplay)
    if(MSVC)
        target_compile_options(omega_gameplay_tests PRIVATE
            /W4
            /WX
            /permissive-
            /Zc:__cplusplus
            /Zc:preprocessor
            /utf-8
        )
    else()
        target_compile_options(omega_gameplay_tests PRIVATE
            -Wall
            -Wextra
            -Wpedantic
            -Werror
            -Wno-missing-field-initializers
        )
    endif()
    add_test(NAME omega_gameplay_tests COMMAND omega_gameplay_tests)
    set_tests_properties(omega_gameplay_tests PROPERTIES TIMEOUT 5)

    add_executable(omega_front_end_tests
        native/tests/front_end_tests.cpp
    )
    target_link_libraries(omega_front_end_tests PRIVATE omega_app_core)
    if(MSVC)
        target_compile_options(omega_front_end_tests PRIVATE
            /W4
            /WX
            /permissive-
            /Zc:__cplusplus
            /Zc:preprocessor
            /utf-8
        )
    else()
        target_compile_options(omega_front_end_tests PRIVATE
            -Wall
            -Wextra
            -Wpedantic
            -Werror
            -Wno-missing-field-initializers
        )
    endif()
    add_test(NAME omega_front_end_tests COMMAND omega_front_end_tests)
    set_tests_properties(omega_front_end_tests PROPERTIES TIMEOUT 5)

    add_executable(omega_character_front_end_flow_tests
        native/tests/character_front_end_flow_tests.cpp
    )
    target_link_libraries(omega_character_front_end_flow_tests
        PRIVATE omega_app_core)
    if(MSVC)
        target_compile_options(omega_character_front_end_flow_tests PRIVATE
            /W4
            /WX
            /permissive-
            /Zc:__cplusplus
            /Zc:preprocessor
            /utf-8
        )
    else()
        target_compile_options(omega_character_front_end_flow_tests PRIVATE
            -Wall
            -Wextra
            -Wpedantic
            -Werror
            -Wno-missing-field-initializers
        )
    endif()
    add_test(NAME omega_character_front_end_flow_tests
        COMMAND omega_character_front_end_flow_tests)
    set_tests_properties(omega_character_front_end_flow_tests
        PROPERTIES TIMEOUT 5)

    add_executable(omega_diagnostic_actor_marker_tests
        native/tests/diagnostic_actor_marker_tests.cpp
    )
    target_link_libraries(omega_diagnostic_actor_marker_tests PRIVATE omega_app_core)
    if(MSVC)
        target_compile_options(omega_diagnostic_actor_marker_tests PRIVATE
            /W4
            /WX
            /permissive-
            /Zc:__cplusplus
            /Zc:preprocessor
            /utf-8
        )
    else()
        target_compile_options(omega_diagnostic_actor_marker_tests PRIVATE
            -Wall
            -Wextra
            -Wpedantic
            -Werror
            -Wno-missing-field-initializers
        )
    endif()
    add_test(NAME omega_diagnostic_actor_marker_tests
        COMMAND omega_diagnostic_actor_marker_tests)
    set_tests_properties(omega_diagnostic_actor_marker_tests PROPERTIES TIMEOUT 5)

    add_executable(omega_render_texture_pool_tests
        native/tests/render_texture_pool_tests.cpp
    )
    target_link_libraries(omega_render_texture_pool_tests PRIVATE omega_runtime)
    if(MSVC)
        target_compile_options(omega_render_texture_pool_tests PRIVATE
            /W4
            /WX
            /permissive-
            /Zc:__cplusplus
            /Zc:preprocessor
            /utf-8
        )
    else()
        target_compile_options(omega_render_texture_pool_tests PRIVATE -Wall -Wextra -Wpedantic -Werror -Wno-missing-field-initializers)
    endif()
    add_test(NAME omega_render_texture_pool_tests COMMAND omega_render_texture_pool_tests)
    set_tests_properties(omega_render_texture_pool_tests PROPERTIES TIMEOUT 5)

    add_executable(omega_render_mesh_pool_tests
        native/tests/render_mesh_pool_tests.cpp
    )
    target_link_libraries(omega_render_mesh_pool_tests PRIVATE omega_runtime)
    if(MSVC)
        target_compile_options(omega_render_mesh_pool_tests PRIVATE
            /W4
            /WX
            /permissive-
            /Zc:__cplusplus
            /Zc:preprocessor
            /utf-8
        )
    else()
        target_compile_options(omega_render_mesh_pool_tests PRIVATE
            -Wall
            -Wextra
            -Wpedantic
            -Werror
            -Wno-missing-field-initializers
        )
    endif()
    add_test(NAME omega_render_mesh_pool_tests COMMAND omega_render_mesh_pool_tests)
    set_tests_properties(omega_render_mesh_pool_tests PROPERTIES TIMEOUT 5)

    add_executable(omega_render_mesh_draw_list_tests
        native/tests/render_mesh_draw_list_tests.cpp
    )
    target_link_libraries(omega_render_mesh_draw_list_tests PRIVATE omega_runtime)
    if(MSVC)
        target_compile_options(omega_render_mesh_draw_list_tests PRIVATE
            /W4
            /WX
            /permissive-
            /Zc:__cplusplus
            /Zc:preprocessor
            /utf-8
        )
    else()
        target_compile_options(omega_render_mesh_draw_list_tests PRIVATE
            -Wall
            -Wextra
            -Wpedantic
            -Werror
            -Wno-missing-field-initializers
        )
    endif()
    add_test(NAME omega_render_mesh_draw_list_tests COMMAND omega_render_mesh_draw_list_tests)
    set_tests_properties(omega_render_mesh_draw_list_tests PROPERTIES TIMEOUT 5)

    add_executable(omega_render_draw_list_tests
        native/tests/render_draw_list_tests.cpp
    )
    target_link_libraries(omega_render_draw_list_tests PRIVATE omega_runtime)
    if(MSVC)
        target_compile_options(omega_render_draw_list_tests PRIVATE
            /W4
            /WX
            /permissive-
            /Zc:__cplusplus
            /Zc:preprocessor
            /utf-8
        )
    else()
        target_compile_options(omega_render_draw_list_tests PRIVATE -Wall -Wextra -Wpedantic -Werror -Wno-missing-field-initializers)
    endif()
    add_test(NAME omega_render_draw_list_tests COMMAND omega_render_draw_list_tests)
    set_tests_properties(omega_render_draw_list_tests PROPERTIES TIMEOUT 5)

    add_executable(omega_spatial_diagnostic_scene_tests
        native/tests/spatial_diagnostic_scene_tests.cpp
    )
    target_link_libraries(omega_spatial_diagnostic_scene_tests PRIVATE omega_runtime)
    if(MSVC)
        target_compile_options(omega_spatial_diagnostic_scene_tests PRIVATE
            /W4
            /WX
            /permissive-
            /Zc:__cplusplus
            /Zc:preprocessor
            /utf-8
        )
    else()
        target_compile_options(omega_spatial_diagnostic_scene_tests PRIVATE
            -Wall
            -Wextra
            -Wpedantic
            -Werror
            -Wno-missing-field-initializers
        )
    endif()
    add_test(NAME omega_spatial_diagnostic_scene_tests
        COMMAND omega_spatial_diagnostic_scene_tests)
    set_tests_properties(omega_spatial_diagnostic_scene_tests PROPERTIES TIMEOUT 5)

    add_executable(omega_texture_storage_topology_debug_image_tests
        native/tests/texture_storage_topology_debug_image_tests.cpp
    )
    target_link_libraries(omega_texture_storage_topology_debug_image_tests PRIVATE omega_runtime)
    if(MSVC)
        target_compile_options(omega_texture_storage_topology_debug_image_tests PRIVATE
            /W4
            /WX
            /permissive-
            /Zc:__cplusplus
            /Zc:preprocessor
            /utf-8
        )
    else()
        target_compile_options(omega_texture_storage_topology_debug_image_tests PRIVATE
            -Wall
            -Wextra
            -Wpedantic
            -Werror
            -Wno-missing-field-initializers
        )
    endif()
    add_test(NAME omega_texture_storage_topology_debug_image_tests
        COMMAND omega_texture_storage_topology_debug_image_tests)
    set_tests_properties(omega_texture_storage_topology_debug_image_tests PROPERTIES TIMEOUT 5)

    add_executable(omega_packed24_transfer_debug_image_tests
        native/tests/packed24_transfer_debug_image_tests.cpp
    )
    target_link_libraries(omega_packed24_transfer_debug_image_tests PRIVATE omega_runtime)
    if(MSVC)
        target_compile_options(omega_packed24_transfer_debug_image_tests PRIVATE
            /W4
            /WX
            /permissive-
            /Zc:__cplusplus
            /Zc:preprocessor
            /utf-8
        )
    else()
        target_compile_options(omega_packed24_transfer_debug_image_tests PRIVATE
            -Wall
            -Wextra
            -Wpedantic
            -Werror
            -Wno-missing-field-initializers
        )
    endif()
    add_test(NAME omega_packed24_transfer_debug_image_tests
        COMMAND omega_packed24_transfer_debug_image_tests)
    set_tests_properties(omega_packed24_transfer_debug_image_tests PROPERTIES TIMEOUT 5)

    add_executable(omega_tdx_indexed8_candidate_debug_image_tests
        native/tests/tdx_indexed8_candidate_debug_image_tests.cpp
    )
    target_link_libraries(omega_tdx_indexed8_candidate_debug_image_tests PRIVATE omega_runtime)
    if(MSVC)
        target_compile_options(omega_tdx_indexed8_candidate_debug_image_tests PRIVATE
            /W4
            /WX
            /permissive-
            /Zc:__cplusplus
            /Zc:preprocessor
            /utf-8
        )
    else()
        target_compile_options(omega_tdx_indexed8_candidate_debug_image_tests PRIVATE
            -Wall
            -Wextra
            -Wpedantic
            -Werror
            -Wno-missing-field-initializers
        )
    endif()
    add_test(NAME omega_tdx_indexed8_candidate_debug_image_tests
        COMMAND omega_tdx_indexed8_candidate_debug_image_tests)
    set_tests_properties(omega_tdx_indexed8_candidate_debug_image_tests PROPERTIES TIMEOUT 5)

    if(TARGET omega_sdl_backend)
        add_executable(omega_opening_movie_safety_tests
            native/apps/openomega/opening_movie_safety.cpp
            native/tests/opening_movie_safety_tests.cpp
        )
        target_include_directories(omega_opening_movie_safety_tests PRIVATE
            native/apps/openomega
        )
        target_link_libraries(omega_opening_movie_safety_tests PRIVATE omega_media)
        if(MSVC)
            target_compile_options(omega_opening_movie_safety_tests PRIVATE
                /W4
                /WX
                /permissive-
                /Zc:__cplusplus
                /Zc:preprocessor
                /utf-8
            )
        else()
            target_compile_options(omega_opening_movie_safety_tests PRIVATE
                -Wall
                -Wextra
                -Wpedantic
                -Werror
                -Wno-missing-field-initializers
            )
        endif()
        add_test(NAME omega_opening_movie_safety_tests
            COMMAND omega_opening_movie_safety_tests)
        set_tests_properties(omega_opening_movie_safety_tests PROPERTIES TIMEOUT 5)

        add_executable(omega_startup_failure_dialog_tests
            native/tests/startup_failure_dialog_tests.cpp
        )
        target_link_libraries(omega_startup_failure_dialog_tests
            PRIVATE
                omega_runtime
                omega_sdl_backend
        )
        if(MSVC)
            target_compile_options(omega_startup_failure_dialog_tests PRIVATE
                /W4
                /WX
                /permissive-
                /Zc:__cplusplus
                /Zc:preprocessor
                /utf-8
            )
        else()
            target_compile_options(omega_startup_failure_dialog_tests PRIVATE
                -Wall
                -Wextra
                -Wpedantic
                -Werror
                -Wno-missing-field-initializers
            )
        endif()
        add_test(NAME omega_startup_failure_dialog_tests
            COMMAND omega_startup_failure_dialog_tests)
        set_tests_properties(omega_startup_failure_dialog_tests PROPERTIES TIMEOUT 5)

        add_executable(omega_sdl_gpu_texture_smoke
            native/tests/sdl_gpu_texture_smoke.cpp
        )
        target_link_libraries(omega_sdl_gpu_texture_smoke
            PRIVATE
                omega_sdl_backend
                omega_runtime
        )
        if(MSVC)
            target_compile_options(omega_sdl_gpu_texture_smoke PRIVATE
                /W4
                /WX
                /permissive-
                /Zc:__cplusplus
                /Zc:preprocessor
                /utf-8
            )
        else()
            target_compile_options(omega_sdl_gpu_texture_smoke PRIVATE
                -Wall
                -Wextra
                -Wpedantic
                -Werror
                -Wno-missing-field-initializers
            )
        endif()
        if(OMEGA_RUN_GPU_SMOKE_TEST)
            add_test(NAME omega_sdl_gpu_texture_smoke COMMAND omega_sdl_gpu_texture_smoke)
            set_tests_properties(omega_sdl_gpu_texture_smoke PROPERTIES
                TIMEOUT 15
                RUN_SERIAL TRUE
                LABELS "gpu;integration"
            )
        endif()

        add_executable(omega_sdl_gpu_mesh_smoke
            native/tests/sdl_gpu_mesh_smoke.cpp
        )
        target_link_libraries(omega_sdl_gpu_mesh_smoke
            PRIVATE
                omega_sdl_backend
                omega_runtime
        )
        if(MSVC)
            target_compile_options(omega_sdl_gpu_mesh_smoke PRIVATE
                /W4
                /WX
                /permissive-
                /Zc:__cplusplus
                /Zc:preprocessor
                /utf-8
            )
        else()
            target_compile_options(omega_sdl_gpu_mesh_smoke PRIVATE
                -Wall
                -Wextra
                -Wpedantic
                -Werror
                -Wno-missing-field-initializers
            )
        endif()
        if(OMEGA_RUN_GPU_SMOKE_TEST)
            add_test(NAME omega_sdl_gpu_mesh_smoke COMMAND omega_sdl_gpu_mesh_smoke)
            set_tests_properties(omega_sdl_gpu_mesh_smoke PROPERTIES
                TIMEOUT 15
                RUN_SERIAL TRUE
                LABELS "gpu;integration"
            )
        endif()

        add_executable(omega_app_capture_smoke
            native/tests/omega_app_capture_smoke.cpp
        )
        target_link_libraries(omega_app_capture_smoke PRIVATE omega_app_host)
        if(MSVC)
            target_compile_options(omega_app_capture_smoke PRIVATE
                /W4
                /WX
                /permissive-
                /Zc:__cplusplus
                /Zc:preprocessor
                /utf-8
            )
        else()
            target_compile_options(omega_app_capture_smoke PRIVATE
                -Wall
                -Wextra
                -Wpedantic
                -Werror
                -Wno-missing-field-initializers
            )
        endif()

        add_executable(omega_app_opening_movie_smoke
            native/tests/omega_app_opening_movie_smoke.cpp
        )
        target_link_libraries(omega_app_opening_movie_smoke PRIVATE omega_app_host)
        if(MSVC)
            target_compile_options(omega_app_opening_movie_smoke PRIVATE
                /W4
                /WX
                /permissive-
                /Zc:__cplusplus
                /Zc:preprocessor
                /utf-8
            )
        else()
            target_compile_options(omega_app_opening_movie_smoke PRIVATE
                -Wall
                -Wextra
                -Wpedantic
                -Werror
                -Wno-missing-field-initializers
            )
        endif()

        if(OMEGA_RUN_GPU_SMOKE_TEST)
            add_test(NAME omega_app_opening_movie_smoke
                COMMAND omega_app_opening_movie_smoke)
            set_tests_properties(omega_app_opening_movie_smoke PROPERTIES
                ENVIRONMENT "SDL_AUDIO_DRIVER=dummy"
                TIMEOUT 30
                RUN_SERIAL TRUE
                LABELS "gpu;integration;opening-movie"
            )

            add_test(NAME omega_app_capture_smoke COMMAND omega_app_capture_smoke)
            set_tests_properties(omega_app_capture_smoke PROPERTIES
                ENVIRONMENT "SDL_AUDIO_DRIVER=dummy"
                TIMEOUT 20
                RUN_SERIAL TRUE
                LABELS "gpu;integration"
            )

            add_test(NAME openomega_capture_cli_smoke
                COMMAND "${CMAKE_COMMAND}"
                    "-DOPENOMEGA_EXECUTABLE=$<TARGET_FILE:openomega>"
                    -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/openomega_capture_cli_smoke.cmake"
            )
            set_tests_properties(openomega_capture_cli_smoke PROPERTIES
                ENVIRONMENT "SDL_AUDIO_DRIVER=dummy"
                TIMEOUT 20
                RUN_SERIAL TRUE
                LABELS "gpu;integration"
            )
            set_openomega_synthetic_profile_environment(openomega_capture_cli_smoke)

            add_test(NAME openomega_capture_replay_cli_smoke
                COMMAND "${CMAKE_COMMAND}"
                    "-DOPENOMEGA_EXECUTABLE=$<TARGET_FILE:openomega>"
                    -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/openomega_capture_replay_cli_smoke.cmake"
            )
            set_tests_properties(openomega_capture_replay_cli_smoke PROPERTIES
                ENVIRONMENT "SDL_AUDIO_DRIVER=dummy"
                TIMEOUT 20
                RUN_SERIAL TRUE
                LABELS "gpu;integration"
            )
            set_openomega_synthetic_profile_environment(openomega_capture_replay_cli_smoke)
        endif()

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
        set_tests_properties(omega_sdl_input_tests PROPERTIES TIMEOUT 15)
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
        native/tests/runtime_config_discovery_tests.cpp
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
    set_tests_properties(omega_core_tests PROPERTIES TIMEOUT 30)

    add_executable(omega_lpd_envelope_decoder_tests
        native/tests/lpd_envelope_decoder_tests.cpp
    )
    target_link_libraries(omega_lpd_envelope_decoder_tests PRIVATE omega_retail_formats)
    if(MSVC)
        target_compile_options(omega_lpd_envelope_decoder_tests PRIVATE
            /W4
            /WX
            /permissive-
            /Zc:__cplusplus
            /Zc:preprocessor
            /utf-8
        )
    else()
        target_compile_options(omega_lpd_envelope_decoder_tests PRIVATE
            -Wall
            -Wextra
            -Wpedantic
            -Werror
            -Wno-missing-field-initializers
        )
    endif()
    add_test(NAME omega_lpd_envelope_decoder_tests COMMAND omega_lpd_envelope_decoder_tests)
    set_tests_properties(omega_lpd_envelope_decoder_tests PROPERTIES TIMEOUT 5)

    add_executable(omega_so_module_descriptor_tests
        native/tests/so_module_descriptor_tests.cpp
    )
    target_link_libraries(omega_so_module_descriptor_tests PRIVATE omega_retail_formats)
    if(MSVC)
        target_compile_options(omega_so_module_descriptor_tests PRIVATE
            /W4
            /WX
            /permissive-
            /Zc:__cplusplus
            /Zc:preprocessor
            /utf-8
        )
    else()
        target_compile_options(omega_so_module_descriptor_tests PRIVATE
            -Wall
            -Wextra
            -Wpedantic
            -Werror
            -Wno-missing-field-initializers
        )
    endif()
    add_test(NAME omega_so_module_descriptor_tests COMMAND omega_so_module_descriptor_tests)
    set_tests_properties(omega_so_module_descriptor_tests PROPERTIES TIMEOUT 5)

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
    add_test(NAME omega_tool_rejects_empty_frontend_envelope_coverage_tree
        COMMAND omega_tool frontend-envelope-coverage-verify-tree
            "${CMAKE_CURRENT_BINARY_DIR}/empty-corpus")
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
        omega_tool_rejects_empty_frontend_envelope_coverage_tree
        PROPERTIES
            WILL_FAIL TRUE
            TIMEOUT 10
    )
    set_tests_properties(
        omega_tool_empty_level_material_catalogs_signature
        PROPERTIES
            PASS_REGULAR_EXPRESSION "\"catalogs\":0"
            TIMEOUT 10
    )
endif()

if(OMEGA_WINDOWS_PORTABLE_PACKAGE_ENABLED)
    if(NOT IS_ABSOLUTE "${CMAKE_LINKER}" OR NOT EXISTS "${CMAKE_LINKER}")
        message(FATAL_ERROR
            "OpenOmega Windows portable packaging requires an absolute existing MSVC linker"
        )
    endif()
    get_filename_component(OMEGA_MSVC_TOOL_DIRECTORY "${CMAKE_LINKER}" DIRECTORY)
    unset(OMEGA_WINDOWS_DEPENDENCY_DUMP_TOOL CACHE)
    unset(OMEGA_WINDOWS_DEPENDENCY_DUMP_TOOL)
    find_program(OMEGA_WINDOWS_DEPENDENCY_DUMP_TOOL
        NAMES dumpbin.exe
        PATHS "${OMEGA_MSVC_TOOL_DIRECTORY}"
        NO_DEFAULT_PATH
        NO_CACHE
    )
    if(OMEGA_WINDOWS_DEPENDENCY_DUMP_TOOL)
        set(OMEGA_WINDOWS_DEPENDENCY_DUMP_MODE "DUMPBIN")
    else()
        set(OMEGA_WINDOWS_DEPENDENCY_DUMP_TOOL "${CMAKE_LINKER}")
        set(OMEGA_WINDOWS_DEPENDENCY_DUMP_MODE "LINKER")
    endif()
    if(NOT IS_ABSOLUTE "${OMEGA_WINDOWS_DEPENDENCY_DUMP_TOOL}" OR
       NOT EXISTS "${OMEGA_WINDOWS_DEPENDENCY_DUMP_TOOL}")
        message(FATAL_ERROR
            "OpenOmega Windows portable packaging requires an absolute PE dependency dump tool"
        )
    endif()
    unset(OMEGA_WINDOWS_POWERSHELL CACHE)
    unset(OMEGA_WINDOWS_POWERSHELL)
    find_program(OMEGA_WINDOWS_POWERSHELL
        NAMES pwsh.exe powershell.exe
        REQUIRED
        NO_CACHE
    )
    if(NOT IS_ABSOLUTE "${OMEGA_WINDOWS_POWERSHELL}" OR
       NOT EXISTS "${OMEGA_WINDOWS_POWERSHELL}")
        message(FATAL_ERROR
            "OpenOmega Windows portable packaging requires an absolute PowerShell host"
        )
    endif()
    if(NOT IS_ABSOLUTE "$ENV{ComSpec}" OR NOT EXISTS "$ENV{ComSpec}")
        message(FATAL_ERROR
            "OpenOmega Windows portable packaging requires an absolute Windows command processor"
        )
    endif()

    if(BUILD_TESTING)
        add_test(NAME openomega_windows_portable_package_contract
            COMMAND "${CMAKE_COMMAND}"
                "-DOPENOMEGA_PACKAGE_CONTRACT_MODE=DRIVER"
                "-DOPENOMEGA_CPACK_COMMAND=${CMAKE_CPACK_COMMAND}"
                "-DOPENOMEGA_CPACK_CONFIG=${CMAKE_BINARY_DIR}/CPackConfig.cmake"
                "-DOPENOMEGA_BUILD_DIRECTORY=${CMAKE_BINARY_DIR}"
                "-DOPENOMEGA_SOURCE_DIRECTORY=${CMAKE_SOURCE_DIR}"
                "-DOPENOMEGA_PACKAGE_DIRECTORY=${CMAKE_BINARY_DIR}/package"
                "-DOPENOMEGA_PACKAGE_STAGE_DIRECTORY=${CMAKE_BINARY_DIR}/package-contract/stage"
                "-DOPENOMEGA_PACKAGE_EXTRACT_DIRECTORY=${CMAKE_BINARY_DIR}/package-contract/extract"
                "-DOPENOMEGA_PACKAGE_UNRELATED_DIRECTORY=${CMAKE_BINARY_DIR}/package-contract/unrelated"
                "-DOPENOMEGA_PACKAGE_PROFILE_DIRECTORY=${CMAKE_BINARY_DIR}/package-contract/profile"
                "-DOPENOMEGA_PACKAGE_CONFIRMED_PROFILE_DIRECTORY=${CMAKE_BINARY_DIR}/package-contract/confirmed-profile"
                "-DOPENOMEGA_NATIVE_PERSISTENCE_FIXTURE_WRITER=$<TARGET_FILE:omega_native_persistence_fixture_writer>"
                "-DOPENOMEGA_PACKAGE_BASENAME=${OMEGA_WINDOWS_PORTABLE_PACKAGE_ROOT}"
                "-DOPENOMEGA_CONFIGURED_ARCHITECTURE=${CMAKE_CXX_COMPILER_ARCHITECTURE_ID}"
                "-DOPENOMEGA_DUMP_TOOL=${OMEGA_WINDOWS_DEPENDENCY_DUMP_TOOL}"
                "-DOPENOMEGA_DUMP_TOOL_MODE=${OMEGA_WINDOWS_DEPENDENCY_DUMP_MODE}"
                "-DOPENOMEGA_COMSPEC=$ENV{ComSpec}"
                "-DOPENOMEGA_POWERSHELL=${OMEGA_WINDOWS_POWERSHELL}"
                -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/openomega_windows_package_contract.cmake"
            CONFIGURATIONS Release
        )
        set_tests_properties(openomega_windows_portable_package_contract PROPERTIES
            LABELS "integration;package"
            RUN_SERIAL TRUE
            # The package driver has a 530-second weighted sequential subprocess maximum.
            # Ten minutes preserves room for bounded file checks while remaining well below
            # the 30-minute Windows package-job watchdog.
            TIMEOUT 600
        )
    endif()

    set(CPACK_GENERATOR "ZIP")
    set(CPACK_PACKAGE_NAME "OpenOmega")
    set(CPACK_PACKAGE_VENDOR "OpenOmega clean-room project")
    set(CPACK_PACKAGE_VERSION "${PROJECT_VERSION}")
    set(CPACK_PACKAGE_FILE_NAME "${OMEGA_WINDOWS_PORTABLE_PACKAGE_ROOT}")
    set(CPACK_PACKAGE_CHECKSUM "SHA256")
    set(CPACK_INCLUDE_TOPLEVEL_DIRECTORY OFF)
    set(CPACK_PACKAGE_DIRECTORY "${CMAKE_BINARY_DIR}/package")
    set(CPACK_PROJECT_CONFIG_FILE
        "${CMAKE_CURRENT_SOURCE_DIR}/cmake/openomega_windows_package_contract.cmake"
    )
    set(CPACK_OPENOMEGA_PROJECT_CONFIG_MODE ON)
    include(CPack)
endif()
