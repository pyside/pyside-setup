# Copyright (C) 2023 The Qt Company Ltd.
# SPDX-License-Identifier: BSD-3-Clause

find_package(Qt6 COMPONENTS Core CorePrivate)

set(libpyside_libraries Qt::Core Qt::CorePrivate Qt::PlatformCommonInternal)

set(CMAKE_AUTOMOC ON)

set(libpyside_HEADERS # installed below
    class_property.h
    dynamicqmetaobject.h
    dynamicslot_p.h
    feature_select.h
    pyobjectwrapper.h
    pysideclassdecorator_p.h
    pysideclassinfo.h
    pysideclassinfo_p.h
    pysidecleanup.h
    pyside.h
    pysideinit.h
    pysidelogging_p.h
    pysidemacros.h
    pysidemetafunction.h
    pysidemetafunction_p.h
    pysidemetatype.h
    pyside_numpy.h
    pyside_p.h
    pysideglobals_p.h
    pysideproperty.h
    pysidepropertybase_p.h
    pysideproperty_p.h
    pysideqapp.h
    pysideqenum.h
    pysideqhash.h
    pysideqobject.h pysideqobject_p.h
    pysideqslotobject_p.h
    pysidesignal.h
    pysidesignal_p.h
    pysideslot_p.h
    pysidestaticstrings.h
    pysideutils.h
    pysidevariantutils.h
    pysideweakref.h
    qobjectconnect.h
    signalmanager.h signalmanager_p.h
)

set(libpyside_SRC
    class_property.cpp
    dynamicqmetaobject.cpp
    dynamicslot.cpp
    feature_select.cpp
    signalmanager.cpp
    pyobjectwrapper.cpp
    pysideclassdecorator.cpp
    pysideclassinfo.cpp
    pysideglobals.cpp
    pysideqenum.cpp
    pysideqslotobject_p.cpp
    pysidemetafunction.cpp
    pysidesignal.cpp
    pysideslot.cpp
    pysideproperty.cpp
    pysideutils.cpp
    pysidevariantutils.cpp
    pysideweakref.cpp
    pyside.cpp
    pyside_numpy.cpp
    pysidestaticstrings.cpp
    qobjectconnect.cpp
    ${libpyside_HEADERS}
)

qt6_add_resources(libpyside_SRC libpyside.qrc)

# Add python files to project explorer in Qt Creator, when opening the CMakeLists.txt as a project,
# so you can look up python files with the Locator.
macro(add_other_files)
     foreach(_it ${ARGN})
         if(NOT IS_DIRECTORY ${_it})
             get_filename_component(name ${_it} NAME)
             if(NOT ${_it} MATCHES "^/\\\\..*$;~$")
                 set_source_files_properties(${_it} PROPERTIES HEADER_FILE_ONLY TRUE)
             endif()
         endif()
     endforeach()
endmacro()

# Test files.
file(GLOB_RECURSE pyside_folder_py_files "../*.py")

# Mostly for setup.py.
file(GLOB setup_folder_py_files "../../../*.py")

set(other_files ${pyside_folder_py_files} ${setup_folder_py_files})
add_other_files(${other_files})

if(IOS)
    set(library_type STATIC)
else()
    set(library_type SHARED)
endif()

qfp_sbom_get_python_runtime_target(python_runtime_sbom_target)
qfp_add_library(pyside6
    FUNCTION_ARGS
        ${library_type} ${libpyside_SRC} ${other_files}
    RUNTIME_PATH bin
    LIBRARY_PATH "${LIB_INSTALL_DIR}"
    ARCHIVE_PATH "${LIB_INSTALL_DIR}"
    SBOM_ARGS
        SBOM_DEPENDENCIES ${python_runtime_sbom_target}
)

add_library(PySide6::pyside6 ALIAS pyside6)

#does nothing if QFP_NO_OVERRIDE_OPTIMIZATION_FLAGS (no-size-optimization) flag is not set
append_size_optimization_flags(pyside6)

target_include_directories(pyside6 PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
)

target_compile_definitions(pyside6 PRIVATE -DQT_LEAN_HEADERS=1 -DQT_NO_KEYWORDS=1)

target_link_libraries(pyside6
                      PRIVATE Shiboken6::libshiboken ${libpyside_libraries})

set(set_props_args DEFINE_SYMBOL BUILD_LIBPYSIDE)
if(IOS)
    list(APPEND set_props_args OUTPUT_NAME "pyside6${pyside6_SUFFIX}")
else()
    list(APPEND set_props_args
        VERSION ${BINDING_API_VERSION}
        SOVERSION "${PYSIDE_SO_VERSION}"
        OUTPUT_NAME
            "pyside6${pyside6_SUFFIX}${SHIBOKEN_PYTHON_SHARED_LIBRARY_SUFFIX}"
    )
endif()
set_target_properties(pyside6 PROPERTIES ${set_props_args})

