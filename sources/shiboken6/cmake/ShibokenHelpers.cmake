# Copyright (C) 2023 The Qt Company Ltd.
# SPDX-License-Identifier: BSD-3-Clause

include(CMakeParseArguments)

macro(set_limited_api)
    if (WIN32 AND NOT EXISTS "${PYTHON_LIMITED_LIBRARIES}")
        message(FATAL_ERROR "The Limited API was enabled, but ${PYTHON_LIMITED_LIBRARIES} was not found!")
    endif()
    message(STATUS "******************************************************")
    message(STATUS "** Limited API enabled ${PYTHON_LIMITED_LIBRARIES}")
    message(STATUS "******************************************************")
endmacro()

macro(set_debug_build)
    set(SHIBOKEN_BUILD_TYPE "Debug")

    if(NOT Python_LIBRARIES)
        message(WARNING "Python debug shared library not found; \
            assuming python was built with shared library support disabled.")
    endif()

    if(NOT PYTHON_WITH_DEBUG)
        message(WARNING "Compiling shiboken6 with debug enabled, \
            but the python executable was not compiled with debug support.")
    else()
        set(SBK_PKG_CONFIG_PY_DEBUG_DEFINITION " -DPy_DEBUG")
    endif()

    if (PYTHON_WITH_COUNT_ALLOCS)
        set(SBK_PKG_CONFIG_PY_DEBUG_DEFINITION "${SBK_PKG_CONFIG_PY_DEBUG_DEFINITION} -DCOUNT_ALLOCS")
    endif()
endmacro()

macro(setup_sanitize_address)
    # Currently this does not check that the clang / gcc / MSVC version used supports Address
    # sanitizer, so once again, use at your own risk.
    if(MSVC)
        add_compile_options("/fsanitize=address")
    else()
        add_compile_options("-fsanitize=address" "-g" "-fno-omit-frame-pointer")
    endif()
    # We need to add the sanitize address option to all linked executables / shared libraries
    # so that proper sanitizer symbols are linked in.
    #
    # Note that when running tests, you may need to set an additional environment variable
    # in set_tests_properties for shiboken6 / pyside tests, or exported in your shell. Address
    # sanitizer will tell you what environment variable needs to be exported. For example:
    # export DYLD_INSERT_LIBRARIES=/Applications/Xcode.app/Contents/Developer/Toolchains/
    #   ./XcodeDefault.xctoolchain/usr/lib/clang/8.1.0/lib/darwin/libclang_rt.asan_osx_dynamic.dylib
    if(MSVC)
        set(CMAKE_CXX_STANDARD_LIBRARIES "${CMAKE_STANDARD_LIBRARIES} /fsanitize=address")
    else()
        set(CMAKE_CXX_STANDARD_LIBRARIES "${CMAKE_STANDARD_LIBRARIES} -fsanitize=address")
    endif()
endmacro()

macro(setup_sanitize_thread)
    if(MSVC)
        set(sanitize_thread_option "/fsanitize=thread")
    else()
        set(sanitize_thread_option "-fsanitize=thread")
    endif()
    add_compile_options("${sanitize_thread_option}")
    set(CMAKE_CXX_STANDARD_LIBRARIES "${CMAKE_STANDARD_LIBRARIES} ${sanitize_thread_option}")
endmacro()

macro(set_cmake_cxx_flags)
if(MSVC)
    # Qt5: this flag has changed from /Zc:wchar_t- in Qt4.X
    set(CMAKE_CXX_FLAGS "/Zc:wchar_t /GR /EHsc /DWIN32 /D_WINDOWS /D_SCL_SECURE_NO_WARNINGS")
    #set(CMAKE_CXX_FLAGS "/Zc:wchar_t /GR /EHsc /DNOCOLOR /DWIN32 /D_WINDOWS /D_SCL_SECURE_NO_WARNINGS") # XXX
else()
    set (gcc_warnings_options "-Wall -Wextra -Wno-strict-aliasing")
    # Clang has -Wno-bad-function-cast, but does not need it.
    if ("${CMAKE_CXX_COMPILER_ID}" STREQUAL  "GNU")
        set (gcc_warnings_options "${gcc_warnings_options} -Wno-cast-function-type")
    endif()
    if(CMAKE_HOST_UNIX AND NOT CYGWIN)
        add_definitions(-fPIC)
        set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} ${gcc_warnings_options} -fvisibility=hidden")
    endif()
    set(CMAKE_CXX_FLAGS_DEBUG "-g")
    option(ENABLE_GCC_OPTIMIZATION "Enable specific GCC flags to optimization library \
        size and performance. Only available on Release Mode" 0)
    if(ENABLE_GCC_OPTIMIZATION)
        set(CMAKE_CXX_FLAGS_RELEASE "-DNDEBUG -Os -Wl,-O1")
        if(NOT CMAKE_HOST_APPLE)
            set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wl,--hash-style=gnu")
        endif()
    endif()
    if(CMAKE_HOST_APPLE)
        # ALTERNATIVE_QT_INCLUDE_DIR is deprecated, because CMake takes care of finding the proper
        # include folders using the qmake found in the environment. Only use it for now in case
        # something goes wrong with the cmake process.
        if(ALTERNATIVE_QT_INCLUDE_DIR AND NOT QT_INCLUDE_DIR)
            set(QT_INCLUDE_DIR ${ALTERNATIVE_QT_INCLUDE_DIR})
        endif()
    endif()
endif()

endmacro()

