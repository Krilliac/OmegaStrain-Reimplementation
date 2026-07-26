cmake_minimum_required(VERSION 3.28)

include("${CMAKE_CURRENT_LIST_DIR}/OmegaSccache.cmake")

function(require_true value description)
    if(NOT value)
        message(FATAL_ERROR "${description}")
    endif()
endfunction()

function(require_false value description)
    if(value)
        message(FATAL_ERROR "${description}")
    endif()
endfunction()

function(require_equal actual expected description)
    if(NOT "${actual}" STREQUAL "${expected}")
        message(FATAL_ERROR
            "${description}\n"
            "expected: [${expected}]\n"
            "actual:   [${actual}]")
    endif()
endfunction()

function(require_not_equal actual unexpected description)
    if("${actual}" STREQUAL "${unexpected}")
        message(FATAL_ERROR "${description}: [${actual}]")
    endif()
endfunction()

function(require_partition_prefix launcher token description)
    set(expected_prefix
        "${CMAKE_COMMAND}"
        -E
        env
        "SCCACHE_C_CUSTOM_CACHE_BUSTER=${token}"
    )
    list(SUBLIST launcher 0 4 actual_prefix)
    require_equal("${actual_prefix}" "${expected_prefix}" "${description}")
endfunction()

function(validate_generated_ninja_partition
        generator build_directory expected_compile_definition)
    if(NOT generator MATCHES "^Ninja")
        return()
    endif()
    if(NOT IS_DIRECTORY "${build_directory}")
        message(FATAL_ERROR
            "configured Ninja build directory is unavailable")
    endif()

    file(GLOB ninja_files LIST_DIRECTORIES FALSE
        "${build_directory}/build*.ninja"
        "${build_directory}/CMakeFiles/impl-*.ninja")
    list(SORT ninja_files)
    if(NOT ninja_files)
        message(FATAL_ERROR "configured Ninja graph is unavailable")
    endif()

    set(expected_launcher_token "")
    if(expected_compile_definition)
        string(REGEX REPLACE
            "^OPENOMEGA_SCCACHE_PARTITION_" ""
            expected_partition_suffix "${expected_compile_definition}")
        string(REGEX REPLACE
            "=1$" "" expected_partition_hash "${expected_partition_suffix}")
        string(LENGTH "${expected_partition_hash}" expected_hash_length)
        if(NOT expected_partition_hash MATCHES "^[0-9a-f]+$"
                OR NOT expected_hash_length EQUAL 64
                OR NOT expected_compile_definition STREQUAL
                    "OPENOMEGA_SCCACHE_PARTITION_${expected_partition_hash}=1")
            message(FATAL_ERROR
                "configured compile definition is not sanitized")
        endif()
        set(expected_launcher_token
            "SCCACHE_C_CUSTOM_CACHE_BUSTER=omega-${expected_partition_hash}")
    endif()

    set(partition_definition_count 0)
    set(partition_launcher_count 0)
    set(omega_core_definition_found FALSE)
    set(omega_core_tests_definition_found FALSE)
    foreach(ninja_file IN LISTS ninja_files)
        file(SIZE "${ninja_file}" ninja_file_size)
        if(ninja_file_size GREATER 67108864)
            message(FATAL_ERROR "configured Ninja graph exceeds contract limit")
        endif()
        file(STRINGS "${ninja_file}" ninja_lines)
        set(current_edge "")
        foreach(ninja_line IN LISTS ninja_lines)
            if(ninja_line MATCHES "^build ")
                set(current_edge "${ninja_line}")
                continue()
            endif()

            string(FIND "${ninja_line}"
                "OPENOMEGA_SCCACHE_PARTITION_" definition_position)
            if(NOT definition_position EQUAL -1)
                if(NOT expected_compile_definition)
                    message(FATAL_ERROR
                        "non-sccache Ninja graph contains a partition definition")
                endif()
                string(FIND "${ninja_line}"
                    "${expected_compile_definition}" expected_position)
                if(expected_position EQUAL -1)
                    message(FATAL_ERROR
                        "Ninja graph contains the wrong partition definition")
                endif()
                string(REPLACE "${expected_compile_definition}" ""
                    remaining_line "${ninja_line}")
                string(FIND "${remaining_line}"
                    "OPENOMEGA_SCCACHE_PARTITION_" extra_definition_position)
                if(NOT extra_definition_position EQUAL -1)
                    message(FATAL_ERROR
                        "Ninja graph contains multiple partition definitions")
                endif()
                math(EXPR partition_definition_count
                    "${partition_definition_count} + 1")
                if(current_edge MATCHES
                        "CMakeFiles[\\\\/]omega_core\\.dir[\\\\/]")
                    set(omega_core_definition_found TRUE)
                endif()
                if(current_edge MATCHES
                        "CMakeFiles[\\\\/]omega_core_tests\\.dir[\\\\/]")
                    set(omega_core_tests_definition_found TRUE)
                endif()
            endif()

            string(FIND "${ninja_line}"
                "SCCACHE_C_CUSTOM_CACHE_BUSTER=omega-"
                launcher_position)
            if(NOT launcher_position EQUAL -1)
                if(NOT expected_launcher_token)
                    message(FATAL_ERROR
                        "non-sccache Ninja graph contains a partition launcher")
                endif()
                string(FIND "${ninja_line}"
                    "${expected_launcher_token}" expected_launcher_position)
                if(expected_launcher_position EQUAL -1)
                    message(FATAL_ERROR
                        "Ninja graph contains the wrong partition launcher")
                endif()
                math(EXPR partition_launcher_count
                    "${partition_launcher_count} + 1")
            endif()
        endforeach()
    endforeach()

    if(expected_compile_definition)
        if(partition_definition_count LESS 2)
            message(FATAL_ERROR
                "Ninja object edges omit the partition definition")
        endif()
        if(NOT omega_core_definition_found)
            message(FATAL_ERROR
                "early omega_core object edges omit the partition definition")
        endif()
        if(NOT omega_core_tests_definition_found)
            message(FATAL_ERROR
                "late omega_core_tests object edges omit the partition definition")
        endif()
        if(partition_launcher_count LESS 1)
            message(FATAL_ERROR
                "Ninja object edges omit the partition launcher")
        endif()
    endif()
