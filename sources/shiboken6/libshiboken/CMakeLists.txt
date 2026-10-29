# Copyright (C) 2023 The Qt Company Ltd.
# SPDX-License-Identifier: BSD-3-Clause

option(ENABLE_VERSION_SUFFIX "Used to use current version in suffix to generated files. This is used to allow multiples versions installed simultaneous." FALSE)
if(ENABLE_VERSION_SUFFIX)
    set(shiboken6_SUFFIX "-${shiboken_MAJOR_VERSION}.${shiboken_MINOR_VERSION}")
else()
    set(shiboken6_SUFFIX "")
endif()

configure_file("${CMAKE_CURRENT_SOURCE_DIR}/sbkversion.h.in"
               "${CMAKE_CURRENT_BINARY_DIR}/sbkversion.h" @ONLY)
configure_file("${CMAKE_CURRENT_SOURCE_DIR}/embed/signature_bootstrap.py"
               "${CMAKE_CURRENT_BINARY_DIR}/embed/signature_bootstrap.py" @ONLY)

# Variable from enclosing scope.
list(TRANSFORM shiboken_python_files
     PREPEND "${CMAKE_CURRENT_SOURCE_DIR}/../shibokenmodule/files.dir/shibokensupport/"
     OUTPUT_VARIABLE embedded_shiboken_files)

if (QUIET_BUILD)
    set(embedding_option "--quiet")
else()
    set(embedding_option "")
endif()

if(SHIBOKEN_IS_CROSS_BUILD)
    set(host_python_path "${QFP_PYTHON_HOST_PATH}")
    set(use_pyc_in_embedding FALSE)
else()
    set(host_python_path "${Python_EXECUTABLE}")
    if(PYTHON_LIMITED_API)
        set(use_pyc_in_embedding FALSE)
    else()
        set(use_pyc_in_embedding TRUE)
    endif()
endif()

add_custom_command(
    OUTPUT  "${CMAKE_CURRENT_BINARY_DIR}/embed/signature_bootstrap_inc.h"
    OUTPUT  "${CMAKE_CURRENT_BINARY_DIR}/embed/signature_inc.h"
    COMMAND ${host_python_path} -E
            "${CMAKE_CURRENT_SOURCE_DIR}/embed/embedding_generator.py"
            --cmake-dir "${CMAKE_CURRENT_BINARY_DIR}/embed"
            --use-pyc ${use_pyc_in_embedding}
            ${embedding_option}
    DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/embed/embedding_generator.py"
            "${CMAKE_CURRENT_SOURCE_DIR}/embed/signature_bootstrap.py"
            ${embedded_shiboken_files}
    )

set(libshiboken_MAJOR_VERSION ${shiboken_MAJOR_VERSION})
set(libshiboken_MINOR_VERSION ${shiboken_MINOR_VERSION})
set(libshiboken_MICRO_VERSION ${shiboken_MICRO_VERSION})
set(libshiboken_VERSION "${libshiboken_MAJOR_VERSION}.${libshiboken_MINOR_VERSION}.${libshiboken_MICRO_VERSION}")
set(libshiboken_SOVERSION "${shiboken6_library_so_version}")