function(qfp_strip_library target)
    # Strip unless macOS (/strip: error: symbols referenced by indirect symbol
    # table entries that can't be stripped).
    if (CMAKE_STRIP AND UNIX AND NOT APPLE AND NOT QFP_NO_STRIP
        AND NOT CMAKE_BUILD_TYPE STREQUAL "Debug")
        set(post_command COMMAND ${CMAKE_STRIP} $<TARGET_FILE:${target}>)
        add_custom_command(TARGET ${target} POST_BUILD ${post_command})
    endif()

endfunction()

macro(shiboken_internal_set_python_site_packages)
    # When cross-building, we can't run the target python executable to find out the information,
    # so we allow an explicit variable assignment or use a default / sensible value.
    if(SHIBOKEN_IS_CROSS_BUILD OR PYSIDE_IS_CROSS_BUILD OR QFP_FIND_NEW_PYTHON_PACKAGE)
        # Allow manual assignment.
        if(QFP_PYTHON_SITE_PACKAGES)
            set(PYTHON_SITE_PACKAGES "${QFP_PYTHON_SITE_PACKAGES}")
        else()
            # Assumes POSIX.
            # Convention can be checked in cpython's source code in
            # Lib/sysconfig.py's _INSTALL_SCHEMES
            set(__version_major_minor
                "${Python_VERSION_MAJOR}.${Python_VERSION_MINOR}")

            set(PYTHON_SITE_PACKAGES_WITHOUT_PREFIX
                "lib/python${__version_major_minor}/site-packages")
            set(PYTHON_SITE_PACKAGES
                "${CMAKE_INSTALL_PREFIX}/${PYTHON_SITE_PACKAGES_WITHOUT_PREFIX}")
            unset(__version_major_minor)
        endif()
    else()
        execute_process(
            COMMAND ${Python_EXECUTABLE} -c "if True:
                import sys
                import sysconfig
                from os.path import sep

                # /home/qt/dev/env/lib/python3.9/site-packages
                if sys.platform == 'win32':
                    lib_path = sysconfig.get_path('purelib')
                else:
                    lib_path = sysconfig.get_path('purelib', scheme='posix_prefix')

                # /home/qt/dev/env
                if sys.platform == 'win32':
                    data_path = sysconfig.get_path('data')
                else:
                    data_path = sysconfig.get_path('data', scheme='posix_prefix')

                # /lib/python3.9/site-packages
                rel_path = lib_path.replace(data_path, '')

                print(f'${CMAKE_INSTALL_PREFIX}{rel_path}'.replace(sep, '/'))
                "
            OUTPUT_VARIABLE PYTHON_SITE_PACKAGES
            OUTPUT_STRIP_TRAILING_WHITESPACE)
    endif()
    if (NOT PYTHON_SITE_PACKAGES)
        message(FATAL_ERROR "Could not detect Python module installation directory.")
    elseif (APPLE)
        message(STATUS "!!! The generated bindings will be installed on ${PYTHON_SITE_PACKAGES}, \
            is it right!?")
    endif()
endmacro()

macro(set_python_config_suffix)
    if (PYTHON_LIMITED_API)
        if(WIN32)
            set(PYTHON_EXTENSION_SUFFIX "")
        else()
            set(PYTHON_EXTENSION_SUFFIX ".abi3")
        endif()
        set(PYTHON_CONFIG_SUFFIX ".abi3")
    else()
        set(PYTHON_CONFIG_SUFFIX "${PYTHON_EXTENSION_SUFFIX}")
    endif()
endmacro()

macro(set_quiet_build)
    # Don't display "up-to-date / install" messages when installing, to reduce visual clutter.
    set(CMAKE_INSTALL_MESSAGE NEVER)
    # Override message not to display info messages when doing a quiet build.
    function(message)
        list(GET ARGV 0 MessageType)
        if (MessageType STREQUAL FATAL_ERROR OR
            MessageType STREQUAL SEND_ERROR OR
            MessageType STREQUAL WARNING OR
            MessageType STREQUAL AUTHOR_WARNING)
                list(REMOVE_AT ARGV 0)
                _message(${MessageType} "${ARGV}")
        endif()
    endfunction()
endmacro()

macro(get_python_extension_suffix)
    # When cross-building, we can't run the target python executable to find out the information,
    # so we rely on Python_SOABI being set by find_package(Python).
    # Python_SOABI is only set by CMake 3.17+
    # TODO: Lower this to CMake 3.16 if possible.
    if(SHIBOKEN_IS_CROSS_BUILD)
        if(NOT Python_SOABI)
            message(FATAL_ERROR "Python_SOABI variable is empty.")
        endif()
        set(PYTHON_EXTENSION_SUFFIX ".${Python_SOABI}")
    else()
        # See PYSIDE-1841 / https://bugs.python.org/issue39825 for distutils vs sysconfig
        execute_process(
          COMMAND ${Python_EXECUTABLE} -c "if True:
             import sys
             if sys.version_info >= (3, 8, 2):
                 import sysconfig
                 suffix = sysconfig.get_config_var('EXT_SUFFIX')
             else:
                 from distutils import sysconfig
                 suffix = sysconfig.get_config_var('EXT_SUFFIX')
             pos = suffix.rfind('.')
             if pos > 0:
                 print(suffix[:pos])
             else:
                 print(f'Unable to determine PYTHON_EXTENSION_SUFFIX from EXT_SUFFIX: \"{suffix}\"',
                     file=sys.stderr)
             "
          OUTPUT_VARIABLE PYTHON_EXTENSION_SUFFIX
          OUTPUT_STRIP_TRAILING_WHITESPACE)
    endif()
    message(STATUS "PYTHON_EXTENSION_SUFFIX: " ${PYTHON_EXTENSION_SUFFIX})
