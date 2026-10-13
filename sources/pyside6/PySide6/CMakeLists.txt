# Copyright (C) 2023 The Qt Company Ltd.
# SPDX-License-Identifier: BSD-3-Clause

# Configure include based on platform
if(SHIBOKEN_PYTHON_FREE_THREADED)
    # Shiboken keeps the module dict, so __all__ is computed at import.
    set(PYSIDE_FT_INIT_ALL "__all__ = _find_all_qt_modules()\n")
endif()
configure_file("${CMAKE_CURRENT_SOURCE_DIR}/__init__.py.in"
               "${CMAKE_CURRENT_BINARY_DIR}/__init__.py" @ONLY)
configure_file("${CMAKE_CURRENT_SOURCE_DIR}/_config.py.in"
               "${CMAKE_CURRENT_BINARY_DIR}/_config.py" @ONLY)
configure_file("${CMAKE_CURRENT_SOURCE_DIR}/__feature__.pyi.in"
               "${CMAKE_CURRENT_BINARY_DIR}/__feature__.pyi" @ONLY)
# typing support for mypy
configure_file("${CMAKE_CURRENT_SOURCE_DIR}/py.typed.in"
               "${CMAKE_CURRENT_BINARY_DIR}/py.typed" @ONLY)

# Use absolute path instead of relative path, to avoid ninja build errors due to
# duplicate file dependency inconsistency.
set(pyside_version_relative_path "${CMAKE_CURRENT_SOURCE_DIR}/../pyside_version.py")
get_filename_component(pyside_version_path ${pyside_version_relative_path} ABSOLUTE)
configure_file("${pyside_version_path}"
               "${CMAKE_CURRENT_BINARY_DIR}/_git_pyside_version.py" @ONLY)

# qt.conf needs to be placed next to QtWebEngineProcess so that the executable uses the correct
# Prefix location leading to an existing icu data file. It is needed on Windows, Linux, and macOS
# non-framework build. In framework build, instead of using qt.conf, Bundle querying is used.
if (WIN32 OR (UNIX AND NOT APPLE) OR (APPLE AND NOT QtCore_is_framework))

    if (WIN32)
        # On Windows, the QtWebEngineProcess is directly located in the Prefix, due to not using
        # a "Qt" subfolder like on the other platforms.
        set(QT_CONF_PREFIX ".")
    else()
        # On Linux and non-framework macOS, the QtWebEngineProcess is in ./libexec, so prefix is one
        # level higher.
        set(QT_CONF_PREFIX "..")
    endif()

    configure_file("${CMAKE_CURRENT_SOURCE_DIR}/qt.conf.in"
                   "${CMAKE_CURRENT_BINARY_DIR}/qt.conf" @ONLY)
endif()

configure_file("${CMAKE_CURRENT_SOURCE_DIR}/support/__init__.py"
               "${CMAKE_CURRENT_BINARY_DIR}/support/__init__.py" COPYONLY)
configure_file("${CMAKE_CURRENT_SOURCE_DIR}/support/generate_pyi.py"
               "${CMAKE_CURRENT_BINARY_DIR}/support/generate_pyi.py" COPYONLY)
configure_file("${CMAKE_CURRENT_SOURCE_DIR}/support/deprecated.py"
               "${CMAKE_CURRENT_BINARY_DIR}/support/deprecated.py" COPYONLY)
configure_file("${CMAKE_CURRENT_SOURCE_DIR}/support/auto_property_helper.py"
               "${CMAKE_CURRENT_BINARY_DIR}/support/auto_property_helper.py" COPYONLY)
configure_file("${CMAKE_CURRENT_SOURCE_DIR}/support/qml_component_helper.py"
               "${CMAKE_CURRENT_BINARY_DIR}/support/qml_component_helper.py" COPYONLY)

# Additional (non-Qt) modules implemented in PySide only
foreach(mod IN LISTS PURE_PYTHON_MODULES)
    set(src_dir "${CMAKE_CURRENT_SOURCE_DIR}/Qt${mod}")
    set(dst_dir "${CMAKE_CURRENT_BINARY_DIR}/Qt${mod}")

    if(EXISTS "${src_dir}")
        file(GLOB_RECURSE module_files "${src_dir}/*")

        foreach(f ${module_files})
            file(RELATIVE_PATH relpath "${src_dir}" "${f}")
            set(dst "${dst_dir}/${relpath}")
            configure_file("${f}" "${dst}" COPYONLY)
        endforeach()
    endif()
endforeach()

# now compile all modules.
file(READ "${CMAKE_CURRENT_LIST_DIR}/pyside6_global.h" pyside6_global_contents)

