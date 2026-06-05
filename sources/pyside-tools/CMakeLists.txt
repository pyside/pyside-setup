# Copyright (C) 2023 The Qt Company Ltd.
# SPDX-License-Identifier: BSD-3-Clause

cmake_minimum_required(VERSION 3.25)

include(../pyside6/.cmake.conf)

project(pyside-tools)

include(cmake/PySideToolsSetup.cmake)

qfp_setup_sbom()

get_filename_component(qfp_license_dir_path "${CMAKE_CURRENT_SOURCE_DIR}/../../LICENSES" REALPATH)
# The tools are split across the PySide6_Essentials and PySide6_Addons wheels, so there is one
# SBOM document per wheel. The addons one is opened at the end.
qfp_sbom_begin_project(
    SBOM_PROJECT_NAME "PySide6-Tools-Essentials"
    LICENSE_DIR_PATHS "${qfp_license_dir_path}"
    INSTALL_SBOM_DIR "share/sbom/PySide6-Tools-Essentials"
    PYPI_PURL_NAME "pyside6-essentials"
)

if(${CMAKE_SYSTEM_NAME} STREQUAL "Android")
    # create Qt6AndroidBindings.jar
    if (NOT DEFINED ANDROID_SDK_ROOT)
        message(FATAL_ERROR "Please provide the location of the Android SDK directory via "
                            "your toolchain file")
    endif()
    if (NOT DEFINED ANDROID_PLATFORM)
        message(FATAL_ERROR "Please provide the location of the Android Platform API level via "
                            "your toolchain file eg: android-31")
    endif()
    include(cmake/PySideAndroid.cmake)
    create_and_install_qt_javabindings()
