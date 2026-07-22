include_guard(GLOBAL)

# Include this module once, then call omega_configure_product_outputs() and
# omega_configure_visual_studio() after all project and optional test targets
# have been declared.  Both entry points are safe to call again if a parent
# project adds more optional targets later.

# Product locations are intentionally generator-independent.  Command-line Ninja
# builds and Visual Studio builds should expose the same layout below their own
# binary directories, so launchers never need to understand generator defaults.
function(omega_configure_product_outputs)
    if(TARGET openomega_launcher)
        set_target_properties(openomega_launcher PROPERTIES
            RUNTIME_OUTPUT_DIRECTORY
                "${CMAKE_BINARY_DIR}/products/game/$<CONFIG>"
            PDB_OUTPUT_DIRECTORY
                "${CMAKE_BINARY_DIR}/products/game/$<CONFIG>"
        )
    endif()

    if(TARGET openomega)
        set_target_properties(openomega PROPERTIES
            RUNTIME_OUTPUT_DIRECTORY
                "${CMAKE_BINARY_DIR}/products/game/$<CONFIG>"
            PDB_OUTPUT_DIRECTORY
                "${CMAKE_BINARY_DIR}/products/game/$<CONFIG>"
        )
    endif()

    if(TARGET omega_tool)
        set_target_properties(omega_tool PROPERTIES
            RUNTIME_OUTPUT_DIRECTORY
                "${CMAKE_BINARY_DIR}/products/sdk/$<CONFIG>"
            PDB_OUTPUT_DIRECTORY
                "${CMAKE_BINARY_DIR}/products/sdk/$<CONFIG>"
        )
    endif()
endfunction()

function(_omega_is_visual_studio_generator output_variable)
    if(CMAKE_GENERATOR MATCHES "^Visual Studio ")
        set(${output_variable} TRUE PARENT_SCOPE)
    else()
        set(${output_variable} FALSE PARENT_SCOPE)
    endif()
endfunction()

function(_omega_vs_assign_folder folder)
    foreach(target_name IN LISTS ARGN)
        if(NOT TARGET "${target_name}")
            continue()
        endif()

        get_target_property(aliased_target "${target_name}" ALIASED_TARGET)
        get_target_property(imported_target "${target_name}" IMPORTED)
        if(aliased_target OR imported_target)
            continue()
        endif()

        set_property(TARGET "${target_name}" PROPERTY FOLDER "${folder}")
        _omega_vs_configure_executable("${target_name}")
    endforeach()
endfunction()

function(_omega_vs_configure_executable target_name)
    if(NOT TARGET "${target_name}")
        return()
    endif()

    get_target_property(target_type "${target_name}" TYPE)
    get_target_property(aliased_target "${target_name}" ALIASED_TARGET)
    get_target_property(imported_target "${target_name}" IMPORTED)
    if(NOT target_type STREQUAL "EXECUTABLE" OR aliased_target OR imported_target)
        return()
    endif()

    set_target_properties("${target_name}" PROPERTIES
        VS_DEBUGGER_WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}"
        VS_JUST_MY_CODE_DEBUGGING YES
    )
endfunction()

