# Copyright (C) 2026 The Qt Company Ltd.
# SPDX-License-Identifier: BSD-3-Clause

# Checks whether any unparsed arguments have been passed to the function at the call site.
# Use this right after `cmake_parse_arguments`.
function(qfp_validate_all_args_are_parsed prefix)
    if(DEFINED ${prefix}_UNPARSED_ARGUMENTS)
        message(FATAL_ERROR "Unknown arguments: (${${prefix}_UNPARSED_ARGUMENTS})")
    endif()
endfunction()

# Defer calls command_name and its arguments to the end of the current add_subdirectory() scope.
function(qfp_defer_call command_name)
    cmake_language(EVAL CODE "cmake_language(DEFER CALL \"${command_name}\" ${ARGN}) ")
endfunction()

# Defer calls the function arguments to the end of the current add_subdirectory() scope.
# Also records that the target will be finalized, but has not been finalized yet. This is needed
# to properly handle SBOM generation for a project, where the project needs to be handled
# only after all the targets are finalized.
function(qfp_mark_for_deferred_finalization target command_name)
    set_property(GLOBAL APPEND PROPERTY _qfp_sbom_targets_expecting_finalization "${target}")

    qfp_defer_call("${command_name}" "${target}" ${ARGN})
endfunction()

# This function can remove single-value arguments from a list of arguments in order to pass
# a filtered list of arguments to a different function that uses
# cmake_parse_arguments.
# This is a stripped down version of _qt_internal_qt_remove_args.
# Parameters:
#   out_var: result of removing all arguments specified by ARGS_TO_REMOVE from ARGS
#   ARGS_TO_REMOVE: Arguments to remove.
#   ARGS: Arguments passed into the function, usually ${ARGV}
#   E.g.:
#   We want to forward all arguments from foo to bar, except INSTALL_PATH <path> since it will
#   trigger an error in bar.
#
#   foo(target BAR... INSTALL_PATH /tmp)
#   bar(target BAR...)
#
#   function(foo target)
#       cmake_parse_arguments(PARSE_ARGV 1 arg "" "INSTALL_PATH" "BAR)
#       qfp_remove_single_args(forward_args
#           ARGS_TO_REMOVE INSTALL_PATH
#           ARGS ${ARGV}
#       )
#       bar(${target} ${forward_args})
#   endfunction()
function(qfp_remove_single_args out_var)
    set(opt_args "")
    set(single_args "")
    set(multi_args
        ARGS
        ARGS_TO_REMOVE
    )

    cmake_parse_arguments(PARSE_ARGV 0 arg "${opt_args}" "${single_args}" "${multi_args}")

    set(out_args ${arg_ARGS})

    foreach(arg IN LISTS arg_ARGS_TO_REMOVE)
        # Find arg.
        list(FIND out_args ${arg} index_to_remove)
        if(index_to_remove EQUAL -1)
            continue()
        endif()

        # Remove arg.
        list(REMOVE_AT out_args ${index_to_remove})

        list(LENGTH out_args result_len)
        if(index_to_remove EQUAL result_len)
            # We removed the last argument.
            continue()
        endif()

        # Remove arg value.
        list(REMOVE_AT out_args ${index_to_remove})
    endforeach()

    set(${out_var} "${out_args}" PARENT_SCOPE)
endfunction()

