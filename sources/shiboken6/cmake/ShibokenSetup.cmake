# Copyright (C) 2023 The Qt Company Ltd.
# SPDX-License-Identifier: BSD-3-Clause

include(CheckIncludeFileCXX)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_LIST_DIR}")
list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_LIST_DIR}/../../shiboken6_generator/cmake")

include(ShibokenHelpers)
include(ShibokenGeneratorCMakeHelpers)
include(ShibokenGeneratorSbomHelpers)
include(ShibokenGeneratorTargetHelpers)

option(DISABLE_DOCSTRINGS "Disable documentation extraction." FALSE)

shiboken_internal_disable_pkg_config_if_needed()
shiboken_internal_detect_if_cross_building()

# Note: For cross building, we rely on FindPython shipped with CMake 3.17+ to
# provide the value of Python_SOABI.

shiboken_internal_decide_parts_to_build()
shiboken_internal_find_host_shiboken_tools()
shiboken_internal_set_up_extra_dependency_paths()

set(QT_MAJOR_VERSION 6)
message(STATUS "Using Qt ${QT_MAJOR_VERSION}")
find_package(Qt6 REQUIRED COMPONENTS Core)

if(QUIET_BUILD)
    set_quiet_build()
endif()

if(USE_PYTHON_VERSION)
    shiboken_find_required_python(${USE_PYTHON_VERSION})
else()
    shiboken_find_required_python()
endif()

set(shiboken6_VERSION "${shiboken_MAJOR_VERSION}.${shiboken_MINOR_VERSION}.${shiboken_MICRO_VERSION}")
set(shiboken6_library_so_version "${shiboken_MAJOR_VERSION}.${shiboken_MINOR_VERSION}")

compute_config_py_values(shiboken6_VERSION)

## For debugging the PYTHON* variables
message(STATUS "Python_Development_FOUND:       " ${Python_Development_FOUND})
message(STATUS "Python_LIBRARIES:       " ${Python_LIBRARIES})
message(STATUS "Python_INCLUDE_DIRS:    " ${Python_INCLUDE_DIRS})
message(STATUS "Python_Interpreter_FOUND:     " ${Python_Interpreter_FOUND})
message(STATUS "Python_EXECUTABLE:      " ${Python_EXECUTABLE})
message(STATUS "Python_VERSION:         " ${Python_VERSION_MAJOR}.${Python_VERSION_MINOR}.${Python_VERSION_PATCH})

if(NOT PYTHON_EXTENSION_SUFFIX)
    get_python_extension_suffix()
endif()

option(FORCE_LIMITED_API "Enable the limited API." "yes")
set(PYTHON_LIMITED_API 0)

shiboken_check_if_limited_api()

if(PYTHON_LIMITED_API)
    set_limited_api()
endif()

if(NOT PYTHON_CONFIG_SUFFIX)
    set_python_config_suffix()
endif()

set(PYTHON_SHARED_LIBRARY_SUFFIX "${PYTHON_CONFIG_SUFFIX}")

if(NOT PYTHON_CONFIG_SUFFIX)
    message(FATAL_ERROR
        "PYTHON_CONFIG_SUFFIX is empty. It should never be empty. Please file a bug report.")
endif()

message(STATUS "PYTHON_EXTENSION_SUFFIX:      ${PYTHON_EXTENSION_SUFFIX}")
message(STATUS "PYTHON_CONFIG_SUFFIX:         ${PYTHON_CONFIG_SUFFIX}")
message(STATUS "PYTHON_SHARED_LIBRARY_SUFFIX: ${PYTHON_SHARED_LIBRARY_SUFFIX}")


if(NOT PYTHON_SITE_PACKAGES)
    shiboken_internal_set_python_site_packages()
endif()

set_cmake_cxx_flags()
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -D QT_NO_CAST_FROM_ASCII -D QT_NO_CAST_TO_ASCII")

