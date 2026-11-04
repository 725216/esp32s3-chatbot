include_guard(GLOBAL)

# This module is intentionally passive: it defines build-time helpers but does
# not modify targets until a product or test project calls one of the apply
# functions below.  Component CMake files are evaluated while IDF is still
# expanding requirements, so applying a global job policy from this module
# would be order-dependent and would affect unrelated consumers.

get_filename_component(
    _brookesia_compile_tuning_utils_dir
    "${CMAKE_CURRENT_LIST_DIR}"
    DIRECTORY
)
get_filename_component(
    _brookesia_compile_tuning_repo_root
    "${_brookesia_compile_tuning_utils_dir}"
    DIRECTORY
)
get_filename_component(
    _BROOKESIA_COMPILE_TUNING_REPO_ROOT
    "${_brookesia_compile_tuning_repo_root}"
    DIRECTORY
)
set_property(
    GLOBAL PROPERTY
        BROOKESIA_COMPILE_TUNING_REPO_ROOT
        "${_BROOKESIA_COMPILE_TUNING_REPO_ROOT}"
)

if(DEFINED BROOKESIA_SUPER_CXX_JOBS AND NOT DEFINED BROOKESIA_CXX_JOBS)
    set(BROOKESIA_CXX_JOBS "${BROOKESIA_SUPER_CXX_JOBS}" CACHE STRING
        "Maximum concurrent compile jobs for C++-bearing Brookesia targets")
    message(DEPRECATION
        "BROOKESIA_SUPER_CXX_JOBS is deprecated; use BROOKESIA_CXX_JOBS instead")
endif()
if(NOT DEFINED BROOKESIA_CXX_JOBS)
    set(BROOKESIA_CXX_JOBS "6" CACHE STRING
        "Maximum concurrent compile jobs for C++-bearing Brookesia targets; 0 disables the pool")
endif()
if(NOT BROOKESIA_CXX_JOBS MATCHES "^[0-9]+$")
    message(FATAL_ERROR
        "BROOKESIA_CXX_JOBS must be a non-negative integer")
endif()

if(DEFINED BROOKESIA_SUPER_FAST_COMPILE AND NOT DEFINED BROOKESIA_FAST_COMPILE)
    set(BROOKESIA_FAST_COMPILE "${BROOKESIA_SUPER_FAST_COMPILE}" CACHE BOOL
        "Reduce generated debug information for local C++ builds")
    message(DEPRECATION
        "BROOKESIA_SUPER_FAST_COMPILE is deprecated; use BROOKESIA_FAST_COMPILE instead")
endif()
option(
    BROOKESIA_FAST_COMPILE
    "Reduce generated debug information for local C++ builds"
    OFF
)
option(
    BROOKESIA_COMPILE_TUNING_INCLUDE_ESP_BOOST
    "Include the esp-boost target in the Brookesia C++ compile job pool"
    ON
)

function(_brookesia_compile_tuning_target_has_cxx_sources target_name output_var)
    set(_has_cxx FALSE)
    if(TARGET "${target_name}")
        get_target_property(_target_sources "${target_name}" SOURCES)
        if(NOT _target_sources STREQUAL "_target_sources-NOTFOUND")
            foreach(_source IN LISTS _target_sources)
                string(TOLOWER "${_source}" _source_lower)
                if(_source_lower MATCHES "[.](cc|cpp|cxx)$")
                    set(_has_cxx TRUE)
                    break()
                endif()
            endforeach()
        endif()
    endif()
    set(${output_var} "${_has_cxx}" PARENT_SCOPE)
endfunction()

function(_brookesia_compile_tuning_target_is_usable target_name output_var)
    set(_usable FALSE)
    if(TARGET "${target_name}")
        get_target_property(_target_type "${target_name}" TYPE)
        get_target_property(_target_imported "${target_name}" IMPORTED)
        if(NOT _target_type STREQUAL "INTERFACE_LIBRARY" AND
            NOT _target_imported)
            set(_usable TRUE)
        endif()
    endif()
    set(${output_var} "${_usable}" PARENT_SCOPE)
endfunction()

function(_brookesia_compile_tuning_create_pool pool_name jobs)
    if(NOT CMAKE_GENERATOR MATCHES "Ninja" OR "${jobs}" STREQUAL "0")
        return()
    endif()

    get_property(_job_pools GLOBAL PROPERTY JOB_POOLS)
    set(_pool_entry "${pool_name}=${jobs}")
    if(NOT _pool_entry IN_LIST _job_pools)
        set_property(GLOBAL APPEND PROPERTY JOB_POOLS "${_pool_entry}")
    endif()
endfunction()