endmacro()

macro(shiboken_parse_all_arguments prefix type flags options multiopts)
    cmake_parse_arguments(${prefix} "${flags}" "${options}" "${multiopts}" ${ARGN})
    if(DEFINED ${prefix}_UNPARSED_ARGUMENTS)
        message(FATAL_ERROR "Unknown arguments were passed to ${type} (${${prefix}_UNPARSED_ARGUMENTS}).")
    endif()
endmacro()

macro(shiboken_check_if_limited_api)
    # TODO: Figure out how to use limited API libs when cross-building to Windows, if that's ever
    # needed. Perhaps use host python to walk the libs of the target python installation.

    if(NOT SHIBOKEN_IS_CROSS_BUILD AND WIN32)
        # On Windows, PYTHON_LIBRARIES can be a list. Example:
        #    optimized;C:/Python36/libs/python36.lib;debug;C:/Python36/libs/python36_d.lib
        # On other platforms, this result is not used at all.
        execute_process(
            COMMAND ${Python_EXECUTABLE} -c "if True:
                from pathlib import Path
                libs = r'${Python_LIBRARIES}'
                libs = libs.split(';')
                for lib in libs:
                    if ('\\\\' in lib or '/' in lib) and Path(lib).is_file():
                        lib = Path(lib)
                        prefix = lib.parent
                        py = lib.name
                        if py.startswith('python3'):
                            print(prefix / 'python3.lib')
                            break
                "
            OUTPUT_VARIABLE PYTHON_LIMITED_LIBRARIES
            OUTPUT_STRIP_TRAILING_WHITESPACE)
    endif()

    message(STATUS "PYTHON_LIMITED_LIBRARIES: " ${PYTHON_LIMITED_LIBRARIES})

    if(FORCE_LIMITED_API OR SHIBOKEN_PYTHON_LIMITED_API)
        set(PYTHON_LIMITED_API 1)
        if(WIN32)
            set(SHIBOKEN_PYTHON_LIBRARIES ${PYTHON_LIMITED_LIBRARIES})
        endif()
    endif()
endmacro()


macro(shiboken_find_required_python)
    set(_shiboken_find_python_version_args "")
    if(${ARGC} GREATER 0)
        list(APPEND _shiboken_find_python_version_args "${ARGV0}")
    endif()
    # This function can also be called by consumers of ShibokenConfig.cmake package like pyside,
    # that's why we also check for PYSIDE_IS_CROSS_BUILD (which is set by pyside project)
    # and QFP_FIND_NEW_PYTHON_PACKAGE for an explicit opt in.
    #
    # We have to use FindPython package instead of FindPythonInterp to get required target Python
    # information.
    if(SHIBOKEN_IS_CROSS_BUILD OR PYSIDE_IS_CROSS_BUILD OR QFP_FIND_NEW_PYTHON_PACKAGE)
        # We want FindPython to look in the sysroot for the python-config executable,
        # but toolchain files might set CMAKE_FIND_ROOT_PATH_MODE_PROGRAM to NEVER because
        # programs are mostly found for running and you usually can't run a target executable on
        # a host platform. python-config can likely be ran though, because it's a shell script
        # to be run on a host Linux.
        set(_shiboken_backup_CMAKE_FIND_ROOT_PATH_MODE_PROGRAM
            "${CMAKE_FIND_ROOT_PATH_MODE_PROGRAM}")
        set(_shiboken_backup_CMAKE_FIND_ROOT_PATH
            "${CMAKE_FIND_ROOT_PATH}")
        set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM ONLY)
        if(Python_ROOT_DIR)
            list(PREPEND CMAKE_FIND_ROOT_PATH "${Python_ROOT_DIR}")
        endif()

        # We can't look for the Python interpreter because FindPython tries to execute it, which
        # usually won't work on a host platform due to different architectures / platforms.
        # Thus we only look for the Python include and lib directories which are part of the
        # Development component.
        find_package(
            Python
            ${_shiboken_find_python_version_args}
            REQUIRED
            COMPONENTS Development
        )
        set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM
            "${_shiboken_backup_CMAKE_FIND_ROOT_PATH_MODE_PROGRAM}")
        set(CMAKE_FIND_ROOT_PATH
            "${_shiboken_backup_CMAKE_FIND_ROOT_PATH}")

        # For Android platform sometimes the FindPython module returns Python_SOABI as empty in
        # certain scenarios eg: macOS host etc. This is because
        # it is unable to set Python_CONFIG i.e. `python3-config` script
        # This workaround derives the Python_SOABI from the found Python version.
        # TODO: Find a better way to set Python_SOABI for Android platform
        if(CMAKE_SYSTEM_NAME STREQUAL "Android" AND NOT Python_SOABI)
            set(Python_SOABI "cpython-${Python_VERSION_MAJOR}${Python_VERSION_MINOR}")
        endif()
    else()
        find_package(
            Python
            ${_shiboken_find_python_version_args}
            REQUIRED
            COMPONENTS Interpreter Development.Module
        )
    endif()

    shiboken_validate_python_version()

    # Free-threaded: "t" in the SOABI (cpython-314t-darwin, cp314t-win_amd64);
    # also for cross builds.
    set(SHIBOKEN_PYTHON_FREE_THREADED FALSE)
    if(Python_SOABI MATCHES "^cp(ython-)?[0-9]+t")
        set(SHIBOKEN_PYTHON_FREE_THREADED TRUE)
    endif()

    set(SHIBOKEN_PYTHON_INTERPRETER "${Python_EXECUTABLE}")
    set_property(GLOBAL PROPERTY SHIBOKEN_PYTHON_INTERPRETER "${Python_EXECUTABLE}")
