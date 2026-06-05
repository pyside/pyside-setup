# Copyright (C) 2026 The Qt Company Ltd.
# SPDX-License-Identifier: BSD-3-Clause

file(RELATIVE_PATH relative_python_site_packages
    "${CMAKE_INSTALL_PREFIX}" "${PYTHON_SITE_PACKAGES}")
set(install_path "${relative_python_site_packages}/PySide6")

if(IOS)
    set(library_type STATIC)
    set(artifact_type ARCHIVE)
    set(install_destination "PySide6")
    set(library_install_path "${install_destination}")
else()
    set(library_type MODULE)
    set(artifact_type LIBRARY)
    set(install_destination "${PYTHON_SITE_PACKAGES}/PySide6")
    set(library_install_path "${install_path}")
endif()

qfp_sbom_get_python_runtime_target(python_runtime_sbom_target)
qfp_add_library(QtQmlFeatures
    FUNCTION_ARGS
        ${library_type} pyside_feature_module.cpp
    LIBRARY_PATH "${library_install_path}"
    SBOM_ARGS
        SBOM_ENTITY_TYPE LIBRARY
        SBOM_DEPENDENCIES ${python_runtime_sbom_target}
)

if(IOS)
    set_target_properties(QtQmlFeatures PROPERTIES
                          OUTPUT_NAME "QtQmlFeatures"
                          ARCHIVE_OUTPUT_DIRECTORY ${PYSIDE_PACKAGE_BINARY_DIR})
else()
    set_target_properties(QtQmlFeatures PROPERTIES
                            PREFIX ""
                            OUTPUT_NAME "QtQmlFeatures${SHIBOKEN_PYTHON_EXTENSION_SUFFIX}"
                            LIBRARY_OUTPUT_DIRECTORY ${PYSIDE_PACKAGE_BINARY_DIR})
    if(WIN32)
        set_target_properties(QtQmlFeatures PROPERTIES SUFFIX ".pyd")
    endif()
endif()

target_link_libraries(QtQmlFeatures pyside6qml Shiboken6::libshiboken)

if(CMAKE_SYSTEM_NAME STREQUAL "Android")
    # See libshiboken/CMakeLists.txt: QtQmlFeatures has no Qt dependency
    # either, so it needs the same 16 KB alignment flag directly.
    target_link_options(QtQmlFeatures PRIVATE "-Wl,-z,max-page-size=16384")
endif()

if(NOT (PYSIDE_IS_CROSS_BUILD OR DISABLE_PYI))
    pyside_setup_ld_prefix()

    set(generate_pyi_sys_path
        "${pysidebindings_BINARY_DIR}"
        "${PYTHON_SITE_PACKAGES}"
        "${PYTHON_SITE_PACKAGES}/../..")
    if(TARGET shibokenmodule)
        list(PREPEND generate_pyi_sys_path "$<TARGET_FILE_DIR:shibokenmodule>/..")
    endif()
    set(generate_pyi_options QtQmlFeatures --sys-path ${generate_pyi_sys_path})
    if(QUIET_BUILD)
        list(APPEND generate_pyi_options "--quiet")
    endif()

    add_custom_target(QtQmlFeatures_pyi ALL
                      COMMAND
                          ${CMAKE_COMMAND} -E env ${ld_prefix}
                          "${SHIBOKEN_PYTHON_INTERPRETER}"
                          "${CMAKE_CURRENT_SOURCE_DIR}/../support/generate_pyi.py"
                          ${generate_pyi_options})
    add_dependencies(QtQmlFeatures_pyi QtQmlFeatures)
    # QtQmlFeatures_pyi runs generate_pyi.py which imports PySide6.QtCore via
    # feature.__init__(), and PySide6.QtQml for resolving the QQmlEngine
    # reference in load_qml_component's signature. Add explicit dependencies so
    # that the Python extension modules are built before pyi generation starts
    if(TARGET QtCore)
        add_dependencies(QtQmlFeatures_pyi QtCore)
    endif()
    if(TARGET QtQml)
        add_dependencies(QtQmlFeatures_pyi QtQml)
    endif()
    if(TARGET shibokenmodule)
        add_dependencies(QtQmlFeatures_pyi shibokenmodule)
    endif()

    install(FILES "${PYSIDE_PACKAGE_BINARY_DIR}/QtQmlFeatures.pyi"
            DESTINATION "${PYTHON_SITE_PACKAGES}/PySide6")
    qfp_sbom_add_files(QtQmlFeatures
        FILES "${PYSIDE_PACKAGE_BINARY_DIR}/QtQmlFeatures.pyi"
        FILE_TYPE "SOURCE"
        INSTALL_PATH "${install_path}"
    )
endif()

install(TARGETS QtQmlFeatures ${artifact_type} DESTINATION ${install_destination})
