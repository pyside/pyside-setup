# Copyright (C) 2026 The Qt Company Ltd.
# SPDX-License-Identifier: BSD-3-Clause

# Sets up SBOM support when the Qt version is 6.12.1+.
# SBOM generation can be enabled by setting QT_GENERATE_SBOM to ON.
# By default generation is OFF.
function(qfp_setup_sbom)
    get_cmake_property(setup_called _qfp_internal_sbom_setup_called)
    if(setup_called)
        return()
    endif()

    set_property(GLOBAL PROPERTY _qfp_internal_sbom_setup_called TRUE)

    # Just-in-case opt-in to stop running any kind of SBOM code.
    if(QFP_NO_GENERATE_SBOM)
        qfp_force_disable_sbom()
        return()
    endif()

    # Check that the Qt version supports generating sboms.
    if(Qt6_VERSION VERSION_LESS 6.12.1)
        set(sbom_supported FALSE)
    else()
        set(sbom_supported TRUE)
    endif()

    # Show error when trying to generate SBOM, but the underlying Qt does not support it.
    if(QT_GENERATE_SBOM AND NOT sbom_supported)

        if(NOT Qt6_DIR)
            message(FATAL_ERROR
                "SBOM generation requested via -DQT_GENERATE_SBOM=ON, but no valid Qt6 version "
                "found.")
        endif()

        message(FATAL_ERROR
            "Found Qt version '${Qt6_VERSION}' at '${Qt6_DIR}' does not have all necessary support "
            "for generating an SBOM. Qt 6.12.1 or later is required.")
    endif()

    set(generate_by_default "OFF")
    option(QT_GENERATE_SBOM "Generate SBOM documents in SPDX v2.3 tag:value format."
        ${generate_by_default})

    # Return early if SBOM is not supported and it isn't explicitly enabled.
    if(NOT sbom_supported)
        return()
    endif()

    # Call Qt's internal setup function.
    _qt_internal_setup_sbom(
        GENERATE_SBOM_DEFAULT "${generate_by_default}"
    )
endfunction()

function(qfp_force_disable_sbom)
    set(QT_GENERATE_SBOM "OFF" CACHE BOOL "Generate SBOM documents in SPDX v2.3 tag:value format."
        FORCE)
endfunction()

# This function should be called to set up the SBOM generation for a project.
# One project == one document.
# Each sbom-enabled target will have its data written into the document.
# Various options can be passed to set up information like project name, version, download location,
# document install prefix, etc. See the options below.
# The function and its _end() counterpart can be called multiple times inside a CMake project, to
# create multiple documents and split up targets into specific documents.
function(qfp_sbom_begin_project)
    if(NOT QT_GENERATE_SBOM)
        return()
    endif()

    # These are 99% of the options that Qt knows about, excluding some internal ones.
    set(generic_opt_args
            USE_GIT_VERSION
            MIRROR_TARGET_LICENSE_DECLARED_EXPRESSION
            NO_DEFAULT_TARGET_LICENSE_CONCLUDED_EXPRESSION
            NO_DEFAULT_TARGET_LICENSE_DECLARED_EXPRESSION
    )
    set(generic_single_args
            INSTALL_PREFIX
            INSTALL_SBOM_DIR
            PROJECT_LICENSE_CONCLUDED_EXPRESSION
            PROJECT_LICENSE_DECLARED_EXPRESSION
            DEFAULT_TARGET_LICENSE_CONCLUDED_EXPRESSION
            DEFAULT_TARGET_LICENSE_DECLARED_EXPRESSION
            SUPPLIER
            SUPPLIER_URL
            DOWNLOAD_LOCATION
            DOCUMENT_NAMESPACE
            VERSION
            SBOM_PROJECT_NAME
            QT_REPO_PROJECT_NAME
            CPE
    )
    set(generic_multi_args
            COPYRIGHTS
            LICENSE_DIR_PATHS
    )

    # Qt ones + Qt For Python specific options.
    set(no_default_options
        NO_DEFAULT_USE_GIT_VERSION
        NO_DEFAULT_INSTALL_SBOM_DIR
        NO_DEFAULT_SUPPLIER
        NO_DEFAULT_SUPPLIER_URL
        NO_DEFAULT_DOWNLOAD_LOCATION
        NO_DEFAULT_CPE
        NO_DEFAULT_GENERIC_PURL_NAME
        NO_DEFAULT_GENERIC_PURL_NAMESPACE
        NO_DEFAULT_GIT_PURL_NAME
        NO_DEFAULT_GIT_PURL_NAMESPACE
        NO_DEFAULT_GITHUB_PURL_NAME
        NO_DEFAULT_GITHUB_PURL_NAMESPACE
        NO_DEFAULT_VCS_URL_BASE
        NO_DEFAULT_COPYRIGHTS
        NO_DEFAULT_VERSION
    )

    set(opt_args
        ${generic_opt_args}
        ${no_default_options}
        NO_DEFAULT_QFP_SBOM_OPTIONS
    )
    set(single_args
        GENERIC_PURL_NAME
        GENERIC_PURL_NAMESPACE
        GIT_PURL_NAME
        GIT_PURL_NAMESPACE
        PYPI_PURL_NAME
        GITHUB_PURL_NAME
        GITHUB_PURL_NAMESPACE
        VCS_URL_BASE
        DEFAULT_VERSION
        ${generic_single_args}
    )
    set(multi_args
        ${generic_multi_args}
        DEFAULT_COPYRIGHTS
    )

    cmake_parse_arguments(PARSE_ARGV 0 arg "${opt_args}" "${single_args}" "${multi_args}")
    qfp_validate_all_args_are_parsed(arg)

    set(begin_project_args "")

    if(arg_NO_DEFAULT_QFP_SBOM_OPTIONS)
        foreach(option_name IN LISTS no_default_options)
            set(arg_${option_name} TRUE)
        endforeach()
    endif()

    # Set default Qt For Python specific values, unless caller opted into no defaults.
    # FINAL_PACKAGE_VERSION comes from compute_config_py_values(), called by each project's
    # *Setup.cmake before any SBOM function, so it is visible in this function's calling scope.
    if(NOT arg_VERSION AND NOT arg_DEFAULT_VERSION AND NOT arg_NO_DEFAULT_USE_GIT_VERSION)
        if(FINAL_PACKAGE_VERSION)
            # Passed as the package version.
            set(arg_VERSION "${FINAL_PACKAGE_VERSION}")
            # Passed as each target version.
            set(arg_DEFAULT_VERSION "${FINAL_PACKAGE_VERSION}")
        endif()

        # VERSION has priority, but the git info is still embedded into the auto-generated PURLs.
        set(arg_USE_GIT_VERSION TRUE)
    endif()

    if(NOT arg_INSTALL_SBOM_DIR AND NOT arg_NO_DEFAULT_INSTALL_SBOM_DIR)
        qfp_sbom_get_sbom_install_path(arg_INSTALL_SBOM_DIR)
    endif()

    if(NOT arg_SUPPLIER AND NOT arg_NO_DEFAULT_SUPPLIER)
        set(arg_SUPPLIER "QtGroup")
    endif()
    if(arg_SUPPLIER)
            set_property(GLOBAL PROPERTY _qfp_sbom_supplier "${arg_SUPPLIER}")
    endif()

    if(NOT arg_SUPPLIER_URL AND NOT arg_NO_DEFAULT_SUPPLIER_URL)
        set(arg_SUPPLIER_URL "https://qt.io")
    endif()

    if(arg_DOWNLOAD_LOCATION)
        set_property(GLOBAL PROPERTY _qfp_sbom_download_location_base "${arg_DOWNLOAD_LOCATION}")
    elseif(NOT arg_NO_DEFAULT_DOWNLOAD_LOCATION)
        qfp_sbom_get_repo_source_download_location_default(OUT_VAR download_location_base)
        set_property(GLOBAL PROPERTY _qfp_sbom_download_location_base "${download_location_base}")
    endif()
    qfp_sbom_get_repo_source_download_location(OUT_VAR download_location_versioned)
    if(download_location_versioned)
        set(arg_DOWNLOAD_LOCATION "${download_location_versioned}")
    endif()

    if(NOT arg_CPE AND NOT arg_NO_DEFAULT_CPE)
        set(cpe_args VENDOR "qt" PRODUCT "qt_for_python")
        if(arg_VERSION)
            list(APPEND cpe_args VERSION "${arg_VERSION}")
        endif()
        qfp_sbom_compute_cpe(cpe ${cpe_args})
        set(arg_CPE "${cpe}")
    endif()
    if(arg_CPE)
        set_property(GLOBAL PROPERTY _qfp_sbom_cpe "${arg_CPE}")
    endif()

    if(NOT arg_GENERIC_PURL_NAME AND NOT arg_NO_DEFAULT_GENERIC_PURL_NAME)
        set(arg_GENERIC_PURL_NAME "qt_for_python")
    endif()
    if(arg_GENERIC_PURL_NAME)
        set_property(GLOBAL PROPERTY _qfp_sbom_generic_purl_name "${arg_GENERIC_PURL_NAME}")
    endif()

    if(NOT arg_GENERIC_PURL_NAMESPACE AND NOT arg_NO_DEFAULT_GENERIC_PURL_NAMESPACE)
        set(arg_GENERIC_PURL_NAMESPACE "qt_for_python")
    endif()
    if(arg_GENERIC_PURL_NAMESPACE)
        set_property(GLOBAL PROPERTY _qfp_sbom_generic_purl_namespace
            "${arg_GENERIC_PURL_NAMESPACE}")
    endif()

    # A git purl namespace is the git host and the name is the repo path on that host.
    if(NOT arg_GIT_PURL_NAME AND NOT arg_NO_DEFAULT_GIT_PURL_NAME)
        qfp_sbom_get_git_purl_name_default(arg_GIT_PURL_NAME)
    endif()
    if(arg_GIT_PURL_NAME)
        set_property(GLOBAL PROPERTY _qfp_sbom_git_purl_name "${arg_GIT_PURL_NAME}")
    endif()

    if(NOT arg_GIT_PURL_NAMESPACE AND NOT arg_NO_DEFAULT_GIT_PURL_NAMESPACE)
        qfp_sbom_get_git_purl_namespace_default(arg_GIT_PURL_NAMESPACE)
    endif()
    if(arg_GIT_PURL_NAMESPACE)
        set_property(GLOBAL PROPERTY _qfp_sbom_git_purl_namespace "${arg_GIT_PURL_NAMESPACE}")
    endif()

    # No default.
    if(arg_PYPI_PURL_NAME)
        set_property(GLOBAL PROPERTY _qfp_sbom_pypi_purl_name "${arg_PYPI_PURL_NAME}")
    endif()

    # All Qt For Python projects live in the same repo, so they share the same github purl by
    # default.
    if(NOT arg_GITHUB_PURL_NAME AND NOT arg_NO_DEFAULT_GITHUB_PURL_NAME)
        set(arg_GITHUB_PURL_NAME "pyside-setup")
    endif()
    if(arg_GITHUB_PURL_NAME)
        set_property(GLOBAL PROPERTY _qfp_sbom_github_purl_name "${arg_GITHUB_PURL_NAME}")
    endif()

    if(NOT arg_GITHUB_PURL_NAMESPACE AND NOT arg_NO_DEFAULT_GITHUB_PURL_NAMESPACE)
        set(arg_GITHUB_PURL_NAMESPACE "pyside")
    endif()
    if(arg_GITHUB_PURL_NAMESPACE)
        set_property(GLOBAL PROPERTY _qfp_sbom_github_purl_namespace "${arg_GITHUB_PURL_NAMESPACE}")
    endif()

    if(NOT arg_VCS_URL_BASE AND NOT arg_NO_DEFAULT_VCS_URL_BASE)
        qfp_sbom_get_qfp_vcs_url_base_default(OUT_VAR vcs_url)
        set(arg_VCS_URL_BASE "${vcs_url}")
    endif()
    if(arg_VCS_URL_BASE)
        set_property(GLOBAL PROPERTY _qfp_sbom_vcs_base_url "${arg_VCS_URL_BASE}")
    endif()

    if(NOT arg_NO_DEFAULT_COPYRIGHTS AND NOT arg_DEFAULT_COPYRIGHTS AND QFP_DEFAULT_COPYRIGHT)
        # Passed as the repo package copyright.
        set(arg_COPYRIGHTS "${QFP_DEFAULT_COPYRIGHT}")

        # Passed to Qt For Python entities.
        set(arg_DEFAULT_COPYRIGHTS "${QFP_DEFAULT_COPYRIGHT}")
    endif()
    if(arg_DEFAULT_COPYRIGHTS)
        set_property(GLOBAL PROPERTY _qfp_sbom_default_copyrights "${arg_DEFAULT_COPYRIGHTS}")
    endif()
    if(arg_NO_DEFAULT_COPYRIGHTS)
        set_property(GLOBAL PROPERTY _qfp_sbom_no_default_copyrights "TRUE")
    endif()

    if(arg_DEFAULT_TARGET_LICENSE_CONCLUDED_EXPRESSION
            OR arg_NO_DEFAULT_TARGET_LICENSE_CONCLUDED_EXPRESSION
            OR arg_NO_DEFAULT_QFP_SBOM_OPTIONS)
        set_property(GLOBAL PROPERTY _qfp_sbom_no_entity_type_default_license_concluded_expression
            "TRUE")
    endif()

    # A caller that passes only VERSION should still get versioned targets.
    if(NOT arg_DEFAULT_VERSION AND arg_VERSION AND NOT arg_NO_DEFAULT_VERSION)
        set(arg_DEFAULT_VERSION "${arg_VERSION}")
    endif()
    if(arg_DEFAULT_VERSION)
        set_property(GLOBAL PROPERTY _qfp_sbom_default_version "${arg_DEFAULT_VERSION}")
    endif()
    if(arg_NO_DEFAULT_VERSION)
        set_property(GLOBAL PROPERTY _qfp_sbom_no_default_version "TRUE")
    endif()

    # Forward any set options.
    qfp_forward_function_args(
            FORWARD_APPEND
            FORWARD_PREFIX arg
            FORWARD_OUT_VAR begin_project_args
            FORWARD_OPTIONS
                    ${generic_opt_args}
            FORWARD_SINGLE
                    ${generic_single_args}
            FORWARD_MULTI
                    ${generic_multi_args}
    )

    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/LICENSES")
        set(license_dirs "${CMAKE_CURRENT_SOURCE_DIR}/LICENSES")

        # This is read by the call to _qt_internal_sbom_begin_project, so we need to set it in this
        # scope, because there is no extra option to specify that directly.
        list(APPEND QT_SBOM_LICENSE_DIRS "${license_dirs}")
    endif()

    # find_package(<module>) via <module>Config.cmake might have exported an sbom path location
    # to the <module> SBOM document. Make sure we make it visible for the Qt API.
    qfp_add_exported_sbom_paths_for_current_project()

    # Call the Qt provided SBOM API.
    _qt_internal_sbom_begin_project(${begin_project_args})

    qfp_sbom_add_python_build_tool()
    qfp_sbom_add_android_ndk()
