# Copyright (C) 2023 The Qt Company Ltd.
# SPDX-License-Identifier: BSD-3-Clause

set(package_name "Shiboken6Tools")

set(CMAKE_AUTOMOC ON)

set(shiboken6_SRC
defaultvalue.cpp defaultvalue.h
generator.cpp generator.h
generatorcontext.cpp generatorcontext.h
main.cpp
shiboken/configurablescope.h
shiboken/cppgenerator.cpp shiboken/cppgenerator.h
shiboken/cppgenerator_container.cpp
shiboken/cppgenerator_smartpointer.cpp
shiboken/ctypenames.h
shiboken/overridecacheentry.h shiboken/overridecacheentry.cpp
shiboken/generatorargument.cpp shiboken/generatorargument.h shiboken/generatorstrings.h
shiboken/headergenerator.cpp shiboken/headergenerator.h
shiboken/overloaddata.cpp shiboken/overloaddata.h
shiboken/pytypenames.h
shiboken/shibokengenerator.cpp shiboken/shibokengenerator.h
)

find_libclang()

if(${STANDALONE})
        list(APPEND CMAKE_INSTALL_RPATH ${base}/Qt/lib)
else()
        list(APPEND CMAKE_INSTALL_RPATH ${QT6_INSTALL_PREFIX}/${QT6_INSTALL_LIBS}
             ${libclang_lib_dir})
endif()

qfp_add_executable(shiboken6
    RUNTIME_PATH "bin"
    FUNCTION_ARGS
        ${shiboken6_SRC}
)
add_executable(Shiboken6::shiboken6 ALIAS shiboken6)
add_dependencies(shiboken6 apiextractor)

set_target_properties(shiboken6 PROPERTIES OUTPUT_NAME shiboken6${shiboken6_SUFFIX})
target_include_directories(shiboken6 PRIVATE
                    ${CMAKE_CURRENT_SOURCE_DIR}/shiboken
                    ${CMAKE_CURRENT_SOURCE_DIR}/qtdoc
                    ${CMAKE_CURRENT_SOURCE_DIR}
                    ${CMAKE_CURRENT_BINARY_DIR}
                    )
target_link_libraries(shiboken6 apiextractor Qt::Core)
if (NOT DISABLE_DOCSTRINGS)
    target_sources(shiboken6 PRIVATE
                   qtdoc/qtdocgenerator.cpp qtdoc/qtdocgenerator.h
                   qtdoc/qtxmltosphinx.cpp qtdoc/qtxmltosphinx.h
                   qtdoc/qtxmltosphinxinterface.h
                   qtdoc/rstformat.h)
    target_compile_definitions(shiboken6 PUBLIC DOCSTRINGS_ENABLED QT_LEAN_HEADERS=1)
endif()

# TODO: We are not actually using the tool_wrapper, but we need to make sure
# the properties for the tool are properly set
shiboken_get_tool_shell_wrapper(shiboken tool_wrapper)

configure_file(shibokenconfig.h.in "${CMAKE_CURRENT_BINARY_DIR}/shibokenconfig.h" @ONLY)

install(TARGETS shiboken6
        EXPORT "${package_name}Targets"
        DESTINATION "bin")
install(EXPORT "${package_name}Targets"
        NAMESPACE "Shiboken6::"
        DESTINATION ${LIB_INSTALL_DIR}/cmake/${package_name})

# Add wheel specific installation
if(NOT is_pyside6_superproject_build)
    install(TARGETS shiboken6
            EXPORT "${package_name}WheelTargets"
            DESTINATION "shiboken6_generator")
    # The SBOM package of shiboken6 only covers the copy installed into bin.
    qfp_sbom_add_files(shiboken6
        FILES "$<TARGET_FILE_NAME:shiboken6>"
        FILE_TYPE "BINARY"
        INSTALL_PATH "shiboken6_generator"
    )

    install(EXPORT "${package_name}WheelTargets"
            NAMESPACE "Shiboken6::"
            DESTINATION "${LIB_INSTALL_DIR}/wheels/cmake/${package_name}"
            FILE "${package_name}Targets.cmake")
endif()

set(shiboken_generator_package_name "shiboken6_generator")

configure_file("${CMAKE_CURRENT_SOURCE_DIR}/_config.py.in"
               "${CMAKE_CURRENT_BINARY_DIR}/_config.py" @ONLY)
install(FILES "${CMAKE_CURRENT_BINARY_DIR}/_config.py"
        DESTINATION "${PYTHON_SITE_PACKAGES}/${shiboken_generator_package_name}")

configure_file("${CMAKE_CURRENT_SOURCE_DIR}/__init__.py.in"
               "${CMAKE_CURRENT_BINARY_DIR}/__init__.py" @ONLY)
install(FILES "${CMAKE_CURRENT_BINARY_DIR}/__init__.py"
       DESTINATION "${PYTHON_SITE_PACKAGES}/${shiboken_generator_package_name}")