endmacro()

macro(shiboken_validate_python_version)
    if(Python_VERSION_MAJOR EQUAL "3" AND Python_VERSION_MINOR LESS "10")
            message(FATAL_ERROR
                   "Shiboken requires Python 3.10+.")
    endif()
endmacro()

macro(shiboken_compute_python_includes)
    shiboken_parse_all_arguments(
        "SHIBOKEN_COMPUTE_INCLUDES" "shiboken_compute_python_includes"
        "IS_CALLED_FROM_EXPORT" "" "" ${ARGN})


    # If the installed shiboken config file is used,
    # append the found Python include dirs as an interface property on the libshiboken target.
    # This needs to be dynamic because the user of the library might have python installed
    # in a different path than when shiboken was originally built.
    # Otherwise if shiboken is currently being built itself (either as standalone, or super project
    # build) append the include dirs as PUBLIC.
    if (SHIBOKEN_COMPUTE_INCLUDES_IS_CALLED_FROM_EXPORT)
        #TODO target_include_directories works on imported targets only starting with v3.11.0.
        set_property(TARGET Shiboken6::libshiboken
                     APPEND PROPERTY INTERFACE_INCLUDE_DIRECTORIES ${Python_INCLUDE_DIRS})
    else()
        target_include_directories(libshiboken
                                   PUBLIC $<BUILD_INTERFACE:${Python_INCLUDE_DIRS}>)
    endif()


    set(SHIBOKEN_PYTHON_INCLUDE_DIRS "${Python_INCLUDE_DIRS}")

    set_property(GLOBAL PROPERTY shiboken_python_include_dirs "${SHIBOKEN_PYTHON_INCLUDE_DIRS}")

    message(STATUS
            "SHIBOKEN_PYTHON_INCLUDE_DIRS computed to value: '${SHIBOKEN_PYTHON_INCLUDE_DIRS}'")
endmacro()

# Given a list of the following form:
#     optimized;C:/Python36/libs/python36.lib;debug;C:/Python36/libs/python36_d.lib
# choose the corresponding library to use, based on the current configuration type.
function(shiboken_get_library_for_current_config library_list current_config out_var)
    list(FIND library_list "optimized" optimized_found)
    list(FIND library_list "general" general_found)
    list(FIND library_list "debug" debug_found)

    if (optimized_found OR general_found OR debug_found)
        # Iterate over library list to find the most appropriate library.
        foreach(token ${library_list})
            if(token STREQUAL "optimized" OR token STREQUAL "general")
                set(is_debug 0)
                set(token1 1)
                set(lib "")
            elseif(token STREQUAL "debug")
                set(is_debug 1)
                set(token1 1)
                set(lib "")
            elseif(EXISTS ${token})
                set(lib ${token})
                set(token2 1)
            else()
                set(token1 0)
                set(token2 0)
                set(lib "")
            endif()

            if(token1 AND token2)
                if((is_debug AND lib AND current_config STREQUAL "Debug")
                   OR (NOT is_debug AND lib AND (NOT current_config STREQUAL "Debug")))
                    set(${out_var} ${lib} PARENT_SCOPE)
                    return()
                endif()
            endif()
        endforeach()
    else()
        # No configuration specific libraries found, just set the original value.
        set(${out_var} "${library_list}" PARENT_SCOPE)
    endif()

endfunction()

macro(shiboken_compute_python_libraries)
    shiboken_parse_all_arguments(
        "SHIBOKEN_COMPUTE_LIBS" "shiboken_compute_python_libraries"
        "IS_CALLED_FROM_EXPORT" "" "" ${ARGN})

    if (NOT SHIBOKEN_PYTHON_LIBRARIES)
        set(SHIBOKEN_PYTHON_LIBRARIES "")
    endif()

    # Android's Bionic linker does not resolve an undefined symbol against
    # whatever already happens to be loaded in the process, unlike the ELF
    # linker on desktop Linux/macOS. Every extension module needs an explicit
    # NEEDED entry pointing at libpython, same as Windows already requires.
    if((WIN32 OR CMAKE_SYSTEM_NAME STREQUAL "Android") AND NOT SHIBOKEN_PYTHON_LIBRARIES)
        set(SHIBOKEN_PYTHON_LIBRARIES ${Python_LIBRARIES})
    endif()

    # If the resulting variable
    # contains a "debug;X;optimized;Y" list like described in shiboken_check_if_limited_api,
    # make sure to pick just one, so that the final generator expressions are valid.
    shiboken_get_library_for_current_config("${SHIBOKEN_PYTHON_LIBRARIES}" "${CMAKE_BUILD_TYPE}" "SHIBOKEN_PYTHON_LIBRARIES")

    if(APPLE)
        set(SHIBOKEN_PYTHON_LIBRARIES "-undefined dynamic_lookup")
    endif()

    # If the installed shiboken config file is used,
    # append the computed Python libraries as an interface property on the libshiboken target.
    # This needs to be dynamic because the user of the library might have python installed
    # in a different path than when shiboken was originally built.
    # Otherwise if shiboken is currently being built itself (either as standalone, or super project
    # build) append the libraries as PUBLIC.
    if (SHIBOKEN_COMPUTE_LIBS_IS_CALLED_FROM_EXPORT)
        #TODO target_link_libraries works on imported targets only starting with v3.11.0.
        set_property(TARGET Shiboken6::libshiboken
                     APPEND PROPERTY INTERFACE_LINK_LIBRARIES ${SHIBOKEN_PYTHON_LIBRARIES})
    else()
        target_link_libraries(libshiboken
                              PUBLIC $<BUILD_INTERFACE:${SHIBOKEN_PYTHON_LIBRARIES}>)
    endif()

    set_property(GLOBAL PROPERTY shiboken_python_libraries "${SHIBOKEN_PYTHON_LIBRARIES}")

    message(STATUS "SHIBOKEN_PYTHON_LIBRARIES computed to value: '${SHIBOKEN_PYTHON_LIBRARIES}'")