endfunction()

# This function should be called after all relevant targets that should be part of the document
# are created. It triggers the actual generation and installation of the SBOM document.
#
# The DEFER_END_PROJECT option can be used to defer the generation to the end of current
# directory scope. It shouldn't be needed usually because it is auto-enabled when the build system
# detects if there are any targets have not been finalized yet.
function(qfp_sbom_end_project)
    if(NOT QT_GENERATE_SBOM)
        return()
    endif()

    set(opt_args
        DEFER_END_PROJECT
    )
    set(single_args "")
    set(multi_args "")
    cmake_parse_arguments(PARSE_ARGV 0 arg "${opt_args}" "${single_args}" "${multi_args}")
    qfp_validate_all_args_are_parsed(arg)

    set(need_to_defer FALSE)
    if(arg_DEFER_END_PROJECT)
        set(need_to_defer TRUE)
    endif()

    # Check if there are any targets that still haven't been finalized.
    get_cmake_property(targets_expecting_finalization _qfp_sbom_targets_expecting_finalization)
    if(targets_expecting_finalization)
        foreach(target IN LISTS targets_expecting_finalization)
            get_target_property(is_finalized "${target}" _qfp_is_finalized)
            if(NOT is_finalized)
                set(need_to_defer TRUE)
                break()
            endif()
        endforeach()
    endif()

    # This is to support the case when a subdirectory first has to process the deferred target
    # calls, before the project should be ended.
    if(need_to_defer)
        qfp_defer_call(qfp_sbom_end_project_impl)
    else()
        qfp_sbom_end_project_impl()
    endif()
endfunction()