function(_brookesia_compile_tuning_apply_target target_name is_external)
    _brookesia_compile_tuning_target_is_usable("${target_name}" _usable)
    if(NOT _usable)
        return()
    endif()

    get_target_property(_already_applied "${target_name}"
        BROOKESIA_COMPILE_TUNING_APPLIED)
    if(_already_applied)
        return()
    endif()

    _brookesia_compile_tuning_target_has_cxx_sources("${target_name}" _has_cxx)
    if(NOT _has_cxx)
        set_property(TARGET "${target_name}"
            PROPERTY BROOKESIA_COMPILE_TUNING_APPLIED TRUE)
        return()
    endif()

    if(NOT is_external AND CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
        target_compile_options("${target_name}" PRIVATE
            $<$<COMPILE_LANGUAGE:CXX>:-fno-ipa-cp-clone>
            $<$<COMPILE_LANGUAGE:CXX>:-fno-ipa-sra>
        )
    endif()

    if(BROOKESIA_FAST_COMPILE)
        target_compile_options("${target_name}" PRIVATE
            $<$<COMPILE_LANGUAGE:CXX>:-g1>
        )
    endif()

    if(CMAKE_GENERATOR MATCHES "Ninja" AND
        NOT BROOKESIA_CXX_JOBS STREQUAL "0")
        set_property(TARGET "${target_name}"
            PROPERTY JOB_POOL_COMPILE brookesia_cxx)
    endif()

    set_property(TARGET "${target_name}"
        PROPERTY BROOKESIA_COMPILE_TUNING_APPLIED TRUE)
endfunction()

function(brookesia_compile_tuning_apply_targets)
    set(_options)
    set(_one_value_args)
    set(_multi_value_args TARGETS EXTERNAL_TARGETS)
    cmake_parse_arguments(
        _BROOKESIA_TUNING
        "${_options}"
        "${_one_value_args}"
        "${_multi_value_args}"
        ${ARGN}
    )

    _brookesia_compile_tuning_create_pool(brookesia_cxx "${BROOKESIA_CXX_JOBS}")

    set(_external_targets ${_BROOKESIA_TUNING_EXTERNAL_TARGETS})
    set(_applied_count 0)
    foreach(_target_name IN LISTS _BROOKESIA_TUNING_TARGETS)
        list(FIND _external_targets "${_target_name}" _external_index)
        if(_external_index GREATER -1)
            set(_is_external TRUE)
        else()
            set(_is_external FALSE)
        endif()

        get_target_property(_already_applied "${_target_name}"
            BROOKESIA_COMPILE_TUNING_APPLIED)
        _brookesia_compile_tuning_apply_target("${_target_name}" "${_is_external}")
        if(NOT _already_applied)
            get_target_property(_was_applied "${_target_name}"
                BROOKESIA_COMPILE_TUNING_APPLIED)
            if(_was_applied)
                math(EXPR _applied_count "${_applied_count} + 1")
            endif()
        endif()
    endforeach()

    message(STATUS
        "Brookesia compile tuning: configured ${_applied_count} C++ target(s), "
        "job pool jobs=${BROOKESIA_CXX_JOBS}, fast_compile=${BROOKESIA_FAST_COMPILE}")
endfunction()

function(_brookesia_compile_tuning_target_has_repo_source
    target_name source_roots output_var
)
    set(_has_repo_source FALSE)
    if(TARGET "${target_name}")
        get_target_property(_target_sources "${target_name}" SOURCES)
        get_target_property(_target_source_dir "${target_name}" SOURCE_DIR)
        foreach(_source IN LISTS _target_sources)
            if(_source MATCHES "^[<$]")
                continue()
            endif()
            get_filename_component(
                _source_abs
                "${_source}"
                ABSOLUTE
                BASE_DIR "${_target_source_dir}"
            )
            foreach(_root IN LISTS source_roots)
                if(NOT EXISTS "${_root}")
                    continue()
                endif()
                file(RELATIVE_PATH _relative_source "${_root}" "${_source_abs}")
                if(NOT _relative_source MATCHES "^\\.\\."
                    AND NOT _relative_source MATCHES "(^|[/\\])managed_components([/\\]|$)")
                    set(_has_repo_source TRUE)
                    break()
                endif()
            endforeach()
            if(_has_repo_source)
                break()
            endif()
        endforeach()
    endif()
    set(${output_var} "${_has_repo_source}" PARENT_SCOPE)
endfunction()

function(_brookesia_compile_tuning_collect_pc_targets directory source_roots)
    get_property(_targets DIRECTORY "${directory}" PROPERTY BUILDSYSTEM_TARGETS)
    get_property(_subdirectories DIRECTORY "${directory}" PROPERTY SUBDIRECTORIES)

    set(_collected_targets ${_BROOKESIA_PC_TARGETS})
    foreach(_target_name IN LISTS _targets)
        _brookesia_compile_tuning_target_has_repo_source(
            "${_target_name}" "${source_roots}" _has_repo_source)
        if(_has_repo_source)
            list(APPEND _collected_targets "${_target_name}")
        endif()
    endforeach()

    foreach(_subdirectory IN LISTS _subdirectories)
        _brookesia_compile_tuning_collect_pc_targets(
            "${_subdirectory}" "${source_roots}")
        list(APPEND _collected_targets ${_BROOKESIA_PC_TARGETS})
    endforeach()
    list(REMOVE_DUPLICATES _collected_targets)
    set(_BROOKESIA_PC_TARGETS "${_collected_targets}" PARENT_SCOPE)
endfunction()

function(brookesia_compile_tuning_apply_idf_components)
    if(NOT COMMAND idf_build_get_property)
        message(FATAL_ERROR
            "brookesia_compile_tuning_apply_idf_components requires ESP-IDF project.cmake")
    endif()

    set(_options)
    set(_one_value_args)
    set(_multi_value_args EXTERNAL_COMPONENTS)
    cmake_parse_arguments(
        _BROOKESIA_IDF_TUNING
        "${_options}"
        "${_one_value_args}"
        "${_multi_value_args}"
        ${ARGN}
    )

    idf_build_get_property(_build_components BUILD_COMPONENTS)
    set(_targets)
    set(_external_targets)
    foreach(_component IN LISTS _build_components)
        if(_component MATCHES "(^|__)esp-boost$" AND
            BROOKESIA_COMPILE_TUNING_INCLUDE_ESP_BOOST)
            set(_target "__idf_${_component}")
            if(TARGET "${_target}")
                list(APPEND _targets "${_target}")
                list(APPEND _external_targets "${_target}")
            endif()
        elseif(_component MATCHES "(^|__)brookesia")
            set(_target "__idf_${_component}")
            if(TARGET "${_target}")
                list(APPEND _targets "${_target}")
            endif()
        elseif(_component STREQUAL "main")
            set(_target "__idf_main")
            if(TARGET "${_target}")
                list(APPEND _targets "${_target}")
            endif()
        endif()
    endforeach()

    foreach(_component IN LISTS _BROOKESIA_IDF_TUNING_EXTERNAL_COMPONENTS)
        set(_target "__idf_${_component}")
        if(TARGET "${_target}")
            list(APPEND _targets "${_target}")
            list(APPEND _external_targets "${_target}")
        endif()
    endforeach()

    brookesia_compile_tuning_apply_targets(
        TARGETS ${_targets}
        EXTERNAL_TARGETS ${_external_targets}
    )
endfunction()

function(brookesia_compile_tuning_apply_pc_targets)
    set(_options)
    set(_one_value_args DIRECTORY)
    set(_multi_value_args SOURCE_ROOTS)
    cmake_parse_arguments(
        _BROOKESIA_PC_TUNING
        "${_options}"
        "${_one_value_args}"
        "${_multi_value_args}"
        ${ARGN}
    )

    if(NOT _BROOKESIA_PC_TUNING_DIRECTORY)
        # BUILDSYSTEM_TARGETS and SUBDIRECTORIES are source-directory
        # properties.  Starting from the source tree also works for projects
        # whose binary tree is outside the repository.
        set(_BROOKESIA_PC_TUNING_DIRECTORY "${CMAKE_SOURCE_DIR}")
    endif()
    if(NOT _BROOKESIA_PC_TUNING_SOURCE_ROOTS)
        get_property(_default_root GLOBAL PROPERTY BROOKESIA_COMPILE_TUNING_REPO_ROOT)
        set(_BROOKESIA_PC_TUNING_SOURCE_ROOTS "${_default_root}")
    endif()

    set(_BROOKESIA_PC_TARGETS)
    _brookesia_compile_tuning_collect_pc_targets(
        "${_BROOKESIA_PC_TUNING_DIRECTORY}"
        "${_BROOKESIA_PC_TUNING_SOURCE_ROOTS}"
    )

    set(_external_targets)
    if(BROOKESIA_COMPILE_TUNING_INCLUDE_ESP_BOOST)
        foreach(_candidate IN ITEMS
            esp-boost
            esp_boost
            espressif__esp-boost
            __idf_espressif__esp-boost
        )
            if(TARGET "${_candidate}")
                list(APPEND _BROOKESIA_PC_TARGETS "${_candidate}")
                list(APPEND _external_targets "${_candidate}")
            endif()
        endforeach()
    endif()

    list(REMOVE_DUPLICATES _BROOKESIA_PC_TARGETS)
    brookesia_compile_tuning_apply_targets(
        TARGETS ${_BROOKESIA_PC_TARGETS}
        EXTERNAL_TARGETS ${_external_targets}
    )
endfunction()

function(brookesia_compile_tuning_apply_current_project)
    if(COMMAND idf_build_get_property)
        brookesia_compile_tuning_apply_idf_components(${ARGN})
        return()
    endif()

    set(_options)
    set(_one_value_args DIRECTORY)
    set(_multi_value_args SOURCE_ROOTS)
    cmake_parse_arguments(
        _BROOKESIA_CURRENT_TUNING
        "${_options}"
        "${_one_value_args}"
        "${_multi_value_args}"
        ${ARGN}
    )
    set(_arguments)
    if(_BROOKESIA_CURRENT_TUNING_DIRECTORY)
        list(APPEND _arguments DIRECTORY "${_BROOKESIA_CURRENT_TUNING_DIRECTORY}")
    endif()
    if(_BROOKESIA_CURRENT_TUNING_SOURCE_ROOTS)
        list(APPEND _arguments SOURCE_ROOTS ${_BROOKESIA_CURRENT_TUNING_SOURCE_ROOTS})
    endif()
    brookesia_compile_tuning_apply_pc_targets(${_arguments})
endfunction()
