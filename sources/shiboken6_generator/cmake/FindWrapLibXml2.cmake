# Copyright (C) 2026 The Qt Company Ltd.
# SPDX-License-Identifier: BSD-3-Clause

# Provides the WrapLibXml2::WrapLibXml2 target, which links to LibXml2::LibXml2.

if(TARGET WrapLibXml2::WrapLibXml2)
    set(WrapLibXml2_FOUND TRUE)
    return()
endif()
set(WrapLibXml2_REQUIRED_VARS __libxml2_found)

find_package(LibXml2 ${${CMAKE_FIND_PACKAGE_NAME}_FIND_VERSION})

if(LibXml2_FOUND AND TARGET LibXml2::LibXml2)
    set(__libxml2_found TRUE)
endif()

if(LIBXML2_LIBRARIES)
    list(PREPEND WrapLibXml2_REQUIRED_VARS LIBXML2_LIBRARIES)
endif()

# LIBXML2_VERSION_STRING is deprecated in favor of LibXml2_VERSION since CMake 4.2.
if(LibXml2_VERSION)
    set(WrapLibXml2_VERSION "${LibXml2_VERSION}")
elseif(LIBXML2_VERSION_STRING)
    set(WrapLibXml2_VERSION "${LIBXML2_VERSION_STRING}")
endif()

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(WrapLibXml2
                                  REQUIRED_VARS ${WrapLibXml2_REQUIRED_VARS}
                                  VERSION_VAR WrapLibXml2_VERSION)

if(WrapLibXml2_FOUND)
    add_library(WrapLibXml2::WrapLibXml2 INTERFACE IMPORTED)
    target_link_libraries(WrapLibXml2::WrapLibXml2 INTERFACE LibXml2::LibXml2)
endif()

unset(__libxml2_found)