# Implementation helper for the SBOM document generation.
function(qfp_sbom_end_project_impl)
    if(NOT QT_GENERATE_SBOM)
        return()
    endif()

    _qt_internal_sbom_end_project()

    # Clean up global properties that were set by the previous project.
    set_property(GLOBAL PROPERTY _qfp_sbom_generic_purl_name "")
    set_property(GLOBAL PROPERTY _qfp_sbom_generic_purl_namespace "")
    set_property(GLOBAL PROPERTY _qfp_sbom_git_purl_name "")
    set_property(GLOBAL PROPERTY _qfp_sbom_git_purl_namespace "")
    set_property(GLOBAL PROPERTY _qfp_sbom_pypi_purl_name "")
    set_property(GLOBAL PROPERTY _qfp_sbom_github_purl_name "")
    set_property(GLOBAL PROPERTY _qfp_sbom_github_purl_namespace "")
    set_property(GLOBAL PROPERTY _qfp_sbom_vcs_base_url "")
    set_property(GLOBAL PROPERTY _qfp_sbom_cpe "")
    set_property(GLOBAL PROPERTY _qfp_sbom_download_location_base "")
    set_property(GLOBAL PROPERTY _qfp_sbom_supplier "")

    set_property(GLOBAL PROPERTY _qfp_sbom_default_copyrights "")
    set_property(GLOBAL PROPERTY _qfp_sbom_default_version "")

    set_property(GLOBAL PROPERTY _qfp_sbom_no_default_copyrights "")
    set_property(GLOBAL PROPERTY _qfp_sbom_no_entity_type_default_license_concluded_expression "")
    set_property(GLOBAL PROPERTY _qfp_sbom_no_default_version "")

    set_property(GLOBAL PROPERTY _qfp_sbom_targets_expecting_finalization "")

    qfp_reset_exported_sbom_paths_for_current_project()
endfunction()

# Gets the name of the dir where SBOMs should be installed.
function(qfp_sbom_get_sbom_install_dir out_var)
    if(QFP_SBOM_INSTALL_DIR)
        set(sbom_dir "${QFP_SBOM_INSTALL_DIR}")
    else()
        set(sbom_dir "sbom")
    endif()

    set(${out_var} "${sbom_dir}" PARENT_SCOPE)
endfunction()

# Gets the relative install path where Qt for Python specific SBOMs should be installed.
function(qfp_sbom_get_sbom_install_path out_var)
    qfp_sbom_get_sbom_install_dir(sbom_dir)

    if(QFP_SBOM_INSTALL_PATH)
        set(out_path "${QFP_SBOM_INSTALL_PATH}/${sbom_dir}")
    elseif(CMAKE_INSTALL_DATAROOTDIR)
        set(out_path "${CMAKE_INSTALL_DATAROOTDIR}/${sbom_dir}")
    else()
        set(out_path "share/${sbom_dir}")
    endif()

    set(${out_var} "${out_path}" PARENT_SCOPE)
endfunction()

# This function is meant to be called inside a Config.cmake file, to tell Qt about extra sbom paths
# that should be searched when finding dependency sbom documents.
# The paths currently need to be handled by the Qt for Python-specific
# qfp_sbom_begin_project explicitly, but in the future Qt should do that automatically.
function(qfp_add_exported_sbom_paths)
    if(NOT QT_GENERATE_SBOM)
        return()
    endif()

    set(opt_args "")
    set(single_args "")
    set(multi_args
        PREFIXES
    )

    cmake_parse_arguments(PARSE_ARGV 0 arg "${opt_args}" "${single_args}" "${multi_args}")
    qfp_validate_all_args_are_parsed(arg)

    set_property(GLOBAL APPEND PROPERTY _qt_internal_exported_sbom_prefixes ${arg_PREFIXES})
endfunction()

# Internal function to make Qt aware of extra sbom dirs that have documents.
function(qfp_add_exported_sbom_paths_for_current_project)
    if(NOT QT_GENERATE_SBOM)
        return()
    endif()

    # Backup current set of sbom dirs.
    get_cmake_property(build_sbom_dirs _qt_internal_sbom_dirs)
    if(NOT build_sbom_dirs)
        set(build_sbom_dirs "")
    endif()
    set_property(GLOBAL PROPERTY _qt_internal_sbom_dirs_backup "${build_sbom_dirs}")

    # Append the exported sbom paths to the sbom dirs.
    get_cmake_property(sbom_prefixes _qt_internal_exported_sbom_prefixes)
    if(sbom_prefixes)
        set_property(GLOBAL APPEND PROPERTY _qt_internal_sbom_dirs ${sbom_prefixes})
    endif()
endfunction()

# Internal function to reset the extra sbom dirs above.
function(qfp_reset_exported_sbom_paths_for_current_project)
    if(NOT QT_GENERATE_SBOM)
        return()
    endif()

    # Restore backup of sbom dirs.
    get_cmake_property(build_sbom_dirs _qt_internal_sbom_dirs_backup)
    if(NOT build_sbom_dirs)
        set(build_sbom_dirs "")
    endif()
    set_property(GLOBAL PROPERTY _qt_internal_sbom_dirs "${build_sbom_dirs}")
endfunction()

# Get the license spdx id for the Qt Commercial license.
function(qfp_sbom_get_commercial_license out_var)
    if(NOT QT_GENERATE_SBOM)
        set(${out_var} "" PARENT_SCOPE)
        return()
    endif()

    _qt_internal_sbom_get_spdx_license_expression("QT_COMMERCIAL" license)
    set(${out_var} "${license}" PARENT_SCOPE)
endfunction()

# Get the license spdx id for permissive library targets.
function(qfp_sbom_get_open_source_permissive_library_license out_var)
    if(NOT QT_GENERATE_SBOM)
        set(${out_var} "" PARENT_SCOPE)
        return()
    endif()

    _qt_internal_sbom_get_spdx_license_expression("QT_COMMERCIAL_OR_LGPL3" license)
    set(${out_var} "${license}" PARENT_SCOPE)
endfunction()

# Get the license spdx id for GPL library targets.
function(qfp_sbom_get_open_source_gpl_library_license out_var)
    if(NOT QT_GENERATE_SBOM)
        set(${out_var} "" PARENT_SCOPE)
        return()
    endif()

    _qt_internal_sbom_get_spdx_license_expression("QT_COMMERCIAL_OR_GPL3" license)
    set(${out_var} "${license}" PARENT_SCOPE)
endfunction()

# Get the license spdx id for application / tools.
function(qfp_sbom_get_open_source_tool_license out_var)
    if(NOT QT_GENERATE_SBOM)
        set(${out_var} "" PARENT_SCOPE)
        return()
    endif()

    _qt_internal_sbom_get_spdx_license_expression("QT_COMMERCIAL_OR_GPL3_WITH_EXCEPTION" license)
    set(${out_var} "${license}" PARENT_SCOPE)
endfunction()

# Get the license spdx id for Qt Commercial or BSD-3-Clause licensed files.
function(qfp_sbom_get_commercial_or_bsd3_license out_var)
    if(NOT QT_GENERATE_SBOM)
        set(${out_var} "" PARENT_SCOPE)
        return()
    endif()

    _qt_internal_sbom_get_spdx_license_expression("QT_COMMERCIAL_OR_BSD3" license)
    set(${out_var} "${license}" PARENT_SCOPE)
endfunction()

# Get the license spdx id for build system files, like CMake package and pkg-config files.
function(qfp_sbom_get_build_system_license out_var)
    set(${out_var} "BSD-3-Clause" PARENT_SCOPE)
endfunction()

# Gets the names of the files installed by install(EXPORT ... FILE <file_name_base>.cmake).
# The configuration specific file name is only known at generation time, so it is returned as a
# generator expression.
function(qfp_sbom_get_export_file_names out_var file_name_base)
    set(config_suffix "$<IF:$<BOOL:$<CONFIG>>,$<LOWER_CASE:$<CONFIG>>,noconfig>")
    set(${out_var}
        "${file_name_base}.cmake"
        "${file_name_base}-${config_suffix}.cmake"
        PARENT_SCOPE
    )
endfunction()

# Gets the install path of the Python site-packages directory, relative to the install prefix,
# as needed for the INSTALL_PATH of SBOM files.
function(qfp_sbom_get_relative_python_site_packages out_var)
    file(RELATIVE_PATH path "${CMAKE_INSTALL_PREFIX}" "${PYTHON_SITE_PACKAGES}")
    set(${out_var} "${path}" PARENT_SCOPE)
endfunction()

# Get the Qt Company default copyright text.
function(qfp_sbom_get_default_copyright out_var)
    if(QFP_DEFAULT_COPYRIGHT)
        set(value "${QFP_DEFAULT_COPYRIGHT}")
    else()
        set(value "Copyright (C) The Qt Company Ltd. and other contributors.")
    endif()
    set(${out_var} "${value}" PARENT_SCOPE)
endfunction()

# Gets the supplier that was configured for the current sbom project.
function(qfp_sbom_get_supplier out_var)
    get_cmake_property(supplier _qfp_sbom_supplier)
    set(${out_var} "${supplier}" PARENT_SCOPE)
endfunction()

# Gets the git host of the Qt For Python repo, which is the namespace of a git purl.
function(qfp_sbom_get_git_purl_namespace_default out_var)
    set(${out_var} "code.qt.io" PARENT_SCOPE)
endfunction()

