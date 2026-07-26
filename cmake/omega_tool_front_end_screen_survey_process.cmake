cmake_minimum_required(VERSION 3.28)

if(NOT DEFINED OMEGA_TOOL_EXECUTABLE OR OMEGA_TOOL_EXECUTABLE STREQUAL "")
    message(FATAL_ERROR "OMEGA_TOOL_EXECUTABLE is required")
endif()
if(NOT EXISTS "${OMEGA_TOOL_EXECUTABLE}")
    message(FATAL_ERROR "OMEGA_TOOL_EXECUTABLE does not name an existing file")
endif()
if(NOT DEFINED OMEGA_SURVEY_PRIVATE_ROOT OR OMEGA_SURVEY_PRIVATE_ROOT STREQUAL "")
    message(FATAL_ERROR "OMEGA_SURVEY_PRIVATE_ROOT is required")
endif()
if(EXISTS "${OMEGA_SURVEY_PRIVATE_ROOT}")
    message(FATAL_ERROR "synthetic survey root must remain nonexistent")
endif()

execute_process(
    COMMAND "${OMEGA_TOOL_EXECUTABLE}"
        front-end-screen-survey
        "${OMEGA_SURVEY_PRIVATE_ROOT}"
    RESULT_VARIABLE survey_result
    OUTPUT_VARIABLE survey_stdout
    ERROR_VARIABLE survey_stderr
)

string(REPLACE "\r\n" "\n" survey_stdout "${survey_stdout}")
string(REPLACE "\r" "\n" survey_stdout "${survey_stdout}")
string(REPLACE "\r\n" "\n" survey_stderr "${survey_stderr}")
string(REPLACE "\r" "\n" survey_stderr "${survey_stderr}")

if(NOT survey_result STREQUAL "1")
    message(FATAL_ERROR
        "survey invalid-root exit mismatch: expected 1, got '${survey_result}'\n"
        "stdout=[${survey_stdout}]\nstderr=[${survey_stderr}]")
endif()
if(NOT survey_stdout STREQUAL "")
    message(FATAL_ERROR
        "survey invalid-root stdout must be empty: [${survey_stdout}]")
endif()

set(expected_stderr
    "front-end screen survey: game data root unavailable [mount-failed]\n")
if(NOT survey_stderr STREQUAL expected_stderr)
    message(FATAL_ERROR
        "survey invalid-root stderr mismatch\n"
        "expected=[${expected_stderr}]\nactual=[${survey_stderr}]")
endif()

string(FIND "${survey_stdout}" "${OMEGA_SURVEY_PRIVATE_ROOT}" stdout_path_index)
string(FIND "${survey_stderr}" "${OMEGA_SURVEY_PRIVATE_ROOT}" stderr_path_index)
if(NOT stdout_path_index EQUAL -1 OR NOT stderr_path_index EQUAL -1)
    message(FATAL_ERROR "survey output exposed the synthetic owner path")
endif()
if(survey_stdout MATCHES "private-sentinel" OR
   survey_stderr MATCHES "private-sentinel")
    message(FATAL_ERROR "survey output exposed the synthetic private sentinel")
endif()
