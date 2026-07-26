cmake_minimum_required(VERSION 3.28)

if(NOT DEFINED OMEGA_TOOL_EXECUTABLE OR OMEGA_TOOL_EXECUTABLE STREQUAL "")
    message(FATAL_ERROR "OMEGA_TOOL_EXECUTABLE is required")
endif()
if(NOT IS_ABSOLUTE "${OMEGA_TOOL_EXECUTABLE}" OR
   NOT EXISTS "${OMEGA_TOOL_EXECUTABLE}" OR
   IS_DIRECTORY "${OMEGA_TOOL_EXECUTABLE}")
    message(FATAL_ERROR "OMEGA_TOOL_EXECUTABLE must name an existing absolute file")
endif()

function(normalize_process_output output_variable input_value)
    string(REPLACE "\r\n" "\n" normalized "${input_value}")
    string(REPLACE "\r" "\n" normalized "${normalized}")
    set(${output_variable} "${normalized}" PARENT_SCOPE)
endfunction()

function(make_depth_fixture fixture_name deepest_depth output_variable)
    set(root
        "${CMAKE_CURRENT_BINARY_DIR}/omega-tool-corpus-bounds-${fixture_name}-depth-${deepest_depth}")
    file(REMOVE_RECURSE "${root}")
    file(MAKE_DIRECTORY "${root}")

    # recursive_directory_iterator reports the first child as depth zero.
    set(current "${root}")
    foreach(depth RANGE 0 ${deepest_depth})
        string(APPEND current "/d")
        file(MAKE_DIRECTORY "${current}")
    endforeach()

    set(${output_variable} "${root}" PARENT_SCOPE)
endfunction()

function(run_depth_case
    case_name command expected_stdout expected_stderr fixture_root)
    execute_process(
        COMMAND "${OMEGA_TOOL_EXECUTABLE}" "${command}" "${fixture_root}"
        RESULT_VARIABLE actual_result
        OUTPUT_VARIABLE actual_stdout
        ERROR_VARIABLE actual_stderr
        TIMEOUT 10
    )

    normalize_process_output(actual_stdout "${actual_stdout}")
    normalize_process_output(actual_stderr "${actual_stderr}")

    if(NOT actual_result STREQUAL "2")
        message(FATAL_ERROR
            "${case_name}: expected exit code 2, got '${actual_result}'")
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

set(hog_accepted_stdout [[{"archives":0,"valid":0,"errors":0,"entries":0,"bytes":0}]])
string(APPEND hog_accepted_stdout "\n")
set(nested_hog_accepted_stdout
    [[{"top_level_archives":0,"top_level_valid":0,"top_level_errors":0,"top_level_entries":0,"nested_candidates":0,"nested_valid":0,"nested_errors":0,"nested_entries":0,"nested_bytes":0,"exact_spans":0,"zero_padded_spans":0}]])
string(APPEND nested_hog_accepted_stdout "\n")
set(pop_accepted_stdout
    [[{"files":0,"valid":0,"errors":0,"terrain_records":0,"nonzero_alignment_records":0}]])
string(APPEND pop_accepted_stdout "\n")

make_depth_fixture("hog-accepted" 32 hog_accepted_root)
run_depth_case(
    "hog-verify-tree depth 32"
    "hog-verify-tree"
    "${hog_accepted_stdout}"
    "no top-level HOG files were found\n"
    "${hog_accepted_root}")

make_depth_fixture("nested-hog-accepted" 32 nested_hog_accepted_root)
run_depth_case(
    "hog-verify-nested-tree depth 32"
    "hog-verify-nested-tree"
    "${nested_hog_accepted_stdout}"
    "no top-level HOG files were found\nno nested HOG spans were found\n"
    "${nested_hog_accepted_root}")

make_depth_fixture("pop-accepted" 32 pop_accepted_root)
run_depth_case(
    "pop-verify-tree depth 32"
    "pop-verify-tree"
    "${pop_accepted_stdout}"
    "no POP files were found\n"
    "${pop_accepted_root}")

make_depth_fixture("hog-rejected" 33 hog_rejected_root)
run_depth_case(
    "hog-verify-tree depth 33"
    "hog-verify-tree"
    "{\"archives\":0,\"valid\":0,\"errors\":1,\"entries\":0,\"bytes\":0}\n"
    "HOG corpus depth exceeds safety limit\nno top-level HOG files were found\n"
    "${hog_rejected_root}")

make_depth_fixture("nested-hog-rejected" 33 nested_hog_rejected_root)
run_depth_case(
    "hog-verify-nested-tree depth 33"
    "hog-verify-nested-tree"
    "{\"top_level_archives\":0,\"top_level_valid\":0,\"top_level_errors\":1,\"top_level_entries\":0,\"nested_candidates\":0,\"nested_valid\":0,\"nested_errors\":0,\"nested_entries\":0,\"nested_bytes\":0,\"exact_spans\":0,\"zero_padded_spans\":0}\n"
    "HOG corpus depth exceeds safety limit\nno top-level HOG files were found\nno nested HOG spans were found\n"
    "${nested_hog_rejected_root}")

make_depth_fixture("pop-rejected" 33 pop_rejected_root)
run_depth_case(
    "pop-verify-tree depth 33"
    "pop-verify-tree"
    "{\"files\":0,\"valid\":0,\"errors\":1,\"terrain_records\":0,\"nonzero_alignment_records\":0}\n"
    "POP corpus depth exceeds safety limit\nno POP files were found\n"
    "${pop_rejected_root}")
