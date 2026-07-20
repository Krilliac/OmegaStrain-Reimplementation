cmake_minimum_required(VERSION 3.28)

# CPack evaluates this file once per selected generator. Keep this mode side-effect free so every
# unsupported generator/configuration fails before any install rule or archive write can run.
if(DEFINED CPACK_GENERATOR AND NOT DEFINED OPENOMEGA_PACKAGE_CONTRACT_MODE)
    if(NOT CPACK_GENERATOR STREQUAL "ZIP")
        message(FATAL_ERROR
            "OpenOmega portable package contract requires CPACK_GENERATOR=ZIP"
        )
    endif()
    if(NOT DEFINED CPACK_BUILD_CONFIG OR NOT CPACK_BUILD_CONFIG STREQUAL "Release")
        message(FATAL_ERROR
            "OpenOmega portable package contract requires CPACK_BUILD_CONFIG=Release"
        )
    endif()
    return()
endif()

if(NOT DEFINED OPENOMEGA_PACKAGE_CONTRACT_MODE OR
   NOT OPENOMEGA_PACKAGE_CONTRACT_MODE STREQUAL "DRIVER")
    message(FATAL_ERROR "OPENOMEGA_PACKAGE_CONTRACT_MODE=DRIVER is required")
endif()

set(required_path_variables
    OPENOMEGA_CPACK_COMMAND
    OPENOMEGA_CPACK_CONFIG
    OPENOMEGA_BUILD_DIRECTORY
    OPENOMEGA_SOURCE_DIRECTORY
    OPENOMEGA_PACKAGE_DIRECTORY
    OPENOMEGA_PACKAGE_STAGE_DIRECTORY
    OPENOMEGA_PACKAGE_EXTRACT_DIRECTORY
    OPENOMEGA_PACKAGE_UNRELATED_DIRECTORY
    OPENOMEGA_PACKAGE_PROFILE_DIRECTORY
    OPENOMEGA_DUMP_TOOL
    OPENOMEGA_COMSPEC
    OPENOMEGA_POWERSHELL
)
foreach(variable_name IN LISTS required_path_variables)
    if(NOT DEFINED ${variable_name} OR "${${variable_name}}" STREQUAL "")
        message(FATAL_ERROR "${variable_name} is required")
    endif()
    if(NOT IS_ABSOLUTE "${${variable_name}}")
        message(FATAL_ERROR "${variable_name} must be absolute")
    endif()
endforeach()

if(NOT EXISTS "${OPENOMEGA_BUILD_DIRECTORY}" OR
   NOT IS_DIRECTORY "${OPENOMEGA_BUILD_DIRECTORY}" OR
   NOT EXISTS "${OPENOMEGA_SOURCE_DIRECTORY}" OR
   NOT IS_DIRECTORY "${OPENOMEGA_SOURCE_DIRECTORY}")
    message(FATAL_ERROR "OpenOmega build and source directories must exist")
endif()

foreach(existing_file_variable IN ITEMS
        OPENOMEGA_CPACK_COMMAND
        OPENOMEGA_CPACK_CONFIG
        OPENOMEGA_DUMP_TOOL
        OPENOMEGA_COMSPEC
        OPENOMEGA_POWERSHELL)
    if(NOT EXISTS "${${existing_file_variable}}" OR
       IS_DIRECTORY "${${existing_file_variable}}")
        message(FATAL_ERROR "${existing_file_variable} must name an existing file")
    endif()
endforeach()

if(NOT DEFINED OPENOMEGA_PACKAGE_BASENAME OR
   NOT OPENOMEGA_PACKAGE_BASENAME STREQUAL "OpenOmega-0.1.0-windows-x86_64")
    message(FATAL_ERROR "unexpected OpenOmega portable package basename")
endif()
if(NOT DEFINED OPENOMEGA_CONFIGURED_ARCHITECTURE OR
   NOT OPENOMEGA_CONFIGURED_ARCHITECTURE STREQUAL "x64")
    message(FATAL_ERROR "OpenOmega portable package must be configured for x64")
endif()
if(NOT DEFINED OPENOMEGA_DUMP_TOOL_MODE OR
   NOT OPENOMEGA_DUMP_TOOL_MODE MATCHES "^(DUMPBIN|LINKER)$")
    message(FATAL_ERROR "OPENOMEGA_DUMP_TOOL_MODE must be DUMPBIN or LINKER")
endif()

get_filename_component(comspec_name "${OPENOMEGA_COMSPEC}" NAME)
string(TOLOWER "${comspec_name}" comspec_name)
if(NOT comspec_name STREQUAL "cmd.exe")
    message(FATAL_ERROR "OPENOMEGA_COMSPEC must name cmd.exe")
endif()