# Gets the path of the Qt For Python repo on its git host, which is the name of a git purl.
function(qfp_sbom_get_git_purl_name_default out_var)
    set(${out_var} "pyside/pyside-setup" PARENT_SCOPE)
endfunction()

# Gets the default Qt for Python HTTPS scheme repo URL: aka https://code.qt.io
function(qfp_sbom_get_qfp_vcs_url_base_default)
    set(opt_args "")
    set(single_args
        OUT_VAR
    )
    set(multi_args "")
    cmake_parse_arguments(PARSE_ARGV 0 arg "${opt_args}" "${single_args}" "${multi_args}")
    qfp_validate_all_args_are_parsed(arg)

    if(NOT arg_OUT_VAR)
        message(FATAL_ERROR "OUT_VAR must be set")
    endif()

    qfp_sbom_get_git_purl_namespace_default(git_host)
    qfp_sbom_get_git_purl_name_default(repo_path)

    set(vcs_url "https://${git_host}/${repo_path}.git")
    set(${arg_OUT_VAR} "${vcs_url}" PARENT_SCOPE)
endfunction()

# Gets the VCS url that was configured for the current sbom project.
function(qfp_sbom_get_qfp_vcs_url target)
    set(opt_args "")
    set(single_args
        VERSION
        OUT_VAR
    )
    set(multi_args "")
    cmake_parse_arguments(PARSE_ARGV 1 arg "${opt_args}" "${single_args}" "${multi_args}")
    qfp_validate_all_args_are_parsed(arg)

    if(NOT arg_OUT_VAR)
        message(FATAL_ERROR "OUT_VAR must be set")
    endif()

    set(version_part "")
    if(arg_VERSION)
        set(version_part "@${arg_VERSION}")
    endif()

    get_cmake_property(vcs_url _qfp_sbom_vcs_base_url)
    string(APPEND vcs_url "${version_part}")
    set(${arg_OUT_VAR} "${vcs_url}" PARENT_SCOPE)
endfunction()

# Gets the default Qt for Python git scheme repo URL: aka git://code.qt.io
function(qfp_sbom_get_repo_source_download_location_default)
    set(opt_args "")
    set(single_args
        OUT_VAR
    )
    set(multi_args "")
    cmake_parse_arguments(PARSE_ARGV 0 arg "${opt_args}" "${single_args}" "${multi_args}")
    qfp_validate_all_args_are_parsed(arg)

    if(NOT arg_OUT_VAR)
        message(FATAL_ERROR "OUT_VAR must be set")
    endif()

    qfp_sbom_get_git_purl_namespace_default(git_host)
    qfp_sbom_get_git_purl_name_default(repo_path)

    set(download_location "git://${git_host}/${repo_path}.git")

    set(${arg_OUT_VAR} "${download_location}" PARENT_SCOPE)
endfunction()

# Gets the download location that was configured for the current sbom project.
function(qfp_sbom_get_repo_source_download_location)
    set(opt_args
        ADD_CURRENT_SOURCE_DIR_SUB_PATH
    )
    set(single_args
        TARGET
        OUT_VAR
    )
    set(multi_args "")
    cmake_parse_arguments(PARSE_ARGV 0 arg "${opt_args}" "${single_args}" "${multi_args}")
    qfp_validate_all_args_are_parsed(arg)

    get_cmake_property(download_location _qfp_sbom_download_location_base)
    if(NOT download_location)
        set(${arg_OUT_VAR} "" PARENT_SCOPE)
        return()
    endif()

    _qt_internal_sbom_get_git_version_vars()
    if(QT_SBOM_GIT_HASH)
        string(APPEND download_location "@${QT_SBOM_GIT_HASH}")
    endif()

    if(arg_ADD_CURRENT_SOURCE_DIR_SUB_PATH AND arg_TARGET)
        # Add the subdirectory path where the target was created as a custom qualifier.
        _qt_internal_sbom_get_qt_entity_repo_source_dir("${arg_TARGET}" OUT_VAR sub_path)
        if(sub_path)
            string(APPEND download_location "#${sub_path}")
        endif()
    endif()

    set(${arg_OUT_VAR} "${download_location}" PARENT_SCOPE)
endfunction()

# Gets the args to create a specific type of PURL. Supported: GENERIC, GIT, GITHUB, PYPI
# Exits early if the purl namespace or name have not been previously set by the project begin
# command.
function(qfp_sbom_get_purl target)
    set(opt_args "")
    set(single_args
        PURL_TYPE
        OUT_VAR
    )
    set(multi_args "")
    cmake_parse_arguments(PARSE_ARGV 1 arg "${opt_args}" "${single_args}" "${multi_args}")
    qfp_validate_all_args_are_parsed(arg)

    if(NOT arg_PURL_TYPE)
        message(FATAL_ERROR "PURL_TYPE must be set")
    endif()

    if(NOT arg_OUT_VAR)
        message(FATAL_ERROR "OUT_VAR must be set")
    endif()

    set(purl_args "")

    if(arg_PURL_TYPE STREQUAL "GENERIC")
        list(APPEND purl_args
            PURL_ID "GENERIC"
            PURL_TYPE "generic"
        )
    elseif(arg_PURL_TYPE STREQUAL "GIT")
        list(APPEND purl_args
            PURL_ID "GIT"
            PURL_TYPE "git"
        )
    elseif(arg_PURL_TYPE STREQUAL "GITHUB")
        list(APPEND purl_args
            PURL_ID "GITHUB"
            PURL_TYPE "github"
        )
    elseif(arg_PURL_TYPE STREQUAL "PYPI")
        list(APPEND purl_args
            PURL_ID "PYPI"
            PURL_TYPE "pypi"
        )
    else()
        message(FATAL_ERROR "Invalid PURL_TYPE: ${arg_PURL_TYPE}")
    endif()

    get_cmake_property(generic_purl_name _qfp_sbom_generic_purl_name)
    get_cmake_property(generic_purl_namespace _qfp_sbom_generic_purl_namespace)

    get_cmake_property(git_purl_name _qfp_sbom_git_purl_name)
    get_cmake_property(git_purl_namespace _qfp_sbom_git_purl_namespace)

    get_cmake_property(github_purl_name _qfp_sbom_github_purl_name)
    get_cmake_property(github_purl_namespace _qfp_sbom_github_purl_namespace)

    get_cmake_property(pypi_purl_name _qfp_sbom_pypi_purl_name)

    if(arg_PURL_TYPE STREQUAL "PYPI")
        # A pypi purl has no namespace.
        set(final_purl_name "${pypi_purl_name}")
        set(final_purl_namespace_args "")
        if(pypi_purl_name)
            set(has_required_values TRUE)
        else()
            set(has_required_values FALSE)
        endif()
    elseif(arg_PURL_TYPE STREQUAL "GITHUB")
        set(final_purl_name "${github_purl_name}")
        set(final_purl_namespace_args PURL_NAMESPACE "${github_purl_namespace}")
        if(github_purl_name AND github_purl_namespace)
            set(has_required_values TRUE)
        else()
            set(has_required_values FALSE)
        endif()
    elseif(arg_PURL_TYPE STREQUAL "GIT")
        set(final_purl_name "${git_purl_name}")
        set(final_purl_namespace_args PURL_NAMESPACE "${git_purl_namespace}")
        if(git_purl_name AND git_purl_namespace)
            set(has_required_values TRUE)
        else()
            set(has_required_values FALSE)
        endif()
    else()
        set(final_purl_name "${generic_purl_name}-${target}")
        set(final_purl_namespace_args PURL_NAMESPACE "${generic_purl_namespace}")
        if(generic_purl_name AND generic_purl_namespace)
            set(has_required_values TRUE)
        else()
            set(has_required_values FALSE)
        endif()
    endif()

    if(NOT has_required_values)
        set(${arg_OUT_VAR} "" PARENT_SCOPE)
        return()
    endif()

    _qt_internal_sbom_get_git_version_vars()

    set(entity_vcs_url_version_option "")
    # Can be empty.
    if(QT_SBOM_GIT_HASH_SHORT)
        set(entity_vcs_url_version_option VERSION "${QT_SBOM_GIT_HASH_SHORT}")
    endif()

    qfp_sbom_get_qfp_vcs_url(${target}
        ${entity_vcs_url_version_option}
        OUT_VAR vcs_url)
    list(APPEND purl_args PURL_QUALIFIERS "vcs_url=${vcs_url}")

    # Add the subdirectory path where the target was created as a custom qualifier.
    _qt_internal_sbom_get_qt_entity_repo_source_dir("${target}" OUT_VAR sub_path)
    if(sub_path)
        list(APPEND purl_args PURL_SUBPATH "${sub_path}")
    endif()

    # Add the target name as a custom qualifer.
    list(APPEND purl_args PURL_QUALIFIERS "library_name=${target}")

    # Can be empty.
    if(QT_SBOM_GIT_HASH_SHORT)
        list(APPEND purl_args PURL_VERSION "${QT_SBOM_GIT_HASH_SHORT}")
    endif()

    list(APPEND purl_args
        PURL_NAME "${final_purl_name}"
        ${final_purl_namespace_args}
    )

    set(${arg_OUT_VAR} "${purl_args}" PARENT_SCOPE)