set_property(TARGET pyside6 PROPERTY CXX_STANDARD 17)

if(PYSIDE_QT_CONF_PREFIX)
    set_property(SOURCE pyside.cpp
                 APPEND
                 PROPERTY COMPILE_DEFINITIONS
                 PYSIDE_QT_CONF_PREFIX=${PYSIDE_QT_CONF_PREFIX})
endif()

#
# install stuff
#

if (CMAKE_BUILD_TYPE STREQUAL "Debug")
    set(LIBRARY_OUTPUT_SUFFIX ${CMAKE_DEBUG_POSTFIX})
else()
    set(LIBRARY_OUTPUT_SUFFIX ${CMAKE_RELEASE_POSTFIX})
endif()

set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -D QT_NO_CAST_FROM_ASCII -D QT_NO_CAST_TO_ASCII")

qfp_strip_library("pyside6")

# create pkg-config file
configure_file("${CMAKE_CURRENT_SOURCE_DIR}/pyside6.pc.in"
               "${CMAKE_CURRENT_BINARY_DIR}/pyside6${pyside6_SUFFIX}.pc" @ONLY)

# documentation
set(libpyside_qdocconf "${CMAKE_CURRENT_BINARY_DIR}/libpyside.qdocconf")

configure_file("${CMAKE_CURRENT_SOURCE_DIR}/libpyside.qdocconf.in" "${libpyside_qdocconf}" @ONLY)

add_custom_target(libpyside_qdoc
                  COMMAND qdoc "${libpyside_qdocconf}"
                  COMMENT "Generating libpyside documentation"
                  VERBATIM)

# for creating cmake-config files
include(CMakePackageConfigHelpers)

# Build-tree / super project package config file.
set(PYSIDE_PYTHONPATH "${PYTHON_SITE_PACKAGES}/PySide6")
set(PYSIDE_TYPESYSTEMS "${CMAKE_INSTALL_PREFIX}/share/PySide6${pyside6_SUFFIX}/typesystems")
set(PYSIDE_GLUE "${CMAKE_INSTALL_PREFIX}/share/PySide6${pyside6_SUFFIX}/glue")

configure_package_config_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/PySide6Config-spec.cmake.in"
    "${CMAKE_CURRENT_BINARY_DIR}/PySide6Config${SHIBOKEN_PYTHON_CONFIG_SUFFIX}.cmake"
    INSTALL_DESTINATION "${CMAKE_CURRENT_BINARY_DIR}"
    PATH_VARS PYSIDE_PYTHONPATH PYSIDE_TYPESYSTEMS PYSIDE_GLUE
    INSTALL_PREFIX "${CMAKE_CURRENT_BINARY_DIR}"
)

# Install-tree configuration (for downstream CMake projects using a regular cmake install)
set(PYSIDE_PYTHONPATH "${PYTHON_SITE_PACKAGES}/PySide6")
set(PYSIDE_TYPESYSTEMS "${CMAKE_INSTALL_PREFIX}/share/PySide6${pyside6_SUFFIX}/typesystems")
set(PYSIDE_GLUE "${CMAKE_INSTALL_PREFIX}/share/PySide6${pyside6_SUFFIX}/glue")
set(PYSIDE_SOVERSION "${pyside6_library_so_version}")

configure_package_config_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/PySide6Config-spec.cmake.in"
    "${CMAKE_CURRENT_BINARY_DIR}/install/PySide6Config${SHIBOKEN_PYTHON_CONFIG_SUFFIX}.cmake"
    INSTALL_DESTINATION "${LIB_INSTALL_DIR}/cmake/PySide6"
    PATH_VARS PYSIDE_PYTHONPATH PYSIDE_TYPESYSTEMS PYSIDE_GLUE
)

# Wheel configuration (typesystems/glue sit directly inside the PySide6 Python package)
set(PYSIDE_PYTHONPATH "")
set(PYSIDE_TYPESYSTEMS "typesystems")
set(PYSIDE_GLUE "glue")

configure_package_config_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/PySide6Config-spec.cmake.in"
    "${CMAKE_CURRENT_BINARY_DIR}/wheel/PySide6Config${SHIBOKEN_PYTHON_CONFIG_SUFFIX}.cmake"
    INSTALL_DESTINATION "${LIB_INSTALL_DIR}/cmake/PySide6"
    PATH_VARS PYSIDE_PYTHONPATH PYSIDE_TYPESYSTEMS PYSIDE_GLUE
)

configure_file("${CMAKE_CURRENT_SOURCE_DIR}/PySide6Config.cmake.in"
               "${CMAKE_CURRENT_BINARY_DIR}/PySide6Config.cmake" @ONLY)
configure_file("${CMAKE_CURRENT_SOURCE_DIR}/PySide6ConfigVersion.cmake.in"
               "${CMAKE_CURRENT_BINARY_DIR}/PySide6ConfigVersion.cmake" @ONLY)