else()
    set(files "")
    set(pyside_tools_scripts ${CMAKE_CURRENT_SOURCE_DIR}/pyside_tool.py
            ${CMAKE_CURRENT_SOURCE_DIR}/metaobjectdump.py
            ${CMAKE_CURRENT_SOURCE_DIR}/project.py
            ${CMAKE_CURRENT_SOURCE_DIR}/qml.py
            ${CMAKE_CURRENT_SOURCE_DIR}/qtpy2cpp.py
            ${CMAKE_CURRENT_SOURCE_DIR}/deploy.py
            ${CMAKE_CURRENT_SOURCE_DIR}/android_deploy.py
            ${CMAKE_CURRENT_SOURCE_DIR}/requirements-android.txt
            ${CMAKE_CURRENT_SOURCE_DIR}/ios_deploy.py
            ${CMAKE_CURRENT_SOURCE_DIR}/requirements-ios.txt)
    list(APPEND files ${pyside_tools_scripts})

    set(directories "")
    set(deploy_lib_resource_dirs
        ${CMAKE_CURRENT_SOURCE_DIR}/deploy_lib
        ${CMAKE_CURRENT_SOURCE_DIR}/project_lib)
    list(APPEND directories ${deploy_lib_resource_dirs})

    if(NOT NO_QT_TOOLS STREQUAL "yes")
        set(TOOLS_PATH "${QT6_INSTALL_PREFIX}/${QT6_HOST_INFO_BINDIR}")
        set(LIBEXEC_PATH "${QT6_INSTALL_PREFIX}/${QT6_HOST_INFO_LIBEXECDIR}")

        set(qt_tools "${LIBEXEC_PATH}/uic${CMAKE_EXECUTABLE_SUFFIX}"
                        "${LIBEXEC_PATH}/rcc${CMAKE_EXECUTABLE_SUFFIX}"
                        "${LIBEXEC_PATH}/qmltyperegistrar${CMAKE_EXECUTABLE_SUFFIX}"
                        "${LIBEXEC_PATH}/qmlimportscanner${CMAKE_EXECUTABLE_SUFFIX}"
                        "${LIBEXEC_PATH}/qmlcachegen${CMAKE_EXECUTABLE_SUFFIX}"
                        "${TOOLS_PATH}/lrelease${CMAKE_EXECUTABLE_SUFFIX}"
                        "${TOOLS_PATH}/lupdate${CMAKE_EXECUTABLE_SUFFIX}"
                        "${TOOLS_PATH}/qmllint${CMAKE_EXECUTABLE_SUFFIX}"
                        "${TOOLS_PATH}/qmlformat${CMAKE_EXECUTABLE_SUFFIX}"
                        "${TOOLS_PATH}/qmlls${CMAKE_EXECUTABLE_SUFFIX}"
                        "${TOOLS_PATH}/qsb${CMAKE_EXECUTABLE_SUFFIX}"
                        "${TOOLS_PATH}/balsam${CMAKE_EXECUTABLE_SUFFIX}"
                        "${TOOLS_PATH}/balsamui${CMAKE_EXECUTABLE_SUFFIX}"
                        "${TOOLS_PATH}/svgtoqml${CMAKE_EXECUTABLE_SUFFIX}")

        set(apple_bundle_apps "")
        if(APPLE)
            list(APPEND apple_bundle_apps
                "${TOOLS_PATH}/Assistant.app"
                "${TOOLS_PATH}/Designer.app"
                "${TOOLS_PATH}/Linguist.app")
            list(APPEND directories ${apple_bundle_apps})
        else()
            list(APPEND qt_tools "${TOOLS_PATH}/assistant${CMAKE_EXECUTABLE_SUFFIX}"
                               "${TOOLS_PATH}/designer${CMAKE_EXECUTABLE_SUFFIX}"
                               "${TOOLS_PATH}/linguist${CMAKE_EXECUTABLE_SUFFIX}")
        endif()
    endif()

    set(qt_tools_Essentials "")
    set(qt_tools_Addons "")
    foreach(qt_tool IN LISTS qt_tools)
        get_filename_component(qt_tool_name "${qt_tool}" NAME_WE)
        pyside_tools_get_qt_tool_wheel(qt_tool_wheel "${qt_tool_name}")
        list(APPEND qt_tools_${qt_tool_wheel} "${qt_tool}")
    endforeach()
    list(APPEND files ${qt_tools_Essentials})

    set(qtpty2_lib ${CMAKE_CURRENT_SOURCE_DIR}/qtpy2cpp_lib)
    list(APPEND directories ${qtpty2_lib})

    # pyside6-rcc, pyside6-uic, pyside6-designer, shiboken and pyside6-lupdate entrypoints
    pyside_tools_install_executable_files(missing_files ${files})

    qfp_sbom_get_open_source_permissive_library_license(permissive_license_expression)
    qfp_sbom_get_default_copyright(qfp_copyright)
    qfp_sbom_get_supplier(qfp_supplier)

    qfp_add_custom_sbom(PySideToolsScripts
        LICENSE_CONCLUDED_EXPRESSION "${permissive_license_expression}"
    )
    qfp_sbom_add_files(PySideToolsScripts
        FILES "${pyside_tools_scripts}"
        FILE_TYPE "BINARY"
        INSTALL_PATH "bin"
    )

    # Skipped tools are not installed, so they must not be in the SBOM either.
    list(REMOVE_ITEM qt_tools_Essentials ${missing_files})

    qfp_sbom_get_open_source_tool_license(tool_license_expression)
    # Skip the sbom packages when there are no Qt tools (e.g. --no-qt-tools is passed).
    if(qt_tools_Essentials OR apple_bundle_apps)
        qfp_add_sbom(PySideToolsQtEssentialsTools
            SBOM_ENTITY_TYPE CUSTOM_NO_INFIX
            COPYRIGHTS "${qfp_copyright}"
            SUPPLIER "${qfp_supplier}"
            LICENSE_CONCLUDED_EXPRESSION "${tool_license_expression}"
            MIRROR_LICENSE_DECLARED_EXPRESSION
        )
        qfp_sbom_add_files(PySideToolsQtEssentialsTools
            FILES "${qt_tools_Essentials}"
            FILE_TYPE "BINARY"
            INSTALL_PATH "bin"
        )
    endif()

    foreach(directory ${directories})
        install(DIRECTORY "${directory}"
                DESTINATION bin
                FILE_PERMISSIONS
                OWNER_EXECUTE OWNER_WRITE OWNER_READ
                GROUP_EXECUTE GROUP_READ
                WORLD_EXECUTE WORLD_READ
                PATTERN "android_utilities.py" EXCLUDE   # excluding the symlink
                PATTERN "python_xcframework.py" EXCLUDE) # excluding the symlink
    endforeach()

    foreach(deploy_lib_resource_dir IN LISTS deploy_lib_resource_dirs)
        get_filename_component(deploy_lib_resource_dir_name "${deploy_lib_resource_dir}" NAME)
        set(sbom_target PySideDeployLibResources_${deploy_lib_resource_dir_name})
        qfp_add_custom_sbom(${sbom_target}
            SBOM_ENTITY_TYPE RESOURCES
            LICENSE_CONCLUDED_EXPRESSION "${permissive_license_expression}"
        )
        qfp_sbom_add_directory_files(${sbom_target}
            DIRECTORY "${deploy_lib_resource_dir}"
            INSTALL_PATH "bin"
            FILE_TYPE "RESOURCE"
            # Mirror the symlink exclusions of install(DIRECTORY) above. The real files are added
            # to PySideToolsScripts further below.
            EXCLUDE_REGEX "(^|/)(android_utilities|python_xcframework)\\.py$"
        )
    endforeach()

    qfp_sbom_add_directory_files(PySideToolsScripts
        DIRECTORY "${qtpty2_lib}"
        INSTALL_PATH "bin"
        FILE_TYPE "SOURCE"
        EXCLUDE_REGEX "^tests/baseline/"
    )

    # The test baselines are licensed differently than the rest of qtpy2cpp_lib.
    qfp_add_custom_sbom(PySideToolsQtPy2CppTestBaselines
        LICENSE_CONCLUDED_EXPRESSION "${tool_license_expression}"
    )
    qfp_sbom_add_directory_files(PySideToolsQtPy2CppTestBaselines
        DIRECTORY "${qtpty2_lib}"
        INSTALL_PATH "bin"
        FILE_TYPE "SOURCE"
        INCLUDE_REGEX "^tests/baseline/"
    )

    foreach(apple_bundle_app IN LISTS apple_bundle_apps)
        qfp_sbom_add_directory_files(PySideToolsQtEssentialsTools
            DIRECTORY "${apple_bundle_app}"
            INSTALL_PATH "bin"
            FILE_TYPE "BINARY"
            INCLUDE_REGEX "^Contents/MacOS/"
        )
        qfp_sbom_add_directory_files(PySideToolsQtEssentialsTools
            DIRECTORY "${apple_bundle_app}"
            INSTALL_PATH "bin"
            FILE_TYPE "RESOURCE"
            EXCLUDE_REGEX "^Contents/MacOS/"
        )
    endforeach()

    # dealing with android_utilities.py
    set(ANDROID_UTILITIES_REALPATH
        "${CMAKE_CURRENT_SOURCE_DIR}/../../tools/cross_compile_android/android_utilities.py")
    message(STATUS "Installing ${ANDROID_UTILITIES_REALPATH}")
    set(destination_path "bin/deploy_lib/android")
    install(FILES "${ANDROID_UTILITIES_REALPATH}"
            DESTINATION "${destination_path}"
            PERMISSIONS
                OWNER_EXECUTE OWNER_WRITE OWNER_READ
                GROUP_EXECUTE GROUP_READ
                WORLD_EXECUTE WORLD_READ)

    # dealing with python_xcframework.py
    set(PYTHON_XCFRAMEWORK_REALPATH
        "${CMAKE_CURRENT_SOURCE_DIR}/../../tools/cross_compile_ios/python_xcframework.py")
    message(STATUS "Installing ${PYTHON_XCFRAMEWORK_REALPATH}")
    install(FILES "${PYTHON_XCFRAMEWORK_REALPATH}"
            DESTINATION bin/deploy_lib/ios
            PERMISSIONS
                OWNER_EXECUTE OWNER_WRITE OWNER_READ
                GROUP_EXECUTE GROUP_READ
                WORLD_EXECUTE WORLD_READ)

    qfp_sbom_add_files(PySideToolsScripts
        FILES "${ANDROID_UTILITIES_REALPATH}"
        FILE_TYPE "BINARY"
        INSTALL_PATH "${destination_path}"
    )
    qfp_sbom_add_files(PySideToolsScripts
        FILES "${PYTHON_XCFRAMEWORK_REALPATH}"
        FILE_TYPE "BINARY"
        INSTALL_PATH "bin/deploy_lib/ios"
    )