cmake_path(NORMAL_PATH OPENOMEGA_BUILD_DIRECTORY OUTPUT_VARIABLE normalized_build_directory)
set(OPENOMEGA_BUILD_DIRECTORY "${normalized_build_directory}")
set(managed_directory_variables
    OPENOMEGA_PACKAGE_DIRECTORY
    OPENOMEGA_PACKAGE_STAGE_DIRECTORY
    OPENOMEGA_PACKAGE_EXTRACT_DIRECTORY
    OPENOMEGA_PACKAGE_UNRELATED_DIRECTORY
    OPENOMEGA_PACKAGE_PROFILE_DIRECTORY
)
set(expected_managed_directories
    "${OPENOMEGA_BUILD_DIRECTORY}/package"
    "${OPENOMEGA_BUILD_DIRECTORY}/package-contract/stage"
    "${OPENOMEGA_BUILD_DIRECTORY}/package-contract/extract"
    "${OPENOMEGA_BUILD_DIRECTORY}/package-contract/unrelated"
    "${OPENOMEGA_BUILD_DIRECTORY}/package-contract/profile"
)
list(LENGTH managed_directory_variables managed_directory_count)
math(EXPR managed_directory_last "${managed_directory_count} - 1")
foreach(managed_index RANGE 0 ${managed_directory_last})
    list(GET managed_directory_variables ${managed_index} managed_variable)
    list(GET expected_managed_directories ${managed_index} expected_directory)
    set(candidate_directory "${${managed_variable}}")
    cmake_path(NORMAL_PATH candidate_directory OUTPUT_VARIABLE candidate_directory)
    cmake_path(NORMAL_PATH expected_directory OUTPUT_VARIABLE expected_directory)
    string(TOLOWER "${candidate_directory}" candidate_directory_compare)
    string(TOLOWER "${expected_directory}" expected_directory_compare)
    if(NOT candidate_directory_compare STREQUAL expected_directory_compare)
        message(FATAL_ERROR
            "${managed_variable} must be the dedicated path ${expected_directory}"
        )
    endif()
    set(${managed_variable} "${candidate_directory}")
endforeach()

set(package_zip
    "${OPENOMEGA_PACKAGE_DIRECTORY}/${OPENOMEGA_PACKAGE_BASENAME}.zip")
set(package_checksum "${package_zip}.sha256")
set(package_tgz
    "${OPENOMEGA_PACKAGE_DIRECTORY}/${OPENOMEGA_PACKAGE_BASENAME}.tar.gz")
set(package_tgz_checksum "${package_tgz}.sha256")
set(cpack_scratch "${OPENOMEGA_PACKAGE_DIRECTORY}/_CPack_Packages")

function(normalize_process_output output_variable input_value)
    string(REPLACE "\r\n" "\n" normalized "${input_value}")
    string(REPLACE "\r" "\n" normalized "${normalized}")
    set(${output_variable} "${normalized}" PARENT_SCOPE)
endfunction()

function(directory_manifest root output_variable)
    set(excluded_entries ${ARGN})
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
        file(TO_CMAKE_PATH "${relative_path}" relative_path)
        if(relative_path IN_LIST excluded_entries)
            continue()
        endif()
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

