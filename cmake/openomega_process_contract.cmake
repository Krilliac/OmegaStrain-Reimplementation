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

string(CONCAT openomega_usage
    "usage: openomega [-h|--help]\n"
    "       openomega [--config=PATH] [--set=KEY=VALUE ...] "
    "[--frames=N [--capture-run [--replay-capture]]] "
    "[--data-root=PATH [--level=CODE] [--probe-only]]\n"
)
set(zero_frame_stdout "OpenOmega native shell: rendered_frames=0\n")
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
   NOT "$ENV{HOME}" STREQUAL "$ENV{OPENOMEGA_TEST_PROFILE_ROOT}/home")
    message(FATAL_ERROR "synthetic runtime-profile environment is required")
endif()

# E-0075 consults only the host-family profile root captured by the composition root. A malformed
# neighboring file remains irrelevant unless selected explicitly.
file(WRITE "${process_working_directory}/openomega.cfg"
    "content.level_code = AMBIENT1\n")

if(WIN32)
    set(default_profile_directory "$ENV{LOCALAPPDATA}/OpenOmega")
elseif(APPLE)
    set(default_profile_directory
        "$ENV{HOME}/Library/Application Support/OpenOmega")
else()
    set(default_profile_directory "$ENV{XDG_CONFIG_HOME}/openomega")
endif()
set(default_profile "${default_profile_directory}/openomega.cfg")
file(REMOVE_RECURSE "${default_profile_directory}")

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
set(missing_explicit_config "${process_working_directory}/missing-explicit.cfg")

run_openomega_case(zero_frames TRUE "${zero_frame_stdout}" "" --frames=0)

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
    "runtime configuration ${missing_explicit_config}: unable to open config file: ${missing_explicit_config}\n"
    "--config=${missing_explicit_config}" --frames=0
)
run_openomega_case(help_bypasses_malformed_default TRUE "${openomega_usage}" "" --help)
run_openomega_case(parse_error_bypasses_malformed_default FALSE ""
    "unknown option: --replay-capture=true\n${openomega_usage}"
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