install(FILES ${libpyside_HEADERS}
        DESTINATION include/${BINDING_NAME}${pyside6_SUFFIX})
qfp_sbom_add_files(pyside6
    FILES ${libpyside_HEADERS}
    FILE_TYPE "SOURCE"
    INSTALL_PATH "include/${BINDING_NAME}${pyside6_SUFFIX}"
)

# build-time installation
install(TARGETS pyside6 EXPORT PySide6Targets
                        LIBRARY DESTINATION "${LIB_INSTALL_DIR}"
                        ARCHIVE DESTINATION "${LIB_INSTALL_DIR}"
                        RUNTIME DESTINATION bin
                        INCLUDES DESTINATION "include/PySide6")

# wheel installation
set_target_properties(pyside6 PROPERTIES
    VERSION ${PYSIDE_SOVERSION})

if(NOT is_pyside6_superproject_build)
    install(TARGETS pyside6 EXPORT PySide6WheelTargets
                            LIBRARY DESTINATION "PySide6"
                            ARCHIVE DESTINATION "PySide6"
                            RUNTIME DESTINATION "PySide6"
                            INCLUDES DESTINATION "PySide6/include/PySide6")
    # The SBOM package of pyside6 only covers the copy installed into LIB_INSTALL_DIR.
    set(pyside6_wheel_files "$<TARGET_FILE_NAME:pyside6>")
    if(WIN32)
        list(APPEND pyside6_wheel_files "$<TARGET_LINKER_FILE_NAME:pyside6>")
    endif()
    qfp_sbom_add_files(pyside6
        FILES ${pyside6_wheel_files}
        FILE_TYPE "BINARY"
        INSTALL_PATH "PySide6"
    )

    install(FILES "${CMAKE_CURRENT_BINARY_DIR}/wheel/PySide6Config${SHIBOKEN_PYTHON_CONFIG_SUFFIX}.cmake"
            DESTINATION "${LIB_INSTALL_DIR}/wheels/cmake/PySide6")
endif()

install(FILES "${CMAKE_CURRENT_BINARY_DIR}/pyside6${pyside6_SUFFIX}.pc"
        DESTINATION "${LIB_INSTALL_DIR}/pkgconfig")

install(FILES "${CMAKE_CURRENT_BINARY_DIR}/PySide6Config.cmake"
        DESTINATION "${LIB_INSTALL_DIR}/cmake/PySide6")

install(FILES "${CMAKE_CURRENT_BINARY_DIR}/install/PySide6Config${SHIBOKEN_PYTHON_CONFIG_SUFFIX}.cmake"
        DESTINATION "${LIB_INSTALL_DIR}/cmake/PySide6")

install(FILES "${CMAKE_CURRENT_BINARY_DIR}/PySide6ConfigVersion.cmake"
        DESTINATION "${LIB_INSTALL_DIR}/cmake/PySide6")

qfp_sbom_get_build_system_license(build_system_license_expression)
qfp_add_custom_sbom(PySide6PackageConfigFiles
    LICENSE_CONCLUDED_EXPRESSION "${build_system_license_expression}"
)
qfp_sbom_add_files(PySide6PackageConfigFiles
    FILES
        "${CMAKE_CURRENT_BINARY_DIR}/PySide6Config.cmake"
        "${CMAKE_CURRENT_BINARY_DIR}/install/PySide6Config${SHIBOKEN_PYTHON_CONFIG_SUFFIX}.cmake"
        "${CMAKE_CURRENT_BINARY_DIR}/PySide6ConfigVersion.cmake"
    FILE_TYPE "TEXT"
    INSTALL_PATH "${LIB_INSTALL_DIR}/cmake/PySide6"
)
qfp_sbom_add_files(PySide6PackageConfigFiles
    FILES "${CMAKE_CURRENT_BINARY_DIR}/pyside6${pyside6_SUFFIX}.pc"
    FILE_TYPE "TEXT"
    INSTALL_PATH "${LIB_INSTALL_DIR}/pkgconfig"
)

# The export files are installed by ../CMakeLists.txt.
qfp_sbom_get_export_file_names(pyside6_export_files PySide6Targets)
qfp_sbom_add_files(PySide6PackageConfigFiles
    FILES ${pyside6_export_files}
    FILE_TYPE "TEXT"
    INSTALL_PATH "${LIB_INSTALL_DIR}/cmake/PySide6"
)
if(NOT is_pyside6_superproject_build)
    qfp_sbom_add_files(PySide6PackageConfigFiles
        FILES
            "${CMAKE_CURRENT_BINARY_DIR}/wheel/PySide6Config${SHIBOKEN_PYTHON_CONFIG_SUFFIX}.cmake"
            ${pyside6_export_files}
        FILE_TYPE "TEXT"
        INSTALL_PATH "${LIB_INSTALL_DIR}/wheels/cmake/PySide6"
    )
endif()
