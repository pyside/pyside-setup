# Function to configure a binding project
function(shiboken_generator_create_binding)
    set(options FORCE_LIMITED_API)
    set(one_value_args
        EXTENSION_TARGET
        TYPESYSTEM_FILE
        LIBRARY_TARGET)
    set(multi_value_args
        GENERATED_SOURCES
        HEADERS
        QT_MODULES
        SHIBOKEN_EXTRA_OPTIONS)

    cmake_parse_arguments(PARSE_ARGV 0 arg
        "${options}"
        "${one_value_args}"
        "${multi_value_args}")

    # Validate required arguments
    foreach(req EXTENSION_TARGET GENERATED_SOURCES HEADERS TYPESYSTEM_FILE LIBRARY_TARGET)
        if(NOT DEFINED arg_${req})
            message(FATAL_ERROR "shiboken_generator_create_binding: ${req} is required")
        endif()
    endforeach()

    get_target_property(shiboken_include_dirs Shiboken6::libshiboken INTERFACE_INCLUDE_DIRECTORIES)

    # Get Shiboken path, falling back to the other config if the requested one is absent
    # (e.g. consumer project is Release but wheel was built as Debug, or vice versa).
    if(CMAKE_BUILD_TYPE STREQUAL "Debug")
        get_target_property(shiboken_path Shiboken6::shiboken6 IMPORTED_LOCATION_DEBUG)
        if(NOT shiboken_path)
            get_target_property(shiboken_path Shiboken6::shiboken6 IMPORTED_LOCATION_RELEASE)
        endif()
    else()
        get_target_property(shiboken_path Shiboken6::shiboken6 IMPORTED_LOCATION_RELEASE)
        if(NOT shiboken_path)
            get_target_property(shiboken_path Shiboken6::shiboken6 IMPORTED_LOCATION_DEBUG)
        endif()
    endif()

    # Basic shiboken options
    set(shiboken_options
        --generator-set=shiboken
        --enable-parent-ctor-heuristic
        --enable-return-value-heuristic
        --use-isnull-as-nb_nonzero
        --avoid-protected-hack
        -I${CMAKE_CURRENT_SOURCE_DIR}
        -T${CMAKE_CURRENT_SOURCE_DIR}
        --output-directory=${CMAKE_CURRENT_BINARY_DIR})

    # Add extra options if specified
    if(arg_SHIBOKEN_EXTRA_OPTIONS)
        list(APPEND shiboken_options ${arg_SHIBOKEN_EXTRA_OPTIONS})
    endif()

    # Add Qt/PySide specific configurations only if Qt modules are specified
    if(arg_QT_MODULES)
        # Get Qt include directories
        set(qt_include_dirs "")
        foreach(module ${arg_QT_MODULES})
            get_property(module_includes TARGET Qt6::${module} PROPERTY
                INTERFACE_INCLUDE_DIRECTORIES)
            list(APPEND qt_include_dirs ${module_includes})

            # Check each module for framework on macOS
            if(APPLE)
                get_target_property(is_framework Qt6::${module} FRAMEWORK)
                if(is_framework)
                    get_target_property(lib_location Qt6::${module} LOCATION)
                    # strip /QtCore.framework and everything after it to get the parent lib/ dir
                    string(REGEX REPLACE "/[^/]+\\.framework.*" "" framework_dir "${lib_location}")
                    list(APPEND shiboken_options "--framework-include-paths=${framework_dir}")
                endif()
            endif()

            # Add include paths to shiboken options
            foreach(include_dir ${module_includes})
                list(APPEND shiboken_options "-I${include_dir}")
            endforeach()
        endforeach()

        get_target_property(pyside_include_dir PySide6::pyside6 INTERFACE_INCLUDE_DIRECTORIES)

        # Add PySide typesystems path
        list(APPEND shiboken_options "-T${PYSIDE_TYPESYSTEMS}")

        # Enable PySide extensions
        list(APPEND shiboken_options "--enable-pyside-extensions")
    endif()

    # Generate binding sources
    add_custom_command(
        OUTPUT ${arg_GENERATED_SOURCES}
        COMMAND "${shiboken_path}"
        ${shiboken_options} ${arg_HEADERS} "${arg_TYPESYSTEM_FILE}"
        DEPENDS ${arg_HEADERS} ${arg_TYPESYSTEM_FILE}
        IMPLICIT_DEPENDS CXX ${arg_HEADERS}
        WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
        COMMENT "Generating bindings for ${arg_EXTENSION_TARGET}"
    )

    # Create binding library
    add_library(${arg_EXTENSION_TARGET} MODULE ${arg_GENERATED_SOURCES})

    # set limited API (a free-threaded Python has none)
    if((arg_FORCE_LIMITED_API OR FORCE_LIMITED_API) AND NOT SHIBOKEN_PYTHON_FREE_THREADED)
        target_compile_definitions(${arg_EXTENSION_TARGET} PRIVATE -DPy_LIMITED_API=0x030a0000)
    endif()


    # Configure include paths
    target_include_directories(
        ${arg_EXTENSION_TARGET} PRIVATE
        ${SHIBOKEN_PYTHON_INCLUDE_DIRS}
        ${shiboken_include_dirs}
        ${CMAKE_CURRENT_SOURCE_DIR}
    )

    # Link with Python, Shiboken and C++ library
    target_link_libraries(
        ${arg_EXTENSION_TARGET} PRIVATE
        Shiboken6::libshiboken
        ${arg_LIBRARY_TARGET}
    )

    if(arg_QT_MODULES)
        # Add Qt and PySide includes
        target_include_directories(
            ${arg_EXTENSION_TARGET} PRIVATE ${qt_include_dirs}
        )
        target_include_directories(
            ${arg_EXTENSION_TARGET} PRIVATE ${pyside_include_dir}
        )

        # Add PySide Qt module-specific includes and link libraries
        foreach(module ${arg_QT_MODULES})
            target_include_directories(
                ${arg_EXTENSION_TARGET} PRIVATE "${pyside_include_dir}/Qt${module}"
            )
            target_link_libraries(
                ${arg_EXTENSION_TARGET} PRIVATE Qt6::${module}
            )
        endforeach()

        # Link base PySide6 library
        target_link_libraries(
            ${arg_EXTENSION_TARGET} PRIVATE PySide6::pyside6
        )

        # Link PySide6 QML library if Qml module is used
        if("Qml" IN_LIST arg_QT_MODULES)
            target_link_libraries(
                ${arg_EXTENSION_TARGET} PRIVATE PySide6::pyside6qml
            )
        endif()
    endif()

    # Configure target properties
    set_target_properties(
        ${arg_EXTENSION_TARGET} PROPERTIES
        PREFIX ""
        OUTPUT_NAME "${arg_EXTENSION_TARGET}${SHIBOKEN_PYTHON_EXTENSION_SUFFIX}"
    )

    # Platform specific settings
    if(WIN32)
        # Add Python libraries only on Windows
        get_property(SHIBOKEN_PYTHON_LIBRARIES GLOBAL PROPERTY shiboken_python_libraries)

        target_link_libraries(
            ${arg_EXTENSION_TARGET} PRIVATE "${SHIBOKEN_PYTHON_LIBRARIES}"
        )

        # Set Windows-specific suffix
        if(CMAKE_BUILD_TYPE STREQUAL "Debug")
            set_property(
                TARGET ${arg_EXTENSION_TARGET} PROPERTY SUFFIX "_d.pyd"
            )
        else()
            set_property(
                TARGET ${arg_EXTENSION_TARGET} PROPERTY SUFFIX ".pyd"
            )
        endif()
    endif()

    if(APPLE)
        set_target_properties(
            ${arg_EXTENSION_TARGET} PROPERTIES
            LINK_FLAGS "-undefined dynamic_lookup"
        )
    endif()
endfunction()
