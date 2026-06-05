# Copyright (C) 2023 The Qt Company Ltd.
# SPDX-License-Identifier: BSD-3-Clause

include(cmake/Macros/icecc.cmake) # this must be the first line!

cmake_minimum_required(VERSION 3.25)
cmake_policy(VERSION 3.22)

include(".cmake.conf")
project(pysidebindings)

set(QT_NO_PRIVATE_MODULE_WARNING ON)

include(cmake/PySideSetup.cmake)

qfp_setup_sbom()

get_filename_component(qfp_license_dir_path "${CMAKE_CURRENT_SOURCE_DIR}/../../LICENSES" REALPATH)

# The PySide6 build creates one SBOM document per wheel: an essentials document (opened here,
# covering libpyside and the essential modules), and addons, webengine and pdf documents
# (opened mid-way through the module loop in PySide6/CMakeLists.txt).
qfp_sbom_begin_project(
    SBOM_PROJECT_NAME "PySide6-Essentials"
    LICENSE_DIR_PATHS "${qfp_license_dir_path}"
    INSTALL_SBOM_DIR "share/sbom/PySide6-Essentials"
    PYPI_PURL_NAME "pyside6-essentials"
)

get_rpath_base_token(base)

if (${STANDALONE})
    set(CMAKE_INSTALL_RPATH ${base}/ ${base}/Qt/lib ${base}/../shiboken6/)
else()
    set(CMAKE_INSTALL_RPATH ${base}/ ${QT6_INSTALL_PREFIX}/${QT6_INSTALL_LIBS} ${base}/../shiboken6/)
endif()

add_subdirectory(libpyside)

if(Qt${QT_MAJOR_VERSION}Qml_FOUND)
    add_subdirectory(libpysideqml)
endif()

if(Qt${QT_MAJOR_VERSION}RemoteObjects_FOUND)
    add_subdirectory(libpysideremoteobjects)
endif()

# build-time export set for PySide6 full build
install(EXPORT PySide6Targets
        NAMESPACE PySide6::
        DESTINATION "${LIB_INSTALL_DIR}/cmake/PySide6")

# wheel export set
if(NOT is_pyside6_superproject_build)
    install(EXPORT PySide6WheelTargets
            NAMESPACE PySide6::
            DESTINATION "${LIB_INSTALL_DIR}/wheels/cmake/PySide6"
            FILE PySide6Targets.cmake)
endif()

if(Qt${QT_MAJOR_VERSION}UiTools_FOUND)
    add_subdirectory(plugins/uitools)
    find_package(Qt6 COMPONENTS Designer)
    # PYSIDE-535: Enable when PyPy supports embedding
    if (Qt${QT_MAJOR_VERSION}Designer_FOUND AND NOT PYPY_VERSION)
        add_subdirectory(plugins/designer)
    endif()
endif()

add_subdirectory(PySide6)

qfp_sbom_end_project()

if(BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

add_subdirectory(doc)