# Force usage of the C++17 standard
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(LIB_SUFFIX "" CACHE STRING "Define suffix of directory name (32/64)" )
set(LIB_INSTALL_DIR "lib${LIB_SUFFIX}" CACHE PATH "The subdirectory relative to the install \
    prefix where libraries will be installed (default is /lib${LIB_SUFFIX})" FORCE)
set(BIN_INSTALL_DIR "bin" CACHE PATH "The subdirectory relative to the install prefix where \
    dlls will be installed (default is /bin)" FORCE)

if(WIN32)
    set(PATH_SEP "\;")
else()
    set(PATH_SEP ":")
endif()

if(CMAKE_HOST_APPLE)
    set(OSX_USE_LIBCPP "OFF" CACHE BOOL "Explicitly link the libc++ standard library \
        (useful for macOS deployment targets lower than 10.9.")
    if(OSX_USE_LIBCPP)
        set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -stdlib=libc++")
    endif()
endif()

# Build with Address sanitizer enabled if requested.
# This may break things, so use at your own risk.
if(SANITIZE_ADDRESS AND NOT MSVC)
    setup_sanitize_address()
endif()

# Detect if the python libs were compiled in debug mode
# On Linux distros there is no standard way to check that.
execute_process(
    COMMAND ${Python_EXECUTABLE} -c "if True:
        import sys
        import sysconfig
        config_py_debug = sysconfig.get_config_var('Py_DEBUG')
        print(bool(config_py_debug))
        "
    OUTPUT_VARIABLE PYTHON_WITH_DEBUG
    OUTPUT_STRIP_TRAILING_WHITESPACE)

# Detect if python interpeter was compiled with COUNT_ALLOCS define
# Linux distros are inconsistent in setting the sysconfig.get_config_var('COUNT_ALLOCS') value
# We can't detect it when cross-building, because we can't run the target python executable.
# TODO: Is there another way to detect this and is it relevant for cross-built python interpreters?
#       At the very least, upstream CPython removed COUNT_ALLOCS support in Python 3.9.
if(SHIBOKEN_IS_CROSS_BUILD)
    set(PYTHON_WITH_COUNT_ALLOCS 0)
else()
    execute_process(
        COMMAND ${Python_EXECUTABLE} -c "if True:
            count_allocs = False
            import sys
            try:
                if sys.getcounts:
                    count_allocs = True
            except:
                pass

            print(bool(count_allocs))
            "
        OUTPUT_VARIABLE PYTHON_WITH_COUNT_ALLOCS
        OUTPUT_STRIP_TRAILING_WHITESPACE)
endif()


set(SHIBOKEN_BUILD_TYPE "${CMAKE_BUILD_TYPE}")

if(CMAKE_BUILD_TYPE STREQUAL "Debug")
    set_debug_build()
endif()

######################################################################
## Define the Python files involved in the build process.
##
## They are installed into the file system (see shibokenmodule)
## and embedded into the libshiboken binary through a .zip file.
######################################################################

set(shiboken_python_files
    "signature/lib/__init__.py"
    "signature/lib/enum_sig.py"
    "signature/lib/pyi_generator.py"
    "signature/lib/tool.py"
    "signature/__init__.py"
    "signature/errorhandler.py"
    "signature/importhandler.py"
    "signature/layout.py"
    "signature/loader.py"
    "signature/mapping.py"
    "signature/parser.py"
    "__init__.py"
    "feature.py"
    )

# uninstall target
configure_file("${CMAKE_CURRENT_SOURCE_DIR}/cmake_uninstall.cmake"
               "${CMAKE_CURRENT_BINARY_DIR}/cmake_uninstall.cmake"
               IMMEDIATE @ONLY)
add_custom_target(uninstall "${CMAKE_COMMAND}"
                  -P "${CMAKE_CURRENT_BINARY_DIR}/cmake_uninstall.cmake")

set(generator_plugin_DIR ${LIB_INSTALL_DIR}/generatorrunner${generator_SUFFIX})
