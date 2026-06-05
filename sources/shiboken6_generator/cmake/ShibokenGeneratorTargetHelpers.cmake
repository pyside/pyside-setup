# Copyright (C) 2026 The Qt Company Ltd.
# SPDX-License-Identifier: BSD-3-Clause

# Wrapper around add_library that can create either a library or a plugin.
# Also takes care of finalizing the target and generating SBOM information if requested.
# Pass add_library options to FUNCTION_ARGS, and SBOM options to SBOM_ARGS.
# RUNTIME_PATH, LIBRARY_PATH, and ARCHIVE_PATH are required for library targets that are not
# marked with NO_INSTALL, these are used for SBOM installation purposes.
function(qfp_add_library target)
    set(opt_args
        NO_INSTALL
        # Used for the Designer PySidePlugin
        CREATE_QT_PLUGIN
    )
    set(single_args
        RUNTIME_PATH
        LIBRARY_PATH
        ARCHIVE_PATH
    )
    set(multi_args
        FUNCTION_ARGS
        SBOM_ARGS
    )

    cmake_parse_arguments(PARSE_ARGV 1 arg "${opt_args}" "${single_args}" "${multi_args}")
    qfp_validate_all_args_are_parsed(arg)

    if(NOT arg_NO_INSTALL
            AND NOT arg_RUNTIME_PATH
            AND NOT arg_LIBRARY_PATH
            AND NOT arg_ARCHIVE_PATH
            AND NOT arg_INSTALL_PATH
        )
        message(FATAL_ERROR "One of RUNTIME_PATH, LIBRARY_PATH, ARCHIVE_PATH must "
            "be specified for library targets not marked with NO_INSTALL.")
    endif()

    set(function_args "")
    if(arg_FUNCTION_ARGS)
        list(APPEND function_args ${arg_FUNCTION_ARGS})
    endif()

    if(arg_CREATE_QT_PLUGIN)
        qt_add_plugin("${target}" ${function_args})
    else()
        add_library("${target}" ${function_args})
    endif()

    qfp_mark_for_deferred_finalization("${target}" qfp_finalize_library)

    if(QT_GENERATE_SBOM)
        set(sbom_args
            DEFAULT_SBOM_ENTITY_TYPE "LIBRARY"
            ${arg_SBOM_ARGS}
        )

        if(arg_RUNTIME_PATH)
            list(APPEND sbom_args RUNTIME_PATH "${arg_RUNTIME_PATH}")
        endif()

        if(arg_LIBRARY_PATH)
            list(APPEND sbom_args LIBRARY_PATH "${arg_LIBRARY_PATH}")
        endif()

        if(arg_ARCHIVE_PATH)
            list(APPEND sbom_args ARCHIVE_PATH "${arg_ARCHIVE_PATH}")
        endif()

        get_target_property(target_type "${target}" TYPE)
        if(arg_NO_INSTALL)
            list(APPEND sbom_args NO_INSTALL)
        endif()

        qfp_extend_qfp_entity_sbom("${target}" ${sbom_args})
    endif()
endfunction()

# Wrapper around add_executable that also takes care of finalizing the target and
# generating SBOM information if requested.
# Pass add_exectuable options to FUNCTION_ARGS, and SBOM options to SBOM_ARGS.
# RUNTIME_PATH are required for executable targets that are not marked with NO_INSTALL, this is
# used for SBOM installation purposes.
function(qfp_add_executable target)
    set(opt_args
        NO_INSTALL
    )
    set(single_args
        RUNTIME_PATH
    )
    set(multi_args
        FUNCTION_ARGS
        SBOM_ARGS
    )

    cmake_parse_arguments(PARSE_ARGV 1 arg "${opt_args}" "${single_args}" "${multi_args}")
    qfp_validate_all_args_are_parsed(arg)

    if(NOT arg_NO_INSTALL AND NOT arg_RUNTIME_PATH)
        message(FATAL_ERROR "RUNTIME_PATH must be specified for executable targets that are not "
            "marked with NO_INSTALL.")
    endif()

    set(function_args "")
    if(arg_FUNCTION_ARGS)
        list(APPEND function_args ${arg_FUNCTION_ARGS})
    endif()

    add_executable("${target}" ${function_args})

    qfp_mark_for_deferred_finalization("${target}" qfp_finalize_executable)

    if(QT_GENERATE_SBOM)
        set(sbom_args
            DEFAULT_SBOM_ENTITY_TYPE "EXECUTABLE"
            ${arg_SBOM_ARGS}
        )

        if(arg_RUNTIME_PATH)
            list(APPEND sbom_args RUNTIME_PATH "${arg_RUNTIME_PATH}")
        endif()

        get_target_property(target_type "${target}" TYPE)
        if(arg_NO_INSTALL)
            list(APPEND sbom_args NO_INSTALL)
        endif()

        qfp_extend_qfp_entity_sbom("${target}" ${sbom_args})
    endif()
endfunction()

function(qfp_finalize_executable target)
    qfp_finalize_target("${target}")
endfunction()

function(qfp_finalize_library target)
    qfp_finalize_target("${target}")
endfunction()

function(qfp_finalize_target target)
    qfp_finalize_sbom("${target}")

    # Mark as finalized.
    set_target_properties(${target} PROPERTIES _qfp_is_finalized TRUE)
endfunction()

# SBOM finalizer handler that is run after finalizing a target.
function(qfp_finalize_sbom target)
    if(NOT QT_GENERATE_SBOM)
        return()
    endif()

    _qt_internal_finalize_sbom(${target})
endfunction()