endmacro()

function(shiboken_check_if_built_and_target_python_are_compatible)
    if(NOT SHIBOKEN_PYTHON_VERSION_MAJOR STREQUAL Python_VERSION_MAJOR)
        message(FATAL_ERROR "The detected Python major version is not \
compatible with the Python major version which was used when Shiboken was built.
Built with: '${SHIBOKEN_PYTHON_VERSION_MAJOR}.${SHIBOKEN_PYTHON_VERSION_MINOR}' \
Detected: '${Python_VERSION_MAJOR}.${Python_VERSION_MINOR}'")
    else()
        if(NOT SHIBOKEN_PYTHON_LIMITED_API
           AND NOT SHIBOKEN_PYTHON_VERSION_MINOR STREQUAL Python_VERSION_MINOR)
            message(FATAL_ERROR
                    "The detected Python minor version is not compatible with the Python minor \
version which was used when Shiboken was built. Consider building shiboken with \
FORCE_LIMITED_API set to '1', so that only the Python major version matters.
Built with: '${SHIBOKEN_PYTHON_VERSION_MAJOR}.${SHIBOKEN_PYTHON_VERSION_MINOR}' \
Detected: '${Python_VERSION_MAJOR}.${Python_VERSION_MINOR}'")
        endif()
    endif()
endfunction()

function(shiboken_internal_disable_pkg_config)
    # Disable pkg-config by setting an empty executable path. There's no documented way to
    # mark the package as not found, but we can force all pkg_check_modules calls to do nothing
    # by setting the variable to an empty value.
    set(PKG_CONFIG_EXECUTABLE "" CACHE STRING "Disabled pkg-config usage." FORCE)
endfunction()

function(shiboken_internal_disable_pkg_config_if_needed)
    if(SHIBOKEN_SKIP_PKG_CONFIG_ADJUSTMENT)
        return()
    endif()

    # pkg-config should not be used by default on Darwin platforms.
    if(APPLE)
        set(pkg_config_enabled OFF)
    else()
        set(pkg_config_enabled ON)
    endif()

    if(NOT pkg_config_enabled)
        shiboken_internal_disable_pkg_config()
    endif()
endfunction()

function(shiboken_internal_detect_if_cross_building)
    if(CMAKE_CROSSCOMPILING OR QFP_SHIBOKEN_HOST_PATH)
        set(is_cross_build TRUE)
    else()
        set(is_cross_build FALSE)
    endif()
    set(SHIBOKEN_IS_CROSS_BUILD "${is_cross_build}" PARENT_SCOPE)
    message(STATUS "SHIBOKEN_IS_CROSS_BUILD: ${is_cross_build}")
endfunction()

function(shiboken_internal_decide_parts_to_build)
    if(SHIBOKEN_IS_CROSS_BUILD)
        set(_shiboken_build_tests_default OFF)
    elseif(SHIBOKEN_BUILD_LIBS)
        set(_shiboken_build_tests_default ON)
    endif()
    option(BUILD_TESTS "Build tests." ${_shiboken_build_tests_default})
    message(STATUS "BUILD_TESTS: ${BUILD_TESTS}")
endfunction()

function(shiboken_internal_find_host_shiboken_tools)
    set(find_package_extra_args)
    if(QFP_SHIBOKEN_HOST_PATH)
        list(APPEND find_package_extra_args PATHS "${QFP_SHIBOKEN_HOST_PATH}/lib/cmake")
        list(PREPEND CMAKE_FIND_ROOT_PATH "${QFP_SHIBOKEN_HOST_PATH}")
    endif()
    set(SHIBOKEN6TOOLS_SKIP_FIND_DEPENDENCIES TRUE)
    find_package(
        Shiboken6Tools 6 CONFIG
        ${find_package_extra_args}
    )

    if(NOT Shiboken6Tools_DIR AND QFP_SHIBOKEN_HOST_PATH)
        message(FATAL_ERROR
            "Shiboken6Tools package was not found. "
            "Please set QFP_SHIBOKEN_HOST_PATH to the location where the Shiboken6Tools CMake "
            "package is installed.")
    endif()
endfunction()

function(shiboken_internal_set_up_extra_dependency_paths)
    set(extra_root_path_vars
        QFP_QT_TARGET_PATH
        QFP_PYTHON_TARGET_PATH
    )
    foreach(root_path IN LISTS extra_root_path_vars)
        set(new_root_path_value "${${root_path}}")
        if(new_root_path_value)
            set(new_prefix_path "${CMAKE_PREFIX_PATH}")
            list(PREPEND new_prefix_path "${new_root_path_value}/lib/cmake")
            set(CMAKE_PREFIX_PATH "${new_prefix_path}")
            set(CMAKE_PREFIX_PATH "${new_prefix_path}" PARENT_SCOPE)

            # Need to adjust the prefix and root paths so that find_package(Qt) and other 3rd
            # party packages are found successfully when they are located outside of the
            # default sysroot (whatever that maybe for the target platform).
            if(SHIBOKEN_IS_CROSS_BUILD)
                set(new_root_path "${CMAKE_FIND_ROOT_PATH}")
                list(PREPEND new_root_path "${new_root_path_value}")
                set(CMAKE_FIND_ROOT_PATH "${new_root_path}")
                set(CMAKE_FIND_ROOT_PATH "${new_root_path}" PARENT_SCOPE)
            endif()
        endif()
    endforeach()
endfunction()

macro(compute_config_py_values
      full_version_var_name
        )
    set(QT_MACOS_DEPLOYMENT_TARGET "")
    if (Qt${QT_MAJOR_VERSION}Core_FOUND)
        get_target_property(darwin_target Qt6::Core QT_DARWIN_MIN_DEPLOYMENT_TARGET)
        if(darwin_target)
            set(QT_MACOS_DEPLOYMENT_TARGET
                "__qt_macos_min_deployment_target__ = '${darwin_target}'")
        endif()
    elseif(APPLE AND NOT PROJECT_NAME STREQUAL "pyside-tools")
        message(FATAL_ERROR "Qt6::Core should be found before calling this macro")
    endif()

    string(TIMESTAMP PACKAGE_BUILD_DATE "%Y-%m-%dT%H:%M:%S+00:00" UTC)
    if (PACKAGE_BUILD_DATE)
        set(PACKAGE_BUILD_DATE "__build_date__ = '${PACKAGE_BUILD_DATE}'")
    endif()

    if (PACKAGE_SETUP_PY_PACKAGE_VERSION)
        set(PACKAGE_SETUP_PY_PACKAGE_VERSION_ASSIGNMENT "__setup_py_package_version__ = '${PACKAGE_SETUP_PY_PACKAGE_VERSION}'")
        set(FINAL_PACKAGE_VERSION ${PACKAGE_SETUP_PY_PACKAGE_VERSION})
    else()
        set(FINAL_PACKAGE_VERSION ${${full_version_var_name}})
    endif()

    if (PACKAGE_SETUP_PY_PACKAGE_TIMESTAMP)
        set(PACKAGE_SETUP_PY_PACKAGE_TIMESTAMP_ASSIGNMENT "__setup_py_package_timestamp__ = '${PACKAGE_SETUP_PY_PACKAGE_TIMESTAMP}'")
    else()
        set(PACKAGE_SETUP_PY_PACKAGE_TIMESTAMP_ASSIGNMENT "")
    endif()

    find_package(Git)
    if(GIT_FOUND)
        # Check if current source folder is inside a git repo, so that commit information can be
        # queried.
        execute_process(
          COMMAND ${GIT_EXECUTABLE} rev-parse --git-dir
          OUTPUT_VARIABLE PACKAGE_SOURCE_IS_INSIDE_REPO
          ERROR_QUIET
          OUTPUT_STRIP_TRAILING_WHITESPACE)

        if(PACKAGE_SOURCE_IS_INSIDE_REPO)
            # Force git dates to be UTC-based.
            set(ENV{TZ} UTC)
            execute_process(
              COMMAND ${GIT_EXECUTABLE} --no-pager show --date=format-local:%Y-%m-%dT%H:%M:%S+00:00 -s --format=%cd HEAD
              OUTPUT_VARIABLE PACKAGE_BUILD_COMMIT_DATE
              OUTPUT_STRIP_TRAILING_WHITESPACE)
            if(PACKAGE_BUILD_COMMIT_DATE)
                set(PACKAGE_BUILD_COMMIT_DATE "__build_commit_date__ = '${PACKAGE_BUILD_COMMIT_DATE}'")
            endif()
            unset(ENV{TZ})

            execute_process(
              COMMAND ${GIT_EXECUTABLE} rev-parse HEAD
              OUTPUT_VARIABLE PACKAGE_BUILD_COMMIT_HASH
              OUTPUT_STRIP_TRAILING_WHITESPACE)
            if(PACKAGE_BUILD_COMMIT_HASH)
                set(PACKAGE_BUILD_COMMIT_HASH "__build_commit_hash__ = '${PACKAGE_BUILD_COMMIT_HASH}'")
            endif()

            execute_process(
              COMMAND ${GIT_EXECUTABLE} describe HEAD
              OUTPUT_VARIABLE PACKAGE_BUILD_COMMIT_HASH_DESCRIBED
              OUTPUT_STRIP_TRAILING_WHITESPACE)
            if(PACKAGE_BUILD_COMMIT_HASH_DESCRIBED)
                set(PACKAGE_BUILD_COMMIT_HASH_DESCRIBED "__build_commit_hash_described__ = '${PACKAGE_BUILD_COMMIT_HASH_DESCRIBED}'")
            endif()

        endif()
    endif()

endmacro()

# Creates a new target called "${library_name}_generator" which
# depends on the mjb_rejected_classes.log file generated by shiboken.
# This target is added as a dependency to ${library_name} target.
# This file's timestamp informs cmake when the last generation was
# done, without force-updating the timestamps of the generated class
# cpp files.
# In practical terms this means that changing some injection code in
# an xml file that modifies only one specific class cpp file, will
# not force rebuilding all the cpp files, and thus allow for better
# incremental builds.
macro(create_generator_target library_name)
    add_custom_target(${library_name}_generator DEPENDS "${CMAKE_CURRENT_BINARY_DIR}/mjb_rejected_classes.log")
    add_dependencies(${library_name} ${library_name}_generator)
endmacro()

# Generate a shell script wrapper that sets environment variables for executing a specific tool.
#
# tool_name should be a unique tool name, preferably without spaces.
# Returns the wrapper path in path_out_var.
#
# Currently adds the Qt lib dir and libclang to PATH / LD_LIBRARY_PATH / DYLD_LIBRARY_PATH.
# Meant to be used as the first argument to add_custom_command's COMMAND option.
# TODO: Remove tool_name as the tool_name for this function is always shiboken.
function(shiboken_get_tool_shell_wrapper tool_name path_out_var)
    # Generate the wrapper only once during the execution of CMake.
    get_property(is_called GLOBAL PROPERTY "_shiboken_tool_wrapper_${tool_name}_created")

    if(is_called)
        get_property(wrapper_path GLOBAL PROPERTY "_shiboken_tool_wrapper_${tool_name}_path")
        set(${path_out_var} "${wrapper_path}" PARENT_SCOPE)
        return()
    endif()

    set(path_dirs "")
    set(path_dirs_native "")

    if(CMAKE_HOST_WIN32)
        set(wrapper_script_extension ".bat")
    else()
        set(wrapper_script_extension ".sh")
    endif()

    # Try to get original host shiboken paths from exported target properties.
    shiboken_get_host_tool_wrapper_properties(orig_qt_library_dir_absolute orig_libclang_lib_dir)

    # Get path to the Qt bin/lib dir depending on the platform and developer input.
    # Prefer values given on the command line, then the original host path if it exists, otherwise
    # try to use the Qt install prefix and libclang env vars.
    #
    # Note that in a cross-compiling case, using the Qt install prefix is very likely
    # wrong, because you want to use the location of the host Qt, not the target Qt. Same for
    # libclang. Unfortunately we currently don't provide a host Qt and host libclang option via
    # setup.py, so the manual cmake vars will have to suffice.
    if(SHIBOKEN_WRAPPER_HOST_QT_LIB_PATH AND EXISTS "${SHIBOKEN_WRAPPER_HOST_QT_LIB_PATH}")
        set(qt_library_dir_absolute "${SHIBOKEN_WRAPPER_HOST_QT_LIB_PATH}")
    elseif(orig_qt_library_dir_absolute AND EXISTS "${orig_qt_library_dir_absolute}")
        set(qt_library_dir_absolute "${orig_qt_library_dir_absolute}")
    elseif(CMAKE_HOST_WIN32)
        # in Windows the Qt dll are store `bin` in directory
        set(qt_library_dir ${QT6_INSTALL_BINS})
    else()
        # in Unix the .so are stored in `lib` directory
        set(qt_library_dir ${QT6_INSTALL_LIBS})
    endif()

    # Assert that Qt is already found.
    if((QT6_INSTALL_PREFIX AND qt_library_dir) OR orig_qt_library_dir_absolute)
    else()
        message(FATAL_ERROR "Qt should have been found already by now.")
    endif()

    if(NOT qt_library_dir_absolute)
        set(qt_library_dir_absolute "${QT6_INSTALL_PREFIX}/${qt_library_dir}")
    endif()
    list(APPEND path_dirs "${qt_library_dir_absolute}")

    # Get libclang lib dir path.
    # Prefer values given on the command line, then the original host path if it exists.
    if(SHIBOKEN_WRAPPER_HOST_CLANG_LIB_PATH AND EXISTS "${SHIBOKEN_WRAPPER_HOST_CLANG_LIB_PATH}")
        set(libclang_lib_dir "${SHIBOKEN_WRAPPER_HOST_CLANG_LIB_PATH}")
    elseif(orig_libclang_lib_dir AND EXISTS "${orig_libclang_lib_dir}")
        set(libclang_lib_dir "${orig_libclang_lib_dir}")
    else()
        # find libclang
        find_libclang()
    endif()

    if(libclang_lib_dir)
        list(APPEND path_dirs "${libclang_lib_dir}")
    endif()

    # Convert the paths from unix-style to native Windows style.
    foreach(path_dir IN LISTS path_dirs)
        if(EXISTS "${path_dir}")
            file(TO_NATIVE_PATH "${path_dir}" path_dir_native)
            list(APPEND path_dirs_native "${path_dir_native}")
        endif()
    endforeach()

    set(wrapper_dir "${CMAKE_BINARY_DIR}/.qfp/bin")
    file(MAKE_DIRECTORY "${wrapper_dir}")
    set(wrapper_path "${wrapper_dir}/${tool_name}_wrapper${wrapper_script_extension}")

    if(CMAKE_HOST_WIN32)
        file(WRITE "${wrapper_path}" "@echo off
set PATH=${path_dirs_native};%PATH%
%*")
    elseif(CMAKE_HOST_APPLE)
        string(REPLACE ";" ":" path_dirs_native "${path_dirs_native}")
        file(WRITE "${wrapper_path}" "#!/bin/bash
export DYLD_LIBRARY_PATH=${path_dirs_native}:$DYLD_LIBRARY_PATH
export DYLD_FRAMEWORK_PATH=${path_dirs_native}:$DYLD_FRAMEWORK_PATH
$@")
    else()
        string(REPLACE ";" ":" path_dirs_native "${path_dirs_native}")
        file(WRITE "${wrapper_path}" "#!/bin/bash
export LD_LIBRARY_PATH=${path_dirs_native}:$LD_LIBRARY_PATH
$@")
    endif()

    # Remember the creation of the file for a specific tool.
    set_property(GLOBAL PROPERTY "_shiboken_tool_wrapper_${tool_name}_path" "${wrapper_path}")
    set_property(GLOBAL PROPERTY "_shiboken_tool_wrapper_${tool_name}_created" TRUE)

    # Save original host paths for future cross-builds.
    shiboken_save_host_tool_wrapper_properties("${qt_library_dir_absolute}" "${libclang_lib_dir}")

    # give execute permission to run the file
    if(CMAKE_HOST_UNIX)
        execute_process(COMMAND chmod +x ${wrapper_path})
    endif()

    set(${path_out_var} "${wrapper_path}" PARENT_SCOPE)
endfunction()

# Retrieve the original host shiboken runtime dependency paths from the installed (namespaced)
# shiboken generator target.
function(shiboken_get_host_tool_wrapper_properties out_qt_library_dir out_libclang_lib_dir)
    if(TARGET Shiboken6::shiboken6)
        get_target_property(qt_library_dir Shiboken6::shiboken6 _shiboken_original_qt_lib_dir)
        if(NOT qt_library_dir)
            set(qt_library_dir "")
        endif()
        get_target_property(libclang_lib_dir Shiboken6::shiboken6
            _shiboken_original_libclang_lib_dir)
        if(NOT libclang_lib_dir)
            set(libclang_lib_dir "")
        endif()
    endif()

    set(${out_qt_library_dir} "${qt_library_dir}" PARENT_SCOPE)
    set(${out_libclang_lib_dir} "${libclang_lib_dir}" PARENT_SCOPE)
endfunction()

# Save original host shiboken runtime dependency paths as target properties, so they can be used
# when generating the wrapper file for cross-builds.
# Should only be done when shiboken is being built (aka it's a non-imported target).
function(shiboken_save_host_tool_wrapper_properties qt_library_dir libclang_lib_dir)
    if(TARGET shiboken6)
        get_target_property(is_imported shiboken6 IMPORTED)
        if(is_imported)
            return()
        endif()

        set_target_properties(shiboken6 PROPERTIES
            _shiboken_original_qt_lib_dir "${qt_library_dir}")
        set_property(TARGET shiboken6 APPEND PROPERTY
            EXPORT_PROPERTIES _shiboken_original_qt_lib_dir)
        if(libclang_lib_dir)
            set_target_properties(shiboken6 PROPERTIES
                _shiboken_original_libclang_lib_dir "${libclang_lib_dir}")
            set_property(TARGET shiboken6 APPEND PROPERTY
                EXPORT_PROPERTIES _shiboken_original_libclang_lib_dir)
        endif()
    endif()
endfunction()

# Returns the platform-specific relative rpath base token, if it's supported.
# If it's not supported, returns the string NO_KNOWN_RPATH_REL_BASE.
function(get_rpath_base_token out_var)
    if(APPLE)
        set(rpath_rel_base "@loader_path")
    elseif(UNIX)
        set(rpath_rel_base "$ORIGIN")
    else()
        #has no effect on Windows
        set(rpath_rel_base "NO_KNOWN_RPATH_REL_BASE")
    endif()
    set(${out_var} "${rpath_rel_base}" PARENT_SCOPE)
endfunction()

# Get path to libclang.dll/libclang.so depending on the platform
macro(find_libclang)
    find_package(Clang CONFIG REQUIRED)
    get_target_property(libclang_location libclang LOCATION)
    get_filename_component(libclang_lib_dir "${libclang_location}" DIRECTORY)
endmacro()

# Allow setting a shiboken debug level from the the build system or from the environment
# to all shiboken invocations.
function(shiboken_get_debug_level out_var)
    set(debug_level "")
    if(SHIBOKEN_DEBUG_LEVEL)
        set(debug_level "--debug-level=${SHIBOKEN_DEBUG_LEVEL}")
    elseif(DEFINED ENV{SHIBOKEN_DEBUG_LEVEL})
        set(debug_level "--debug-level=$ENV{SHIBOKEN_DEBUG_LEVEL}")
    endif()
    set(${out_var} "${debug_level}" PARENT_SCOPE)
endfunction()

# Adds a target that runs qdoc on a .qdocconf file, using the qdoc given by QDOC_EXECUTABLE
# (set by setup.py from the Qt specified by --qtpaths). Logs the qdoc version, or a warning if
# qdoc is not available and the documentation is therefore not built.
function(shiboken_add_qdoc_target target qdocconf title)
    if(NOT QDOC_EXECUTABLE)
        message(WARNING "The ${title} documentation was not built: QDOC_EXECUTABLE is not set.")
        return()
    endif()

    execute_process(COMMAND "${QDOC_EXECUTABLE}" --version
                    OUTPUT_VARIABLE qdoc_version OUTPUT_STRIP_TRAILING_WHITESPACE)
    message(STATUS "${title} documentation tool: ${qdoc_version}")

    add_custom_target(${target}
                      COMMAND "${QDOC_EXECUTABLE}" "${qdocconf}"
                      COMMENT "Generating ${title} documentation"
                      VERBATIM)
endfunction()