endfunction()

# Internal function that is called by qfp_add_executable and friends to forward sbom related args.
function(qfp_extend_qfp_entity_sbom target)
    if(NOT QT_GENERATE_SBOM)
        return()
    endif()

    qfp_sbom_assert_no_license_expression(${ARGN})

    # Get the sbom entity type if we have relevant values in the passed args.
    qfp_sbom_add_sbom_entity_type_to_args(args_to_forward ${ARGN})

    # Get Qt for Python project specific sbom args.
    qfp_sbom_get_default_sbom_args("${target}" sbom_extra_args ${args_to_forward})

    # Pass args to the Qt function.
    _qt_internal_extend_sbom(${target} ${args_to_forward} ${sbom_extra_args})
endfunction()

# Gets the default sbom args for Qt for Python entity types.
# Things like copyrights, licenses, download location, VCS url, CPE, PURL, etc.
function(qfp_sbom_get_default_sbom_args target out_var)
    _qt_internal_get_sbom_specific_options(opt_args single_args multi_args)
    list(APPEND opt_args NO_INSTALL)
    list(APPEND single_args TYPE)

    cmake_parse_arguments(PARSE_ARGV 2 arg "${opt_args}" "${single_args}" "${multi_args}")
    qfp_validate_all_args_are_parsed(arg)

    set(sbom_args "")

    # Skip adding Qt for Python specific versions, licenses or copyrights to 3rd party entity types,
    # aka bundled / vendored libraries.
    set(third_party_types
        QT_THIRD_PARTY_MODULE
        QT_THIRD_PARTY_SOURCES
        THIRD_PARTY_LIBRARY
        THIRD_PARTY_LIBRARY_WITH_FILES
        THIRD_PARTY_SOURCES
    )
    if(arg_SBOM_ENTITY_TYPE IN_LIST third_party_types)
        set(is_third_party_type TRUE)
    else()
        set(is_third_party_type FALSE)
    endif()

    set(library_types
        LIBRARY
        MODULE_LIBRARY
    )

    set(package_version "")
    get_cmake_property(project_no_default_version _qfp_sbom_no_default_version)
    if(NOT project_no_default_version AND NOT is_third_party_type)
        get_cmake_property(project_default_version _qfp_sbom_default_version)
        if(project_default_version)
            set(package_version "${project_default_version}")
        endif()
    endif()

    if(package_version)
        list(APPEND sbom_args PACKAGE_VERSION "${package_version}")
    endif()

    set(license_expression "")
    get_cmake_property(no_entity_type_default_license_concluded_expression
        _qfp_sbom_no_entity_type_default_license_concluded_expression)
    if(NOT no_entity_type_default_license_concluded_expression
            AND NOT is_third_party_type
            AND NOT arg_LICENSE_CONCLUDED_EXPRESSION)
        if(arg_SBOM_ENTITY_TYPE IN_LIST library_types)
            qfp_sbom_get_open_source_permissive_library_license(license_expression)
        elseif(arg_SBOM_ENTITY_TYPE STREQUAL "EXECUTABLE")
            qfp_sbom_get_open_source_tool_license(license_expression)
        endif()
    endif()

    if(license_expression)
        list(APPEND sbom_args LICENSE_CONCLUDED_EXPRESSION "${license_expression}")
    endif()

    _qt_internal_sbom_is_third_party_or_system_entity_type("${arg_SBOM_ENTITY_TYPE}"
        is_third_party_or_system_entity_type)
    if(NOT is_third_party_or_system_entity_type)
        list(APPEND sbom_args MIRROR_LICENSE_DECLARED_EXPRESSION)
    endif()

    set(copyright "")
    get_cmake_property(project_no_default_copyrights _qfp_sbom_no_default_copyrights)
    if(NOT project_no_default_copyrights AND NOT is_third_party_type)
        get_cmake_property(project_default_copyrights _qfp_sbom_default_copyrights)
        if(project_default_copyrights)
            set(copyright "${project_default_copyrights}")
        else()
            qfp_sbom_get_default_copyright(copyright)
        endif()
    endif()

    if(copyright)
        list(APPEND sbom_args COPYRIGHTS "${copyright}")
    endif()

    qfp_sbom_get_supplier(supplier)
    if(supplier)
        list(APPEND sbom_args SUPPLIER "${supplier}")
    endif()

    get_cmake_property(cpe _qfp_sbom_cpe)
    if(cpe)
        list(APPEND sbom_args CPE "${cpe}")
    endif()

    qfp_sbom_get_repo_source_download_location(
        ADD_CURRENT_SOURCE_DIR_SUB_PATH
        TARGET "${target}"
        OUT_VAR download_location
    )
    if(download_location)
        list(APPEND sbom_args DOWNLOAD_LOCATION "${download_location}")
    endif()

    set(purl_entries "")

    qfp_sbom_get_purl(${target} PURL_TYPE PYPI OUT_VAR purl_pypi_args)
    if(purl_pypi_args)
        list(APPEND purl_entries PURL_ENTRY ${purl_pypi_args})
    endif()

    qfp_sbom_get_purl(${target} PURL_TYPE GITHUB OUT_VAR purl_github_args)
    if(purl_github_args)
        list(APPEND purl_entries PURL_ENTRY ${purl_github_args})
    endif()

    qfp_sbom_get_purl(${target} PURL_TYPE GIT OUT_VAR purl_git_args)
    if(purl_git_args)
        list(APPEND purl_entries PURL_ENTRY ${purl_git_args})
    endif()

    qfp_sbom_get_purl(${target} PURL_TYPE GENERIC OUT_VAR purl_generic_args)
    if(purl_generic_args)
        list(APPEND purl_entries PURL_ENTRY ${purl_generic_args})
    endif()

    if(purl_entries)
        list(APPEND sbom_args PURLS ${purl_entries})
    endif()

    set(${out_var} "${sbom_args}" PARENT_SCOPE)
endfunction()

# Replaces the '+' of versions like '<major.minor.patch>+commercial', because CPE strings only
# allow it escaped, but it's cleaner to have some unescaped char.
function(qfp_sbom_sanitize_version_for_cpe out_var version)
    string(REPLACE "+" "-" version "${version}")
    set(${out_var} "${version}" PARENT_SCOPE)
endfunction()

# Constructs a CPE string based on the passed vendor, product and version.
function(qfp_sbom_compute_cpe out_var)
    set(opt_args "")
    set(single_args
        VENDOR
        PRODUCT
        VERSION
    )
    set(multi_args "")

    cmake_parse_arguments(PARSE_ARGV 0 arg "${opt_args}" "${single_args}" "${multi_args}")

    if(NOT QT_GENERATE_SBOM)
        set(${out_var} "" PARENT_SCOPE)
        return()
    endif()

    set(cpe_args "")

    if(arg_VENDOR)
        list(APPEND cpe_args VENDOR "${arg_VENDOR}")
    else()
        message(FATAL_ERROR "VENDOR must be set")
    endif()

    if(arg_PRODUCT)
        list(APPEND cpe_args PRODUCT "${arg_PRODUCT}")
    else()
        message(FATAL_ERROR "PRODUCT must be set")
    endif()

    if(arg_VERSION)
        qfp_sbom_sanitize_version_for_cpe(cpe_version "${arg_VERSION}")
        list(APPEND cpe_args VERSION "${cpe_version}")
    endif()

    _qt_internal_sbom_compute_security_cpe(cpe ${cpe_args})
    set(${out_var} "${cpe}" PARENT_SCOPE)
endfunction()

# Resolves Qt for Python's DEFAULT_SBOM_ENTITY_TYPE or SBOM_ENTITY_TYPE options to a single
# SBOM_ENTITY_TYPE option, and adds it to the out_var forwarded args.
function(qfp_sbom_add_sbom_entity_type_to_args out_var)
    set(opt_args "")
    set(single_args
        DEFAULT_SBOM_ENTITY_TYPE
        SBOM_ENTITY_TYPE
    )
    set(multi_args "")

    cmake_parse_arguments(PARSE_ARGV 1 arg "${opt_args}" "${single_args}" "${multi_args}")

    if(arg_SBOM_ENTITY_TYPE)
        set(sbom_entity_type "${arg_SBOM_ENTITY_TYPE}")
    elseif(arg_DEFAULT_SBOM_ENTITY_TYPE)
        set(sbom_entity_type "${arg_DEFAULT_SBOM_ENTITY_TYPE}")
    else()
        set(sbom_entity_type "")
    endif()

    # Remove the SBOM_ENTITY_TYPE and DEFAULT_SBOM_ENTITY_TYPE single-arg options, because
    # _qt_internal_extend_sbom doesn't know about them.
    qfp_remove_single_args(filtered_args
        ARGS ${ARGN}
        ARGS_TO_REMOVE SBOM_ENTITY_TYPE DEFAULT_SBOM_ENTITY_TYPE
    )

    # Forward the sbom entity type if we have one.
    if(sbom_entity_type)
        list(APPEND filtered_args SBOM_ENTITY_TYPE "${sbom_entity_type}")
    endif()

    set(${out_var} "${filtered_args}" PARENT_SCOPE)
