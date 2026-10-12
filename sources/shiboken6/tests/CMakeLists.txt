# Copyright (C) 2023 The Qt Company Ltd.
# SPDX-License-Identifier: BSD-3-Clause

cmake_minimum_required(VERSION 3.25)

if(BUILD_TESTS)
    find_package(Qt${QT_MAJOR_VERSION}Test 6.0 REQUIRED)
endif()

# Failpoint names vs. call sites; here as libshiboken precedes enable_testing().
if(SHIBOKEN_PYTHON_FREE_THREADED)
    add_test(NAME failpoint_names
             COMMAND ${Python_EXECUTABLE}
                     "${CMAKE_CURRENT_SOURCE_DIR}/../libshiboken/check_failpoint_names.py")
endif()

add_subdirectory(libminimal)
if(NOT DEFINED MINIMAL_TESTS)
    add_subdirectory(libsample)
    add_subdirectory(libsmart)
    add_subdirectory(libother)
endif()

shiboken_get_debug_level(debug_level)

if(WIN32 OR DEFINED AVOID_PROTECTED_HACK)
    message(STATUS "Tests will be generated avoiding the protected hack!")
    set(GENERATOR_EXTRA_FLAGS --avoid-protected-hack)
else()
    message(STATUS "Tests will be generated using the protected hack!")
    set(GENERATOR_EXTRA_FLAGS )
endif()
list(APPEND GENERATOR_EXTRA_FLAGS ${SHIBOKEN_GENERATOR_EXTRA_FLAGS} ${debug_level})
if (CMAKE_CROSSCOMPILING)
    list(APPEND GENERATOR_EXTRA_FLAGS
         "--platform=${CMAKE_SYSTEM_NAME}" "--arch=${CMAKE_SYSTEM_PROCESSOR}"
         "--compiler-path=${CMAKE_CXX_COMPILER}")
endif()

# The test bindings do not need the GIL in a free-threaded build
list(APPEND GENERATOR_EXTRA_FLAGS --module-uses-gil=auto)

add_subdirectory(minimalbinding)
if(NOT DEFINED MINIMAL_TESTS)
    add_subdirectory(samplebinding)
    add_subdirectory(smartbinding)
    add_subdirectory(otherbinding)
endif()

if(DEFINED MINIMAL_TESTS)
    file(GLOB TEST_FILES minimalbinding/*_test.py)
else()
    file(GLOB TEST_FILES minimalbinding/*_test.py
                         samplebinding/*_test.py
                         otherbinding/*_test.py
                         smartbinding/*_test.py
                         shibokenmodule/*_test.py)
endif()
list(SORT TEST_FILES)

set(test_blacklist "")

if(SHIBOKEN_IS_CROSS_BUILD)
    # Python_EXECUTABLE will be empty when cross-building.
    message(WARNING
        "Running tests when cross-compiling is not supported because it would require running "
        "a target python interpreter which might have a different architecture than the host."
    )
else()
    find_package(
        Python
        ${USE_PYTHON_VERSION}
        REQUIRED
        COMPONENTS Interpreter Development
    )
endif()

if(NOT CTEST_TESTING_TIMEOUT)
    set(CTEST_TESTING_TIMEOUT 60)
endif()

get_filename_component(BUILD_DIR "${libminimal_BINARY_DIR}" DIRECTORY)
get_filename_component(BUILD_DIR "${BUILD_DIR}" DIRECTORY)
get_filename_component(BUILD_DIR "${BUILD_DIR}" DIRECTORY)
foreach(test_file ${TEST_FILES})
    string(REGEX MATCH "/([^/]+)(binding|module)/([^/]+)_test.py" tmp ${test_file})
    set(test_name "${CMAKE_MATCH_1}_${CMAKE_MATCH_3}")
    list(FIND test_blacklist ${test_name} expect_fail)
    add_test(${test_name} ${Python_EXECUTABLE} ${test_file})
    set_tests_properties(${test_name} PROPERTIES ENVIRONMENT "BUILD_DIR=${BUILD_DIR}")
    set_tests_properties(${test_name} PROPERTIES TIMEOUT ${CTEST_TESTING_TIMEOUT})
    if (${expect_fail} GREATER -1)
        set_tests_properties(${test_name} PROPERTIES WILL_FAIL TRUE)
    endif()
endforeach()

# Free-threaded builds only; not *_test.py, so the glob above skips them.
if(SHIBOKEN_PYTHON_FREE_THREADED)
    foreach(ft_test samplebinding/refcount_freethreading)
        string(REPLACE "binding/" "_" ft_name ${ft_test})
        add_test(${ft_name} ${Python_EXECUTABLE} "${CMAKE_CURRENT_SOURCE_DIR}/${ft_test}.py")
        set_tests_properties(${ft_name} PROPERTIES ENVIRONMENT "BUILD_DIR=${BUILD_DIR}"
                                                   TIMEOUT ${CTEST_TESTING_TIMEOUT})
    endforeach()
endif()
