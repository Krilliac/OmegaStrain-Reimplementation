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
    omega_enable_sccache_worktree_partition()
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

    set(CMAKE_GENERATOR "Visual Studio 17 2022")
    set(CMAKE_CXX_COMPILER_LAUNCHER sccache)
    omega_enable_sccache_worktree_partition()
    require_equal("${CMAKE_CXX_COMPILER_LAUNCHER}" "sccache"
        "non-Ninja generator unexpectedly received sccache partitioning")
    unset(ENV{SCCACHE_C_CUSTOM_CACHE_BUSTER})
endif()
