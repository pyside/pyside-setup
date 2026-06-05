list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_LIST_DIR}"
                              "${CMAKE_CURRENT_LIST_DIR}/../../shiboken6/cmake")

include(ShibokenHelpers)
include(ShibokenGeneratorHelpers)
include(ShibokenGeneratorCMakeHelpers)
include(ShibokenGeneratorSbomHelpers)
include(ShibokenGeneratorTargetHelpers)

shiboken_internal_disable_pkg_config_if_needed()
shiboken_internal_detect_if_cross_building()

# Note: For cross building, we rely on FindPython shipped with CMake 3.17+ to
# provide the value of Python_SOABI.

shiboken_internal_decide_parts_to_build()
shiboken_internal_set_up_extra_dependency_paths()

set(QT_MAJOR_VERSION 6)
message(STATUS "Using Qt ${QT_MAJOR_VERSION}")
find_package(Qt6 REQUIRED COMPONENTS Core)

# Must be called before any packages are looked up with qfp_find_package.
qfp_setup_sbom()

if(QUIET_BUILD)
    set_quiet_build()
endif()

if(USE_PYTHON_VERSION)
    shiboken_find_required_python(${USE_PYTHON_VERSION})
else()
    shiboken_find_required_python()
endif()

setup_clang()

# from cmake.conf
set(shiboken6_VERSION "${shiboken_MAJOR_VERSION}.${shiboken_MINOR_VERSION}.${shiboken_MICRO_VERSION}")

compute_config_py_values(shiboken6_VERSION)

shiboken_internal_set_python_site_packages()

string(REGEX REPLACE "\\.[0-9]+\\.[0-9]+.*$" "" LLVM_VERSION "${LLVM_PACKAGE_VERSION}")

set_cmake_cxx_flags()
set(CMAKE_CXX_FLAGS
    "${CMAKE_CXX_FLAGS} -D QT_NO_CAST_FROM_ASCII -D QT_NO_CAST_TO_ASCII -D LLVM_VERSION=${LLVM_VERSION}")

# Force usage of the C++17 standard
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(LIB_SUFFIX "" CACHE STRING "Define suffix of directory name (32/64)" )
set(LIB_INSTALL_DIR "lib${LIB_SUFFIX}" CACHE PATH "The subdirectory relative to the install \
    prefix where libraries will be installed (default is /lib${LIB_SUFFIX})" FORCE)
set(BIN_INSTALL_DIR "bin" CACHE PATH "The subdirectory relative to the install prefix where \
    dlls will be installed (default is /bin)" FORCE)