endif()

qfp_sbom_end_project()

# Skip the sbom packages when there are no Qt tools (e.g. --no-qt-tools is passed).
if(qt_tools_Addons)
    pyside_tools_install_executable_files(missing_files ${qt_tools_Addons})
    list(REMOVE_ITEM qt_tools_Addons ${missing_files})
endif()

if(qt_tools_Addons)
    qfp_sbom_begin_project(
        SBOM_PROJECT_NAME "PySide6-Tools-Addons"
        LICENSE_DIR_PATHS "${qfp_license_dir_path}"
        INSTALL_SBOM_DIR "share/sbom/PySide6-Tools-Addons"
        PYPI_PURL_NAME "pyside6-addons"
    )

    qfp_add_sbom(PySideToolsQtAddonsTools
        SBOM_ENTITY_TYPE CUSTOM_NO_INFIX
        COPYRIGHTS "${qfp_copyright}"
        SUPPLIER "${qfp_supplier}"
        LICENSE_CONCLUDED_EXPRESSION "${tool_license_expression}"
        MIRROR_LICENSE_DECLARED_EXPRESSION
    )
    qfp_sbom_add_files(PySideToolsQtAddonsTools
        FILES "${qt_tools_Addons}"
        FILE_TYPE "BINARY"
        INSTALL_PATH "bin"
    )

    qfp_sbom_end_project()
endif()
