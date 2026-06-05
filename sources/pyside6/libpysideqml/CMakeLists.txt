# Copyright (C) 2023 The Qt Company Ltd.
# SPDX-License-Identifier: BSD-3-Clause

find_package(Qt6 COMPONENTS Core CorePrivate Qml QmlPrivate)

set(libpysideqml_libraries Qt::Core Qt::CorePrivate Qt::Qml Qt::QmlPrivate)

set(libpysideqml_HEADERS # installed below
    pysideautoproperties.h
    pysidechange.h
    pysidecomputed_p.h
    pysideeffect_p.h
    pysidewatch_p.h
    pysideqmlcomponent.h
    pysideqmlattached.h
    pysideqmlattached_p.h
    pysideqmlextended_p.h
    pysideqmlforeign_p.h
    pysideqml.h
    pysideqmllistproperty_p.h
    pysideqmllistpropertymixin.h
    pysideqmlmacros.h
    pysideqmlmetacallerror_p.h
    pysideqmlnamedelement_p.h
    pysideqmlregistertype.h
    pysideqmlregistertype_p.h
    pysideqmltypeinfo_p.h
    pysideqmluncreatable.h
)

set(libpysideqml_SRC
    pysideautoproperties.cpp
    pysidechange.cpp
    pysidecomputed.cpp
    pysideeffect.cpp
    pysidewatch.cpp
    pysideqmlcomponent.cpp
    pysideqml.cpp
    pysideqmlattached.cpp
    pysideqmlforeign.cpp
    pysideqmlextended.cpp
    pysideqmlregistertype.cpp
    pysideqmlmetacallerror.cpp
    pysideqmllistproperty.cpp
    pysideqmllistpropertymixin.cpp
    pysideqmlnamedelement.cpp
    pysideqmluncreatable.cpp
    pysideqmltypeinfo.cpp
    ${libpysideqml_HEADERS}
)

# Hack for // https://github.com/python/cpython/issues/86286 causes issues
set_source_files_properties(
    pysideqmlmetacallerror.cpp PROPERTIES SKIP_UNITY_BUILD_INCLUSION ON
)

if(IOS)
    set(library_type STATIC)
else()
    set(library_type SHARED)
endif()

qfp_sbom_get_python_runtime_target(python_runtime_sbom_target)
qfp_add_library(pyside6qml
    FUNCTION_ARGS
        ${library_type} ${libpysideqml_SRC} ${other_files}
    RUNTIME_PATH bin
    LIBRARY_PATH "${LIB_INSTALL_DIR}"
    ARCHIVE_PATH "${LIB_INSTALL_DIR}"
    SBOM_ARGS
        SBOM_DEPENDENCIES ${python_runtime_sbom_target}
)

add_library(PySide6::pyside6qml ALIAS pyside6qml)

target_include_directories(pyside6qml PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
)

target_compile_definitions(pyside6qml PRIVATE -DQT_LEAN_HEADERS=1 -DQT_NO_KEYWORDS=1)

target_link_libraries(pyside6qml
                      PRIVATE PySide6::pyside6 Shiboken6::libshiboken ${libpysideqml_libraries})

set(set_props_args DEFINE_SYMBOL BUILD_LIBPYSIDEQML)
if(IOS)
    list(APPEND set_props_args OUTPUT_NAME "pyside6qml${pyside6_SUFFIX}")
else()
    list(APPEND set_props_args
        VERSION ${BINDING_API_VERSION}
        SOVERSION "${PYSIDE_SO_VERSION}"
        OUTPUT_NAME
            "pyside6qml${pyside6_SUFFIX}${SHIBOKEN_PYTHON_SHARED_LIBRARY_SUFFIX}"
    )
endif()
set_target_properties(pyside6qml PROPERTIES ${set_props_args})

target_compile_definitions(pyside6qml PRIVATE -DQT_LEAN_HEADERS=1)

set_property(TARGET pyside6qml PROPERTY CXX_STANDARD 17)

if(PYSIDE_QT_CONF_PREFIX)
    set_property(SOURCE pysideqml.cpp
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

qfp_strip_library("pyside6qml")

install(FILES ${libpysideqml_HEADERS}
        DESTINATION include/${BINDING_NAME}${pyside6qml_SUFFIX})
qfp_sbom_add_files(pyside6qml
    FILES ${libpysideqml_HEADERS}
    FILE_TYPE "SOURCE"
    INSTALL_PATH "include/${BINDING_NAME}${pyside6qml_SUFFIX}"
)

#built-time installation
install(TARGETS pyside6qml EXPORT PySide6Targets
                           LIBRARY DESTINATION "${LIB_INSTALL_DIR}"
                           ARCHIVE DESTINATION "${LIB_INSTALL_DIR}"
                           RUNTIME DESTINATION bin)

# install-tree or relocatable package installation
if(NOT is_pyside6_superproject_build)
    set_target_properties(pyside6qml PROPERTIES
                        VERSION ${PYSIDE_SO_VERSION})
    install(TARGETS pyside6qml EXPORT PySide6WheelTargets
                            LIBRARY DESTINATION "PySide6"
                            ARCHIVE DESTINATION "PySide6"
                            RUNTIME DESTINATION "PySide6")
    # The SBOM package of pyside6qml only covers the copy installed into LIB_INSTALL_DIR.
    set(pyside6qml_wheel_files "$<TARGET_FILE_NAME:pyside6qml>")
    if(WIN32)
        list(APPEND pyside6qml_wheel_files "$<TARGET_LINKER_FILE_NAME:pyside6qml>")
    endif()
    qfp_sbom_add_files(pyside6qml
        FILES ${pyside6qml_wheel_files}
        FILE_TYPE "BINARY"
        INSTALL_PATH "PySide6"
    )
endif()

# documentation
set(libpysideqml_qdocconf "${CMAKE_CURRENT_BINARY_DIR}/libpysideqml.qdocconf")

configure_file("${CMAKE_CURRENT_SOURCE_DIR}/libpysideqml.qdocconf.in"
               "${libpysideqml_qdocconf}" @ONLY)

add_custom_target(libpysideqml_qdoc
                 COMMAND qdoc "${libpysideqml_qdocconf}"
                 COMMENT "Generating libpysideqml documentation"
                 VERBATIM)