endfunction()

# LICENSE_EXPRESSION should not be used anymore.
function(qfp_sbom_assert_no_license_expression)
    if("LICENSE_EXPRESSION" IN_LIST ARGN)
        message(FATAL_ERROR
            "LICENSE_EXPRESSION is not supported, use LICENSE_CONCLUDED_EXPRESSION and "
            "LICENSE_DECLARED_EXPRESSION instead.")
    endif()
endfunction()

# Gets SBOM information about the Python found via shiboken_find_required_python or the
# interpreter passed via INTERPRETER.
# The Python on the build machine can come from anywhere, so no supplier or similar info is set.
function(qfp_sbom_get_python_package_info)
    set(opt_args "")
    set(single_args
        INTERPRETER
        OUT_VAR_PACKAGE_NAME
        OUT_VAR_PACKAGE_VERSION
        OUT_VAR_SBOM_ARGS
    )
    set(multi_args "")
    cmake_parse_arguments(PARSE_ARGV 0 arg "${opt_args}" "${single_args}" "${multi_args}")
    qfp_validate_all_args_are_parsed(arg)

    set(py_version "")
    set(pypy_version "")
    set(include_dirs "${Python_INCLUDE_DIRS}")

    # Check version info of a specific interpreter, not necessary the one found by CMake, e.g. the
    # host Python of a cross build.
    if(arg_INTERPRETER)
        set(include_dirs "")

        # Prints the Python language version and the PyPy version, e.g. '3.13.1 none' for
        # CPython or '3.11.13 7.3.20' for PyPy.
        string(CONCAT query_code
            "import platform\n"
            "import sys\n"
            "python_version = platform.python_version()\n"
            "pypy_version = 'none'\n"
            "if hasattr(sys, 'pypy_version_info'):\n"
            "    major, minor, micro = sys.pypy_version_info[:3]\n"
            "    pypy_version = f'{major}.{minor}.{micro}'\n"
            "print(python_version, pypy_version)\n"
        )
        execute_process(
            COMMAND "${arg_INTERPRETER}" -c "${query_code}"
            OUTPUT_VARIABLE versions
            RESULT_VARIABLE result
            OUTPUT_STRIP_TRAILING_WHITESPACE
            ERROR_QUIET
        )
        if(result EQUAL 0 AND versions MATCHES "^([^ ]+) ([^ ]+)$")
            set(py_version "${CMAKE_MATCH_1}")
            if(NOT CMAKE_MATCH_2 STREQUAL "none")
                set(pypy_version "${CMAKE_MATCH_2}")
            endif()
        endif()
    endif()

    # Read the exact versions from the Development component headers, because
    # Python_INTERPRETER_ID is not set in cross builds, Python_VERSION doesn't contain pre-release
    # suffixes, and for PyPy it is the implemented language version rather than the PyPy version.
    foreach(include_dir IN LISTS include_dirs)
        set(patchlevel_h "${include_dir}/patchlevel.h")
        if(EXISTS "${patchlevel_h}")
            file(STRINGS "${patchlevel_h}" version_lines
                REGEX "^#define[ \t]+(PY_VERSION|PYPY_VERSION)[ \t]+\"")
            foreach(line IN LISTS version_lines)
                if(line MATCHES "^#define[ \t]+PY_VERSION[ \t]+\"([^\"]+)\"")
                    set(py_version "${CMAKE_MATCH_1}")
                elseif(line MATCHES "^#define[ \t]+PYPY_VERSION[ \t]+\"([^\"]+)\"")
                    set(pypy_version "${CMAKE_MATCH_1}")
                endif()
            endforeach()
            break()
        endif()
    endforeach()

    if(NOT py_version AND NOT arg_INTERPRETER)
        set(py_version "${Python_VERSION}")
    endif()

    set(package_version "")
    set(sbom_args "")

    if(pypy_version OR (NOT arg_INTERPRETER AND Python_INTERPRETER_ID STREQUAL "PyPy"))
        set(package_name "PyPy")
        set(package_version "${pypy_version}")
        if(py_version)
            list(APPEND sbom_args
                PACKAGE_SUMMARY "PyPy implementing Python ${py_version}"
            )
        endif()
    else()
        set(package_name "CPython")
        set(package_version "${py_version}")
        list(APPEND sbom_args
            LICENSE_DECLARED_EXPRESSION "PSF-2.0"
        )
        if(package_version)
            qfp_sbom_compute_cpe(cpe
                VENDOR "python" PRODUCT "python" VERSION "${package_version}")
            list(APPEND sbom_args CPE "${cpe}")
        endif()
    endif()

    if(package_version)
        list(APPEND sbom_args PACKAGE_VERSION "${package_version}")
    endif()

    set(${arg_OUT_VAR_PACKAGE_NAME} "${package_name}" PARENT_SCOPE)
    if(arg_OUT_VAR_PACKAGE_VERSION)
        set(${arg_OUT_VAR_PACKAGE_VERSION} "${package_version}" PARENT_SCOPE)
    endif()
    set(${arg_OUT_VAR_SBOM_ARGS} "${sbom_args}" PARENT_SCOPE)
endfunction()

# Returns a target representing the Python runtime in the current SBOM document, to be passed
# to SBOM_DEPENDENCIES of targets that use the Python C API.
# Python is linked via raw library paths or not at all (see shiboken_compute_python_libraries),
# so the dependency can't be detected from the linked libraries.
# The target is created on first use, so only documents that have a Python C API user get a
# package for it.
function(qfp_sbom_get_python_runtime_target out_var)
    if(NOT QT_GENERATE_SBOM)
        set(${out_var} "" PARENT_SCOPE)
        return()
    endif()

    # A target can be part of only one SBOM document, so create one per document.
    _qt_internal_sbom_get_root_project_name_lower_case(project_name)
    set(target "${project_name}SbomPythonRuntime")

    if(NOT TARGET "${target}")
        qfp_sbom_get_python_package_info(
            OUT_VAR_PACKAGE_NAME package_name
            OUT_VAR_SBOM_ARGS sbom_args
        )

        set(comment "")
        if(CMAKE_CROSSCOMPILING)
            string(CONCAT comment "Python of the target platform, whose headers and library "
                "were used at build time.")
        elseif(PYTHON_LIMITED_API OR SHIBOKEN_PYTHON_LIMITED_API)
            set(comment "Python used at build time.")
        endif()
        if(PYTHON_LIMITED_API OR SHIBOKEN_PYTHON_LIMITED_API)
            list(APPEND comment
                "The limited API (abi3) binaries can run with a different Python version.")
        endif()
        if(comment)
            list(JOIN comment " " comment)
            list(APPEND sbom_args SBOM_PACKAGE_COMMENT "${comment}")
        endif()

        qfp_add_sbom("${target}"
            SBOM_ENTITY_TYPE SYSTEM_LIBRARY
            FRIENDLY_PACKAGE_NAME "${package_name}"
            ${sbom_args}
        )
    endif()

    set(${out_var} "${target}" PARENT_SCOPE)
endfunction()

# Adds a custom build tool as a build dependency of TO_TARGET, or of the current SBOM project
# when TO_TARGET is not given.
# SBOM_ARGS are passed to the SBOM package of the tool.
function(qfp_sbom_add_custom_build_tool target)
    if(NOT QT_GENERATE_SBOM)
        return()
    endif()

    set(opt_args "")
    set(single_args
        TO_TARGET
        RELATIONSHIP_COMMENT
    )
    set(multi_args
        SBOM_ARGS
    )
    cmake_parse_arguments(PARSE_ARGV 1 arg "${opt_args}" "${single_args}" "${multi_args}")
    qfp_validate_all_args_are_parsed(arg)

    if(arg_TO_TARGET)
        set(to_target "${arg_TO_TARGET}")
    else()
        _qt_internal_sbom_get_current_project_target(to_target)
    endif()

    _qt_internal_add_sbom_system_build_tool("${target}"
        BUILD_TOOL_TYPE CUSTOM
        SBOM_ARGS
            ${arg_SBOM_ARGS}
            SBOM_RELATIONSHIP_ENTRIES
                SBOM_RELATIONSHIP_ENTRY
                    SBOM_RELATIONSHIP_FROM "${target}"
                    SBOM_RELATIONSHIP_TYPE BUILD_DEPENDENCY_OF
                    SBOM_RELATIONSHIP_TO "${to_target}"
                    SBOM_RELATIONSHIP_COMMENT "${arg_RELATIONSHIP_COMMENT}"
    )
