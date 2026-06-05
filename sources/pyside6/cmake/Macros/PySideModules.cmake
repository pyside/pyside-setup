# Copyright (C) 2023 The Qt Company Ltd.
# SPDX-License-Identifier: BSD-3-Clause

include(CMakeParseArguments)

# Checks whether any unparsed arguments have been passed to the function at the call site.
# Use this right after `cmake_parse_arguments`.
function(pyside_internal_validate_all_args_are_parsed prefix type)
    if(DEFINED ${prefix}_UNPARSED_ARGUMENTS)
        message(FATAL_ERROR
            "Unknown arguments were passed to ${type}: (${${prefix}_UNPARSED_ARGUMENTS})")
    endif()
endfunction()

# A version of cmake_parse_arguments that makes sure all arguments are processed and errors out
# with a message about ${type} having received unknown arguments.
macro(pyside_parse_all_arguments prefix type flags options multiopts)
    cmake_parse_arguments(${prefix} "${flags}" "${options}" "${multiopts}" ${ARGN})
    pyside_internal_validate_all_args_are_parsed("${prefix}" "${type}")
endmacro()

macro(make_path varname)
   # accepts any number of path variables
   string(REPLACE ";" "${PATH_SEP}" ${varname} "${ARGN}")
endmacro()

macro(unmake_path varname)
   string(REPLACE "${PATH_SEP}" ";" ${varname} "${ARGN}")
endmacro()

# Check for presence of QtOpenGL and modify module variables
# accordingly
macro(check_qt_opengl module include_var deps_var dropped_entries_var)
    if (Qt${QT_MAJOR_VERSION}OpenGL_FOUND)
        message(STATUS "Qt${QT_MAJOR_VERSION}${module}: Building with OpenGL")
        list(APPEND ${include_var} ${Qt${QT_MAJOR_VERSION}OpenGL_INCLUDE_DIRS}
                                   ${QtOpenGL_GEN_DIR})
        list(APPEND ${deps_var} QtOpenGL)
    else()
        message(STATUS "Qt${QT_MAJOR_VERSION}${module}: Dropping OpenGL")
        # This is a dummy entry creating a conditional typesystem keyword
        list(APPEND ${dropped_entries_var} "QtOpenGL")
    endif()
endmacro()

# set size optimization flags for pyside6
macro(append_size_optimization_flags _module_name)
    if(NOT QFP_NO_OVERRIDE_OPTIMIZATION_FLAGS)
        if ("${CMAKE_CXX_COMPILER_ID}" STREQUAL "MSVC")
            target_compile_options(${_module_name} PRIVATE /Gy /Gw /EHsc)
            target_link_options(${_module_name} PRIVATE LINKER:/OPT:REF)
        elseif ("${CMAKE_CXX_COMPILER_ID}" MATCHES "GNU|CLANG")
            target_compile_options(${_module_name} PRIVATE -ffunction-sections -fdata-sections -fno-exceptions)
            target_link_options(${_module_name} PRIVATE LINKER:--gc-sections)
        endif()
    endif()
endmacro()