endfunction()

set(bare_launcher sccache)
omega_partition_sccache_launcher(
    bare_launcher
    "C:/OpenOmega/source"
    "C:/OpenOmega/build"
    "caller-buster"
    bare_result
    bare_partitioned
    bare_token
)
require_true("${bare_partitioned}" "bare sccache launcher was not recognized")
require_partition_prefix(
    "${bare_result}" "${bare_token}"
    "partition token is absent from the generated launcher command")
list(SUBLIST bare_result 4 -1 bare_suffix)
require_equal("${bare_suffix}" "${bare_launcher}"
    "bare sccache launcher changed after its partition prefix")
string(REGEX REPLACE "^omega-" "" bare_hash "${bare_token}")
omega_sccache_partition_compile_definition(
    "${bare_token}" bare_compile_definition)
require_equal(
    "${bare_compile_definition}"
    "OPENOMEGA_SCCACHE_PARTITION_${bare_hash}=1"
    "partition token did not produce the exact sanitized compile definition")

omega_partition_sccache_launcher(
    bare_launcher
    "C:/OpenOmega/source"
    "C:/OpenOmega/build"
    "caller-buster"
    repeated_result
    repeated_partitioned
    repeated_token
)
require_true("${repeated_partitioned}"
    "repeated bare sccache launcher was not recognized")
require_equal("${repeated_token}" "${bare_token}"
    "identical partition inputs produced different tokens")
require_equal("${repeated_result}" "${bare_result}"
    "identical partition inputs produced different launchers")
omega_sccache_partition_compile_definition(
    "${repeated_token}" repeated_compile_definition)
require_equal("${repeated_compile_definition}" "${bare_compile_definition}"
    "identical partition inputs produced different compile definitions")

set(normalized_launcher sccache.exe)
omega_partition_sccache_launcher(
    normalized_launcher
    "C:\\OpenOmega\\source\\."
    "C:\\OpenOmega\\build\\nested\\.."
    "caller-buster"
    normalized_result
    normalized_partitioned
    normalized_token
)
require_true("${normalized_partitioned}"
    "bare sccache.exe launcher was not recognized")
