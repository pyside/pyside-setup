include(../shiboken6/icecc.cmake)
include(../shiboken6/.cmake.conf)

cmake_minimum_required(VERSION 3.25)
cmake_policy(VERSION 3.22)

project(shiboken_generator)

include(cmake/ShibokenGeneratorSetup.cmake)

get_filename_component(qfp_license_dir_path "${CMAKE_CURRENT_SOURCE_DIR}/../../LICENSES" REALPATH)
qfp_sbom_begin_project(
    SBOM_PROJECT_NAME "shiboken6-generator"
    LICENSE_DIR_PATHS "${qfp_license_dir_path}"
    INSTALL_SBOM_DIR "share/sbom/shiboken6_generator"
    PYPI_PURL_NAME "shiboken6-generator"
)

get_rpath_base_token(base)

set(CMAKE_INSTALL_RPATH ${base}/)

set(CMAKE_BUILD_TYPE Release CACHE STRING "Build Type")

add_subdirectory(ApiExtractor) # Uses libclang
add_subdirectory(generator) # Uses ApiExtractor And QtCore

qfp_sbom_end_project()

add_subdirectory(doc)

if(BUILD_TESTS)
    enable_testing()
    if(SHIBOKEN_PYTHON_FREE_THREADED)
        # Windows' pyconfig.h leaves Py_GIL_DISABLED to the build.
        if(WIN32)
            add_compile_definitions("Py_GIL_DISABLED=1")
        endif()
    endif()
    add_subdirectory(tests)
endif()