# Sets the ld_prefix variable in the calling scope to the platform-specific library
# path prefix (DYLD_LIBRARY_PATH / LD_LIBRARY_PATH / PATH) pointing at the build-tree
# locations of libpyside, libpysideqml, libpysideremoteobjects, and libshiboken6.
# Must be called after pysidebindings_BINARY_DIR and Shiboken6::libshiboken are available.
# Used to set up the environment when invoking generate_pyi.py for .pyi stub generation.
macro(pyside_setup_ld_prefix)
    # Need to set the LD_ env vars before invoking the script, because it might use build-time
    # libraries instead of install time libraries.
    if (WIN32)
        set(ld_prefix_var_name "PATH")
    elseif(APPLE)
        set(ld_prefix_var_name "DYLD_LIBRARY_PATH")
    else()
        set(ld_prefix_var_name "LD_LIBRARY_PATH")
    endif()

    # Get the build type, default to RELEASE if not set
    if(CMAKE_BUILD_TYPE)
        string(TOUPPER "${CMAKE_BUILD_TYPE}" _build_type)
    else()
        set(_build_type "RELEASE")
    endif()

    set(ld_prefix_list "")
    list(APPEND ld_prefix_list "${pysidebindings_BINARY_DIR}/libpyside")
    list(APPEND ld_prefix_list "${pysidebindings_BINARY_DIR}/libpysideqml")
    list(APPEND ld_prefix_list "${pysidebindings_BINARY_DIR}/libpysideremoteobjects")

    # For imported targets (shiboken found via find_package), resolve the library directory
    # from IMPORTED_LOCATION. Try the current build type first, then RELEASE, then NONE
    # (NONE is used by distros like Arch Linux that build without an explicit build type).
    # For real build targets (shiboken built from source), IMPORTED_LOCATION_* properties do
    # not exist, but the build-tree shiboken6 Python package loads the correct libshiboken
    # via its own RPATH, so no LD_LIBRARY_PATH entry is needed.
    get_target_property(_shiboken_lib_is_imported Shiboken6::libshiboken IMPORTED)
    if(_shiboken_lib_is_imported)
        get_target_property(_shiboken_lib_location Shiboken6::libshiboken IMPORTED_LOCATION_${_build_type})
        if(NOT _shiboken_lib_location)
            get_target_property(_shiboken_lib_location Shiboken6::libshiboken IMPORTED_LOCATION_RELEASE)
        endif()
        if(NOT _shiboken_lib_location)
            get_target_property(_shiboken_lib_location Shiboken6::libshiboken IMPORTED_LOCATION_NONE)
        endif()
        if(_shiboken_lib_location)
            get_filename_component(SHIBOKEN_SHARED_LIBRARY_DIR "${_shiboken_lib_location}" DIRECTORY)
            list(APPEND ld_prefix_list "${SHIBOKEN_SHARED_LIBRARY_DIR}")
        endif()
    endif()
    if(WIN32)
        list(APPEND ld_prefix_list "${QT6_INSTALL_PREFIX}/${QT6_INSTALL_BINS}")
    endif()

    list(JOIN ld_prefix_list "${PATH_SEP}" ld_prefix_values_string)
    set(ld_prefix "${ld_prefix_var_name}=${ld_prefix_values_string}")

    # Append any existing ld_prefix values, so existing PATH, LD_LIBRARY_PATH, etc.
    # On Windows it is needed because pyside modules import Qt,
    # and the Qt modules are found from PATH.
    # On Linux and macOS, existing values might be set to find system libraries correctly.
    # For example on openSUSE when compiling with icc, libimf.so from Intel has to be found.
    if(WIN32)
        # Get the value of PATH with CMake separators.
        file(TO_CMAKE_PATH "$ENV{${ld_prefix_var_name}}" path_value)

        # Replace the CMake list separators with "\;"s, to avoid the PATH values being
        # interpreted as CMake list elements, we actually want to pass the whole string separated
        # by ";" to the command line.
        if(path_value)
            make_path(path_value "${path_value}")
            string(APPEND ld_prefix "${PATH_SEP}${path_value}")
        endif()
    else()
        # Handles both macOS and Linux.
        set(env_value "$ENV{${ld_prefix_var_name}}")
        if(env_value)
            string(APPEND ld_prefix ":${env_value}")
        endif()
    endif()
endmacro()

