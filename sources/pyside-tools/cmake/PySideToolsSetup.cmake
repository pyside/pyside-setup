# Copyright (C) 2023 The Qt Company Ltd.
# SPDX-License-Identifier: BSD-3-Clause

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_LIST_DIR}")
list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_LIST_DIR}/../../shiboken6/cmake")
list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_LIST_DIR}/../../shiboken6_generator/cmake")
list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_LIST_DIR}/../../pyside6/cmake")

include(ShibokenHelpers)
include(ShibokenGeneratorCMakeHelpers)
include(ShibokenGeneratorSbomHelpers)
include(ShibokenGeneratorTargetHelpers)
include(PySideToolsHelpers)
include(PySideSbomWheelMapping)

# from cmake.conf
set(PySideTools_VERSION
    "${pyside_MAJOR_VERSION}.${pyside_MINOR_VERSION}.${pyside_MICRO_VERSION}")
compute_config_py_values(PySideTools_VERSION)

pyside_tools_internal_detect_if_cross_building()
pyside_tools_internal_set_up_extra_dependency_paths()

find_package(Qt6 REQUIRED COMPONENTS Core HostInfo)

# Don't display "up-to-date / install" messages when installing, to reduce visual clutter.
if (QUIET_BUILD)
    set(CMAKE_INSTALL_MESSAGE NEVER)
endif()
