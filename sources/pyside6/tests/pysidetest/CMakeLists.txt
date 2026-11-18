# Copyright (C) 2023 The Qt Company Ltd.
# SPDX-License-Identifier: BSD-3-Clause

project(pysidetest)
project(testbinding)

cmake_minimum_required(VERSION 3.25)

set(QT_USE_QTCORE 1)
# no more supported: include(${QT_USE_FILE})

# removed after qtbase/054b66a65748c9ebfafeca88bf31669a24994237, is this required?
# add_definitions(${Qt${QT_MAJOR_VERSION}Core_DEFINITIONS})

set(CMAKE_AUTOMOC ON)

add_definitions(-DQT_SHARED)
add_definitions(-DRXX_ALLOCATOR_INIT_0)

find_package(Qt6 REQUIRED COMPONENTS Widgets)

set(pysidetest_SRC
connection.h
containertest.cpp containertest.h
flagstest.cpp flagstest.h
hiddenobject.cpp hiddenobject.h
pysidetest_global.h
pysidetest_macros.h
sharedpointertestbench.cpp sharedpointertestbench.h
testobject.cpp testobject.h
testview.cpp testview.h
testwidget.cpp testwidget.h
testqvariantenum.cpp testqvariantenum.h
)

set(testbinding_SRC
${CMAKE_CURRENT_BINARY_DIR}/testbinding/connection_wrapper.cpp
${CMAKE_CURRENT_BINARY_DIR}/testbinding/containertest_wrapper.cpp
${CMAKE_CURRENT_BINARY_DIR}/testbinding/flagsnamespace_classforenum_wrapper.cpp
${CMAKE_CURRENT_BINARY_DIR}/testbinding/testobject_wrapper.cpp
${CMAKE_CURRENT_BINARY_DIR}/testbinding/intvalue_wrapper.cpp
${CMAKE_CURRENT_BINARY_DIR}/testbinding/pysidecpp_wrapper.cpp
${CMAKE_CURRENT_BINARY_DIR}/testbinding/pysidecpp_testobjectwithnamespace_wrapper.cpp
${CMAKE_CURRENT_BINARY_DIR}/testbinding/pysidecpp_testobject2withnamespace_wrapper.cpp
${CMAKE_CURRENT_BINARY_DIR}/testbinding/pysidecpp2_testobjectwithoutnamespace_wrapper.cpp
${CMAKE_CURRENT_BINARY_DIR}/testbinding/qsharedpointer_qobject_wrapper.cpp
${CMAKE_CURRENT_BINARY_DIR}/testbinding/qsharedpointer_int_wrapper.cpp
${CMAKE_CURRENT_BINARY_DIR}/testbinding/sharedpointertestbench_wrapper.cpp
${CMAKE_CURRENT_BINARY_DIR}/testbinding/testview_wrapper.cpp
${CMAKE_CURRENT_BINARY_DIR}/testbinding/testwidget_wrapper.cpp
${CMAKE_CURRENT_BINARY_DIR}/testbinding/testbinding_module_wrapper.cpp
${CMAKE_CURRENT_BINARY_DIR}/testbinding/testqvariantenum_wrapper.cpp
${CMAKE_CURRENT_BINARY_DIR}/testbinding/qvariantholder_wrapper.cpp
)

# Get per module include dirs.
# There are usually 2 paths there:
# ./qt/include/QtCore ; ./qt/mkspecs/linux-g++
# on framework build they are:
# ./qt/include/QtCore ; ./qt/lib/QtCore.framework ; ./qt/mkspecs/macx-clang

set(Qt6Core_DIRECT_INCLUDE_DIR ${Qt6Core_INCLUDE_DIRS})
set(Qt6Gui_DIRECT_INCLUDE_DIR ${Qt6Gui_INCLUDE_DIRS})
set(Qt6Widgets_DIRECT_INCLUDE_DIR ${Qt6Widgets_INCLUDE_DIRS})

# Adjust include headers paths for frameworks.
set(shiboken_framework_include_dirs_option "")
set(shiboken_dir_after_include_dirs_option "")
set(force_process_system_include_paths_option "")
if(CMAKE_HOST_APPLE AND QtCore_is_framework)
    set(shiboken_framework_include_dirs "${QT_FRAMEWORK_INCLUDE_DIR}")
    set(shiboken_framework_include_dirs_option "--framework-include-paths=${shiboken_framework_include_dirs}")

    # Handle -idirafter option. See create_pyside_module() for details.
    make_path(shiboken_dir_after_include_dirs ${QT_INCLUDE_DIR})
    set(shiboken_dir_after_include_dirs_option
        "--dir-after-include-paths=${shiboken_dir_after_include_dirs}")

    make_path(force_process_system_include_paths
        ${QT_INCLUDE_DIR}
        ${QT_FRAMEWORK_INCLUDE_DIR}
    )
    set(force_process_system_include_paths_option
        "--force-process-system-include-paths=${force_process_system_include_paths}")
endif()