# Sample usage
# create_pyside_module(NAME QtGui
#                      INCLUDE_DIRS QtGui_include_dirs
#                      LIBRARIES QtGui_libraries
#                      DEPS QtGui_deps
#                      TYPESYSTEM_PATH CMAKE_CURRENT_SOURCE_DIR
#                      SOURCES QtGui_SRC
#                      STATIC_SOURCES QtGui_static_sources
#                      TYPESYSTEM_NAME ${CMAKE_CURRENT_BINARY_DIR}/typesystem_gui.xml
#                      DROPPED_ENTRIES QtGui_DROPPED_ENTRIES
#                      GLUE_SOURCES QtGui_glue_sources)
macro(create_pyside_module)
    set(opt_args "")
    set(single_args
        NAME
        TYPESYSTEM_PATH
        TYPESYSTEM_NAME
    )
    set(multi_args
        INCLUDE_DIRS
        LIBRARIES
        DEPS
        SOURCES
        STATIC_SOURCES
        DROPPED_ENTRIES
        GLUE_SOURCES
        ADDITIONAL_INCLUDE_DIRS
    )

    pyside_parse_all_arguments("module" "create_pyside_module"
        "${opt_args}"
        "${single_args}"
        "${multi_args}"
        ${ARGN}
    )

    if ("${module_NAME}" STREQUAL "")
        message(FATAL_ERROR "create_pyside_module needs a NAME value.")
    endif()
    if ("${module_TYPESYSTEM_PATH}" STREQUAL "")
        message(FATAL_ERROR "create_pyside_module needs a TYPESYSTEM_PATH value.")
    endif()
    if ("${module_SOURCES}" STREQUAL "")
        message(FATAL_ERROR "create_pyside_module needs at least one SOURCES value.")
    endif()

    string(REGEX REPLACE ^Qt "" _module_suffix ${module_NAME})
    string(TOLOWER ${_module_suffix} _module)

    if(${module_GLUE_SOURCES})
        set (module_GLUE_SOURCES "${${module_GLUE_SOURCES}}")
    else()
        set (module_GLUE_SOURCES "")
    endif()

    if (NOT EXISTS ${module_TYPESYSTEM_NAME})
        set(typesystem_path ${CMAKE_CURRENT_SOURCE_DIR}/typesystem_${_module}.xml)
    else()
        set(typesystem_path ${module_TYPESYSTEM_NAME})
    endif()

    # Create typesystem XML dependencies list, so that whenever they change, shiboken is invoked
    # automatically.
    # First add the main file.
    set(total_type_system_files ${typesystem_path})

    get_filename_component(typesystem_root "${CMAKE_CURRENT_SOURCE_DIR}" DIRECTORY)

    set(deps ${module_NAME} ${${module_DEPS}})
    foreach(dep ${deps})
        set(glob_expression "${typesystem_root}/${dep}/*.xml")
        file(GLOB type_system_files ${glob_expression})
        set(total_type_system_files ${total_type_system_files} ${type_system_files})
    endforeach(dep)

    # Remove any possible duplicates.
    list(REMOVE_DUPLICATES total_type_system_files)

    # Contains include directories to pass to shiboken's preprocessor (mkspec / global)
    get_target_property(qt_platform_includes Qt${QT_MAJOR_VERSION}::Platform
                        INTERFACE_INCLUDE_DIRECTORIES)
    # Add QtCore since include conventions are sometimes violated for its classes
    get_target_property(qt_core_includes Qt${QT_MAJOR_VERSION}::Core
                        INTERFACE_INCLUDE_DIRECTORIES)
    # Module includes
    get_target_property(qt_module_includes Qt${QT_MAJOR_VERSION}::${_module_suffix}
                        INTERFACE_INCLUDE_DIRECTORIES)

    # For Apple framework builds, Qt 6.12 no longer passes $prefix/include dir (which holds the
    # forwarding headers and the headers of the header-only modules) as an include directory,
    # but as an "-idirafter" compile option on the Platform target, so that a prefixed include
    # resolves to the framework instead of to a forwarding header.
    # Extract the option path, strip the SHELL prefix, and pass it to shiboken.
    get_target_property(qt_platform_compile_options Qt${QT_MAJOR_VERSION}::Platform
                        INTERFACE_COMPILE_OPTIONS)
    set(qt_dir_after_includes "")
    foreach(compile_option IN LISTS qt_platform_compile_options)
        if(compile_option MATCHES "^SHELL:-idirafter +(.*)$")
            list(APPEND qt_dir_after_includes "${CMAKE_MATCH_1}")
        endif()
    endforeach()

    set(shiboken_include_dir_list
        ${PYSIDE_PACKAGE_SOURCE_DIR}
        ${qt_platform_includes}
        ${qt_core_includes}
        ${qt_module_includes}
    )
    if(module_ADDITIONAL_INCLUDE_DIRS)
        list(APPEND shiboken_include_dir_list ${${module_ADDITIONAL_INCLUDE_DIRS}})
    endif()

    # Remove duplicates and transform the path separators into something
    # shiboken understands.
    list(REMOVE_DUPLICATES shiboken_include_dir_list)
    make_path(shiboken_include_dirs ${shiboken_include_dir_list})

    set(force_process_system_include_paths_list "")
    # When building against system Qt (as it happens with yocto / Boot2Qt), the Qt headers are
    # considered system headers by clang_Location_isInSystemHeader, and thus shiboken will not
    # process them.
    #
    # We do want to process them.
    #
    # Tell shiboken to consider them as special typesystem system include paths, which ensures
    # the types are processed and extracted.
    #
    # This option is opt-in because it might cause problems if there are other system headers
    # installed in the same location as the Qt ones, resulting in processing more non-Qt system
    # types that might not be supported by shiboken.
    if(PYSIDE_TREAT_QT_INCLUDE_DIRS_AS_NON_SYSTEM)
        list(APPEND force_process_system_include_paths_list
            ${qt_platform_includes}
            ${qt_core_includes})
    endif()

    # clang considers headers found via -idirafter to be system headers, and marks the headers
    # they pull in as system headers as well. Since the header-only modules (QtUiPlugin,
    # QtQmlIntegration) live there, that would drop their types and the types of
    # the frameworks they include first, so process all of Qt's headers.
    if(qt_dir_after_includes)
        list(APPEND force_process_system_include_paths_list
            ${qt_dir_after_includes} ${QT_FRAMEWORK_INCLUDE_DIR})
    endif()

    # Allow passing extra non system inlcude dirs.
    if(SHIBOKEN_FORCE_PROCESS_SYSTEM_INCLUDE_PATHS)
        list(APPEND force_process_system_include_paths_list
            ${SHIBOKEN_FORCE_PROCESS_SYSTEM_INCLUDE_PATHS})
    endif()

    # Transform the path separators into something shiboken understands.
    make_path(force_process_system_include_paths ${force_process_system_include_paths_list})

    if(force_process_system_include_paths)
        set(force_process_system_include_paths
            "--force-process-system-include-paths=${force_process_system_include_paths}")
    endif()

    get_filename_component(pyside_binary_dir ${CMAKE_CURRENT_BINARY_DIR} DIRECTORY)

    # Install module glue files.
    string(TOLOWER ${module_NAME} lower_module_name)
    set(${module_NAME}_glue "${CMAKE_CURRENT_SOURCE_DIR}/../glue/${lower_module_name}.cpp")
    set(${module_name}_glue_dependency "")
    if(EXISTS ${${module_NAME}_glue})
        install(FILES ${${module_NAME}_glue} DESTINATION share/PySide6${pyside6_SUFFIX}/glue)
        set(${module_NAME}_glue_dependency ${${module_NAME}_glue})
    endif()

    # Install module doc files.
    set(${module_NAME}_doc "${CMAKE_CURRENT_SOURCE_DIR}/../doc/${lower_module_name}.rst")
    set(${module_name}_doc_dependency "")
    if(EXISTS ${${module_NAME}_doc})
        install(FILES ${${module_NAME}_doc} DESTINATION share/PySide6${pyside6_SUFFIX}/doc)
        set(${module_NAME}_doc_dependency ${${module_NAME}_doc})
    endif()

    # Install standalone glue files into typesystems subfolder, so that the resolved relative
    # paths remain correct.
    if (module_GLUE_SOURCES)
        install(FILES ${module_GLUE_SOURCES} DESTINATION share/PySide6${pyside6_SUFFIX}/typesystems/glue)
    endif()

    shiboken_get_tool_shell_wrapper(shiboken tool_wrapper)

    set(shiboken_command
        ${tool_wrapper}
        $<TARGET_FILE:Shiboken6::shiboken6>
        ${GENERATOR_EXTRA_FLAGS}
        "--include-paths=${shiboken_include_dirs}"
        "${force_process_system_include_paths}"
        "--typesystem-paths=${pyside_binary_dir}${PATH_SEP}${PYSIDE_PACKAGE_SOURCE_DIR}${PATH_SEP}${${module_TYPESYSTEM_PATH}}"
        --output-directory=${CMAKE_CURRENT_BINARY_DIR}
        --license-file=${CMAKE_CURRENT_SOURCE_DIR}/../licensecomment.txt
        --lean-headers
        --api-version=${SUPPORTED_QT_VERSION})

    if(qt_dir_after_includes)
        make_path(shiboken_dir_after_include_dirs ${qt_dir_after_includes})
        list(APPEND shiboken_command
             "--dir-after-include-paths=${shiboken_dir_after_include_dirs}")
    endif()

    # check if building for Android with a macOS host
    # This is not needed for Linux because OpenGLES2 development binaries in
    # linux can be installed by installing 'libgles2-mesa-dev' package which
    # comes as a default requirement for building PySide6. As such for
    # cross-compiling in linux, we use the clang compiler from the installed
    # libclang itself.
    if (CMAKE_CROSSCOMPILING)
        list(APPEND shiboken_command "--platform=${CMAKE_SYSTEM_NAME}"
                                     "--arch=${CMAKE_SYSTEM_PROCESSOR}"
                                     "--compiler-path=${CMAKE_CXX_COMPILER}")
    endif()

    if(CMAKE_ANDROID_ARCH_LLVM_TRIPLE)
        message(STATUS "Building for Android with arch ${CMAKE_ANDROID_ARCH_LLVM_TRIPLE}")
        # CMAKE_CXX_COMPILER is the generic clang++; for finding the include paths,
        # it needs "--target".
        # The API level has to be part of the triple, exactly as it is for the
        # actual compile. Without it the NDK headers fall back to the minimum
        # API, hiding declarations the NDK's own libc++ headers then call, such
        # as pthread_cond_clockwait, which is gated on API 30 from NDK r28 on.
        set(android_triple "${CMAKE_ANDROID_ARCH_LLVM_TRIPLE}${CMAKE_ANDROID_API}")
        list(APPEND shiboken_command
             "--compiler-argument=--target=${android_triple}"
             "--clang-option=--target=${android_triple}")
    endif()

    if(CMAKE_SYSTEM_NAME STREQUAL "iOS")
        if(CMAKE_OSX_SYSROOT STREQUAL "iphonesimulator")
            set(ios_triple
                "${CMAKE_SYSTEM_PROCESSOR}-apple-ios${CMAKE_OSX_DEPLOYMENT_TARGET}-simulator")
        else()
            set(ios_triple "${CMAKE_SYSTEM_PROCESSOR}-apple-ios${CMAKE_OSX_DEPLOYMENT_TARGET}")
        endif()
        set(ios_sysroot "${PYSIDE_IOS_SDK_PATH}")
        list(APPEND shiboken_command
             "--compiler-argument=--target=${ios_triple}"
             "--clang-option=--target=${ios_triple}")
        if(ios_sysroot)
            # NOTE: OptionsParser::process() iterates options in reverse order, so paired
            # flags must be listed with the value BEFORE the flag name here so that after
            # reversal clang receives them in the correct order: -isysroot <path>.
            list(APPEND shiboken_command
                 "--compiler-argument=${ios_sysroot}"
                 "--compiler-argument=-isysroot"
                 "--clang-option=${ios_sysroot}"
                 "--clang-option=-isysroot")
        endif()
    endif()

    if(CMAKE_HOST_APPLE)
        set(shiboken_framework_include_dir_list ${QT_FRAMEWORK_INCLUDE_DIR})
        make_path(shiboken_framework_include_dirs ${shiboken_framework_include_dir_list})
        list(APPEND shiboken_command "--framework-include-paths=${shiboken_framework_include_dirs}")
    endif()

    if(${module_DROPPED_ENTRIES})
        list(JOIN ${module_DROPPED_ENTRIES} "\;" dropped_entries)
        list(APPEND shiboken_command "\"--drop-type-entries=${dropped_entries}\"")
    endif()

    list(APPEND shiboken_command "${PYSIDE_PACKAGE_BINARY_DIR}/${module_NAME}_global.h"
         ${typesystem_path})

    add_custom_command( OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/mjb_rejected_classes.log"
                        BYPRODUCTS ${${module_SOURCES}}
                        COMMAND ${shiboken_command}
                        DEPENDS ${total_type_system_files}
                                ${module_GLUE_SOURCES}
                                ${${module_NAME}_glue_dependency}
                                ${${module_NAME}_doc_dependency}
                        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
                        COMMENT "Running generator for ${module_NAME}...")

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
    qfp_add_library(${module_NAME}
        FUNCTION_ARGS
            ${library_type} ${${module_SOURCES}} ${${module_STATIC_SOURCES}}
        LIBRARY_PATH "${library_install_path}"
        SBOM_ARGS
            SBOM_ENTITY_TYPE LIBRARY
            SBOM_DEPENDENCIES ${python_runtime_sbom_target}
    )

    target_include_directories(${module_NAME}
        PRIVATE
            "${CMAKE_CURRENT_SOURCE_DIR}"
            "${PYSIDE_PACKAGE_SOURCE_DIR}"
            ${${module_INCLUDE_DIRS}}
    )
    append_size_optimization_flags(${module_NAME})

    target_compile_definitions(${module_NAME} PRIVATE -DQT_LEAN_HEADERS=1)

    if(IOS)
        set_target_properties(${module_NAME} PROPERTIES
                              OUTPUT_NAME "${module_NAME}"
                              ARCHIVE_OUTPUT_DIRECTORY ${PYSIDE_PACKAGE_BINARY_DIR})
    else()
        set_target_properties(${module_NAME} PROPERTIES
                            PREFIX ""
                            OUTPUT_NAME "${module_NAME}${SHIBOKEN_PYTHON_EXTENSION_SUFFIX}"
                            LIBRARY_OUTPUT_DIRECTORY ${PYSIDE_PACKAGE_BINARY_DIR})
        if(WIN32)
            set_target_properties(${module_NAME} PROPERTIES SUFFIX ".pyd")
            # Sanitize windows.h as pulled by gl.h to prevent clashes with QAbstract3dAxis::min(), etc.
            set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -DNOMINMAX")
        endif()
    endif()

    target_link_libraries(${module_NAME}
        PRIVATE
            Shiboken6::libshiboken
            PySide6::pyside6
            ${${module_LIBRARIES}}
    )

    if(${module_DEPS})
        add_dependencies(${module_NAME} ${${module_DEPS}})
    endif()
    create_generator_target(${module_NAME})

    # build type hinting stubs
    pyside_setup_ld_prefix()

    qfp_strip_library("${module_NAME}")

    # Add target to generate pyi file, which depends on the module target.
    # Don't generate the files when cross-building because the target python can not be executed
    # on the host machine (usually, unless you use some userspace qemu based mechanism).
    # TODO: Can we do something better here to still get pyi files?
    if(NOT (PYSIDE_IS_CROSS_BUILD OR DISABLE_PYI))
        set(SHIBOKEN_PYTHON_MODULE_DIR "${PYTHON_SITE_PACKAGES}/shiboken6")
        set(generate_pyi_sys_path
            "${pysidebindings_BINARY_DIR}"
            "${SHIBOKEN_PYTHON_MODULE_DIR}/.."
            "${SHIBOKEN_PYTHON_MODULE_DIR}/../../..")     # use the layer above shiboken6
        if(TARGET shibokenmodule)
            # Prepend the build-tree parent so the freshly built shiboken6 package is found
            # before any installed version, which may carry an older libshiboken via RPATH.
            list(PREPEND generate_pyi_sys_path "$<TARGET_FILE_DIR:shibokenmodule>/..")
        endif()
        set(generate_pyi_options ${module_NAME} --sys-path ${generate_pyi_sys_path})
        if (QUIET_BUILD)
            list(APPEND generate_pyi_options "--quiet")
        endif()

        add_custom_target("${module_NAME}_pyi" ALL
                          COMMAND
                              ${CMAKE_COMMAND} -E env ${ld_prefix}
                              "${SHIBOKEN_PYTHON_INTERPRETER}"
                              "${CMAKE_CURRENT_SOURCE_DIR}/../support/generate_pyi.py"
                              ${generate_pyi_options})
        add_dependencies("${module_NAME}_pyi" ${module_NAME})

        if(TARGET shibokenmodule)
            add_dependencies("${module_NAME}_pyi" shibokenmodule)
        endif()

        install(FILES "${CMAKE_CURRENT_BINARY_DIR}/../${module_NAME}.pyi"
                DESTINATION "${PYTHON_SITE_PACKAGES}/PySide6")
        qfp_sbom_add_files(${module_NAME}
            FILES "${CMAKE_CURRENT_BINARY_DIR}/../${module_NAME}.pyi"
            FILE_TYPE "SOURCE"
            INSTALL_PATH "${install_path}"
        )
    endif()


    # install
    install(TARGETS ${module_NAME} ${artifact_type} DESTINATION ${install_destination})



    install(FILES ${CMAKE_CURRENT_BINARY_DIR}/PySide6/${module_NAME}/pyside6_${lower_module_name}_python.h
            DESTINATION include/PySide6${pyside6_SUFFIX}/${module_NAME}/)
    qfp_sbom_add_files(${module_NAME}
        FILES
            ${CMAKE_CURRENT_BINARY_DIR}/PySide6/${module_NAME}/pyside6_${lower_module_name}_python.h
        FILE_TYPE "SOURCE"
        INSTALL_PATH "include/PySide6${pyside6_SUFFIX}/${module_NAME}"
    )
    file(GLOB typesystem_files ${CMAKE_CURRENT_SOURCE_DIR}/typesystem_*.xml ${typesystem_path})

