cmake_minimum_required(VERSION 3.28)

if(NOT DEFINED OPENOMEGA_EXECUTABLE OR OPENOMEGA_EXECUTABLE STREQUAL "")
    message(FATAL_ERROR "OPENOMEGA_EXECUTABLE is required")
endif()
if(NOT EXISTS "${OPENOMEGA_EXECUTABLE}")
    message(FATAL_ERROR "OPENOMEGA_EXECUTABLE does not name an existing file")
endif()

function(normalize_process_output output_variable input_value)
    string(REPLACE "\r\n" "\n" normalized "${input_value}")
    string(REPLACE "\r" "\n" normalized "${normalized}")
    set(${output_variable} "${normalized}" PARENT_SCOPE)
endfunction()

function(run_openomega_case case_name expect_success expected_stdout expected_stderr)
    execute_process(
        COMMAND "${OPENOMEGA_EXECUTABLE}" ${ARGN}
        WORKING_DIRECTORY "${process_working_directory}"
        RESULT_VARIABLE actual_result
        OUTPUT_VARIABLE actual_stdout
        ERROR_VARIABLE actual_stderr
    )

    normalize_process_output(actual_stdout "${actual_stdout}")
    normalize_process_output(actual_stderr "${actual_stderr}")

    foreach(forbidden_fragment IN LISTS openomega_forbidden_diagnostic_fragments)
        string(FIND "${actual_stdout}${actual_stderr}" "${forbidden_fragment}"
            forbidden_position)
        if(NOT forbidden_position EQUAL -1)
            message(FATAL_ERROR
                "${case_name}: process output disclosed a forbidden private-path fragment")
        endif()
    endforeach()

    if(expect_success)
        if(NOT actual_result STREQUAL "0")
            message(FATAL_ERROR
                "${case_name}: expected exit code 0, got '${actual_result}'")
        endif()
    elseif(actual_result STREQUAL "0")
        message(FATAL_ERROR "${case_name}: expected a nonzero exit code, got 0")
    elseif(NOT actual_result MATCHES "^[0-9]+$")
        message(FATAL_ERROR
            "${case_name}: process did not report a numeric exit code: '${actual_result}'")
    endif()

    if(NOT actual_stdout STREQUAL expected_stdout)
        message(FATAL_ERROR
            "${case_name}: stdout mismatch\nexpected=[${expected_stdout}]\n"
            "actual=[${actual_stdout}]")
    endif()
    if(NOT actual_stderr STREQUAL expected_stderr)
        message(FATAL_ERROR
            "${case_name}: stderr mismatch\nexpected=[${expected_stderr}]\n"
            "actual=[${actual_stderr}]")
    endif()
endfunction()

function(require_native_save_genesis case_name native_save_directory)
    if(NOT EXISTS "${native_save_directory}" OR
       NOT IS_DIRECTORY "${native_save_directory}" OR
       IS_SYMLINK "${native_save_directory}")
        message(FATAL_ERROR "${case_name}: native-save directory is invalid")
    endif()

    file(GLOB native_save_entries
        LIST_DIRECTORIES TRUE
        RELATIVE "${native_save_directory}"
        "${native_save_directory}/*"
    )
    list(SORT native_save_entries)
    set(expected_native_save_entries
        "openomega-save-a.oodb"
        "openomega-save-b.oodb"
        "openomega-save.lock"
    )
    list(SORT expected_native_save_entries)
    if(NOT native_save_entries STREQUAL expected_native_save_entries)
        message(FATAL_ERROR
            "${case_name}: native-save entries differ\n"
            "expected=[${expected_native_save_entries}]\n"
            "actual=[${native_save_entries}]"
        )
    endif()

    foreach(snapshot IN ITEMS openomega-save-a.oodb openomega-save-b.oodb)
        set(snapshot_path "${native_save_directory}/${snapshot}")
        if(NOT EXISTS "${snapshot_path}" OR IS_DIRECTORY "${snapshot_path}" OR
           IS_SYMLINK "${snapshot_path}")
            message(FATAL_ERROR "${case_name}: ${snapshot} is not a regular file")
        endif()
        file(SIZE "${snapshot_path}" snapshot_size)
        if(NOT snapshot_size EQUAL 64)
            message(FATAL_ERROR
                "${case_name}: ${snapshot} is not a generation-zero snapshot")
        endif()
        file(SHA256 "${snapshot_path}" snapshot_hash)
        if(NOT snapshot_hash STREQUAL
           "5017fce9c2b7f1fe487d6d35ed3ae5af6ac1826460044af0c03af5155d6d11f4")
            message(FATAL_ERROR
                "${case_name}: ${snapshot} bytes do not match format-v1 genesis")
        endif()
    endforeach()

    set(lock_path "${native_save_directory}/openomega-save.lock")
    if(NOT EXISTS "${lock_path}" OR IS_DIRECTORY "${lock_path}" OR
       IS_SYMLINK "${lock_path}")
        message(FATAL_ERROR "${case_name}: native-save lock is not a regular file")
    endif()
    file(SIZE "${lock_path}" lock_size)
    if(NOT lock_size EQUAL 0)
        message(FATAL_ERROR "${case_name}: native-save lock file is not empty")
    endif()