set(testbinding_include_dir_list
    ${PYSIDE_PACKAGE_BINARY_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/../../../shiboken6/libshiboken
    ${CMAKE_CURRENT_SOURCE_DIR}/../../PySide6
    ${CMAKE_CURRENT_SOURCE_DIR}/../../libpyside
    ${Qt${QT_MAJOR_VERSION}Core_DIRECT_INCLUDE_DIR}
    ${Qt${QT_MAJOR_VERSION}Gui_DIRECT_INCLUDE_DIR}
    ${Qt${QT_MAJOR_VERSION}Widgets_DIRECT_INCLUDE_DIR}
 )
# Add the qt include dir only for non-framework builds.
if(NOT shiboken_dir_after_include_dirs_option)
    list(APPEND testbinding_include_dir_list ${QT_INCLUDE_DIR})
endif()
make_path(testbinding_include_dirs ${testbinding_include_dir_list})

make_path(testbinding_typesystem_path ${PYSIDE_PACKAGE_SOURCE_DIR}
                                      ${PYSIDE_PACKAGE_BINARY_DIR})

shiboken_get_tool_shell_wrapper(shiboken tool_wrapper)

add_custom_command(
    OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/mjb_rejected_classes.log"
    BYPRODUCTS ${testbinding_SRC}
    COMMAND
        ${tool_wrapper}
        $<TARGET_FILE:Shiboken6::shiboken6>
        ${GENERATOR_EXTRA_FLAGS}
        ${CMAKE_CURRENT_SOURCE_DIR}/pysidetest_global.h
        --include-paths=${testbinding_include_dirs}
        ${shiboken_dir_after_include_dirs_option}
        ${force_process_system_include_paths_option}
        ${shiboken_framework_include_dirs_option}
        --typesystem-paths=${testbinding_typesystem_path}
        --output-directory=${CMAKE_CURRENT_BINARY_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}/typesystem_pysidetest.xml
        --api-version=${SUPPORTED_QT_VERSION}
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    COMMENT "Running generator for test binding..."
)

include_directories(${CMAKE_CURRENT_SOURCE_DIR}
                    ${CMAKE_CURRENT_BINARY_DIR}
                    ${Qt${QT_MAJOR_VERSION}Core_INCLUDE_DIRS}
                    ${Qt${QT_MAJOR_VERSION}Gui_INCLUDE_DIRS}
                    ${Qt${QT_MAJOR_VERSION}Widgets_INCLUDE_DIRS}
                    ${PYSIDE_PACKAGE_SOURCE_DIR}
                    ${QtCore_GEN_DIR}
                    ${QtGui_GEN_DIR}
                    ${QtWidgets_GEN_DIR})

add_library(pysidetest SHARED ${pysidetest_SRC})
set_target_properties(pysidetest PROPERTIES
                      DEFINE_SYMBOL BUILD_PYSIDETEST)

target_link_libraries(pysidetest
                      Shiboken6::libshiboken
                      PySide6::pyside6
                      Qt::Core Qt::Gui Qt::Widgets)

add_library(testbinding MODULE ${testbinding_SRC})
set_property(TARGET testbinding PROPERTY PREFIX "")
set_property(TARGET testbinding PROPERTY OUTPUT_NAME "testbinding${SHIBOKEN_PYTHON_EXTENSION_SUFFIX}")
if(WIN32)
    set_property(TARGET testbinding PROPERTY SUFFIX ".pyd")
endif()

target_link_libraries(testbinding
                      pysidetest
                      pyside6
                      Shiboken6::libshiboken
                      Qt::Core Qt::Gui Qt::Widgets)

add_dependencies(testbinding pyside6 QtCore QtGui QtWidgets pysidetest)
create_generator_target(testbinding)

PYSIDE_TEST(mypy_correctness_test.py)
PYSIDE_TEST(constructor_properties_test.py)
PYSIDE_TEST(container_test.py)
PYSIDE_TEST(decoratedslot_test.py)
PYSIDE_TEST(delegatecreateseditor_test.py)
PYSIDE_TEST(all_modules_load_test.py)
PYSIDE_TEST(bug_1016.py)
PYSIDE_TEST(enum_test.py)
PYSIDE_TEST(heuristics_test.py)
PYSIDE_TEST(homonymoussignalandmethod_test.py)
PYSIDE_TEST(iterable_test.py)
PYSIDE_TEST(list_signal_test.py)
PYSIDE_TEST(mixin_signal_slots_test.py)
PYSIDE_TEST(modelview_test.py)
PYSIDE_TEST(multiple_inheritance_test.py)
PYSIDE_TEST(multiple_cpp_inheritance_test.py)
PYSIDE_TEST(new_inherited_functions_test.py)
PYSIDE_TEST(notify_id.py)
PYSIDE_TEST(properties_test.py)
PYSIDE_TEST(property_python_test.py)
PYSIDE_TEST(snake_case_test.py)
PYSIDE_TEST(true_property_test.py)
PYSIDE_TEST(qapp_like_a_macro_test.py)
PYSIDE_TEST(qvariant_test.py)
PYSIDE_TEST(repr_test.py)
PYSIDE_TEST(shared_pointer_test.py)
PYSIDE_TEST(signal_tp_descr_get_test.py)
PYSIDE_TEST(signal_slot_warning.py)
PYSIDE_TEST(signalandnamespace_test.py)
PYSIDE_TEST(signalemissionfrompython_test.py)
PYSIDE_TEST(signalinstance_equality_test.py)
PYSIDE_TEST(signalwithdefaultvalue_test.py)
PYSIDE_TEST(typedef_signal_test.py)
PYSIDE_TEST(version_test.py)
PYSIDE_TEST(mock_as_slot_test.py)
PYSIDE_TEST(pyenum_relax_options_and_lazy_star_test.py)