require_equal("${normalized_token}" "${bare_token}"
    "equivalent normalized roots produced different tokens")

omega_partition_sccache_launcher(
    bare_launcher
    "C:/OpenOmega/other-source"
    "C:/OpenOmega/build"
    "caller-buster"
    source_changed_result
    source_changed_partitioned
    source_changed_token
)
require_true("${source_changed_partitioned}"
    "source-change fixture was not partitioned")
require_not_equal("${source_changed_token}" "${bare_token}"
    "source-root change did not alter the partition token")
omega_sccache_partition_compile_definition(
    "${source_changed_token}" source_changed_compile_definition)
require_not_equal(
    "${source_changed_compile_definition}" "${bare_compile_definition}"
    "source-root change did not alter the compile definition")

omega_partition_sccache_launcher(
    bare_launcher
    "C:/OpenOmega/source"
    "C:/OpenOmega/other-build"
    "caller-buster"
    binary_changed_result
    binary_changed_partitioned
    binary_changed_token
)
require_true("${binary_changed_partitioned}"
    "binary-change fixture was not partitioned")
require_not_equal("${binary_changed_token}" "${bare_token}"
    "binary-root change did not alter the partition token")
omega_sccache_partition_compile_definition(
    "${binary_changed_token}" binary_changed_compile_definition)
require_not_equal(
    "${binary_changed_compile_definition}" "${bare_compile_definition}"
    "binary-root change did not alter the compile definition")

set(secret_caller_buster "private-caller-buster-value")
omega_partition_sccache_launcher(
    bare_launcher
    "C:/OpenOmega/source"
    "C:/OpenOmega/build"
    "${secret_caller_buster}"
    caller_changed_result
    caller_changed_partitioned
    caller_changed_token
)
require_true("${caller_changed_partitioned}"
    "caller-change fixture was not partitioned")
require_not_equal("${caller_changed_token}" "${bare_token}"
    "caller buster change did not alter the partition token")
string(FIND "${caller_changed_result}" "${secret_caller_buster}"
    caller_buster_position)
require_equal("${caller_buster_position}" "-1"
    "generated launcher exposed the caller buster")
omega_sccache_partition_compile_definition(
    "${caller_changed_token}" caller_changed_compile_definition)
require_not_equal(
    "${caller_changed_compile_definition}" "${bare_compile_definition}"
    "caller buster change did not alter the compile definition")
string(FIND "${caller_changed_compile_definition}" "${secret_caller_buster}"
    compile_caller_buster_position)
require_equal("${compile_caller_buster_position}" "-1"
    "compile definition exposed the caller buster")

set(full_path_launcher
    "C:/Program Files/sccache/SCCACHE.EXE"
    "--config"
    "C:/Config With Spaces/sccache config.toml"
    "literal=left\\;right"
)
omega_partition_sccache_launcher(
    full_path_launcher
    "C:/Source Root/OpenOmega"
    "C:/Build Root/OpenOmega"
    ""
    full_path_result
    full_path_partitioned
    full_path_token
)
require_true("${full_path_partitioned}"
    "full-path sccache.exe launcher was not recognized")
require_partition_prefix(
    "${full_path_result}" "${full_path_token}"
    "full-path launcher has an invalid partition prefix")
list(SUBLIST full_path_result 4 -1 full_path_suffix)
require_equal("${full_path_suffix}" "${full_path_launcher}"
    "launcher list elements or quoting changed after partitioning")

set(non_sccache_launcher
    "C:/Program Files/ccache/ccache.exe"
    "--config"
    "C:/Config With Spaces/ccache.conf"
)
omega_partition_sccache_launcher(
    non_sccache_launcher
    "C:/OpenOmega/source"
    "C:/OpenOmega/build"
    "caller-buster"
    non_sccache_result
    non_sccache_partitioned
    non_sccache_token
)
require_false("${non_sccache_partitioned}"
    "non-sccache launcher was unexpectedly partitioned")
require_equal("${non_sccache_result}" "${non_sccache_launcher}"
    "non-sccache launcher changed")