endfunction()

function(directory_manifest root output_variable)
    if(NOT EXISTS "${root}")
        set(${output_variable} "<missing>" PARENT_SCOPE)
        return()
    endif()
    file(GLOB_RECURSE entries
        LIST_DIRECTORIES TRUE
        RELATIVE "${root}"
        "${root}/*"
    )
    list(SORT entries)
    set(manifest "")
    foreach(relative_path IN LISTS entries)
        set(absolute_path "${root}/${relative_path}")
        if(IS_SYMLINK "${absolute_path}")
            string(APPEND manifest "L|${relative_path}\n")
        elseif(IS_DIRECTORY "${absolute_path}")
            string(APPEND manifest "D|${relative_path}\n")
        elseif(EXISTS "${absolute_path}")
            file(SIZE "${absolute_path}" file_size)
            file(SHA256 "${absolute_path}" file_hash)
            string(APPEND manifest
                "F|${relative_path}|${file_size}|${file_hash}\n")
        else()
            string(APPEND manifest "N|${relative_path}\n")
        endif()
    endforeach()
    set(${output_variable} "${manifest}" PARENT_SCOPE)
endfunction()

function(require_same_manifest case_name expected actual)
    if(NOT "${expected}" STREQUAL "${actual}")
        message(FATAL_ERROR
            "${case_name}: filesystem manifest changed\n"
            "before=[${expected}]\nafter=[${actual}]"
        )
    endif()
endfunction()

string(CONCAT openomega_usage
    "usage: openomega [-h|--help]\n"
    "       openomega [--config=PATH] [--set=KEY=VALUE ...] "
    "[--frames=N [--capture-run [--replay-capture]]] "
    "[--data-root=PATH [--level=CODE]] [--probe-only] "
    "[--opening-movie=PATH]\n"
)
# E-0089's empty-profile startup exercises the bounded front-end model without entering the frame
# loop; stdout remains the established process contract and no profile is implicitly created.
string(CONCAT zero_frame_stdout
    "OpenOmega native persistence: profiles=0\n"
    "OpenOmega native shell: rendered_frames=0\n"
)
set(empty_data_root "${CMAKE_CURRENT_BINARY_DIR}/openomega-process-contract-empty-data-root")
set(process_working_directory
    "${CMAKE_CURRENT_BINARY_DIR}/openomega-process-contract-working-directory")
file(REMOVE_RECURSE "${empty_data_root}")
file(MAKE_DIRECTORY "${empty_data_root}")
file(REMOVE_RECURSE "${process_working_directory}")
file(MAKE_DIRECTORY "${process_working_directory}")

if("$ENV{OPENOMEGA_TEST_PROFILE_ROOT}" STREQUAL "" OR
   NOT "$ENV{LOCALAPPDATA}" STREQUAL "$ENV{OPENOMEGA_TEST_PROFILE_ROOT}/local-app-data" OR
   NOT "$ENV{XDG_CONFIG_HOME}" STREQUAL "$ENV{OPENOMEGA_TEST_PROFILE_ROOT}/xdg-config-home" OR
   NOT "$ENV{XDG_DATA_HOME}" STREQUAL "$ENV{OPENOMEGA_TEST_PROFILE_ROOT}/xdg-data-home" OR
   NOT "$ENV{HOME}" STREQUAL "$ENV{OPENOMEGA_TEST_PROFILE_ROOT}/home")
    message(FATAL_ERROR "synthetic runtime-profile environment is required")
endif()

# E-0075 consults only the host-family profile root captured by the composition root. A malformed
# neighboring file remains irrelevant unless selected explicitly.
file(WRITE "${process_working_directory}/openomega.cfg"
    "content.level_code = AMBIENT1\n")

if(WIN32)
    set(default_profile_directory "$ENV{LOCALAPPDATA}/OpenOmega")
    set(native_save_directory "$ENV{LOCALAPPDATA}/OpenOmega/native-save")