# Process a single Qt module.
# Add its subdirectory when the module is found and generate its per-module global header.
macro(process_pyside_module shortname)
    set(name "Qt${QT_MAJOR_VERSION}${shortname}")
    set(_qt_module_name "${name}")
    if ("${shortname}" STREQUAL "OpenGLFunctions")
        set(_qt_module_name "Qt${QT_MAJOR_VERSION}Gui")
    endif()
    HAS_QT_MODULE(${_qt_module_name}_FOUND Qt${shortname})

    # Create a module header consisting of pyside6_global.h and the module
    # include. Note: The contents of pyside6_global.h must be copied instead of
    # just #including it since shiboken will otherwise generate an
    # #include <pyside6_global.h> for the the type entries originating from it
    # (cf AbstractMetaBuilderPrivate::setInclude()).
    set(module_header "${CMAKE_CURRENT_BINARY_DIR}/Qt${shortname}_global.h")
    set(module_header_content "")
    set(pre_header "${CMAKE_CURRENT_BINARY_DIR}/Qt${shortname}/Qt${shortname}_global.pre.h")
    if(EXISTS "${pre_header}")
        file(READ "${pre_header}" pre_header_content)
        set(module_header_content "${pre_header_content}")
    endif()
    set(module_header_content "${module_header_content}\n${pyside6_global_contents}")
    # AxContainer/AxServer from Active Qt do not have module headers
    if(NOT "${shortname}" STREQUAL "AxContainer" AND NOT "${shortname}" STREQUAL "OpenGLFunctions")
        set(module_header_content
            "${module_header_content}\n#include <Qt${shortname}/Qt${shortname}>")
    endif()
    set(post_header "${CMAKE_CURRENT_BINARY_DIR}/Qt${shortname}/Qt${shortname}_global.post.h")
    if(EXISTS "${post_header}")
        file(READ "${post_header}" post_header_content)
        set(module_header_content "${module_header_content}\n${post_header_content}")
    endif()
    file(WRITE ${module_header} "${module_header_content}")
endmacro()

# Collects modules that are not assigned to any of the wheel SBOM documents.
set(pyside_unsassigned_sbom_modules "")

# Process the modules in one pass per wheel SBOM document - essentials first, then addons, then
# webengine, etc, so each module's target is recorded into the matching wheel SBOM document.
# Each pass iterates all_module_shortnames in its original order, so the add_subdirectory ordering
# is preserved within a pass.
foreach(shortname IN LISTS all_module_shortnames)
    if(shortname IN_LIST PYSIDE_ESSENTIALS_WHEEL_MODULES)
        process_pyside_module("${shortname}")
    endif()
endforeach()

# Added here so that it is recorded in the essentials SBOM document.
if(NOT DISABLE_QtQmlFeatures)
    add_subdirectory(QtQmlFeatures)
endif()

# The package files are installed here, so that they are part of the essentials SBOM document.
# Their install rules need to come before the end of the document, because the SBOM is generated
# at that point during installation.
install(FILES "${CMAKE_CURRENT_BINARY_DIR}/__init__.py"
              "${CMAKE_CURRENT_BINARY_DIR}/_config.py"
              "${CMAKE_CURRENT_BINARY_DIR}/_git_pyside_version.py"
              "${CMAKE_CURRENT_BINARY_DIR}/py.typed"
              "${CMAKE_CURRENT_BINARY_DIR}/__feature__.pyi"
        DESTINATION "${PYTHON_SITE_PACKAGES}/${BINDING_NAME}${pyside6_SUFFIX}")
install(DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/support"
        DESTINATION "${PYTHON_SITE_PACKAGES}/${BINDING_NAME}${pyside6_SUFFIX}")
install(FILES ${CMAKE_CURRENT_SOURCE_DIR}/templates/common.xml
        DESTINATION share/PySide6${pyside_SUFFIX}/typesystems)
install(FILES ${CMAKE_CURRENT_SOURCE_DIR}/templates/core_common.xml
        DESTINATION share/PySide6${pyside_SUFFIX}/typesystems)
install(FILES ${CMAKE_CURRENT_SOURCE_DIR}/templates/gui_common.xml
        DESTINATION share/PySide6${pyside_SUFFIX}/typesystems)
install(FILES ${CMAKE_CURRENT_SOURCE_DIR}/templates/widgets_common.xml
        DESTINATION share/PySide6${pyside_SUFFIX}/typesystems)
install(FILES ${CMAKE_CURRENT_SOURCE_DIR}/pyside6_global.h
        DESTINATION include/${BINDING_NAME}${pyside6_SUFFIX})