# shiboken6 setuptools entry point
install(FILES ${CMAKE_CURRENT_SOURCE_DIR}/../shiboken_tool.py
        DESTINATION bin
        PERMISSIONS
        OWNER_EXECUTE OWNER_WRITE OWNER_READ
        GROUP_EXECUTE GROUP_READ
        WORLD_EXECUTE WORLD_READ)

# Use absolute path instead of relative path, to avoid ninja build errors due to
# duplicate file dependency inconsistency.
set(shiboken_version_relative_path "${CMAKE_CURRENT_SOURCE_DIR}/../../shiboken6/shiboken_version.py")
get_filename_component(shiboken_version_path ${shiboken_version_relative_path} ABSOLUTE)
configure_file("${shiboken_version_path}"
               "${CMAKE_CURRENT_BINARY_DIR}/_git_shiboken_generator_version.py" @ONLY)

install(FILES "${CMAKE_CURRENT_BINARY_DIR}/_git_shiboken_generator_version.py"
        DESTINATION "${PYTHON_SITE_PACKAGES}/${shiboken_generator_package_name}")

# The Python files are licensed differently than the shiboken6 executable.
qfp_sbom_get_open_source_permissive_library_license(python_files_license_expression)
qfp_add_custom_sbom(ShibokenGeneratorPythonFiles
    LICENSE_CONCLUDED_EXPRESSION "${python_files_license_expression}"
)
qfp_sbom_get_relative_python_site_packages(relative_python_site_packages)
qfp_sbom_add_files(ShibokenGeneratorPythonFiles
    FILES
        "${CMAKE_CURRENT_BINARY_DIR}/_config.py"
        "${CMAKE_CURRENT_BINARY_DIR}/__init__.py"
        "${CMAKE_CURRENT_BINARY_DIR}/_git_shiboken_generator_version.py"
    FILE_TYPE "SOURCE"
    INSTALL_PATH "${relative_python_site_packages}/${shiboken_generator_package_name}"
)
qfp_sbom_add_files(ShibokenGeneratorPythonFiles
    FILES "${CMAKE_CURRENT_SOURCE_DIR}/../shiboken_tool.py"
    FILE_TYPE "SOURCE"
    INSTALL_PATH "bin"
)

include(CMakePackageConfigHelpers)

# Copy macros file to build tree
configure_file("${CMAKE_CURRENT_SOURCE_DIR}/../data/${package_name}Macros.cmake"
               "${CMAKE_CURRENT_BINARY_DIR}/${package_name}Macros.cmake"
               COPYONLY)

# Single build-tree and install-tree Config file. There's no need for separate ones because we
# don't specify any PATH_VARS, so the relative path of PACKAGE_PREFIX_DIR does not really matter.
configure_package_config_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/../data/${package_name}Config.cmake.in"
    "${CMAKE_CURRENT_BINARY_DIR}/${package_name}Config.cmake"
     INSTALL_DESTINATION "${LIB_INSTALL_DIR}/cmake/${package_name}"
)
write_basic_package_version_file(
    "${CMAKE_CURRENT_BINARY_DIR}/${package_name}ConfigVersion.cmake"
    VERSION "${shiboken6_VERSION}"
    COMPATIBILITY AnyNewerVersion
    ARCH_INDEPENDENT
)

# Install the config files
install(FILES
    "${CMAKE_CURRENT_BINARY_DIR}/${package_name}Config.cmake"
    "${CMAKE_CURRENT_BINARY_DIR}/${package_name}ConfigVersion.cmake"
    "${CMAKE_CURRENT_BINARY_DIR}/${package_name}Macros.cmake"
    DESTINATION "${LIB_INSTALL_DIR}/cmake/${package_name}")

qfp_sbom_get_build_system_license(build_system_license_expression)
qfp_add_custom_sbom(${package_name}PackageConfigFiles
    LICENSE_CONCLUDED_EXPRESSION "${build_system_license_expression}"
)
qfp_sbom_get_export_file_names(export_files ${package_name}Targets)
qfp_sbom_add_files(${package_name}PackageConfigFiles
    FILES
        "${CMAKE_CURRENT_BINARY_DIR}/${package_name}Config.cmake"
        "${CMAKE_CURRENT_BINARY_DIR}/${package_name}ConfigVersion.cmake"
        "${CMAKE_CURRENT_BINARY_DIR}/${package_name}Macros.cmake"
        ${export_files}
    FILE_TYPE "TEXT"
    INSTALL_PATH "${LIB_INSTALL_DIR}/cmake/${package_name}"
)
if(NOT is_pyside6_superproject_build)
    qfp_sbom_add_files(${package_name}PackageConfigFiles
        FILES ${export_files}
        FILE_TYPE "TEXT"
        INSTALL_PATH "${LIB_INSTALL_DIR}/wheels/cmake/${package_name}"
    )
endif()