require_equal("${non_sccache_token}" ""
    "non-sccache launcher unexpectedly received a partition token")

if(WIN32)
    set(CMAKE_GENERATOR "Ninja Multi-Config")
    set(CMAKE_SOURCE_DIR "C:/Contract Source/OpenOmega")
    set(CMAKE_BINARY_DIR "C:/Contract Build/OpenOmega")
    set(CMAKE_C_COMPILER_LAUNCHER "ccache")
    set(CMAKE_CXX_COMPILER_LAUNCHER "sccache;--start-server")
    set(ENV{SCCACHE_C_CUSTOM_CACHE_BUSTER} "contract-caller-buster")
    omega_enable_sccache_worktree_partition(configured_compile_definition)
    list(GET CMAKE_CXX_COMPILER_LAUNCHER 3 configured_buster)
    set(configured_buster_prefix "SCCACHE_C_CUSTOM_CACHE_BUSTER=omega-")
    string(REGEX REPLACE "^${configured_buster_prefix}" ""
        configured_hash "${configured_buster}")
    string(LENGTH "${configured_hash}" configured_hash_length)
    if(NOT configured_hash MATCHES "^[0-9a-f]+$"
            OR NOT configured_hash_length EQUAL 64)
        message(FATAL_ERROR
            "Windows Ninja integration did not emit a hashed partition token")
    endif()
    list(SUBLIST CMAKE_CXX_COMPILER_LAUNCHER 4 -1 configured_suffix)
    require_equal("${configured_suffix}" "sccache;--start-server"
        "Windows Ninja integration changed the sccache launcher suffix")
    require_equal("${CMAKE_C_COMPILER_LAUNCHER}" "ccache"
        "Windows Ninja integration changed a non-sccache launcher")
    set(configured_token "omega-${configured_hash}")
    omega_sccache_partition_compile_definition(
        "${configured_token}" expected_configured_compile_definition)
    require_equal(
        "${configured_compile_definition}"
        "${expected_configured_compile_definition}"
        "Windows Ninja integration returned the wrong compile definition")

    set(CMAKE_C_COMPILER_LAUNCHER "ccache;--server")
    set(CMAKE_CXX_COMPILER_LAUNCHER
        "C:/Program Files/clang-tidy/clang-tidy.exe;--quiet")
    omega_enable_sccache_worktree_partition(
        all_non_sccache_compile_definition)
    require_equal("${CMAKE_C_COMPILER_LAUNCHER}" "ccache;--server"
        "all-non-sccache integration changed the C launcher")
    require_equal(
        "${CMAKE_CXX_COMPILER_LAUNCHER}"
        "C:/Program Files/clang-tidy/clang-tidy.exe;--quiet"
        "all-non-sccache integration changed the CXX launcher")
    require_equal("${all_non_sccache_compile_definition}" ""
        "all-non-sccache integration returned a compile definition")

    set(CMAKE_GENERATOR "Visual Studio 17 2022")
    set(CMAKE_CXX_COMPILER_LAUNCHER sccache)
    omega_enable_sccache_worktree_partition(non_ninja_compile_definition)
    require_equal("${CMAKE_CXX_COMPILER_LAUNCHER}" "sccache"
        "non-Ninja generator unexpectedly received sccache partitioning")
    require_equal("${non_ninja_compile_definition}" ""
        "non-Ninja generator unexpectedly received a compile definition")
    unset(ENV{SCCACHE_C_CUSTOM_CACHE_BUSTER})
endif()

if(DEFINED OMEGA_SCCACHE_CONTRACT_GENERATOR
        AND DEFINED OMEGA_SCCACHE_CONTRACT_BUILD_DIRECTORY
        AND DEFINED OMEGA_SCCACHE_CONTRACT_EXPECTED_COMPILE_DEFINITION)
    validate_generated_ninja_partition(
        "${OMEGA_SCCACHE_CONTRACT_GENERATOR}"
        "${OMEGA_SCCACHE_CONTRACT_BUILD_DIRECTORY}"
        "${OMEGA_SCCACHE_CONTRACT_EXPECTED_COMPILE_DEFINITION}")
endif()
