# Copyright (C) 2023 The Qt Company Ltd.
# SPDX-License-Identifier: BSD-3-Clause

Include(icecc.cmake)

cmake_minimum_required(VERSION 3.25)
cmake_policy(VERSION 3.22)

set(CMAKE_BUILD_TYPE Release CACHE STRING "Build Type")

include(".cmake.conf")
project(shiboken6)

include(cmake/ShibokenSetup.cmake)

qfp_setup_sbom()

get_filename_component(qfp_license_dir_path "${CMAKE_CURRENT_SOURCE_DIR}/../../LICENSES" REALPATH)
qfp_sbom_begin_project(
    LICENSE_DIR_PATHS "${qfp_license_dir_path}"
    INSTALL_SBOM_DIR "share/sbom/shiboken6"
    PYPI_PURL_NAME "shiboken6"
)

get_rpath_base_token(base)

set(CMAKE_INSTALL_RPATH ${base}/)

add_subdirectory(libshiboken) # Uses Python
add_subdirectory(shibokenmodule) # Uses libshiboken
add_subdirectory(data)

qfp_sbom_end_project()

if(BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
