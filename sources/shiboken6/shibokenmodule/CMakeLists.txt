# Copyright (C) 2023 The Qt Company Ltd.
# SPDX-License-Identifier: BSD-3-Clause

configure_file("${CMAKE_CURRENT_SOURCE_DIR}/shibokenmodule.txt.in"
               "${CMAKE_CURRENT_BINARY_DIR}/shibokenmodule.txt" @ONLY)

set(sample_SRC ${CMAKE_CURRENT_BINARY_DIR}/Shiboken/shiboken_module_wrapper.cpp)

set(shibokenmodule_TYPESYSTEM ${CMAKE_CURRENT_SOURCE_DIR}/typesystem_shiboken.xml)

shiboken_get_tool_shell_wrapper(shiboken tool_wrapper)

# The module does not need the GIL in a free-threaded build
list(APPEND GENERATOR_EXTRA_FLAGS --module-uses-gil=auto)

add_custom_command(
    OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/mjb_rejected_classes.log"
    BYPRODUCTS ${sample_SRC}
    COMMAND
        ${tool_wrapper}
        $<TARGET_FILE:Shiboken6::shiboken6>
        --project-file=${CMAKE_CURRENT_BINARY_DIR}/shibokenmodule.txt
        ${GENERATOR_EXTRA_FLAGS}
    DEPENDS ${shibokenmodule_TYPESYSTEM}
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    COMMENT "Running generator for 'Shiboken'..."
)

if(IOS)
    set(library_type STATIC)
else()
    set(library_type MODULE)
endif()

file(RELATIVE_PATH relative_python_site_packages
    "${CMAKE_INSTALL_PREFIX}" "${PYTHON_SITE_PACKAGES}")
set(install_path "${relative_python_site_packages}/shiboken6")
qfp_sbom_get_python_runtime_target(python_runtime_sbom_target)
qfp_add_library(shibokenmodule
    FUNCTION_ARGS
        ${library_type} ${sample_SRC}
    ARCHIVE_PATH "${install_path}"
    LIBRARY_PATH "${install_path}"
    SBOM_ARGS
        SBOM_ENTITY_TYPE LIBRARY
        SBOM_DEPENDENCIES ${python_runtime_sbom_target}
)

target_include_directories(shibokenmodule PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
                                                  ${CMAKE_SOURCE_DIR})

set(set_props_args "")
set_target_properties(shibokenmodule PROPERTIES PREFIX "")
if(IOS)
    list(APPEND set_props_args
        OUTPUT_NAME "Shiboken"
        ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/..")
else()
    list(APPEND set_props_args
        OUTPUT_NAME "Shiboken${PYTHON_EXTENSION_SUFFIX}"
        LIBRARY_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/.."
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/..")
    if(WIN32)
        list(APPEND set_props_args SUFFIX ".pyd")
    endif()
endif()
set_target_properties(shibokenmodule PROPERTIES ${set_props_args})
target_link_libraries(shibokenmodule PUBLIC libshiboken)

create_generator_target(shibokenmodule)

qfp_strip_library("shibokenmodule")

install(TARGETS shibokenmodule DESTINATION ${PYTHON_SITE_PACKAGES}/shiboken6)

configure_file("${CMAKE_CURRENT_SOURCE_DIR}/_config.py.in"
               "${CMAKE_CURRENT_BINARY_DIR}/_config.py" @ONLY)
install(FILES "${CMAKE_CURRENT_BINARY_DIR}/_config.py"
        DESTINATION "${PYTHON_SITE_PACKAGES}/shiboken6")
qfp_sbom_add_files(shibokenmodule
    FILES "${CMAKE_CURRENT_BINARY_DIR}/_config.py"
    FILE_TYPE "SOURCE"
    INSTALL_PATH "${install_path}"
)

if ("${MINIMUM_PYTHON_VERSION}" STREQUAL "")
    set(MINIMUM_PYTHON_VERSION None)
    set(MAXIMUM_PYTHON_VERSION None)
endif()

# PYSIDE-1497: This `..` is the crucial trick to unify the path location of `Shiboken`.
configure_file("${CMAKE_CURRENT_SOURCE_DIR}/__init__.py.in"
               "${CMAKE_CURRENT_BINARY_DIR}/../__init__.py" @ONLY)
# PYSIDE-1415: Copy Shiboken.pyi into the target.
configure_file("${CMAKE_CURRENT_SOURCE_DIR}/Shiboken.pyi"
               "${CMAKE_CURRENT_BINARY_DIR}/../Shiboken.pyi" @ONLY)
# typing support for mypy
configure_file("${CMAKE_CURRENT_SOURCE_DIR}/py.typed.in"
               "${CMAKE_CURRENT_BINARY_DIR}/../py.typed" @ONLY)
install(FILES "${CMAKE_CURRENT_BINARY_DIR}/../Shiboken.pyi"
              "${CMAKE_CURRENT_BINARY_DIR}/../py.typed"
        DESTINATION "${PYTHON_SITE_PACKAGES}/shiboken6")
qfp_sbom_add_files(shibokenmodule
    FILES "${CMAKE_CURRENT_BINARY_DIR}/../Shiboken.pyi"
    FILE_TYPE "SOURCE"
    INSTALL_PATH "${install_path}"
)
qfp_sbom_add_files(shibokenmodule
    FILES "${CMAKE_CURRENT_BINARY_DIR}/../py.typed"
    FILE_TYPE "TEXT"
    INSTALL_PATH "${install_path}"
)

# PYSIDE-1497: This `..` is the crucial trick to unify the path location of `Shiboken`.
install(FILES "${CMAKE_CURRENT_BINARY_DIR}/../__init__.py"
       DESTINATION "${PYTHON_SITE_PACKAGES}/shiboken6")
qfp_sbom_add_files(shibokenmodule
    FILES "${CMAKE_CURRENT_BINARY_DIR}/../__init__.py"
    FILE_TYPE "SOURCE"
    INSTALL_PATH "${install_path}"
)

# Use absolute path instead of relative path, to avoid ninja build errors due to
# duplicate file dependency inconsistency.
set(shiboken_version_relative_path "${CMAKE_CURRENT_SOURCE_DIR}/../shiboken_version.py")
get_filename_component(shiboken_version_path ${shiboken_version_relative_path} ABSOLUTE)
configure_file("${shiboken_version_path}"
               "${CMAKE_CURRENT_BINARY_DIR}/_git_shiboken_module_version.py" @ONLY)

install(FILES "${CMAKE_CURRENT_BINARY_DIR}/_git_shiboken_module_version.py"
        DESTINATION "${PYTHON_SITE_PACKAGES}/shiboken6")
qfp_sbom_add_files(shibokenmodule
    FILES "${CMAKE_CURRENT_BINARY_DIR}/_git_shiboken_module_version.py"
    FILE_TYPE "SOURCE"
    INSTALL_PATH "${install_path}"
)