endfunction()

# Adds the Python interpreter that runs build steps (e.g. pyi generation) as a build tool of the
# current SBOM document.
function(qfp_sbom_add_python_build_tool)
    if(NOT QT_GENERATE_SBOM)
        return()
    endif()

    # Cross builds only find the Development component of the target Python. Build steps run the
    # host Python that build_scripts passes as QFP_PYTHON_HOST_PATH instead.
    if(Python_EXECUTABLE)
        set(interpreter_args "")
        set(friendly_name_suffix "interpreter")
    elseif(QFP_PYTHON_HOST_PATH)
        set(interpreter_args INTERPRETER "${QFP_PYTHON_HOST_PATH}")
        set(friendly_name_suffix "host interpreter")
    else()
        return()
    endif()

    qfp_sbom_get_python_package_info(
        ${interpreter_args}
        OUT_VAR_PACKAGE_NAME package_name
        OUT_VAR_PACKAGE_VERSION package_version
        OUT_VAR_SBOM_ARGS sbom_args
    )

    _qt_internal_sbom_get_root_project_name_lower_case(project_name)
    _qt_internal_sbom_get_current_project_spdx_id(project_spdx_id)

    set(relationship_comment
        "${project_spdx_id} is built using Python ${friendly_name_suffix}: '${package_name}'")
    if(package_version)
        string(APPEND relationship_comment " Python version: '${package_version}'")
    endif()

    qfp_sbom_add_custom_build_tool("${project_name}SbomBuildToolPython"
        RELATIONSHIP_COMMENT "${relationship_comment}"
        SBOM_ARGS
            FRIENDLY_PACKAGE_NAME "${package_name} ${friendly_name_suffix}"
            ${sbom_args}
    )
endfunction()

# Reads the value of KEY, e.g. 'Pkg.Revision', from the source.properties file of the Android NDK
# or Android SDK package in PACKAGE_DIR. Sets out_var to an empty string if the file or key
# doesn't exist.
function(qfp_sbom_read_android_source_property out_var package_dir key)
    set(value "")
    set(file "${package_dir}/source.properties")
    if(EXISTS "${file}")
        string(REPLACE "." "\\." key_regex "${key}")
        file(STRINGS "${file}" lines REGEX "^${key_regex}[ \t]*=")
        if(lines MATCHES "=[ \t]*([^\n]*[^ \t\n])")
            set(value "${CMAKE_MATCH_1}")
        endif()
    endif()
    set(${out_var} "${value}" PARENT_SCOPE)
endfunction()

# Adds the Android NDK as an sbom build tool, to the current SBOM project.
# Also adds the shared C++ runtime of the NDK as a runtime dependency.
function(qfp_sbom_add_android_ndk)
    if(NOT QT_GENERATE_SBOM OR NOT ANDROID)
        return()
    endif()

    # CMAKE_ANDROID_NDK_VERSION only contains the major and minor version.
    qfp_sbom_read_android_source_property(ndk_version "${CMAKE_ANDROID_NDK}" "Pkg.Revision")
    if(NOT ndk_version)
        set(ndk_version "${CMAKE_ANDROID_NDK_VERSION}")
    endif()

    set(ndk_sbom_args
        SUPPLIER "Google"
        DOWNLOAD_LOCATION "https://developer.android.com/ndk/downloads"
    )
    if(ndk_version)
        list(APPEND ndk_sbom_args PACKAGE_VERSION "${ndk_version}")
    endif()

    _qt_internal_sbom_get_root_project_name_lower_case(project_name)
    _qt_internal_sbom_get_current_project_target(project_target)
    _qt_internal_sbom_get_current_project_spdx_id(project_spdx_id)

    string(CONCAT relationship_comment
        "${project_spdx_id} is built using the Android NDK toolchain and sysroot for ABI "
        "'${CMAKE_ANDROID_ARCH_ABI}' and API level '${CMAKE_ANDROID_API}'")
    qfp_sbom_add_custom_build_tool("${project_name}SbomBuildToolAndroidNdk"
        RELATIONSHIP_COMMENT "${relationship_comment}"
        SBOM_ARGS
            FRIENDLY_PACKAGE_NAME "Android NDK"
            ${ndk_sbom_args}
    )

    if(CMAKE_ANDROID_STL_TYPE STREQUAL "c++_shared")
        set(target "${project_name}SbomAndroidCxxSharedRuntime")
        string(CONCAT package_comment
            "The C++ runtime of the Android NDK. The library is not shipped in the wheels, "
            "instead it is bundled during application deployment.")
        qfp_add_sbom("${target}"
            SBOM_ENTITY_TYPE SYSTEM_LIBRARY
            FRIENDLY_PACKAGE_NAME "Android NDK libc++_shared"
            ${ndk_sbom_args}
            SBOM_PACKAGE_COMMENT "${package_comment}"
            SBOM_RELATIONSHIP_ENTRIES
                SBOM_RELATIONSHIP_ENTRY
                    SBOM_RELATIONSHIP_FROM "${target}"
                    SBOM_RELATIONSHIP_TYPE RUNTIME_DEPENDENCY_OF
                    SBOM_RELATIONSHIP_TO "${project_target}"
                    SBOM_RELATIONSHIP_COMMENT
                        "${project_spdx_id} is built with CMAKE_ANDROID_STL_TYPE 'c++_shared'"
        )
    endif()
endfunction()

# Adds the Android SDK platform android.jar, e.g. from '<sdk>/platforms/android-35',
# as a build tool of target.
function(qfp_sbom_add_android_sdk_platform target)
    if(NOT QT_GENERATE_SBOM)
        return()
    endif()

    set(opt_args "")
    set(single_args
        SDK_PLATFORM_DIR
    )
    set(multi_args "")
    cmake_parse_arguments(PARSE_ARGV 1 arg "${opt_args}" "${single_args}" "${multi_args}")
    qfp_validate_all_args_are_parsed(arg)

    get_filename_component(platform_name "${arg_SDK_PLATFORM_DIR}" NAME)
    qfp_sbom_read_android_source_property(api_level
        "${arg_SDK_PLATFORM_DIR}" "AndroidVersion.ApiLevel")
    if(NOT api_level AND platform_name MATCHES "^android-(.+)$")
        set(api_level "${CMAKE_MATCH_1}")
    endif()
    qfp_sbom_read_android_source_property(revision "${arg_SDK_PLATFORM_DIR}" "Pkg.Revision")

    set(sbom_args
        FRIENDLY_PACKAGE_NAME "Android SDK Platform"
        SUPPLIER "Google"
        DOWNLOAD_LOCATION "https://developer.android.com/tools/releases/platforms"
    )
    if(api_level)
        list(APPEND sbom_args PACKAGE_VERSION "${api_level}")
    endif()
    if(revision)
        list(APPEND sbom_args
            SBOM_PACKAGE_COMMENT "Revision ${revision} of the '${platform_name}' SDK package.")
    endif()

    qfp_sbom_add_custom_build_tool("${target}SbomBuildToolAndroidSdkPlatform"
        TO_TARGET "${target}"
        RELATIONSHIP_COMMENT
            "${target} is compiled against android.jar of the '${platform_name}' SDK platform"
        SBOM_ARGS ${sbom_args}
    )
endfunction()

# Adds the JDK javac as a build tool of target.
# Expects find_package(Java) to have been called.
function(qfp_sbom_add_jdk target)
    if(NOT QT_GENERATE_SBOM)
        return()
    endif()

    set(sbom_args
        FRIENDLY_PACKAGE_NAME "Java Development Kit"
    )
    if(Java_VERSION_STRING)
        list(APPEND sbom_args PACKAGE_VERSION "${Java_VERSION_STRING}")
    endif()

    qfp_sbom_add_custom_build_tool("${target}SbomBuildToolJdk"
        TO_TARGET "${target}"
        RELATIONSHIP_COMMENT "${target} is compiled by the JDK's javac"
        SBOM_ARGS ${sbom_args}
    )
endfunction()

