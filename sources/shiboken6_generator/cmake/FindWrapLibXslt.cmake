# Copyright (C) 2026 The Qt Company Ltd.
# SPDX-License-Identifier: BSD-3-Clause

# Provides the WrapLibXslt::WrapLibXslt target, which links to LibXslt::LibXslt.

if(TARGET WrapLibXslt::WrapLibXslt)
    set(WrapLibXslt_FOUND TRUE)
    return()
endif()
set(WrapLibXslt_REQUIRED_VARS __libxslt_found)

find_package(LibXslt ${${CMAKE_FIND_PACKAGE_NAME}_FIND_VERSION})

if(LibXslt_FOUND AND TARGET LibXslt::LibXslt)
    set(__libxslt_found TRUE)
endif()

if(LIBXSLT_LIBRARIES)
    list(PREPEND WrapLibXslt_REQUIRED_VARS LIBXSLT_LIBRARIES)
endif()

# LIBXSLT_VERSION_STRING is deprecated in favor of LibXslt_VERSION since CMake 4.2.
if(LibXslt_VERSION)
    set(WrapLibXslt_VERSION "${LibXslt_VERSION}")
elseif(LIBXSLT_VERSION_STRING)
    set(WrapLibXslt_VERSION "${LIBXSLT_VERSION_STRING}")
endif()

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(WrapLibXslt
                                  REQUIRED_VARS ${WrapLibXslt_REQUIRED_VARS}
                                  VERSION_VAR WrapLibXslt_VERSION)

if(WrapLibXslt_FOUND)
    add_library(WrapLibXslt::WrapLibXslt INTERFACE IMPORTED)
    target_link_libraries(WrapLibXslt::WrapLibXslt INTERFACE LibXslt::LibXslt)
endif()

unset(__libxslt_found)
