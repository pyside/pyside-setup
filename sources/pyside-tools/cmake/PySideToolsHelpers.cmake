# Copyright (C) 2023 The Qt Company Ltd.
# SPDX-License-Identifier: BSD-3-Clause

function(pyside_tools_internal_detect_if_cross_building)
    if(CMAKE_CROSSCOMPILING OR QFP_SHIBOKEN_HOST_PATH)
        set(is_cross_build TRUE)
    else()
        set(is_cross_build FALSE)
    endif()
    set(PYSIDE_TOOLS_IS_CROSS_BUILD "${is_cross_build}" PARENT_SCOPE)
    message(STATUS "PYSIDE_TOOLS_IS_CROSS_BUILD: ${PYSIDE_TOOLS_IS_CROSS_BUILD}")
endfunction()

function(pyside_tools_internal_set_up_extra_dependency_paths)
    set(extra_root_path_vars
        QFP_QT_TARGET_PATH
    )
    foreach(root_path IN LISTS extra_root_path_vars)
        set(new_root_path_value "${${root_path}}")
        if(new_root_path_value)
            set(new_prefix_path "${CMAKE_PREFIX_PATH}")
            list(PREPEND new_prefix_path "${new_root_path_value}/lib/cmake")
            set(CMAKE_PREFIX_PATH "${new_prefix_path}")
            set(CMAKE_PREFIX_PATH "${new_prefix_path}" PARENT_SCOPE)

            # Need to adjust the prefix and root paths so that find_package(Qt) and other 3rd
            # party packages are found successfully when they are located outside of the
            # default sysroot (whatever that maybe for the target platform).
            if(PYSIDE_TOOLS_IS_CROSS_BUILD)
                set(new_root_path "${CMAKE_FIND_ROOT_PATH}")
                list(PREPEND new_root_path "${new_root_path_value}")
                set(CMAKE_FIND_ROOT_PATH "${new_root_path}")
                set(CMAKE_FIND_ROOT_PATH "${new_root_path}" PARENT_SCOPE)
            endif()
        endif()
    endforeach()
endfunction()

# Returns "Essentials" or "Addons": the wheel that ships the Qt tool, according to
# PYSIDE_QT_TOOLS_WHEEL_MODULES and the per-wheel module lists of PySideSbomWheelMapping.cmake.
function(pyside_tools_get_qt_tool_wheel out_var tool_name)
    foreach(entry IN LISTS PYSIDE_QT_TOOLS_WHEEL_MODULES)
        string(REPLACE ":" ";" entry "${entry}")
        list(GET entry 0 entry_tool)
        list(GET entry 1 entry_module)
        if(NOT entry_tool STREQUAL tool_name)
            continue()
        endif()
        if(entry_module IN_LIST PYSIDE_ADDONS_WHEEL_MODULES)
            set(${out_var} "Addons" PARENT_SCOPE)
            return()
        endif()
        if(NOT entry_module IN_LIST PYSIDE_ESSENTIALS_WHEEL_MODULES)
            message(WARNING "The module '${entry_module}' of the Qt tool '${tool_name}' is not "
                "in the essentials or addons wheel module list, assuming the essentials wheel.")
        endif()
        set(${out_var} "Essentials" PARENT_SCOPE)
        return()
    endforeach()
    message(WARNING "The Qt tool '${tool_name}' is missing from PYSIDE_QT_TOOLS_WHEEL_MODULES, "
        "assuming the essentials wheel.")
    set(${out_var} "Essentials" PARENT_SCOPE)
endfunction()

# Installs the given files into bin as executables, and returns the ones that don't exist.
function(pyside_tools_install_executable_files out_missing_files)
    set(missing_files "")
    foreach(file IN LISTS ARGN)
        if(EXISTS "${file}")
            message(STATUS "Installing tool: ${file}")
            install(FILES "${file}"
                    DESTINATION bin
                    PERMISSIONS
                    OWNER_EXECUTE OWNER_WRITE OWNER_READ
                    GROUP_EXECUTE GROUP_READ
                    WORLD_EXECUTE WORLD_READ)
        else()
            list(APPEND missing_files "${file}")
            message(WARNING "${file} does not exist. Hence, pyside6-${file} will not work")
        endif()
    endforeach()
    set(${out_missing_files} "${missing_files}" PARENT_SCOPE)
endfunction()