# Adds NumPy as a build dependency of target, which is compiled against the NumPy C API headers
# found in INCLUDE_DIR.
# NumPy is not linked, the numpy module is imported at runtime only if it is installed.
function(qfp_sbom_add_numpy_build_dependency target)
    set(opt_args "")
    set(single_args
        INCLUDE_DIR
    )
    set(multi_args "")
    cmake_parse_arguments(PARSE_ARGV 1 arg "${opt_args}" "${single_args}" "${multi_args}")
    qfp_validate_all_args_are_parsed(arg)

    if(NOT QT_GENERATE_SBOM)
        return()
    endif()

    if(NOT arg_INCLUDE_DIR)
        message(FATAL_ERROR "INCLUDE_DIR must be set")
    endif()

    # The headers don't contain the package version, but the include dir is either
    # numpy/_core/include (NumPy 2) or numpy/core/include (NumPy 1), next to numpy/version.py.
    set(package_version "")
    set(version_py "${arg_INCLUDE_DIR}/../../version.py")
    if(EXISTS "${version_py}")
        file(STRINGS "${version_py}" version_lines REGEX "^version[ \t]*=")
        if(version_lines MATCHES "^version[ \t]*=[ \t]*[\"']([^\"']+)[\"']")
            set(package_version "${CMAKE_MATCH_1}")
        endif()
    endif()

    set(sbom_args "")
    if(package_version)
        qfp_sbom_compute_cpe(cpe VENDOR "numpy" PRODUCT "numpy" VERSION "${package_version}")
        list(APPEND sbom_args
            PACKAGE_VERSION "${package_version}"
            CPE "${cpe}"
            PURL_VALUES "pkg:pypi/numpy@${package_version}"
        )
    endif()

    _qt_internal_sbom_get_root_project_name_lower_case(project_name)
    set(numpy_target "${project_name}SbomNumPy")

    qfp_add_sbom("${numpy_target}"
        SBOM_ENTITY_TYPE SYSTEM_LIBRARY
        FRIENDLY_PACKAGE_NAME "NumPy"
        SBOM_PACKAGE_COMMENT
            "Only the NumPy C API headers are used at build time. NumPy is optional at runtime."
        ${sbom_args}
        SBOM_RELATIONSHIP_ENTRIES
            SBOM_RELATIONSHIP_ENTRY
                SBOM_RELATIONSHIP_FROM "${numpy_target}"
                SBOM_RELATIONSHIP_TYPE BUILD_DEPENDENCY_OF
                SBOM_RELATIONSHIP_TO "${target}"
    )
endfunction()

# Records the TARGETS of a found 3rd party package as system libraries. An SBOM package is
# created for each of them in the current SBOM document, but only if an SBOM-enabled target
# links to it.
function(qfp_sbom_record_system_library package_name)
    set(opt_args "")
    set(single_args "")
    set(multi_args
        TARGETS
    )
    cmake_parse_arguments(PARSE_ARGV 1 arg "${opt_args}" "${single_args}" "${multi_args}")
    qfp_validate_all_args_are_parsed(arg)

    if(NOT QT_GENERATE_SBOM)
        return()
    endif()

    set(sbom_args "")
    if(${package_name}_VERSION)
        list(APPEND sbom_args PACKAGE_VERSION "${${package_name}_VERSION}")
    endif()

    foreach(target IN LISTS arg_TARGETS)
        if(NOT TARGET "${target}")
            continue()
        endif()

        _qt_internal_sbom_record_system_library_usage("${target}"
            SBOM_ENTITY_TYPE SYSTEM_LIBRARY
            FRIENDLY_PACKAGE_NAME "${package_name}"
            ${sbom_args}
        )
    endforeach()
endfunction()

# Forwarding wrappers that should be used in CMakeLists.txt files.
# No-ops in case when sbom generation is not enabled, or when an older
# Qt version that doesn't have sbom support is used.

# Function to create a custom SBOM target that is not backed by a library / executable.
# Useful for things like translations, resources, etc.
function(qfp_add_sbom)
    if(NOT QT_GENERATE_SBOM)
        return()
    endif()

    qfp_sbom_assert_no_license_expression(${ARGN})
    _qt_internal_add_sbom(${ARGN})
endfunction()

# Creates an SBOM target for installed files that are not part of a library or executable target,
# with the same default copyright, supplier, version, download location and PURLs as Qt for Python
# libraries and executables. The license has to be passed explicitly.
function(qfp_add_custom_sbom target)
    if(NOT QT_GENERATE_SBOM)
        return()
    endif()

    qfp_sbom_add_sbom_entity_type_to_args(args_to_forward
        DEFAULT_SBOM_ENTITY_TYPE CUSTOM_NO_INFIX
        ${ARGN}
    )
    _qt_internal_add_sbom("${target}" ${args_to_forward})

    # Needs the target to exist.
    qfp_sbom_get_default_sbom_args("${target}" sbom_extra_args ${args_to_forward})
    _qt_internal_extend_sbom("${target}" ${sbom_extra_args})
endfunction()

# Function to add files to an SBOM-enabled target.
function(qfp_sbom_add_files)
    if(NOT QT_GENERATE_SBOM)
        return()
    endif()

    qfp_sbom_assert_no_license_expression(${ARGN})
    _qt_internal_sbom_add_files(${ARGN})
endfunction()

# Adds the files of a directory installed via install(DIRECTORY <DIRECTORY> DESTINATION
# <INSTALL_PATH>) to an SBOM-enabled target, preserving the subdirectory structure.
# Like install(DIRECTORY), a DIRECTORY without a trailing slash is installed as
# <INSTALL_PATH>/<dir name>/..., while a trailing slash installs only its contents into
# <INSTALL_PATH>/...
# Like install(DIRECTORY), a relative DIRECTORY is resolved against the current source dir.
#
# FILE_TYPE - the file type of all added files. To add files of different types from the same
#   directory, call the function once per type, with complementary INCLUDE_REGEX / EXCLUDE_REGEX.
# INCLUDE_REGEX - only files whose path relative to DIRECTORY matches are added.
# EXCLUDE_REGEX - files whose path relative to DIRECTORY matches are skipped. Can mirror
#   install(DIRECTORY) exclusions.
function(qfp_sbom_add_directory_files target)
    if(NOT QT_GENERATE_SBOM)
        return()
    endif()

    set(opt_args "")
    set(single_args
        DIRECTORY
        INSTALL_PATH
        FILE_TYPE
        INCLUDE_REGEX
        EXCLUDE_REGEX
    )
    set(multi_args "")
    cmake_parse_arguments(PARSE_ARGV 1 arg "${opt_args}" "${single_args}" "${multi_args}")
    qfp_validate_all_args_are_parsed(arg)

    foreach(required_arg IN ITEMS DIRECTORY INSTALL_PATH FILE_TYPE)
        if(NOT arg_${required_arg})
            message(FATAL_ERROR "${required_arg} must be set")
        endif()
    endforeach()

    string(REGEX REPLACE "/+$" "" source_dir "${arg_DIRECTORY}")
    if(NOT IS_ABSOLUTE "${source_dir}")
        set(source_dir "${CMAKE_CURRENT_SOURCE_DIR}/${source_dir}")
    endif()
    if(arg_DIRECTORY MATCHES "/$")
        set(destination "${arg_INSTALL_PATH}")
    else()
        get_filename_component(directory_name "${source_dir}" NAME)
        set(destination "${arg_INSTALL_PATH}/${directory_name}")
    endif()

    file(GLOB_RECURSE directory_files "${source_dir}/*")

    # install(FILES) only keeps the file name when installing, so add each file with its own
    # install path, to mirror the subdirectory structure created by install(DIRECTORY).
    foreach(file_path IN LISTS directory_files)
        file(RELATIVE_PATH relative_file_path "${source_dir}" "${file_path}")

        if(arg_INCLUDE_REGEX AND NOT relative_file_path MATCHES "${arg_INCLUDE_REGEX}")
            continue()
        endif()
        if(arg_EXCLUDE_REGEX AND relative_file_path MATCHES "${arg_EXCLUDE_REGEX}")
            continue()
        endif()

        get_filename_component(relative_dir "${relative_file_path}" DIRECTORY)
        set(install_path "${destination}")
        if(relative_dir)
            string(APPEND install_path "/${relative_dir}")
        endif()

        qfp_sbom_add_files(${target}
            FILES "${file_path}"
            FILE_TYPE "${arg_FILE_TYPE}"
            INSTALL_PATH "${install_path}"
        )
    endforeach()
endfunction()

# Function to add additional SBOM related options to an SBOM-enabled target.
function(qfp_extend_sbom)
    if(NOT QT_GENERATE_SBOM)
        return()
    endif()

    qfp_extend_qfp_entity_sbom(${ARGN})
endfunction()

# Function to add additional SBOM related options to targets provided by a package found with
# qfp_find_package.
function(qfp_find_package_extend_sbom)
    if(NOT QT_GENERATE_SBOM)
        return()
    endif()

    _qt_find_package_extend_sbom(${ARGN})
endfunction()
