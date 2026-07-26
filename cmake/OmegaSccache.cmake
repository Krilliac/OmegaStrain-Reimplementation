include_guard(GLOBAL)

function(_omega_normalize_sccache_partition_root input_root output_root)
    string(REPLACE "\\" "/" normalized_root "${input_root}")
    cmake_path(NORMAL_PATH normalized_root OUTPUT_VARIABLE normalized_root)
    cmake_path(GET normalized_root ROOT_PATH normalized_root_prefix)
    if(NOT normalized_root STREQUAL normalized_root_prefix)
        string(REGEX REPLACE "/+$" "" normalized_root "${normalized_root}")
    endif()
    if(WIN32)
        string(TOLOWER "${normalized_root}" normalized_root)
    endif()
    set("${output_root}" "${normalized_root}" PARENT_SCOPE)
endfunction()

function(_omega_compute_sccache_partition_token
        source_root binary_root caller_buster output_token)
    _omega_normalize_sccache_partition_root(
        "${source_root}" normalized_source_root)
    _omega_normalize_sccache_partition_root(
        "${binary_root}" normalized_binary_root)

    set(partition_version "openomega-sccache-worktree-partition-v1")
    string(LENGTH "${partition_version}" partition_version_length)
    string(LENGTH "${normalized_source_root}" source_root_length)
    string(LENGTH "${normalized_binary_root}" binary_root_length)
    string(LENGTH "${caller_buster}" caller_buster_length)
    string(CONCAT partition_material
        "version:${partition_version_length}:${partition_version}\n"
        "source:${source_root_length}:${normalized_source_root}\n"
        "binary:${binary_root_length}:${normalized_binary_root}\n"
        "caller:${caller_buster_length}:${caller_buster}")
    string(SHA256 partition_hash "${partition_material}")

    set("${output_token}" "omega-${partition_hash}" PARENT_SCOPE)
endfunction()

function(omega_partition_sccache_launcher
        launcher_variable source_root binary_root caller_buster
        output_launcher output_partitioned output_token)
    if(NOT DEFINED "${launcher_variable}")
        set("${output_launcher}" "" PARENT_SCOPE)
        set("${output_partitioned}" FALSE PARENT_SCOPE)
        set("${output_token}" "" PARENT_SCOPE)
        return()
    endif()

    set(original_launcher "${${launcher_variable}}")
    if(NOT original_launcher)
        set("${output_launcher}" "${original_launcher}" PARENT_SCOPE)
        set("${output_partitioned}" FALSE PARENT_SCOPE)
        set("${output_token}" "" PARENT_SCOPE)
        return()
    endif()

    list(GET original_launcher 0 launcher_command)
    string(REPLACE "\\" "/" launcher_command_for_name "${launcher_command}")
    get_filename_component(launcher_name "${launcher_command_for_name}" NAME)
    string(TOLOWER "${launcher_name}" launcher_name)
    if(NOT launcher_name MATCHES "^sccache(\\.exe)?$")
        set("${output_launcher}" "${original_launcher}" PARENT_SCOPE)
        set("${output_partitioned}" FALSE PARENT_SCOPE)
        set("${output_token}" "" PARENT_SCOPE)
        return()
    endif()

    _omega_compute_sccache_partition_token(
        "${source_root}" "${binary_root}" "${caller_buster}" partition_token)
    set(partitioned_launcher
        "${CMAKE_COMMAND}"
        -E
        env
        "SCCACHE_C_CUSTOM_CACHE_BUSTER=${partition_token}"
    )
    foreach(launcher_element IN LISTS original_launcher)
        string(REPLACE ";" "\\\\;" escaped_launcher_element
            "${launcher_element}")
        list(APPEND partitioned_launcher "${escaped_launcher_element}")
    endforeach()

    set("${output_launcher}" "${partitioned_launcher}" PARENT_SCOPE)
    set("${output_partitioned}" TRUE PARENT_SCOPE)
    set("${output_token}" "${partition_token}" PARENT_SCOPE)
endfunction()

function(omega_enable_sccache_worktree_partition)
    if(NOT WIN32 OR NOT CMAKE_GENERATOR MATCHES "^Ninja")
        return()
    endif()

    foreach(language IN ITEMS C CXX)
        set(launcher_variable "CMAKE_${language}_COMPILER_LAUNCHER")
        if(NOT DEFINED "${launcher_variable}")
            continue()
        endif()

        omega_partition_sccache_launcher(
            "${launcher_variable}"
            "${CMAKE_SOURCE_DIR}"
            "${CMAKE_BINARY_DIR}"
            "$ENV{SCCACHE_C_CUSTOM_CACHE_BUSTER}"
            partitioned_launcher
            launcher_was_partitioned
            partition_token
        )
        if(launcher_was_partitioned)
            set("${launcher_variable}" "${partitioned_launcher}" PARENT_SCOPE)
        endif()
    endforeach()
endfunction()
