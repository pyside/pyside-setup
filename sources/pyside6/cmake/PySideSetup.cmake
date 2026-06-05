# Copyright (C) 2023 The Qt Company Ltd.
# SPDX-License-Identifier: BSD-3-Clause

# Don't ignore targets that do not exist, inside add_dependencies calls.
cmake_policy(SET CMP0046 NEW)

set(QT_MAJOR_VERSION 6)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_LIST_DIR}")
list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_LIST_DIR}/../../shiboken6/cmake")
list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_LIST_DIR}/../../shiboken6_generator/cmake")
list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_LIST_DIR}/Macros")

# TODO: Don't directly include, ShibokenHelpers but rather pick it up from the installed Shiboken
# package. Needs to support top-level build as well (Shiboken is not yet installed in that case).
include(ShibokenHelpers)
include(ShibokenGeneratorCMakeHelpers)
include(ShibokenGeneratorSbomHelpers)
include(ShibokenGeneratorTargetHelpers)
include(PySideHelpers)
include(PySideSbomWheelMapping)

if(CMAKE_SYSTEM_NAME STREQUAL "iOS")
    pyside_internal_get_ios_sdk_path(ios_sdk_path)
    set(PYSIDE_IOS_SDK_PATH "${ios_sdk_path}" CACHE STRING "iOS SDK path.")
endif()

#does nothing if QFP_NO_OVERRIDE_OPTIMIZATION_FLAGS (no-size-optimization) flag is not set
override_release_flags_for_size_optimization()

# Don't display "up-to-date / install" messages when installing, to reduce visual clutter.
if(QUIET_BUILD)
    set(CMAKE_INSTALL_MESSAGE NEVER)
endif()

# Override message not to display info messages when doing a quiet build.
if(QUIET_BUILD AND is_pyside6_superproject_build)
    function(message)
        list(GET ARGV 0 MessageType)
        if(MessageType STREQUAL FATAL_ERROR OR
            MessageType STREQUAL SEND_ERROR OR
            MessageType STREQUAL WARNING OR
            MessageType STREQUAL AUTHOR_WARNING)
                list(REMOVE_AT ARGV 0)
                _message(${MessageType} "${ARGV}")
        endif()
    endfunction()
endif()


pyside_internal_detect_if_cross_building()
pyside_internal_set_up_extra_dependency_paths()
pyside_internal_find_host_shiboken_tools()
find_package(Shiboken6 6 CONFIG REQUIRED)

if(is_pyside6_superproject_build)
    shiboken_find_required_python()
endif()

set(BINDING_API_MAJOR_VERSION "${pyside_MAJOR_VERSION}")
set(BINDING_API_MINOR_VERSION "${pyside_MINOR_VERSION}")
set(BINDING_API_MICRO_VERSION "${pyside_MICRO_VERSION}")
set(BINDING_API_PRE_RELEASE_VERSION_TYPE "${pyside_PRE_RELEASE_VERSION_TYPE}")
set(BINDING_API_PRE_RELEASE_VERSION "${pyside_PRE_RELEASE_VERSION}")
set(pyside6_library_so_version "${SHIBOKEN_SO_VERSION}")
set(CHROMIUM_VERSION "${pyside_CHROMIUM_VERSION}")

# Detect if the Python interpreter is actually PyPy
execute_process(
    COMMAND ${Python_EXECUTABLE} -c "if True:
        pypy_version = ''
        import sys
        if hasattr(sys, 'pypy_version_info'):
            pypy_version = '.'.join(map(str, sys.pypy_version_info[:3]))
        print(pypy_version)
        "
    OUTPUT_VARIABLE PYPY_VERSION
    OUTPUT_STRIP_TRAILING_WHITESPACE)

if(WIN32)
    set(PATH_SEP "\;")
else()
    set(PATH_SEP ":")
endif()

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE "${SHIBOKEN_BUILD_TYPE}" CACHE STRING "Build Type")
endif()

if(CMAKE_BUILD_TYPE STREQUAL "Release")
    add_definitions("-DNDEBUG")
endif()

if(SHIBOKEN_PYTHON_LIMITED_API)
    message(STATUS "******************************************************")
    message(STATUS "** PySide6 Limited API enabled.")
    message(STATUS "******************************************************")
endif()

# Need to ensure host Tools packages are found instead of target ones when cross-compiling.
if(QFP_QT_HOST_PATH)
    get_filename_component(__qt_host_path_absolute "${QFP_QT_HOST_PATH}" ABSOLUTE)
    if(EXISTS "${__qt_host_path_absolute}")
        set(QT_HOST_PATH "${__qt_host_path_absolute}")

        set(__qt_candidate_host_path_cmake_dir "${QT_HOST_PATH}/lib/cmake")
        if(EXISTS "${__qt_candidate_host_path_cmake_dir}")
            set(QT_HOST_PATH_CMAKE_DIR "${__qt_candidate_host_path_cmake_dir}")
        endif()
    endif()