qfp_sbom_get_open_source_permissive_library_license(package_files_license_expression)
qfp_sbom_get_relative_python_site_packages(relative_python_site_packages)
set(package_install_path "${relative_python_site_packages}/${BINDING_NAME}${pyside6_SUFFIX}")
qfp_add_custom_sbom(PySide6PackageFiles
    LICENSE_CONCLUDED_EXPRESSION "${package_files_license_expression}"
)
qfp_sbom_add_files(PySide6PackageFiles
    FILES
        "${CMAKE_CURRENT_BINARY_DIR}/__init__.py"
        "${CMAKE_CURRENT_BINARY_DIR}/_config.py"
        "${CMAKE_CURRENT_BINARY_DIR}/_git_pyside_version.py"
        "${CMAKE_CURRENT_BINARY_DIR}/py.typed"
        "${CMAKE_CURRENT_BINARY_DIR}/__feature__.pyi"
    FILE_TYPE "SOURCE"
    INSTALL_PATH "${package_install_path}"
)
qfp_sbom_add_directory_files(PySide6PackageFiles
    DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/support"
    INSTALL_PATH "${package_install_path}"
    FILE_TYPE "SOURCE"
)
qfp_sbom_add_files(PySide6PackageFiles
    FILES
        ${CMAKE_CURRENT_SOURCE_DIR}/templates/common.xml
        ${CMAKE_CURRENT_SOURCE_DIR}/templates/core_common.xml
        ${CMAKE_CURRENT_SOURCE_DIR}/templates/gui_common.xml
        ${CMAKE_CURRENT_SOURCE_DIR}/templates/widgets_common.xml
    FILE_TYPE "SOURCE"
    INSTALL_PATH "share/PySide6${pyside_SUFFIX}/typesystems"
)
qfp_sbom_add_files(PySide6PackageFiles
    FILES ${CMAKE_CURRENT_SOURCE_DIR}/pyside6_global.h
    FILE_TYPE "SOURCE"
    INSTALL_PATH "include/${BINDING_NAME}${pyside6_SUFFIX}"
)

# Close the essentials document and open the addons one.
qfp_sbom_end_project()
qfp_sbom_begin_project(
    SBOM_PROJECT_NAME "PySide6-Addons"
    LICENSE_DIR_PATHS "${qfp_license_dir_path}"
    INSTALL_SBOM_DIR "share/sbom/PySide6-Addons"
    PYPI_PURL_NAME "pyside6-addons"
)

set(pyside_separate_sbom_wheel_modules "")
foreach(wheel IN LISTS PYSIDE_SEPARATE_SBOM_WHEELS)
    string(TOUPPER "${wheel}" wheel_upper)
    list(APPEND pyside_separate_sbom_wheel_modules ${PYSIDE_${wheel_upper}_WHEEL_MODULES})
endforeach()

foreach(shortname IN LISTS all_module_shortnames)
    if(NOT shortname IN_LIST PYSIDE_ESSENTIALS_WHEEL_MODULES
            AND NOT shortname IN_LIST pyside_separate_sbom_wheel_modules)
        process_pyside_module("${shortname}")

        if(NOT shortname IN_LIST PYSIDE_ADDONS_WHEEL_MODULES)
            list(APPEND pyside_unsassigned_sbom_modules "Qt${shortname}")
        endif()
    endif()
endforeach()

# QtAsyncio is part of the addons wheel, so install it while the addons document is open.
install(DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/QtAsyncio"
        DESTINATION "${PYTHON_SITE_PACKAGES}/${BINDING_NAME}${pyside6_SUFFIX}")
qfp_add_custom_sbom(QtAsyncio
    LICENSE_CONCLUDED_EXPRESSION "${package_files_license_expression}"
)
qfp_sbom_add_directory_files(QtAsyncio
    DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/QtAsyncio"
    INSTALL_PATH "${package_install_path}"
    FILE_TYPE "SOURCE"
)

# The template is shipped in the addons wheel with QtDataVisualization, see
# module_QtDataVisualization() in build_scripts/wheel_files.py.
install(FILES ${CMAKE_CURRENT_SOURCE_DIR}/templates/datavisualization_common.xml
        DESTINATION share/PySide6${pyside_SUFFIX}/typesystems)
qfp_add_custom_sbom(PySide6AddonsPackageFiles
    LICENSE_CONCLUDED_EXPRESSION "${package_files_license_expression}"
)
qfp_sbom_add_files(PySide6AddonsPackageFiles
    FILES ${CMAKE_CURRENT_SOURCE_DIR}/templates/datavisualization_common.xml
    FILE_TYPE "SOURCE"
    INSTALL_PATH "share/PySide6${pyside_SUFFIX}/typesystems"
)

foreach(wheel IN LISTS PYSIDE_SEPARATE_SBOM_WHEELS)
    string(TOUPPER "${wheel}" wheel_upper)
    string(TOLOWER "${wheel}" wheel_lower)
    qfp_sbom_end_project()
    qfp_sbom_begin_project(
        SBOM_PROJECT_NAME "PySide6-${wheel}"
        LICENSE_DIR_PATHS "${qfp_license_dir_path}"
        INSTALL_SBOM_DIR "share/sbom/PySide6-${wheel}"
        PYPI_PURL_NAME "pyside6-${wheel_lower}"
    )

    foreach(shortname IN LISTS all_module_shortnames)
        if(shortname IN_LIST PYSIDE_${wheel_upper}_WHEEL_MODULES)
            process_pyside_module("${shortname}")
        endif()
    endforeach()
endforeach()

# Report any built modules that are not assigned to any wheel.
if(QT_GENERATE_SBOM)
    if(pyside_unsassigned_sbom_modules)
        list(JOIN pyside_unsassigned_sbom_modules ", " pyside_unsassigned_sbom_modules_string)
        message(WARNING "PySide6 SBOM: modules that were incorrectly added to an SBOM document "
            "(absent from all wheel lists): ${pyside_unsassigned_sbom_modules_string}")
    else()
        message(STATUS
            "PySide6 SBOM: all built modules were added to their respective SBOM document.")
    endif()
endif()
