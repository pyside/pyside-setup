# Copyright (C) 2023 The Qt Company Ltd.
# SPDX-License-Identifier: BSD-3-Clause

# Configure include based on platform
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

foreach(shortname IN LISTS all_module_shortnames)
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
        set(module_header_content "${module_header_content}\n#include <Qt${shortname}/Qt${shortname}>")
    endif()
    set(post_header "${CMAKE_CURRENT_BINARY_DIR}/Qt${shortname}/Qt${shortname}_global.post.h")
    if(EXISTS "${post_header}")
        file(READ "${post_header}" post_header_content)
        set(module_header_content "${module_header_content}\n${post_header_content}")
    endif()
    file(WRITE ${module_header} "${module_header_content}")
endforeach()

# install
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
install(FILES ${CMAKE_CURRENT_SOURCE_DIR}/templates/datavisualization_common.xml
        DESTINATION share/PySide6${pyside_SUFFIX}/typesystems)
install(FILES ${CMAKE_CURRENT_SOURCE_DIR}/pyside6_global.h
        DESTINATION include/${BINDING_NAME}${pyside6_SUFFIX})

if(NOT DISABLE_QtQmlFeatures)
    add_subdirectory(QtQmlFeatures)
endif()

install(DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/QtAsyncio"
        DESTINATION "${PYTHON_SITE_PACKAGES}/${BINDING_NAME}${pyside6_SUFFIX}")