endif()
# Find QtGui private headers for exposing some QPA classes
find_package(Qt6 REQUIRED COMPONENTS Core CorePrivate Gui GuiPrivate)

add_definitions(${Qt${QT_MAJOR_VERSION}Core_DEFINITIONS})

option(BUILD_TESTS "Build tests." TRUE)
option(ENABLE_VERSION_SUFFIX "Used to use current version in suffix to generated files. This is used to allow multiples versions installed simultaneous." FALSE)
set(LIB_SUFFIX "" CACHE STRING "Define suffix of directory name (32/64)" )
set(LIB_INSTALL_DIR "lib${LIB_SUFFIX}" CACHE PATH "The subdirectory relative to the install prefix where libraries will be installed (default is /lib${LIB_SUFFIX})" FORCE)
if(CMAKE_HOST_APPLE)
    set(ALTERNATIVE_QT_INCLUDE_DIR "" CACHE PATH "Deprecated. CMake now finds the proper include dir itself.")
    set(OSX_USE_LIBCPP "OFF" CACHE BOOL "Explicitly link the libc++ standard library (useful for macOS deployment targets lower than 10.9.")
    if(OSX_USE_LIBCPP)
        set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -stdlib=libc++")
    endif()
endif()

# Force usage of the C++17 standard
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Obtain QT_INCLUDE_DIR from INTERFACE_INCLUDE_DIRECTORIES which returns a list
# ../install-qt-6/include/QtCore;../install-qt-6/include
set(QT_INCLUDE_DIR "")
get_target_property(QT_INCLUDE_DIR_LIST Qt6::Core INTERFACE_INCLUDE_DIRECTORIES)
foreach(_Q ${QT_INCLUDE_DIR_LIST})
    if(NOT "${_Q}" MATCHES "QtCore$")
        set(QT_INCLUDE_DIR "${_Q}")
    endif()
endforeach()
if(QT_INCLUDE_DIR STREQUAL "")
    message(FATAL_ERROR "Unable to obtain the Qt include directory")
endif()

# On macOS, check if Qt is a framework build. This affects how include paths should be handled.
get_target_property(QtCore_is_framework Qt${QT_MAJOR_VERSION}::Core FRAMEWORK)

if(QtCore_is_framework)
    # Get the path to the framework dir.
    set(QT_FRAMEWORK_INCLUDE_DIR "${QT6_INSTALL_PREFIX}/${QT6_INSTALL_LIBS}")
    message(STATUS "*** QT_FRAMEWORK_INCLUDE_DIR is ${QT_FRAMEWORK_INCLUDE_DIR}")
    set(QT_INCLUDE_DIR "${QT6_INSTALL_PREFIX}/${QT6_INSTALL_HEADERS}")
endif()

set_cmake_cxx_flags()

message(STATUS "*** computed QT_INCLUDE_DIR as ${QT_INCLUDE_DIR}")

set(BINDING_NAME PySide6)