set(libshiboken_SRC
autodecref.h
basewrapper.cpp basewrapper.h basewrapper_p.h
basewrapper_info.cpp
bindingmanager.cpp bindingmanager.h
bufferprocs_py37.cpp bufferprocs_py37.h
debugfreehook.cpp debugfreehook.h
gilstate.cpp gilstate.h
helper.cpp helper.h
pep384ext.cpp pep384ext.h
pep384impl.cpp pep384impl.h pep384impl_p.h

pyobjectholder.h
sbkarrayconverter.cpp sbkarrayconverter.h sbkarrayconverter_p.h
sbkarrayspan_p.h
sbkbindingutils.cpp sbkbindingutils.h
sbkcontainer.cpp sbkcontainer.h
sbkconverter.cpp sbkconverter.h sbkconverter_p.h
sbkcppstring.cpp sbkcppstring.h sbkcpptonumpy.h
sbkdestructorentry.h
sbkenum.cpp sbkenum.h sbkenum_p.h
sbkerrors.cpp sbkerrors.h
sbkfeature_base.cpp sbkfeature_base.h
sbkmodule.cpp sbkmodule.h sbkmodule_p.h
sbknumpy.cpp sbknumpycheck.h
sbknumpyview.h
sbkpep.h
sbkpepbuffer.h
sbkpython.h
sbkpydictrange.h
sbkpysequencerange.h
sbkpytuplerange.h
sbkpyrangespolicies.h
sbksmartpointer.cpp sbksmartpointer.h
sbkstaticstrings.cpp sbkstaticstrings.h sbkstaticstrings_p.h
sbkstring.cpp sbkstring.h
sbktypefactory.cpp sbktypefactory.h
sbkwindows.h
shiboken.h
shibokenbuffer.cpp shibokenbuffer.h
shibokenmacros.h shibokenclasshelpermacros.h
threadstatesaver.cpp threadstatesaver.h
voidptr.cpp voidptr.h

embed/signature_bootstrap_inc.h
embed/signature_inc.h

signature/signature.cpp signature.h signature_p.h
signature/signature_globals.cpp
signature/signature_extend.cpp
signature/signature_helper.cpp
)

# This is needed to let the header obey a variable in "pep384impl.h".
# Note: This must be set on the cpp file!
set_property(SOURCE "pep384impl.cpp" PROPERTY SKIP_UNITY_BUILD_INCLUSION ON)

if(IOS)
    set(library_type STATIC)
else()
    set(library_type SHARED)
endif()
add_library(libshiboken ${library_type} ${libshiboken_SRC})

if(CMAKE_SYSTEM_NAME STREQUAL "Android")
    # Android requires 16 KB aligned LOAD segments for Play Store submission.
    # Targets that link against Qt inherit this from Qt6::Platform's
    # INTERFACE_LINK_OPTIONS; libshiboken has no Qt dependency, so the NDK
    # linker falls back to its 4 KB default unless asked explicitly.
    target_link_options(libshiboken PRIVATE "-Wl,-z,max-page-size=16384")
endif()

if(NOT TARGET Shiboken6::libshiboken)
    add_library(Shiboken6::libshiboken ALIAS libshiboken)
endif()

target_include_directories(libshiboken PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
  $<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}>
)

if (NOT "${NUMPY_INCLUDE_DIR}" STREQUAL "")
    message(STATUS "NUMPY_INCLUDE_DIR: " ${NUMPY_INCLUDE_DIR})
    target_include_directories(libshiboken PRIVATE ${NUMPY_INCLUDE_DIR})
    target_compile_definitions(libshiboken PRIVATE -DHAVE_NUMPY
                                           PRIVATE -DNPY_NO_DEPRECATED_API=NPY_1_7_API_VERSION)
else()
    message(STATUS "NUMPY not found")
endif()

if(SHIBOKEN_IS_CROSS_BUILD)
    target_compile_definitions(libshiboken PRIVATE -DSHIBOKEN_NO_EMBEDDING_PYC=1)
endif()

shiboken_compute_python_includes()
# On Windows we need to link against the python.lib import library.
# On macOS and Linux we don't link against the python shared / static library,
# the dynamic linker will pick up the python symbols at runtime automatically.
shiboken_compute_python_libraries()

if(PYTHON_LIMITED_API)
    target_compile_definitions(libshiboken PUBLIC "-DPy_LIMITED_API=0x030a0000")
endif()

if(CMAKE_BUILD_TYPE STREQUAL "Debug")
    if(PYTHON_WITH_DEBUG)
        target_compile_definitions(libshiboken PUBLIC "-DPy_DEBUG")
    endif()
    if (PYTHON_WITH_COUNT_ALLOCS)
        target_compile_definitions(libshiboken PUBLIC "-DCOUNT_ALLOCS")
    endif()
