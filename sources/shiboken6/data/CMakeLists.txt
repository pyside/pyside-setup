# Copyright (C) 2023 The Qt Company Ltd.
# SPDX-License-Identifier: BSD-3-Clause

if (CMAKE_BUILD_TYPE STREQUAL "Debug")
    set(LIBRARY_OUTPUT_SUFFIX ${CMAKE_DEBUG_POSTFIX})
else()
    set(LIBRARY_OUTPUT_SUFFIX ${CMAKE_RELEASE_POSTFIX})
endif()

include(CMakePackageConfigHelpers)

# Build-tree / super project package config file.
configure_package_config_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/Shiboken6Config-spec.cmake.in"
    "${CMAKE_CURRENT_BINARY_DIR}/Shiboken6Config${PYTHON_CONFIG_SUFFIX}.cmake"
     INSTALL_DESTINATION "${CMAKE_CURRENT_BINARY_DIR}"
     INSTALL_PREFIX "${CMAKE_CURRENT_BINARY_DIR}"
)

# Install-tree / relocatable package config file.
configure_package_config_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/Shiboken6Config-spec.cmake.in"
    "${CMAKE_CURRENT_BINARY_DIR}/install/Shiboken6Config${PYTHON_CONFIG_SUFFIX}.cmake"
     INSTALL_DESTINATION "${LIB_INSTALL_DIR}/cmake/Shiboken6"
)

configure_file("${CMAKE_CURRENT_SOURCE_DIR}/Shiboken6Config.cmake.in"
               "${CMAKE_CURRENT_BINARY_DIR}/Shiboken6Config.cmake" @ONLY)
configure_file("${CMAKE_CURRENT_SOURCE_DIR}/Shiboken6ConfigVersion.cmake.in"
               "${CMAKE_CURRENT_BINARY_DIR}/Shiboken6ConfigVersion.cmake" @ONLY)
configure_file("${CMAKE_CURRENT_SOURCE_DIR}/shiboken6.pc.in"
               "${CMAKE_CURRENT_BINARY_DIR}/shiboken6${shiboken6_SUFFIX}.pc" @ONLY)

install(FILES "${CMAKE_CURRENT_BINARY_DIR}/Shiboken6Config.cmake"
        DESTINATION "${LIB_INSTALL_DIR}/cmake/Shiboken6")

install(FILES "${CMAKE_CURRENT_BINARY_DIR}/install/Shiboken6Config${PYTHON_CONFIG_SUFFIX}.cmake"
        DESTINATION "${LIB_INSTALL_DIR}/cmake/Shiboken6")

install(FILES "${CMAKE_CURRENT_BINARY_DIR}/Shiboken6ConfigVersion.cmake"
        DESTINATION "${LIB_INSTALL_DIR}/cmake/Shiboken6")

install(FILES "${CMAKE_CURRENT_SOURCE_DIR}/../cmake/ShibokenHelpers.cmake"
        DESTINATION "${LIB_INSTALL_DIR}/cmake/Shiboken6")

install(FILES "${CMAKE_CURRENT_BINARY_DIR}/shiboken6${shiboken6_SUFFIX}.pc"
        DESTINATION "${LIB_INSTALL_DIR}/pkgconfig")

qfp_sbom_get_build_system_license(build_system_license_expression)
qfp_add_custom_sbom(Shiboken6PackageConfigFiles
    LICENSE_CONCLUDED_EXPRESSION "${build_system_license_expression}"
)
qfp_sbom_add_files(Shiboken6PackageConfigFiles
    FILES
        "${CMAKE_CURRENT_BINARY_DIR}/Shiboken6Config.cmake"
        "${CMAKE_CURRENT_BINARY_DIR}/install/Shiboken6Config${PYTHON_CONFIG_SUFFIX}.cmake"
        "${CMAKE_CURRENT_BINARY_DIR}/Shiboken6ConfigVersion.cmake"
        "${CMAKE_CURRENT_SOURCE_DIR}/../cmake/ShibokenHelpers.cmake"
    FILE_TYPE "TEXT"
    INSTALL_PATH "${LIB_INSTALL_DIR}/cmake/Shiboken6"
)
qfp_sbom_add_files(Shiboken6PackageConfigFiles
    FILES "${CMAKE_CURRENT_BINARY_DIR}/shiboken6${shiboken6_SUFFIX}.pc"
    FILE_TYPE "TEXT"
    INSTALL_PATH "${LIB_INSTALL_DIR}/pkgconfig"
)

# The export files are installed by libshiboken/CMakeLists.txt.
qfp_sbom_get_export_file_names(shiboken6_export_files Shiboken6Targets)
qfp_sbom_add_files(Shiboken6PackageConfigFiles
    FILES ${shiboken6_export_files}
    FILE_TYPE "TEXT"
    INSTALL_PATH "${LIB_INSTALL_DIR}/cmake/Shiboken6"
)
if(NOT is_pyside6_superproject_build)
    qfp_sbom_add_files(Shiboken6PackageConfigFiles
        FILES ${shiboken6_export_files}
        FILE_TYPE "TEXT"
        INSTALL_PATH "${LIB_INSTALL_DIR}/wheels/cmake/Shiboken6"
    )
endif()