# Source and binary directories for the PySide6 subdir.
# Set in this scope, so child subdirectories can access it for use in various file paths.
set(PYSIDE_PACKAGE_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/${BINDING_NAME}")
set(PYSIDE_PACKAGE_BINARY_DIR "${CMAKE_CURRENT_BINARY_DIR}/${BINDING_NAME}")

set(BINDING_API_VERSION "${BINDING_API_MAJOR_VERSION}.${BINDING_API_MINOR_VERSION}.${BINDING_API_MICRO_VERSION}" CACHE STRING "PySide6 version" FORCE)
set(PYSIDE_SO_VERSION ${BINDING_API_MAJOR_VERSION}.${BINDING_API_MINOR_VERSION})
if(BINDING_API_PRE_RELEASE_VERSION_TYPE STREQUAL "")
    set(BINDING_API_VERSION_FULL "${BINDING_API_MAJOR_VERSION}.${BINDING_API_MINOR_VERSION}.${BINDING_API_MICRO_VERSION}"
        CACHE STRING "PySide6 version [full]" FORCE)
else()
    set(BINDING_API_VERSION_FULL "${BINDING_API_MAJOR_VERSION}.${BINDING_API_MINOR_VERSION}.${BINDING_API_MICRO_VERSION}~${BINDING_API_PRE_RELEASE_VERSION_TYPE}${BINDING_API_PRE_RELEASE_VERSION}"
        CACHE STRING "PySide6 version [full]" FORCE)
endif()

compute_config_py_values(BINDING_API_VERSION)

include(PySideModules)

# Set default values for pyside6_global.h
set (Qt${QT_MAJOR_VERSION}Test_FOUND "0")
set (Qt${QT_MAJOR_VERSION}Widgets_FOUND "0")

collect_essential_modules()
collect_optional_modules()

# Additional (non-Qt) modules implemented in PySide only
set(PURE_PYTHON_MODULES Asyncio)
# CPython extension modules that are not Qt bindings (no Qt prefix in short name)
set(CPYTHON_MODULES QmlFeatures)
set(NON_QT_MODULES ${PURE_PYTHON_MODULES} ${CPYTHON_MODULES})

# Modules to be built unless specified by -DMODULES on command line
if(NOT MODULES)
    set(MODULES "${ALL_ESSENTIAL_MODULES};${ALL_OPTIONAL_MODULES};${NON_QT_MODULES}")
    set(required_modules ${ALL_ESSENTIAL_MODULES})
    set(optional_modules ${ALL_OPTIONAL_MODULES})
else()
    set(required_modules ${MODULES})
    set(optional_modules)
endif()
list(REMOVE_ITEM MODULES ${SKIP_MODULES})
list(REMOVE_ITEM required_modules ${SKIP_MODULES})
list(REMOVE_ITEM optional_modules ${SKIP_MODULES})

# Non-Qt modules must be removed before find_packages tries to locate them.
foreach(m IN LISTS NON_QT_MODULES)
    set(DISABLE_Qt${m} 1)
    if("Qt${m}" IN_LIST MODULES OR "${m}" IN_LIST MODULES)
        set(DISABLE_Qt${m} 0)
    endif()
    list(FILTER MODULES EXCLUDE REGEX "^(Qt)?${m}$")
    list(FILTER required_modules EXCLUDE REGEX "^(Qt)?${m}$")
endforeach()

find_package(Qt6 QUIET
    COMPONENTS ${required_modules}
    OPTIONAL_COMPONENTS ${optional_modules}
)

# This will contain the set of modules for which bindings are not built.
set(DISABLED_MODULES "${ALL_ESSENTIAL_MODULES};${ALL_OPTIONAL_MODULES}")

remove_skipped_modules()

# Mark all non-collected modules as disabled. This is used for disabling tests
# that depend on the disabled modules.
foreach(m IN LISTS DISABLED_MODULES)
    set(DISABLE_Qt${m} 1)
endforeach()

# Whether to add libpysideqml
find_package(Qt6 QUIET COMPONENTS Qml)

# Whether to add libpysideremoteobjects
find_package(Qt6 QUIET COMPONENTS RemoteObjects)

string(REGEX MATCHALL "[0-9]+" qt_version_helper "${Qt${QT_MAJOR_VERSION}Core_VERSION}")

list(GET qt_version_helper 0 QT_VERSION_MAJOR)
list(GET qt_version_helper 1 QT_VERSION_MINOR)
list(GET qt_version_helper 2 QT_VERSION_PATCH)
unset(qt_version_helper)

set(PYSIDE_QT_VERSION "${QT_VERSION_MAJOR}.${QT_VERSION_MINOR}" CACHE STRING "Qt version used to compile PySide" FORCE)
if(ENABLE_VERSION_SUFFIX)
      set(pyside_SUFFIX "-${BINDING_API_MAJOR_VERSION}.${BINDING_API_MINOR_VERSION}")
endif()

# no more supported: include(${QT_USE_FILE})

# Define supported Qt Version
set(SUPPORTED_QT_VERSION "${QT_VERSION_MAJOR}.${QT_VERSION_MINOR}.${QT_VERSION_PATCH}")


# uninstall target
configure_file("${CMAKE_CURRENT_SOURCE_DIR}/cmake_uninstall.cmake"
               "${CMAKE_CURRENT_BINARY_DIR}/cmake_uninstall.cmake"
               IMMEDIATE @ONLY)

# When opening super project, prevent redefinition of uninstall target.
if(NOT TARGET uninstall)
    add_custom_target(uninstall "${CMAKE_COMMAND}"
                      -P "${CMAKE_CURRENT_BINARY_DIR}/cmake_uninstall.cmake")
endif()

if(NOT PYTHON_SITE_PACKAGES)
    shiboken_internal_set_python_site_packages()
endif()

shiboken_get_debug_level(debug_level)
if(SHIBOKEN_UNOPTIMIZE)
    SET(UNOPTIMIZE "--unoptimize=${SHIBOKEN_UNOPTIMIZE}")
ENDIF()

set(GENERATOR_EXTRA_FLAGS
    ${SHIBOKEN_GENERATOR_EXTRA_FLAGS}
    ${debug_level}
    ${UNOPTIMIZE}
    --generator-set=shiboken
    --enable-parent-ctor-heuristic
    --enable-pyside-extensions
    --enable-return-value-heuristic
    --use-isnull-as-nb-bool)
use_protected_as_public_hack()

# Build with Address sanitizer enabled if requested. This may break things, so use at your own risk.
if(SANITIZE_ADDRESS)
    setup_sanitize_address()
endif()
if(SANITIZE_THREAD)
    setup_sanitize_thread()
endif()

find_package(Qt6 QUIET COMPONENTS Designer)
