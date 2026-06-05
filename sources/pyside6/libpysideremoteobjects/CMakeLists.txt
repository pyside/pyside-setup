# Copyright (C) 2025 Ford Motor Company
# SPDX-License-Identifier: BSD-3-Clause

find_package(Qt6 REQUIRED COMPONENTS Core RepParser RemoteObjects RemoteObjectsPrivate)

set(libpysideremoteobjects_HEADERS
    pysidecapsulemethod_p.h
    pysidedynamicclass_p.h
    pysidedynamiccommon_p.h
    pysidedynamicenum_p.h
    pysidedynamicpod_p.h
    pysiderephandler_p.h
)

set(libpysideremoteobjects_SRC
    pysiderephandler.cpp
    pysidecapsulemethod.cpp
    pysidedynamiccommon.cpp
    pysidedynamicclass.cpp
    pysidedynamicpod.cpp
    pysidedynamicenum.cpp
    ${libpysideremoteobjects_HEADERS}
)

list(GET Qt6RepParser_INCLUDE_DIRS 0 REPPARSER_DIR)

include(QtTargetHelpers)
include(QtTestHelpers)
include(QtLalrHelpers)

qfp_sbom_get_python_runtime_target(python_runtime_sbom_target)
qfp_add_library(pyside6remoteobjects
    FUNCTION_ARGS
        STATIC ${libpysideremoteobjects_SRC}
    RUNTIME_PATH bin
    LIBRARY_PATH "${LIB_INSTALL_DIR}"
    ARCHIVE_PATH "${LIB_INSTALL_DIR}"
    SBOM_ARGS
        SBOM_DEPENDENCIES ${python_runtime_sbom_target}
)

target_include_directories(pyside6remoteobjects PRIVATE
    ${REPPARSER_DIR}
    ${Qt${QT_VERSION_MAJOR}Core_PRIVATE_INCLUDE_DIRS}
    ${Qt${QT_MAJOR_VERSION}RemoteObjects_INCLUDE_DIRS}
    ${Qt${QT_MAJOR_VERSION}RemoteObjects_PRIVATE_INCLUDE_DIRS}
    ${SHIBOKEN_INCLUDE_DIR}
    ${SHIBOKEN_PYTHON_INCLUDE_DIR}
    ${CMAKE_CURRENT_BINARY_DIR}               # Include the component-specific build directory
)
target_include_directories(pyside6remoteobjects INTERFACE "${CMAKE_CURRENT_SOURCE_DIR}")

target_link_libraries(pyside6remoteobjects PRIVATE
    Shiboken6::libshiboken                    # Added internally by the create_pyside_module function
    PySide6::pyside6
    Qt6::Core
    Qt6::RemoteObjectsPrivate
)

qt_process_qlalr(
    pyside6remoteobjects
    "${REPPARSER_DIR}/parser.g"
    ""
)

set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -D QT_NO_CAST_FROM_ASCII -D QT_NO_CAST_TO_ASCII")

# documentation
set(libpysideremoteobjects_qdocconf "${CMAKE_CURRENT_BINARY_DIR}/libpysideremoteobjects.qdocconf")

configure_file("${CMAKE_CURRENT_SOURCE_DIR}/libpysideremoteobjects.qdocconf.in"
               "${libpysideremoteobjects_qdocconf}" @ONLY)

add_custom_target(libpysideremoteobjects_qdoc
                  COMMAND qdoc "${libpysideremoteobjects_qdocconf}"
                  COMMENT "Generating libpysideremoteobjects documentation"
                  VERBATIM)

#
# install stuff
#

install(FILES ${libpysideremoteobjects_HEADERS}
        DESTINATION include/${BINDING_NAME}${pyside6remoteobjects_SUFFIX})
qfp_sbom_add_files(pyside6remoteobjects
    FILES ${libpysideremoteobjects_HEADERS}
    FILE_TYPE "SOURCE"
    INSTALL_PATH "include/${BINDING_NAME}${pyside6remoteobjects_SUFFIX}"
)

install(TARGETS pyside6remoteobjects EXPORT PySide6RemoteObjectsTargets
                           LIBRARY DESTINATION "${LIB_INSTALL_DIR}"
                           ARCHIVE DESTINATION "${LIB_INSTALL_DIR}"
                           RUNTIME DESTINATION bin)