function(_omega_vs_map_library_to_host library_target host_target subsystem_id)
    set(one_value_arguments ARGUMENTS)
    set(multi_value_arguments ENVIRONMENT)
    cmake_parse_arguments(PARSE_ARGV 3 OMEGA_VS_HOST
        "" "${one_value_arguments}" "${multi_value_arguments}")

    if(NOT TARGET "${library_target}" OR NOT TARGET "${host_target}")
        return()
    endif()

    get_target_property(library_type "${library_target}" TYPE)
    get_target_property(library_alias "${library_target}" ALIASED_TARGET)
    get_target_property(library_imported "${library_target}" IMPORTED)
    get_target_property(host_type "${host_target}" TYPE)
    if(NOT library_type MATCHES
       "^(STATIC_LIBRARY|SHARED_LIBRARY|MODULE_LIBRARY|OBJECT_LIBRARY)$" OR
       library_alias OR library_imported OR NOT host_type STREQUAL "EXECUTABLE")
        return()
    endif()

    # This is deliberately a debugger command, not a target dependency.  Every
    # chosen host already depends on the library it exercises; adding a reverse
    # dependency would create a cycle.  Build the solution (or host) once before
    # launching a library project on a clean tree.
    set(debugger_environment
        "$<$<CONFIG:Debug>:OPENOMEGA_DEBUG_BREAK_SUBSYSTEM=${subsystem_id}>")
    foreach(environment_entry IN LISTS OMEGA_VS_HOST_ENVIRONMENT)
        string(APPEND debugger_environment "\n${environment_entry}")
    endforeach()

    set_target_properties("${library_target}" PROPERTIES
        VS_DEBUGGER_COMMAND "$<TARGET_FILE:${host_target}>"
        VS_DEBUGGER_COMMAND_ARGUMENTS "${OMEGA_VS_HOST_ARGUMENTS}"
        VS_DEBUGGER_ENVIRONMENT "${debugger_environment}"
        VS_DEBUGGER_WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}"
        VS_JUST_MY_CODE_DEBUGGING YES
    )
endfunction()

function(omega_set_visual_studio_startup_project)
    _omega_is_visual_studio_generator(is_visual_studio)
    if(NOT is_visual_studio)
        return()
    endif()

    foreach(target_name IN LISTS ARGN)
        if(NOT TARGET "${target_name}")
            continue()
        endif()
        get_target_property(target_type "${target_name}" TYPE)
        if(target_type STREQUAL "EXECUTABLE")
            set_property(DIRECTORY "${PROJECT_SOURCE_DIR}"
                PROPERTY VS_STARTUP_PROJECT "${target_name}")
            return()
        endif()
    endforeach()
endfunction()