#   Copy typesystem files and remove module names from the <load-typesystem> element
#   so that it works in a flat directory:
#   <load-typesystem name="QtWidgets/typesystem_widgets.xml" ... ->
#   <load-typesystem name="typesystem_widgets.xml"
    foreach(typesystem_file ${typesystem_files})
        get_filename_component(typesystem_file_name "${typesystem_file}" NAME)
        file(READ "${typesystem_file}" typesystemXml)
        string(REGEX REPLACE "<load-typesystem name=\"[^/\"]+/" "<load-typesystem name=\"" typesystemXml "${typesystemXml}")
        set (typesystem_target_file "${CMAKE_CURRENT_BINARY_DIR}/PySide6/typesystems/${typesystem_file_name}")
        file(WRITE "${typesystem_target_file}" "${typesystemXml}")
        install(FILES "${typesystem_target_file}" DESTINATION share/PySide6${pyside6_SUFFIX}/typesystems)
        qfp_sbom_add_files(${module_NAME}
            FILES "${typesystem_target_file}"
            FILE_TYPE "SOURCE"
            INSTALL_PATH "share/PySide6${pyside6_SUFFIX}/typesystems"
        )
    endforeach()

    # The glue and doc files are installed before the module target is created.
    if(EXISTS ${${module_NAME}_glue})
        qfp_sbom_add_files(${module_NAME}
            FILES ${${module_NAME}_glue}
            FILE_TYPE "SOURCE"
            INSTALL_PATH "share/PySide6${pyside6_SUFFIX}/glue"
        )
    endif()
    if(EXISTS ${${module_NAME}_doc})
        qfp_sbom_add_files(${module_NAME}
            FILES ${${module_NAME}_doc}
            FILE_TYPE "DOCUMENTATION"
            INSTALL_PATH "share/PySide6${pyside6_SUFFIX}/doc"
        )
    endif()
    if(module_GLUE_SOURCES)
        qfp_sbom_add_files(${module_NAME}
            FILES ${module_GLUE_SOURCES}
            FILE_TYPE "SOURCE"
            INSTALL_PATH "share/PySide6${pyside6_SUFFIX}/typesystems/glue"
        )
    endif()
endmacro()

# Only add subdirectory if the associated Qt module is found.
# As a side effect, this macro now also defines the variable ${name}_GEN_DIR
# and must be called for every subproject.
macro(HAS_QT_MODULE var name)
    if (NOT DISABLE_${name} AND ${var})
        # we keep the PySide name here because this is compiled into shiboken
        set(${name}_GEN_DIR ${CMAKE_CURRENT_BINARY_DIR}/${name}/PySide6/${name}
            CACHE INTERNAL "dir with generated source" FORCE)
        add_subdirectory(${name})
    else()
        # Used on documentation to skip modules
        set("if_${name}" "<!--" PARENT_SCOPE)
        set("end_${name}" "-->" PARENT_SCOPE)
    endif()
endmacro()