elseif(APPLE)
    set(default_profile_directory
        "$ENV{HOME}/Library/Application Support/OpenOmega")
    set(native_save_directory
        "$ENV{HOME}/Library/Application Support/OpenOmega/native-save")
else()
    set(default_profile_directory "$ENV{XDG_CONFIG_HOME}/openomega")
    set(native_save_directory "$ENV{XDG_DATA_HOME}/openomega/native-save")
endif()
set(default_profile "${default_profile_directory}/openomega.cfg")
file(REMOVE_RECURSE "${default_profile_directory}")
file(REMOVE_RECURSE "${native_save_directory}")

set(missing_root_config "${process_working_directory}/missing-root.cfg")
file(WRITE "${missing_root_config}" "content.level_code = MINSK\n")
set(empty_root_config "${process_working_directory}/empty-root.cfg")
file(WRITE "${empty_root_config}" "content.data_root =\n")
set(invalid_level_config "${process_working_directory}/invalid-level.cfg")
file(WRITE "${invalid_level_config}"
    "content.data_root = secret-configured-root\n"
    "content.level_code = secret-invalid-level\n")
set(configured_root_config "${process_working_directory}/configured-root.cfg")
file(WRITE "${configured_root_config}" "content.data_root = ${empty_data_root}\n")
set(explicit_empty_config "${process_working_directory}/explicit-empty.cfg")
file(WRITE "${explicit_empty_config}" "")
set(missing_explicit_config
    "${process_working_directory}/PrivateUser-SecretVault-missing-explicit.cfg")
set(openomega_forbidden_diagnostic_fragments
    "PrivateUser"
    "SecretVault"
    "synthetic-runtime-profiles"
    "${missing_explicit_config}"
    "${default_profile}"
)

# Keep every probe-root precedence case before the first startup that may create native state.
run_openomega_case(probe_failure_precedes_persistence FALSE ""
    "content startup [missing-required-file]: game-data root is missing SYSTEM.CNF\n"
    "--data-root=${empty_data_root}" --probe-only
)
if(EXISTS "${native_save_directory}" OR IS_SYMLINK "${native_save_directory}")
    message(FATAL_ERROR "probe-only failure touched native persistence")
endif()

run_openomega_case(probe_explicit_config_root_reaches_startup FALSE ""
    "content startup [missing-required-file]: game-data root is missing SYSTEM.CNF\n"
    "--config=${configured_root_config}" --probe-only
)
if(EXISTS "${native_save_directory}" OR IS_SYMLINK "${native_save_directory}")
    message(FATAL_ERROR "explicit-config probe touched native persistence")
endif()

file(MAKE_DIRECTORY "${default_profile_directory}")
file(WRITE "${default_profile}" "content.data_root = ${empty_data_root}\n")
run_openomega_case(probe_default_config_root_reaches_startup FALSE ""
    "content startup [missing-required-file]: game-data root is missing SYSTEM.CNF\n"
    --probe-only
)
if(EXISTS "${native_save_directory}" OR IS_SYMLINK "${native_save_directory}")
    message(FATAL_ERROR "default-config probe touched native persistence")
endif()
file(REMOVE "${default_profile}")

run_openomega_case(probe_without_effective_root_fails_late FALSE ""
    "content startup [invalid-options]: content startup requires a data root\n"
    --probe-only
)
if(EXISTS "${native_save_directory}" OR IS_SYMLINK "${native_save_directory}")
    message(FATAL_ERROR "rootless probe touched native persistence")
endif()

run_openomega_case(zero_frames TRUE "${zero_frame_stdout}" "" --frames=0)
require_native_save_genesis("first zero-frame startup" "${native_save_directory}")
directory_manifest("${native_save_directory}" native_save_after_first_startup)
run_openomega_case(zero_frames_reopen TRUE "${zero_frame_stdout}" "" --frames=0)
directory_manifest("${native_save_directory}" native_save_after_reopen)
require_same_manifest("second zero-frame native-save reopen"
    "${native_save_after_first_startup}" "${native_save_after_reopen}")

file(MAKE_DIRECTORY "${default_profile}")
run_openomega_case(nonregular_default_profile FALSE ""
    "runtime configuration default profile: config path is not a regular file\n"
    --frames=0
)
file(REMOVE_RECURSE "${default_profile}")

file(MAKE_DIRECTORY "${default_profile_directory}")
file(WRITE "${default_profile}" "content.data_root = ${empty_data_root}\n")
run_openomega_case(default_root_reaches_startup FALSE ""
    "content startup [missing-required-file]: game-data root is missing SYSTEM.CNF\n"
    --frames=0
)

