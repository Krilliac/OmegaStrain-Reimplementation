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
file(REMOVE_RECURSE "${empty_data_root}")
file(MAKE_DIRECTORY "${empty_data_root}")

run_openomega_case(zero_frames TRUE "${zero_frame_stdout}" "" --frames=0)
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
run_openomega_case(replay_attached_value FALSE ""
    "unknown option: --replay-capture=true\n${openomega_usage}"
    --replay-capture=true
)
run_openomega_case(capture_zero_frames FALSE ""
    "--capture-run requires --frames in the range 1..65536\n${openomega_usage}"
    --frames=0 --capture-run
)
run_openomega_case(capture_over_limit FALSE ""
    "--capture-run requires --frames in the range 1..65536\n${openomega_usage}"
    --frames=65537 --capture-run
)