elseif(CMAKE_BUILD_TYPE STREQUAL "Release")
    target_compile_definitions(libshiboken PUBLIC "-DNDEBUG")
endif()

set(set_props_args DEFINE_SYMBOL BUILD_LIBSHIBOKEN)
if(IOS)
    list(APPEND set_props_args OUTPUT_NAME "shiboken6${shiboken6_SUFFIX}")
else()
    list(APPEND set_props_args
        OUTPUT_NAME "shiboken6${shiboken6_SUFFIX}${PYTHON_SHARED_LIBRARY_SUFFIX}"
        VERSION ${libshiboken_VERSION}
        SOVERSION ${libshiboken_SOVERSION}
    )
endif()
set_target_properties(libshiboken PROPERTIES ${set_props_args})

qfp_strip_library("libshiboken")

install(FILES
        autodecref.h
        basewrapper.h
        basewrapper_p.h
        bindingmanager.h
        gilstate.h
        helper.h
        pyobjectholder.h
        sbkarrayconverter.h
        sbkarrayspan_p.h
        sbkbindingutils.h
        sbkcontainer.h
        sbkconverter.h
        sbkcpptonumpy.h
        sbkdestructorentry.h
        sbkenum.h
        sbkenum_p.h
        sbkerrors.h
        sbkfeature_base.h
        sbkmodule.h
        sbkmodule_p.h
        sbknumpycheck.h
        sbknumpyview.h
        sbkpydictrange.h
        sbkpysequencerange.h
        sbkpytuplerange.h
        sbkpyrangespolicies.h
        sbkstring.h
        sbkcppstring.h
        sbksmartpointer.h
        sbkstaticstrings.h
        sbktypefactory.h
        shiboken.h
        shibokenmacros.h
        shibokenclasshelpermacros.h
        threadstatesaver.h
        shibokenbuffer.h
        sbkpep.h
        sbkpepbuffer.h
        sbkpython.h
        sbkwindows.h
        pep384impl.h
        pep384impl_p.h
        pep384ext.h
        voidptr.h
        bufferprocs_py37.h
        "${CMAKE_CURRENT_BINARY_DIR}/sbkversion.h"

        signature.h
        signature_p.h

        DESTINATION include/shiboken6${shiboken6_SUFFIX})
install(TARGETS libshiboken EXPORT Shiboken6Targets
                            LIBRARY DESTINATION "${LIB_INSTALL_DIR}"
                            ARCHIVE DESTINATION "${LIB_INSTALL_DIR}"
                            RUNTIME DESTINATION bin
                            INCLUDES DESTINATION "include/shiboken6")
install(EXPORT Shiboken6Targets NAMESPACE Shiboken6::
        DESTINATION ${LIB_INSTALL_DIR}/cmake/Shiboken6)

# wheel specific installation
if(NOT is_pyside6_superproject_build)

    set_target_properties(libshiboken PROPERTIES
        VERSION ${libshiboken_SOVERSION})

    install(TARGETS libshiboken EXPORT Shiboken6WheelTargets
            LIBRARY DESTINATION "shiboken6"
            ARCHIVE DESTINATION "shiboken6"
            RUNTIME DESTINATION "shiboken6"
            INCLUDES DESTINATION "shiboken6/include/shiboken6")

    install(EXPORT Shiboken6WheelTargets
            NAMESPACE Shiboken6::
            DESTINATION "${LIB_INSTALL_DIR}/wheels/cmake/Shiboken6"
            FILE Shiboken6Targets.cmake)
endif()

set(libshiboken_qdocconf "${CMAKE_CURRENT_BINARY_DIR}/libshiboken.qdocconf")

configure_file("${CMAKE_CURRENT_SOURCE_DIR}/libshiboken.qdocconf.in"
               "${libshiboken_qdocconf}" @ONLY)

add_custom_target(libshiboken_qdoc
                  COMMAND qdoc "${libshiboken_qdocconf}"
                  COMMENT "Generating libshiboken documentation"
                  VERBATIM)