file(WRITE "${default_profile}" "not a setting\n")
run_openomega_case(malformed_default FALSE ""
    "runtime configuration default profile: config line 1 is missing '='\n"
    --frames=0
)
run_openomega_case(malformed_default_precedes_direct_content FALSE ""
    "runtime configuration default profile: config line 1 is missing '='\n"
    "--data-root=${empty_data_root}" --frames=0
)
run_openomega_case(explicit_config_bypasses_malformed_default TRUE
    "${zero_frame_stdout}" ""
    "--config=${explicit_empty_config}" --frames=0
)
run_openomega_case(explicit_missing_remains_fatal FALSE ""
    "runtime configuration explicit profile: unable to open config file\n"
    "--config=${missing_explicit_config}" --frames=0
)

if(WIN32)
    set(saved_native_data_root "$ENV{LOCALAPPDATA}")
    unset(ENV{LOCALAPPDATA})
elseif(APPLE)
    set(saved_native_data_root "$ENV{HOME}")
    unset(ENV{HOME})
else()
    set(saved_native_data_root "$ENV{XDG_DATA_HOME}")
    set(saved_native_home "$ENV{HOME}")
    unset(ENV{XDG_DATA_HOME})
    unset(ENV{HOME})
endif()
run_openomega_case(native_save_path_unavailable FALSE ""
    "native persistence [path-unavailable]: native persistence default path: no usable absolute platform data root\n"
    "--config=${explicit_empty_config}" --frames=0
)
if(WIN32)
    set(ENV{LOCALAPPDATA} "${saved_native_data_root}")
elseif(APPLE)
    set(ENV{HOME} "${saved_native_data_root}")
else()
    set(ENV{XDG_DATA_HOME} "${saved_native_data_root}")
    set(ENV{HOME} "${saved_native_home}")
endif()
run_openomega_case(help_bypasses_malformed_default TRUE "${openomega_usage}" "" --help)
run_openomega_case(parse_error_bypasses_malformed_default FALSE ""
    "unknown option: --replay-capture\n${openomega_usage}"
    --replay-capture=true
)

# Direct content launch cases below intentionally run with no default profile.
file(REMOVE "${default_profile}")

run_openomega_case(config_missing_root FALSE ""
    "content launch profile [missing-data-root]: content.data_root is required when content.level_code is set\n"
    "--config=${missing_root_config}" --frames=0
)
run_openomega_case(config_empty_root FALSE ""
    "content launch profile [invalid-data-root]: content.data_root must be a non-empty valid path\n"
    "--config=${empty_root_config}" --frames=0
)
run_openomega_case(config_invalid_level FALSE ""
    "content launch profile [invalid-level-code]: content.level_code must contain 1 to 32 ASCII alphanumeric characters\n"
    "--config=${invalid_level_config}" --frames=0
)
run_openomega_case(config_invalid_even_with_direct_cli FALSE ""
    "content launch profile [invalid-level-code]: content.level_code must contain 1 to 32 ASCII alphanumeric characters\n"
    "--config=${invalid_level_config}" "--data-root=${empty_data_root}" --frames=0
)
run_openomega_case(configured_root_reaches_startup FALSE ""
    "content startup [missing-required-file]: game-data root is missing SYSTEM.CNF\n"
    "--config=${configured_root_config}" --frames=0
)
run_openomega_case(set_supplies_missing_root FALSE ""
    "content startup [missing-required-file]: game-data root is missing SYSTEM.CNF\n"
    "--config=${missing_root_config}" "--set=content.data_root=${empty_data_root}" --frames=0
)
run_openomega_case(missing_system_config FALSE ""
    "content startup [missing-required-file]: game-data root is missing SYSTEM.CNF\n"
    "--data-root=${empty_data_root}"
)
run_openomega_case(capture_without_frames FALSE ""
    "--capture-run requires --frames\n${openomega_usage}"
    --capture-run
)
run_openomega_case(replay_without_capture FALSE ""
    "--replay-capture requires --capture-run\n${openomega_usage}"
    --frames=1 --replay-capture
)
run_openomega_case(replay_without_frames FALSE ""
    "--capture-run requires --frames\n${openomega_usage}"
    --replay-capture --capture-run
)
run_openomega_case(capture_zero_frames FALSE ""
    "--capture-run requires --frames in the range 1..65536\n${openomega_usage}"
    --frames=0 --capture-run
)
run_openomega_case(capture_over_limit FALSE ""
    "--capture-run requires --frames in the range 1..65536\n${openomega_usage}"
    --frames=65537 --capture-run
)

directory_manifest("${native_save_directory}" native_save_after_contract)
require_same_manifest("process contract native-save stability"
    "${native_save_after_first_startup}" "${native_save_after_contract}")