function(require_windows_native_save_genesis case_name profile_root)
    set(native_save_directory "${profile_root}/OpenOmega/native-save")
    if(NOT EXISTS "${native_save_directory}" OR
       NOT IS_DIRECTORY "${native_save_directory}" OR
       IS_SYMLINK "${native_save_directory}")
        message(FATAL_ERROR "${case_name}: native-save directory is invalid")
    endif()

    file(GLOB_RECURSE profile_entries
        LIST_DIRECTORIES TRUE
        RELATIVE "${profile_root}"
        "${profile_root}/*"
    )
    list(SORT profile_entries)
    set(expected_profile_entries
        "OpenOmega"
        "OpenOmega/native-save"
        "OpenOmega/native-save/openomega-save-a.oodb"
        "OpenOmega/native-save/openomega-save-b.oodb"
        "OpenOmega/native-save/openomega-save.lock"
    )
    list(SORT expected_profile_entries)
    if(NOT profile_entries STREQUAL expected_profile_entries)
        message(FATAL_ERROR
            "${case_name}: synthetic profile entries differ\n"
            "expected=[${expected_profile_entries}]\n"
            "actual=[${profile_entries}]"
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

function(clean_cpack_outputs)
    file(REMOVE
        "${package_zip}"
        "${package_checksum}"
        "${package_tgz}"
        "${package_tgz_checksum}"
    )
    file(REMOVE_RECURSE "${cpack_scratch}")
endfunction()

function(require_known_outputs_absent case_name)
    foreach(output_path IN ITEMS
            "${package_zip}"
            "${package_checksum}"
            "${package_tgz}"
            "${package_tgz_checksum}"
            "${cpack_scratch}")
        if(EXISTS "${output_path}" OR IS_SYMLINK "${output_path}")
            message(FATAL_ERROR "${case_name}: stale package output remains: ${output_path}")
        endif()
    endforeach()
endfunction()

function(run_cpack_failure case_name generator configuration expected_guard)
    clean_cpack_outputs()
    require_known_outputs_absent("${case_name} precondition")
    directory_manifest("${OPENOMEGA_PACKAGE_DIRECTORY}" package_before)
    directory_manifest("${OPENOMEGA_PACKAGE_UNRELATED_DIRECTORY}" unrelated_before)
    directory_manifest("${OPENOMEGA_PACKAGE_PROFILE_DIRECTORY}" profile_before_case)

    set(command
        "${OPENOMEGA_CPACK_COMMAND}"
        --config "${OPENOMEGA_CPACK_CONFIG}"
        -G "${generator}"
    )
    if(NOT configuration STREQUAL "<none>")
        list(APPEND command -C "${configuration}")
    endif()

    execute_process(
        COMMAND ${command}
        WORKING_DIRECTORY "${OPENOMEGA_PACKAGE_UNRELATED_DIRECTORY}"
        RESULT_VARIABLE result
        OUTPUT_VARIABLE stdout
        ERROR_VARIABLE stderr
        TIMEOUT 30
    )
    if(result STREQUAL "0")
        message(FATAL_ERROR "${case_name}: unsupported CPack invocation succeeded")
    endif()
    string(CONCAT combined_output "${stdout}\n${stderr}")
    if(NOT combined_output MATCHES "${expected_guard}")
        message(FATAL_ERROR
            "${case_name}: failure did not come from the expected contract gate\n"
            "stdout=[${stdout}]\nstderr=[${stderr}]"
        )
    endif()

    require_known_outputs_absent("${case_name} postcondition")
    directory_manifest("${OPENOMEGA_PACKAGE_DIRECTORY}" package_after)
    directory_manifest("${OPENOMEGA_PACKAGE_UNRELATED_DIRECTORY}" unrelated_after)
    directory_manifest("${OPENOMEGA_PACKAGE_PROFILE_DIRECTORY}" profile_after_case)
    require_same_manifest("${case_name} package directory" "${package_before}" "${package_after}")
    require_same_manifest("${case_name} unrelated directory"
        "${unrelated_before}" "${unrelated_after}")
    require_same_manifest("${case_name} synthetic profile"
        "${profile_before_case}" "${profile_after_case}")
endfunction()

function(run_install_failure configuration)
    set(install_prefix "${OPENOMEGA_PACKAGE_STAGE_DIRECTORY}/${configuration}")
    file(REMOVE_RECURSE "${install_prefix}")
    file(MAKE_DIRECTORY "${install_prefix}")
    directory_manifest("${install_prefix}" before)
    directory_manifest("${OPENOMEGA_PACKAGE_PROFILE_DIRECTORY}" profile_before_case)
    execute_process(
        COMMAND "${CMAKE_COMMAND}"
            --install "${OPENOMEGA_BUILD_DIRECTORY}"
            --config "${configuration}"
            --prefix "${install_prefix}"
        WORKING_DIRECTORY "${OPENOMEGA_PACKAGE_UNRELATED_DIRECTORY}"
        RESULT_VARIABLE result
        OUTPUT_VARIABLE stdout
        ERROR_VARIABLE stderr
        TIMEOUT 30
    )
    if(result STREQUAL "0")
        message(FATAL_ERROR
            "direct install ${configuration}: unsupported configuration succeeded"
        )
    endif()
    string(CONCAT combined_output "${stdout}\n${stderr}")
    if(NOT combined_output MATCHES
       "OpenOmega Windows portable installation requires Release configuration")
        message(FATAL_ERROR
            "direct install ${configuration}: failure did not come from the Release gate\n"
            "stdout=[${stdout}]\nstderr=[${stderr}]"
        )
    endif()
    directory_manifest("${install_prefix}" after)
    directory_manifest("${OPENOMEGA_PACKAGE_PROFILE_DIRECTORY}" profile_after_case)
    require_same_manifest("direct install ${configuration}" "${before}" "${after}")
    require_same_manifest("direct install ${configuration} synthetic profile"
        "${profile_before_case}" "${profile_after_case}")
endfunction()

function(read_uint16_le hex_value byte_offset output_variable)
    math(EXPR character_offset "${byte_offset} * 2")
    math(EXPR required_end "${character_offset} + 4")
    string(LENGTH "${hex_value}" hex_length)
    if(required_end GREATER hex_length)
        message(FATAL_ERROR "truncated PE field at byte offset ${byte_offset}")
    endif()
    string(SUBSTRING "${hex_value}" ${character_offset} 2 byte0)
    math(EXPR character_offset1 "${character_offset} + 2")
    string(SUBSTRING "${hex_value}" ${character_offset1} 2 byte1)
    math(EXPR value "0x${byte1}${byte0}")
    set(${output_variable} "${value}" PARENT_SCOPE)
endfunction()

function(read_uint32_le hex_value byte_offset output_variable)
    math(EXPR character_offset "${byte_offset} * 2")
    math(EXPR required_end "${character_offset} + 8")
    string(LENGTH "${hex_value}" hex_length)
    if(required_end GREATER hex_length)
        message(FATAL_ERROR "truncated PE field at byte offset ${byte_offset}")
    endif()
    string(SUBSTRING "${hex_value}" ${character_offset} 2 byte0)
    math(EXPR character_offset1 "${character_offset} + 2")
    math(EXPR character_offset2 "${character_offset} + 4")
    math(EXPR character_offset3 "${character_offset} + 6")
    string(SUBSTRING "${hex_value}" ${character_offset1} 2 byte1)
    string(SUBSTRING "${hex_value}" ${character_offset2} 2 byte2)
    string(SUBSTRING "${hex_value}" ${character_offset3} 2 byte3)
    math(EXPR value "0x${byte3}${byte2}${byte1}${byte0}")
    set(${output_variable} "${value}" PARENT_SCOPE)
endfunction()

function(validate_pe_contract executable)
    file(SIZE "${executable}" executable_size)
    if(executable_size LESS 158)
        message(FATAL_ERROR "packaged openomega.exe is too small to be a valid PE image")
    endif()
    file(READ "${executable}" dos_header OFFSET 0 LIMIT 64 HEX)
    read_uint16_le("${dos_header}" 0 dos_magic)
    if(NOT dos_magic EQUAL 0x5a4d)
        message(FATAL_ERROR "packaged openomega.exe is missing the MZ signature")
    endif()
    read_uint32_le("${dos_header}" 60 pe_offset)
    math(EXPR required_pe_size "${pe_offset} + 94")
    if(required_pe_size GREATER executable_size)
        message(FATAL_ERROR "packaged openomega.exe has an out-of-range PE header")
    endif()
    file(READ "${executable}" pe_header OFFSET ${pe_offset} LIMIT 120 HEX)
    read_uint32_le("${pe_header}" 0 pe_signature)
    read_uint16_le("${pe_header}" 4 pe_machine)
    read_uint16_le("${pe_header}" 24 optional_magic)
    read_uint16_le("${pe_header}" 92 subsystem)
    if(NOT pe_signature EQUAL 0x00004550)
        message(FATAL_ERROR "packaged openomega.exe is missing the PE signature")
    endif()
    if(NOT pe_machine EQUAL 0x8664)
        message(FATAL_ERROR "packaged openomega.exe is not an x86-64 PE image")
    endif()
    if(NOT optional_magic EQUAL 0x020b)
        message(FATAL_ERROR "packaged openomega.exe is not PE32+")
    endif()
    if(NOT subsystem EQUAL 3)
        message(FATAL_ERROR "packaged openomega.exe is not a Windows console application")
    endif()
endfunction()

function(ascii_to_utf16le_hex input_value output_variable)
    string(HEX "${input_value}" narrow_hex)
    string(LENGTH "${narrow_hex}" narrow_hex_length)
    set(wide_hex "")
    if(narrow_hex_length GREATER 0)
        math(EXPR narrow_hex_last "${narrow_hex_length} - 2")
        foreach(hex_offset RANGE 0 ${narrow_hex_last} 2)
            string(SUBSTRING "${narrow_hex}" ${hex_offset} 2 byte_hex)
            string(APPEND wide_hex "${byte_hex}00")
        endforeach()
    endif()
    set(${output_variable} "${wide_hex}" PARENT_SCOPE)
endfunction()

function(require_no_private_workspace_paths executable)
    file(READ "${executable}" executable_hex HEX)
    string(TOLOWER "${executable_hex}" executable_hex)
    foreach(workspace_path IN ITEMS
            "${OPENOMEGA_SOURCE_DIRECTORY}"
            "${OPENOMEGA_BUILD_DIRECTORY}")
        file(TO_NATIVE_PATH "${workspace_path}" workspace_native_path)
        set(path_variants "${workspace_path}" "${workspace_native_path}")
        string(TOLOWER "${workspace_path}" workspace_path_lower)
        string(TOLOWER "${workspace_native_path}" workspace_native_path_lower)
        string(TOUPPER "${workspace_path}" workspace_path_upper)
        string(TOUPPER "${workspace_native_path}" workspace_native_path_upper)
        list(APPEND path_variants
            "${workspace_path_lower}"
            "${workspace_native_path_lower}"
            "${workspace_path_upper}"
            "${workspace_native_path_upper}"
        )
        list(REMOVE_DUPLICATES path_variants)
        foreach(path_variant IN LISTS path_variants)
            string(HEX "${path_variant}" path_hex)
            string(TOLOWER "${path_hex}" path_hex)
            string(FIND "${executable_hex}" "${path_hex}" narrow_path_offset)
            if(NOT narrow_path_offset EQUAL -1)
                message(FATAL_ERROR
                    "packaged openomega.exe contains a private source/build path"
                )
            endif()
            ascii_to_utf16le_hex("${path_variant}" wide_path_hex)
            string(TOLOWER "${wide_path_hex}" wide_path_hex)
            string(FIND "${executable_hex}" "${wide_path_hex}" wide_path_offset)
            if(NOT wide_path_offset EQUAL -1)
                message(FATAL_ERROR
                    "packaged openomega.exe contains a wide private source/build path"
                )
            endif()
        endforeach()
    endforeach()
endfunction()

function(validate_dependencies executable)
    if(OPENOMEGA_DUMP_TOOL_MODE STREQUAL "DUMPBIN")
        set(dump_arguments /nologo /dependents "${executable}")
    else()
        set(dump_arguments /dump /dependents "${executable}")
    endif()
    execute_process(
        COMMAND "${OPENOMEGA_DUMP_TOOL}" ${dump_arguments}
        RESULT_VARIABLE result
        OUTPUT_VARIABLE stdout
        ERROR_VARIABLE stderr
        TIMEOUT 20
        ENCODING AUTO
    )
    if(NOT result STREQUAL "0")
        message(FATAL_ERROR
            "PE dependency dump failed with '${result}'\n"
            "stdout=[${stdout}]\nstderr=[${stderr}]"
        )
    endif()

    string(CONCAT dump_output "${stdout}\n${stderr}")
    string(REGEX MATCHALL "[A-Za-z0-9_.-]+\\.[dD][lL][lL]" imports "${dump_output}")
    if(imports STREQUAL "")
        message(FATAL_ERROR "PE dependency dump reported no imports")
    endif()

    set(allowed_api_set_imports
        # Newer MSVC/Windows SDK pairs may encode the OS synchronization
        # dependency through this Windows API-set contract instead of only
        # through the traditional umbrella DLL import.
        API-MS-WIN-CORE-SYNCH-L1-2-0
    )
    set(allowed_imports
        KERNEL32 USER32 GDI32 WINMM IMM32 OLE32 OLEAUT32 VERSION ADVAPI32 SETUPAPI SHELL32
        ${allowed_api_set_imports}
    )
    set(normalized_imports "")
    foreach(import_name IN LISTS imports)
        string(REGEX REPLACE "\\.[dD][lL][lL]$" "" import_name "${import_name}")
        string(TOUPPER "${import_name}" import_name)
        list(APPEND normalized_imports "${import_name}")
    endforeach()
    list(REMOVE_DUPLICATES normalized_imports)
    list(SORT normalized_imports)
    if(normalized_imports STREQUAL "")
        message(FATAL_ERROR "PE dependency import set is empty after normalization")
    endif()
    foreach(import_name IN LISTS normalized_imports)
        set(forbidden_api_set FALSE)
        if(import_name MATCHES "^API-MS-" AND
           NOT import_name IN_LIST allowed_api_set_imports)
            set(forbidden_api_set TRUE)
        endif()
        if(import_name MATCHES "^SDL3" OR
           import_name MATCHES "^MSVCP" OR
           import_name MATCHES "^VCRUNTIME" OR
           import_name MATCHES "^UCRT" OR
           forbidden_api_set OR
           import_name MATCHES "D$")
            message(FATAL_ERROR
                "packaged openomega.exe has a forbidden runtime import: ${import_name}"
            )
        endif()
        if(NOT import_name IN_LIST allowed_imports)
            message(FATAL_ERROR
                "packaged openomega.exe has an unapproved import: ${import_name}"
            )
        endif()
    endforeach()
endfunction()

function(require_no_reparse_points root)
    string(REPLACE "'" "''" powershell_root "${root}")
    set(reparse_inspector [=[
& {
    $ErrorActionPreference = 'Stop'
    $rootEntry = Get-Item -LiteralPath '__OPENOMEGA_ROOT__' -Force -ErrorAction Stop
    $entries = @($rootEntry) + @(
        Get-ChildItem -LiteralPath $rootEntry.FullName -Force -Recurse -ErrorAction Stop
    )
    foreach ($entry in $entries) {
        if (($entry.Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0) {
            [Console]::Out.Write('reparse-point-present')
            exit 2
        }
    }
}
]=])
    string(REPLACE "__OPENOMEGA_ROOT__" "${powershell_root}"
        reparse_inspector "${reparse_inspector}")
    execute_process(
        COMMAND "${OPENOMEGA_POWERSHELL}"
            -NoLogo
            -NoProfile
            -NonInteractive
            -Command "${reparse_inspector}"
        RESULT_VARIABLE result
        OUTPUT_VARIABLE output
        ERROR_VARIABLE error
        TIMEOUT 10
        ENCODING AUTO
    )
    string(STRIP "${output}" output)
    if(NOT result STREQUAL "0" OR NOT output STREQUAL "" OR NOT error STREQUAL "")
        message(FATAL_ERROR
            "payload reparse-point inspection failed: "
            "result='${result}' output=[${output}] error=[${error}]"
        )
    endif()
endfunction()

function(validate_payload_tree container output_root)
    file(GLOB_RECURSE payload_entries
        LIST_DIRECTORIES TRUE
        RELATIVE "${container}"
        "${container}/*"
    )
    set(normalized_payload_entries "")
    foreach(relative_path IN LISTS payload_entries)
        file(TO_CMAKE_PATH "${relative_path}" relative_path)
        list(APPEND normalized_payload_entries "${relative_path}")
        set(absolute_path "${container}/${relative_path}")
        if(IS_SYMLINK "${absolute_path}")
            message(FATAL_ERROR "payload contains a symbolic link: ${relative_path}")
        endif()
        if(NOT EXISTS "${absolute_path}")
            message(FATAL_ERROR "payload contains a nonregular entry: ${relative_path}")
        endif()
    endforeach()
    list(SORT normalized_payload_entries)

    set(root "${OPENOMEGA_PACKAGE_BASENAME}")
    set(expected_payload_entries
        "${root}"
        "${root}/LICENSE"
        "${root}/LICENSES"
        "${root}/LICENSES/SDL3.txt"
        "${root}/NOTICE"
        "${root}/README-WINDOWS.md"
        "${root}/THIRD_PARTY_NOTICES.md"
        "${root}/TRADEMARKS.md"
        "${root}/launch-openomega.cmd"
        "${root}/openomega.exe"
    )
    list(SORT expected_payload_entries)
    if(NOT normalized_payload_entries STREQUAL expected_payload_entries)
        message(FATAL_ERROR
            "payload tree mismatch\n"
            "expected=[${expected_payload_entries}]\n"
            "actual=[${normalized_payload_entries}]"
        )
    endif()
    foreach(expected_directory IN ITEMS "${root}" "${root}/LICENSES")
        if(NOT IS_DIRECTORY "${container}/${expected_directory}")
            message(FATAL_ERROR "payload entry is not a directory: ${expected_directory}")
        endif()
    endforeach()
    foreach(expected_file IN ITEMS
            "${root}/openomega.exe"
            "${root}/launch-openomega.cmd"
            "${root}/README-WINDOWS.md"
            "${root}/LICENSE"
            "${root}/NOTICE"
            "${root}/TRADEMARKS.md"
            "${root}/THIRD_PARTY_NOTICES.md"
            "${root}/LICENSES/SDL3.txt")
        set(expected_file_path "${container}/${expected_file}")
        if(NOT EXISTS "${expected_file_path}" OR IS_DIRECTORY "${expected_file_path}" OR
           IS_SYMLINK "${expected_file_path}")
            message(FATAL_ERROR "payload entry is not a regular file: ${expected_file}")
        endif()
    endforeach()
    require_no_reparse_points("${container}")
    set(${output_root} "${container}/${root}" PARENT_SCOPE)
endfunction()

function(validate_launcher_bytes launcher)
    string(CONCAT expected_launcher
        "@echo off\r\n"
        "setlocal\r\n"
        "cd /d \"%~dp0\" || exit /b 1\r\n"
        "\"%~dp0openomega.exe\" %*\r\n"
        "exit /b %ERRORLEVEL%\r\n"
    )
    string(HEX "${expected_launcher}" expected_launcher_hex)
    file(READ "${launcher}" actual_launcher_hex HEX)
    if(NOT actual_launcher_hex STREQUAL expected_launcher_hex)
        message(FATAL_ERROR "packaged launcher is not the exact ASCII CRLF five-line contract")
    endif()
endfunction()

function(run_launcher_case case_name expected_result expected_stdout expected_stderr)
    file(TO_NATIVE_PATH "${package_launcher}" native_launcher)
    execute_process(
        COMMAND "${OPENOMEGA_COMSPEC}" /d /s /c call "${native_launcher}" ${ARGN}
        WORKING_DIRECTORY "${OPENOMEGA_PACKAGE_UNRELATED_DIRECTORY}"
        RESULT_VARIABLE result
        OUTPUT_VARIABLE stdout
        ERROR_VARIABLE stderr
        TIMEOUT 10
        ENCODING AUTO
    )
    normalize_process_output(stdout "${stdout}")
    normalize_process_output(stderr "${stderr}")
    if(NOT result STREQUAL "${expected_result}")
        message(FATAL_ERROR
            "${case_name}: expected exit ${expected_result}, got '${result}'\n"
            "stdout=[${stdout}]\nstderr=[${stderr}]"
        )
    endif()
    if(NOT stdout STREQUAL "${expected_stdout}")
        message(FATAL_ERROR
            "${case_name}: stdout mismatch\nexpected=[${expected_stdout}]\nactual=[${stdout}]"
        )
    endif()
    if(NOT stderr STREQUAL "${expected_stderr}")
        message(FATAL_ERROR
            "${case_name}: stderr mismatch\nexpected=[${expected_stderr}]\nactual=[${stderr}]"
        )
    endif()
endfunction()

file(MAKE_DIRECTORY "${OPENOMEGA_PACKAGE_DIRECTORY}")
file(REMOVE_RECURSE
    "${OPENOMEGA_PACKAGE_STAGE_DIRECTORY}"
    "${OPENOMEGA_PACKAGE_EXTRACT_DIRECTORY}"
    "${OPENOMEGA_PACKAGE_UNRELATED_DIRECTORY}"
    "${OPENOMEGA_PACKAGE_PROFILE_DIRECTORY}"
)
file(MAKE_DIRECTORY
    "${OPENOMEGA_PACKAGE_STAGE_DIRECTORY}"
    "${OPENOMEGA_PACKAGE_UNRELATED_DIRECTORY}"
    "${OPENOMEGA_PACKAGE_PROFILE_DIRECTORY}"
)

run_cpack_failure(
    cpack_tgz_release
    TGZ
    Release
    "OpenOmega portable package contract requires CPACK_GENERATOR=ZIP"
)
run_cpack_failure(
    cpack_zip_no_config
    ZIP
    "<none>"
    "OpenOmega portable package contract requires CPACK_BUILD_CONFIG=Release"
)
run_cpack_failure(
    cpack_zip_debug
    ZIP
    Debug
    "OpenOmega portable package contract requires CPACK_BUILD_CONFIG=Release"
)
run_cpack_failure(
    cpack_zip_relwithdebinfo
    ZIP
    RelWithDebInfo
    "OpenOmega portable package contract requires CPACK_BUILD_CONFIG=Release"
)
run_install_failure(Debug)
run_install_failure(RelWithDebInfo)

set(release_install_prefix "${OPENOMEGA_PACKAGE_STAGE_DIRECTORY}/Release")
file(REMOVE_RECURSE "${release_install_prefix}")
file(MAKE_DIRECTORY "${release_install_prefix}")
directory_manifest("${OPENOMEGA_PACKAGE_DIRECTORY}" install_package_before)
directory_manifest("${OPENOMEGA_PACKAGE_UNRELATED_DIRECTORY}" install_unrelated_before)
directory_manifest("${OPENOMEGA_PACKAGE_PROFILE_DIRECTORY}" install_profile_before)
execute_process(
    COMMAND "${CMAKE_COMMAND}"
        --install "${OPENOMEGA_BUILD_DIRECTORY}"
        --config Release
        --prefix "${release_install_prefix}"
    WORKING_DIRECTORY "${OPENOMEGA_PACKAGE_UNRELATED_DIRECTORY}"
    RESULT_VARIABLE release_install_result
    OUTPUT_VARIABLE release_install_stdout
    ERROR_VARIABLE release_install_stderr
    TIMEOUT 30
)
if(NOT release_install_result STREQUAL "0")
    message(FATAL_ERROR
        "direct Release install failed with '${release_install_result}'\n"
        "stdout=[${release_install_stdout}]\nstderr=[${release_install_stderr}]"
    )
endif()
validate_payload_tree("${release_install_prefix}" installed_package_root)
set(installed_executable "${installed_package_root}/openomega.exe")
set(installed_launcher "${installed_package_root}/launch-openomega.cmd")
validate_launcher_bytes("${installed_launcher}")
validate_pe_contract("${installed_executable}")
require_no_private_workspace_paths("${installed_executable}")
validate_dependencies("${installed_executable}")
directory_manifest("${OPENOMEGA_PACKAGE_DIRECTORY}" install_package_after)
directory_manifest("${OPENOMEGA_PACKAGE_UNRELATED_DIRECTORY}" install_unrelated_after)
directory_manifest("${OPENOMEGA_PACKAGE_PROFILE_DIRECTORY}" install_profile_after)
require_same_manifest("direct Release install package directory"
    "${install_package_before}" "${install_package_after}")
require_same_manifest("direct Release install unrelated directory"
    "${install_unrelated_before}" "${install_unrelated_after}")
require_same_manifest("direct Release install synthetic profile"
    "${install_profile_before}" "${install_profile_after}")

file(REMOVE_RECURSE
    "${OPENOMEGA_PACKAGE_STAGE_DIRECTORY}"
    "${OPENOMEGA_PACKAGE_EXTRACT_DIRECTORY}"
)
if(EXISTS "${OPENOMEGA_PACKAGE_STAGE_DIRECTORY}" OR
   IS_SYMLINK "${OPENOMEGA_PACKAGE_STAGE_DIRECTORY}" OR
   EXISTS "${OPENOMEGA_PACKAGE_EXTRACT_DIRECTORY}" OR
   IS_SYMLINK "${OPENOMEGA_PACKAGE_EXTRACT_DIRECTORY}")
    message(FATAL_ERROR "package stage/extract scratch was not clean before CPack")
endif()

clean_cpack_outputs()
require_known_outputs_absent("positive package precondition")
directory_manifest("${OPENOMEGA_PACKAGE_DIRECTORY}" package_before)
directory_manifest("${OPENOMEGA_PACKAGE_UNRELATED_DIRECTORY}" unrelated_before)

execute_process(
    COMMAND "${OPENOMEGA_CPACK_COMMAND}"
        --preset msvc-windows-portable
    WORKING_DIRECTORY "${OPENOMEGA_SOURCE_DIRECTORY}"
    RESULT_VARIABLE package_result
    OUTPUT_VARIABLE package_stdout
    ERROR_VARIABLE package_stderr
    TIMEOUT 60
)
if(NOT package_result STREQUAL "0")
    message(FATAL_ERROR
        "Release ZIP packaging failed with '${package_result}'\n"
        "stdout=[${package_stdout}]\nstderr=[${package_stderr}]"
    )
endif()
if(NOT EXISTS "${package_zip}" OR IS_DIRECTORY "${package_zip}" OR
   IS_SYMLINK "${package_zip}")
    message(FATAL_ERROR "Release packaging did not create the exact regular ZIP")
endif()
if(NOT EXISTS "${package_checksum}" OR IS_DIRECTORY "${package_checksum}" OR
   IS_SYMLINK "${package_checksum}")
    message(FATAL_ERROR "Release packaging did not create the exact checksum sidecar")
endif()
if(EXISTS "${package_tgz}" OR EXISTS "${package_tgz_checksum}")
    message(FATAL_ERROR "Release ZIP packaging created an unexpected TGZ artifact")
endif()
file(REMOVE_RECURSE "${cpack_scratch}")
directory_manifest("${OPENOMEGA_PACKAGE_DIRECTORY}" package_after
    "${OPENOMEGA_PACKAGE_BASENAME}.zip"
    "${OPENOMEGA_PACKAGE_BASENAME}.zip.sha256"
)
require_same_manifest("positive package directory" "${package_before}" "${package_after}")

file(READ "${package_checksum}" sidecar)
normalize_process_output(sidecar "${sidecar}")
string(REGEX REPLACE "\n+$" "" sidecar "${sidecar}")
string(REGEX MATCH "^([0-9A-Fa-f]+)[ \t]+\\*?([^\r\n]+)$" sidecar_match "${sidecar}")
if(sidecar_match STREQUAL "")
    message(FATAL_ERROR "checksum sidecar format is invalid: [${sidecar}]")
endif()
set(sidecar_hash "${CMAKE_MATCH_1}")
set(sidecar_filename "${CMAKE_MATCH_2}")
string(LENGTH "${sidecar_hash}" sidecar_hash_length)
if(NOT sidecar_hash_length EQUAL 64)
    message(FATAL_ERROR "checksum sidecar hash is not 64 hexadecimal digits")
endif()
if(NOT sidecar_filename STREQUAL "${OPENOMEGA_PACKAGE_BASENAME}.zip")
    message(FATAL_ERROR "checksum sidecar names the wrong archive: ${sidecar_filename}")
endif()
file(SHA256 "${package_zip}" actual_hash)
string(TOLOWER "${sidecar_hash}" sidecar_hash)
string(TOLOWER "${actual_hash}" actual_hash)
if(NOT sidecar_hash STREQUAL actual_hash)
    message(FATAL_ERROR "checksum sidecar does not match the archive bytes")
endif()

execute_process(
    COMMAND "${CMAKE_COMMAND}" -E tar tf "${package_zip}"
    RESULT_VARIABLE archive_list_result
    OUTPUT_VARIABLE archive_list_stdout
    ERROR_VARIABLE archive_list_stderr
    TIMEOUT 20
)
if(NOT archive_list_result STREQUAL "0")
    message(FATAL_ERROR
        "unable to list ZIP members with '${archive_list_result}'\n"
        "stdout=[${archive_list_stdout}]\nstderr=[${archive_list_stderr}]"
    )
endif()
normalize_process_output(archive_list_stdout "${archive_list_stdout}")
string(REPLACE "\n" ";" archive_member_lines "${archive_list_stdout}")
set(archive_members "")
foreach(archive_member IN LISTS archive_member_lines)
    if(archive_member STREQUAL "")
        continue()
    endif()
    string(FIND "${archive_member}" "\\" archive_backslash_index)
    if(NOT archive_backslash_index EQUAL -1 OR
       archive_member MATCHES "^\\./" OR
       archive_member MATCHES "/\\./" OR
       archive_member MATCHES "/\\.$")
        message(FATAL_ERROR "ZIP contains a noncanonical member name: ${archive_member}")
    endif()
    string(REGEX REPLACE "/$" "" archive_member "${archive_member}")
    if(archive_member STREQUAL "" OR
       archive_member MATCHES "(^|/)\\.\\.(/|$)" OR
       archive_member MATCHES "^/" OR
       archive_member MATCHES "^[A-Za-z]:")
        message(FATAL_ERROR "ZIP contains an unsafe member name: ${archive_member}")
    endif()
    list(APPEND archive_members "${archive_member}")
endforeach()
list(LENGTH archive_members archive_member_count)
set(unique_archive_members ${archive_members})
list(REMOVE_DUPLICATES unique_archive_members)
list(LENGTH unique_archive_members unique_archive_member_count)
if(NOT archive_member_count EQUAL unique_archive_member_count)
    message(FATAL_ERROR "ZIP contains duplicate or collapsing member names")
endif()
set(expected_archive_members
    "${OPENOMEGA_PACKAGE_BASENAME}"
    "${OPENOMEGA_PACKAGE_BASENAME}/LICENSE"
    "${OPENOMEGA_PACKAGE_BASENAME}/LICENSES"
    "${OPENOMEGA_PACKAGE_BASENAME}/LICENSES/SDL3.txt"
    "${OPENOMEGA_PACKAGE_BASENAME}/NOTICE"
    "${OPENOMEGA_PACKAGE_BASENAME}/README-WINDOWS.md"
    "${OPENOMEGA_PACKAGE_BASENAME}/THIRD_PARTY_NOTICES.md"
    "${OPENOMEGA_PACKAGE_BASENAME}/TRADEMARKS.md"
    "${OPENOMEGA_PACKAGE_BASENAME}/launch-openomega.cmd"
    "${OPENOMEGA_PACKAGE_BASENAME}/openomega.exe"
)
list(SORT unique_archive_members)
list(SORT expected_archive_members)
if(NOT unique_archive_members STREQUAL expected_archive_members)
    message(FATAL_ERROR
        "ZIP member list mismatch\n"
        "expected=[${expected_archive_members}]\n"
        "actual=[${unique_archive_members}]"
    )
endif()

file(REMOVE_RECURSE "${OPENOMEGA_PACKAGE_EXTRACT_DIRECTORY}")
file(MAKE_DIRECTORY "${OPENOMEGA_PACKAGE_EXTRACT_DIRECTORY}")
file(ARCHIVE_EXTRACT
    INPUT "${package_zip}"
    DESTINATION "${OPENOMEGA_PACKAGE_EXTRACT_DIRECTORY}"
)

validate_payload_tree("${OPENOMEGA_PACKAGE_EXTRACT_DIRECTORY}" package_root)
set(package_executable "${package_root}/openomega.exe")
set(package_launcher "${package_root}/launch-openomega.cmd")
validate_launcher_bytes("${package_launcher}")

validate_pe_contract("${package_executable}")
require_no_private_workspace_paths("${package_executable}")
validate_dependencies("${package_executable}")
directory_manifest("${package_root}" package_root_before_launch)

set(had_localappdata FALSE)
if(DEFINED ENV{LOCALAPPDATA})
    set(had_localappdata TRUE)
    set(saved_localappdata "$ENV{LOCALAPPDATA}")
endif()
set(had_disable_dialog FALSE)
if(DEFINED ENV{OPENOMEGA_DISABLE_STARTUP_DIALOG})
    set(had_disable_dialog TRUE)
    set(saved_disable_dialog "$ENV{OPENOMEGA_DISABLE_STARTUP_DIALOG}")
endif()
set(ENV{LOCALAPPDATA} "${OPENOMEGA_PACKAGE_PROFILE_DIRECTORY}")
set(ENV{OPENOMEGA_DISABLE_STARTUP_DIALOG} "1")

string(CONCAT zero_frame_stdout
    "OpenOmega native persistence: profiles=0\n"
    "OpenOmega native shell: rendered_frames=0\n"
)
run_launcher_case(package_launcher_zero_frames 0 "${zero_frame_stdout}" "" --frames=0)
require_windows_native_save_genesis(
    "packaged first zero-frame startup" "${OPENOMEGA_PACKAGE_PROFILE_DIRECTORY}")
directory_manifest("${OPENOMEGA_PACKAGE_PROFILE_DIRECTORY}"
    profile_after_first_native_startup)
run_launcher_case(package_launcher_zero_frames_reopen 0
    "${zero_frame_stdout}" "" --frames=0)
directory_manifest("${OPENOMEGA_PACKAGE_PROFILE_DIRECTORY}"
    profile_after_native_reopen)
require_same_manifest("packaged second zero-frame native-save reopen"
    "${profile_after_first_native_startup}" "${profile_after_native_reopen}")
string(CONCAT launch_usage
    "usage: openomega [-h|--help]\n"
    "       openomega [--config=PATH] [--set=KEY=VALUE ...] "
    "[--frames=N [--capture-run [--replay-capture]]] "
    "[--data-root=PATH [--level=CODE] [--probe-only]]\n"
)
string(CONCAT sentinel_stderr
    "unknown option: --openomega-package-contract-sentinel\n"
    "${launch_usage}"
)
run_launcher_case(package_launcher_argument_forwarding 1 "" "${sentinel_stderr}"
    --openomega-package-contract-sentinel)

if(had_localappdata)
    set(ENV{LOCALAPPDATA} "${saved_localappdata}")
else()
    unset(ENV{LOCALAPPDATA})
endif()
if(had_disable_dialog)
    set(ENV{OPENOMEGA_DISABLE_STARTUP_DIALOG} "${saved_disable_dialog}")
else()
    unset(ENV{OPENOMEGA_DISABLE_STARTUP_DIALOG})
endif()

directory_manifest("${OPENOMEGA_PACKAGE_UNRELATED_DIRECTORY}" unrelated_after)
directory_manifest("${OPENOMEGA_PACKAGE_PROFILE_DIRECTORY}" profile_after)
directory_manifest("${package_root}" package_root_after_launch)
require_same_manifest("launcher unrelated directory" "${unrelated_before}" "${unrelated_after}")
require_same_manifest("launcher synthetic profile"
    "${profile_after_first_native_startup}" "${profile_after}")
require_same_manifest("launcher package root"
    "${package_root_before_launch}" "${package_root_after_launch}")
