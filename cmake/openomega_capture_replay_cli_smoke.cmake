cmake_minimum_required(VERSION 3.28)

if(NOT DEFINED OPENOMEGA_EXECUTABLE OR OPENOMEGA_EXECUTABLE STREQUAL "")
    message(FATAL_ERROR "OPENOMEGA_EXECUTABLE is required")
endif()
if(NOT EXISTS "${OPENOMEGA_EXECUTABLE}")
    message(FATAL_ERROR "OPENOMEGA_EXECUTABLE does not name an existing file")
endif()

execute_process(
    COMMAND "${OPENOMEGA_EXECUTABLE}" --frames=2 --capture-run --replay-capture
    RESULT_VARIABLE openomega_result
    OUTPUT_VARIABLE openomega_stdout
    ERROR_VARIABLE openomega_stderr
)
string(REPLACE "\r\n" "\n" openomega_stdout "${openomega_stdout}")
string(REPLACE "\r" "\n" openomega_stdout "${openomega_stdout}")
string(REPLACE "\r\n" "\n" openomega_stderr "${openomega_stderr}")
string(REPLACE "\r" "\n" openomega_stderr "${openomega_stderr}")

if(NOT openomega_result STREQUAL "0")
    message(FATAL_ERROR
        "capture-replay openomega run failed with '${openomega_result}'\n"
        "stdout=[${openomega_stdout}]\nstderr=[${openomega_stderr}]")
endif()

string(CONCAT expected_stdout_pattern
    "^OpenOmega native persistence: profiles=0\n"
    "OpenOmega native shell: GPU driver=[A-Za-z0-9_.-]+ audio_driver=dummy "
    "audio_format=f32/[0-9]+Hz/[0-9]+ch\n"
    "OpenOmega native shell: rendered_frames=2 "
    "planned_simulation_steps=[0-9]+ executed_simulation_steps=[0-9]+ "
    "input_frames=2 audio_callbacks=[0-9]+ audio_frames_provided=[0-9]+\n"
    "OpenOmega run capture: requested_frames=2 "
    "completion=frame-limit-reached trace_pair=present "
    "input_trace_frames=2 scheduler_elapsed_trace_frames=2 terminal=absent\n"
    "OpenOmega run capture scheduler: before_planned_steps=[0-9]+ "
    "after_planned_steps=[0-9]+ before_remainder_ns=[0-9]+ "
    "after_remainder_ns=[0-9]+ before_dropped_time_ns=[0-9]+ "
    "after_dropped_time_ns=[0-9]+\n"
    "OpenOmega fresh replay: replayed_frames=2 planned_simulation_steps=[0-9]+ "
    "completed_simulation_steps=[0-9]+ clamped_frames=[0-9]+ "
    "dropped_frames=[0-9]+ completion=complete\n$"
)
if(NOT openomega_stdout MATCHES "${expected_stdout_pattern}")
    message(FATAL_ERROR
        "capture-replay stdout contract mismatch: [${openomega_stdout}]")
endif()

string(CONCAT failure_pattern
    "runtime capture|runtime capture replay|runtime loop ended before|"
    "did not produce a complete consistent run")
if(openomega_stderr MATCHES "${failure_pattern}")
    message(FATAL_ERROR
        "capture-replay run reported a runtime failure: [${openomega_stderr}]")
endif()