# Helper function to forward options from one function to another.
#
# This is somewhat the opposite of _qt_internal_remove_args.
# It is a renamed copy of _qt_internal_forward_function_args from Qt. It's needed to support
# building Creator against older versions of Qt that don't have it.
#
# Parameters:
# FORWARD_PREFIX is usually arg because we pass cmake_parse_arguments(PARSE_ARGV 0 arg) in most code
# FORWARD_OPTIONS, FORWARD_SINGLE, FORWARD_MULTI are the options that should be forwarded.
#
# The forwarded args will be either set in arg_FORWARD_OUT_VAR or appended if FORWARD_APPEND is set.
#
# The function reads the options like ${arg_FORWARD_PREFIX}_${option} in the parent scope.
function(qfp_forward_function_args)
    set(opt_args
        FORWARD_APPEND
    )
    set(single_args
        FORWARD_PREFIX
    )
    set(multi_args
        FORWARD_OPTIONS
        FORWARD_SINGLE
        FORWARD_MULTI
        FORWARD_OUT_VAR
    )
    cmake_parse_arguments(PARSE_ARGV 0 arg "${opt_args}" "${single_args}" "${multi_args}")
    _qt_internal_validate_all_args_are_parsed(arg)

    if(NOT arg_FORWARD_OUT_VAR)
        message(FATAL_ERROR "FORWARD_OUT_VAR must be provided.")
    endif()

    set(forward_args "")
    foreach(option_name IN LISTS arg_FORWARD_OPTIONS)
        if(${arg_FORWARD_PREFIX}_${option_name})
            list(APPEND forward_args "${option_name}")
        endif()
    endforeach()

    foreach(option_name IN LISTS arg_FORWARD_SINGLE)
        if(NOT "${${arg_FORWARD_PREFIX}_${option_name}}" STREQUAL "")
            list(APPEND forward_args "${option_name}" "${${arg_FORWARD_PREFIX}_${option_name}}")
        endif()
    endforeach()

    foreach(option_name IN LISTS arg_FORWARD_MULTI)
        if(NOT "${${arg_FORWARD_PREFIX}_${option_name}}" STREQUAL "")
            list(APPEND forward_args "${option_name}" ${${arg_FORWARD_PREFIX}_${option_name}})
        endif()
    endforeach()

    if(arg_FORWARD_APPEND)
        set(forward_args ${${arg_FORWARD_OUT_VAR}} "${forward_args}")
    endif()

    set(${arg_FORWARD_OUT_VAR} "${forward_args}" PARENT_SCOPE)
endfunction()

# Promotes a 3rd party provided target and its 3rd party link dependencies to global, so they
# are visible in all directory scopes.
# Same as Qt's _qt_internal_promote_3rd_party_provided_target_and_3rd_party_deps_to_global, minus
# the early return when not building Qt.
function(qfp_promote_3rd_party_provided_target_and_3rd_party_deps_to_global target)
    if(NOT TARGET "${target}")
        return()
    endif()

    get_property(is_global TARGET "${target}" PROPERTY IMPORTED_GLOBAL)
    _qt_internal_should_not_promote_package_target_to_global("${target}" should_not_promote)
    if(NOT is_global AND NOT should_not_promote)
        _qt_internal_promote_3rd_party_target_to_global("${target}")
        _qt_internal_promote_3rd_party_link_targets_to_global("${target}")
    endif()
endfunction()

# Wrapper around find_package() for 3rd party packages, similar to qt_find_package.
# If the package is found, the PROVIDED_TARGETS are promoted to global and recorded as system
# libraries, so that SBOM-enabled targets linking to them get an sbom dependency
# All other arguments are forwarded to find_package().
macro(qfp_find_package)
    cmake_parse_arguments(__qfp_find_package_arg "" "" "PROVIDED_TARGETS" ${ARGN})

    find_package(${__qfp_find_package_arg_UNPARSED_ARGUMENTS})

    if(${ARGV0}_FOUND AND __qfp_find_package_arg_PROVIDED_TARGETS)
        foreach(__qfp_find_package_target IN LISTS __qfp_find_package_arg_PROVIDED_TARGETS)
            qfp_promote_3rd_party_provided_target_and_3rd_party_deps_to_global(
                "${__qfp_find_package_target}")
        endforeach()

        qfp_sbom_record_system_library("${ARGV0}"
            TARGETS ${__qfp_find_package_arg_PROVIDED_TARGETS}
        )
    endif()

    unset(__qfp_find_package_target)
    unset(__qfp_find_package_arg_PROVIDED_TARGETS)
    unset(__qfp_find_package_arg_UNPARSED_ARGUMENTS)
endmacro()