function(omega_configure_visual_studio)
    _omega_is_visual_studio_generator(is_visual_studio)
    if(NOT is_visual_studio)
        return()
    endif()

    set_property(GLOBAL PROPERTY USE_FOLDERS ON)
    set_property(GLOBAL PROPERTY PREDEFINED_TARGETS_FOLDER
        "Developer Utilities/CMake")

    omega_set_visual_studio_startup_project(openomega_launcher openomega omega_tool)

    _omega_vs_assign_folder("Products/Launcher"
        openomega_launcher)
    _omega_vs_assign_folder("Products/Launcher/Libraries"
        omega_launcher_core
        omega_launcher_host)
    _omega_vs_assign_folder("Products/Game"
        openomega)
    _omega_vs_assign_folder("Products/Game/Libraries"
        omega_app_core
        omega_app_host
        omega_native_persistence)
    _omega_vs_assign_folder("Products/SDK"
        omega_tool)

    _omega_vs_assign_folder("Engine/Assets"
        omega_assets)
    _omega_vs_assign_folder("Engine/Core"
        omega_core)
    _omega_vs_assign_folder("Engine/Persistence"
        omega_persistence)
    _omega_vs_assign_folder("Engine/Profiles"
        omega_profiles)
    _omega_vs_assign_folder("Engine/Media"
        omega_media)
    _omega_vs_assign_folder("Engine/Simulation"
        omega_simulation)
    _omega_vs_assign_folder("Engine/Gameplay"
        omega_gameplay)
    _omega_vs_assign_folder("Engine/Content"
        omega_content)
    _omega_vs_assign_folder("Engine/Runtime"
        omega_runtime)

    _omega_vs_assign_folder("Compatibility/PS2"
        omega_ps2_compat)
    _omega_vs_assign_folder("Compatibility/Retail Formats"
        omega_retail_formats)

    _omega_vs_assign_folder("Platform/SDL3"
        omega_sdl_backend)
    # Keep vendored dependencies visible as third-party code.  They get no
    # OpenOmega debugger command, environment contract, or product location.
    _omega_vs_assign_folder("Platform/Third Party/SDL3"
        SDL3-static
        SDL3_Headers)

    _omega_vs_assign_folder("Tests/Contracts"
        omega_scene_fragment_wire_contract_tests)
    _omega_vs_assign_folder("Tests/Products/Game"
        omega_boot_sequence_tests
        omega_opening_movie_audio_clock_tests
        omega_opening_movie_audio_fault_tests
        omega_opening_movie_player_error_mapping_tests
        omega_opening_movie_player_boundary_tests
        omega_run_capture_tests
        omega_run_replay_session_tests
        omega_front_end_tests
        omega_character_front_end_flow_tests
        omega_diagnostic_actor_marker_tests
        omega_opening_movie_safety_tests)
    _omega_vs_assign_folder("Tests/Products/Launcher"
        omega_launcher_config_tests)
    _omega_vs_assign_folder("Tests/Products/Game/Persistence"
        omega_native_persistence_tests
        omega_native_character_session_tests)
    _omega_vs_assign_folder("Tests/Products/SDK"
        omega_pop_post_terrain_commands_tests
        omega_frontend_envelope_commands_tests
        omega_level_texture_commands_tests)

    _omega_vs_assign_folder("Tests/Engine/Persistence"
        omega_save_database_tests
        omega_native_save_path_tests)
    _omega_vs_assign_folder("Tests/Engine/Profiles"
        omega_profile_catalog_tests
        omega_character_catalog_tests)
    _omega_vs_assign_folder("Tests/Engine/Media"
        omega_mpeg_program_stream_descriptor_tests
        omega_mpeg_video_elementary_stream_tests
        omega_nv12_to_rgba8_tests
        omega_media_foundation_h262_decoder_tests
        omega_pss_pcm_audio_stream_tests)
    _omega_vs_assign_folder("Tests/Engine/Content"
        omega_level_texture_store_tests
        omega_asset_service_tests)
    _omega_vs_assign_folder("Tests/Engine/Runtime"
        omega_input_trace_tests
        omega_scheduler_elapsed_trace_tests
        omega_run_capture_session_tests
        omega_run_capture_replay_tests
        omega_render_texture_pool_tests
        omega_render_mesh_pool_tests
        omega_render_mesh_draw_list_tests
        omega_render_draw_list_tests
        omega_scene_transform_tests
        omega_diagnostic_actor_scene_tests
        omega_spatial_diagnostic_scene_tests
        omega_texture_storage_topology_debug_image_tests
        omega_packed24_transfer_debug_image_tests
        omega_tdx_indexed4_candidate_debug_image_tests
        omega_tdx_indexed8_candidate_debug_image_tests)
    _omega_vs_assign_folder("Tests/Engine/Gameplay"
        omega_gameplay_tests
        omega_diagnostic_proximity_trigger_tests
        omega_diagnostic_target_fire_tests
        omega_diagnostic_mission_lifecycle_tests)
    _omega_vs_assign_folder("Tests/Compatibility/PS2"
        omega_ps2_memory_card_image_tests
        omega_ps2_memory_card_filesystem_tests
        omega_ps2_memory_card_export_tests)
    _omega_vs_assign_folder("Tests/Compatibility/Retail Formats"
        omega_vag_adpcm_decoder_tests
        omega_skas_text_envelope_decoder_tests
        omega_vpk_wrapper_envelope_decoder_tests
        omega_fnt_envelope_descriptor_tests
        omega_gui_envelope_descriptor_tests
        omega_ie_envelope_descriptor_tests
        omega_tbl_envelope_descriptor_tests
        omega_par_text_envelope_tests
        omega_lpd_envelope_decoder_tests
        omega_so_module_descriptor_tests)
    _omega_vs_assign_folder("Tests/Platform/SDL3"
        omega_sdl_gpu_exception_boundary_tests
        omega_startup_failure_dialog_tests
        omega_sdl_audio_tests
        omega_sdl_input_tests)
    _omega_vs_assign_folder("Tests/Smoke"
        omega_sdl_gpu_texture_smoke
        omega_sdl_gpu_mesh_smoke
        omega_app_capture_smoke
        omega_app_opening_movie_smoke)
    _omega_vs_assign_folder("Tests/Aggregate"
        omega_core_tests)
    _omega_vs_assign_folder("Tests/Developer/Debugging"
        omega_subsystem_entry_break_contract_tests)

    _omega_vs_assign_folder("Developer Utilities/Test Support"
        omega_windows_argv_capture
        omega_native_persistence_fixture_writer)
    _omega_vs_assign_folder("Developer Utilities/Debug Hosts"
        omega_simulation_entry_break_host)
    _omega_vs_assign_folder("Developer Utilities/CTest Dashboard"
        Experimental
        Nightly
        Continuous
        NightlyMemoryCheck
        NightlyStart
        NightlyUpdate
        NightlyConfigure
        NightlyBuild
        NightlyTest
        NightlyCoverage
        NightlyMemCheck
        NightlySubmit
        ExperimentalStart
        ExperimentalUpdate
        ExperimentalConfigure
        ExperimentalBuild
        ExperimentalTest
        ExperimentalCoverage
        ExperimentalMemCheck
        ExperimentalSubmit
        ContinuousStart
        ContinuousUpdate
        ContinuousConfigure
        ContinuousBuild
        ContinuousTest
        ContinuousCoverage
        ContinuousMemCheck
        ContinuousSubmit)

    # Representative test hosts give each engine/compatibility library a small,
    # deterministic F5 path.  The application-facing libraries use a one-frame
    # capture/replay run with isolated native persistence and dummy audio.
    _omega_vs_map_library_to_host(omega_core omega_core_tests omega_core)
    _omega_vs_map_library_to_host(
        omega_persistence omega_save_database_tests omega_persistence)
    _omega_vs_map_library_to_host(
        omega_profiles omega_profile_catalog_tests omega_profiles)
    _omega_vs_map_library_to_host(
        omega_ps2_compat omega_ps2_memory_card_image_tests omega_ps2_compat)
    _omega_vs_map_library_to_host(
        omega_media omega_mpeg_program_stream_descriptor_tests omega_media)
    _omega_vs_map_library_to_host(
        omega_simulation omega_simulation_entry_break_host omega_simulation)
    _omega_vs_map_library_to_host(
        omega_gameplay omega_gameplay_tests omega_gameplay)
    _omega_vs_map_library_to_host(
        omega_retail_formats omega_vag_adpcm_decoder_tests omega_retail_formats)
    _omega_vs_map_library_to_host(
        omega_content omega_level_texture_store_tests omega_content)
    _omega_vs_map_library_to_host(
        omega_runtime omega_input_trace_tests omega_runtime)
    _omega_vs_map_library_to_host(
        omega_launcher_core openomega_launcher omega_launcher_core)
    _omega_vs_map_library_to_host(
        omega_launcher_host openomega_launcher omega_launcher_host)

    foreach(library_target IN ITEMS
            omega_app_core
            omega_native_persistence
            omega_app_host
            omega_sdl_backend)
        _omega_vs_map_library_to_host(
            "${library_target}" openomega "${library_target}"
            ARGUMENTS "--frames=1 --capture-run --replay-capture --developer-diagnostics"
            ENVIRONMENT
                "LOCALAPPDATA=${CMAKE_BINARY_DIR}/visual-studio-profiles/${library_target}/$<CONFIG>"
                "OPENOMEGA_DISABLE_STARTUP_DIALOG=1"
                "SDL_AUDIO_DRIVER=dummy")
    endforeach()

    # Interface-only omega_assets contains no executable entry point.  SDL's
    # targets are third-party code.  Neither receives an OpenOmega debug host.

    if(TARGET openomega AND
       EXISTS "${PROJECT_SOURCE_DIR}/debugger/OpenOmega.natvis")
        set(natvis_file "${PROJECT_SOURCE_DIR}/debugger/OpenOmega.natvis")
        get_target_property(openomega_sources openomega SOURCES)
        list(FIND openomega_sources "${natvis_file}" natvis_source_index)
        if(natvis_source_index EQUAL -1)
            target_sources(openomega PRIVATE "${natvis_file}")
        endif()
        source_group("Debugger Visualizers" FILES
            "${natvis_file}")
    endif()
endfunction()
