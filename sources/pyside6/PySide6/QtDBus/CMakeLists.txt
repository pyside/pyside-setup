# Copyright (C) 2023 The Qt Company Ltd.
# SPDX-License-Identifier: BSD-3-Clause

set(QtDBus_SRC
    ${QtDBus_GEN_DIR}/qdbus_wrapper.cpp
    ${QtDBus_GEN_DIR}/qdbusabstractadaptor_wrapper.cpp
    ${QtDBus_GEN_DIR}/qdbusabstractinterface_wrapper.cpp
    ${QtDBus_GEN_DIR}/qdbusabstractinterfacebase_wrapper.cpp
    ${QtDBus_GEN_DIR}/qdbusargument_wrapper.cpp
    ${QtDBus_GEN_DIR}/qdbusconnection_wrapper.cpp
    ${QtDBus_GEN_DIR}/qdbusconnectioninterface_wrapper.cpp
    ${QtDBus_GEN_DIR}/qdbuscontext_wrapper.cpp
    ${QtDBus_GEN_DIR}/qdbuserror_wrapper.cpp
    ${QtDBus_GEN_DIR}/qdbusinterface_wrapper.cpp
    ${QtDBus_GEN_DIR}/qdbusmessage_wrapper.cpp
    ${QtDBus_GEN_DIR}/qdbusobjectpath_wrapper.cpp
    ${QtDBus_GEN_DIR}/qdbuspendingcall_wrapper.cpp
    ${QtDBus_GEN_DIR}/qdbuspendingcallwatcher_wrapper.cpp
    ${QtDBus_GEN_DIR}/qtdbushelper_qdbusreply_wrapper.cpp
    #${QtDBus_GEN_DIR}/qdbuspendingreply_wrapper.cpp
    ${QtDBus_GEN_DIR}/qdbusserver_wrapper.cpp
    ${QtDBus_GEN_DIR}/qdbusservicewatcher_wrapper.cpp
    ${QtDBus_GEN_DIR}/qdbussignature_wrapper.cpp
    ${QtDBus_GEN_DIR}/qdbusunixfiledescriptor_wrapper.cpp
    ${QtDBus_GEN_DIR}/qdbusvariant_wrapper.cpp
    ${QtDBus_GEN_DIR}/qdbusvirtualobject_wrapper.cpp
# module is always needed
    ${QtDBus_GEN_DIR}/qtdbus_module_wrapper.cpp
)

configure_file("${CMAKE_CURRENT_SOURCE_DIR}/QtDBus_global.post.h.in"
               "${CMAKE_CURRENT_BINARY_DIR}/QtDBus_global.post.h" @ONLY)

set(QtDBus_include_dirs
    ${QtCore_GEN_DIR}
)

set(QtDBus_deps
    QtCore
)

set(QtDBus_libraries
    Qt::DBus
)

create_pyside_module(NAME QtDBus
                     INCLUDE_DIRS QtDBus_include_dirs
                     LIBRARIES QtDBus_libraries
                     DEPS QtDBus_deps
                     TYPESYSTEM_PATH CMAKE_CURRENT_SOURCE_DIR
                     SOURCES QtDBus_SRC)

install(FILES ${PYSIDE_PACKAGE_SOURCE_DIR}/qtdbushelper.h DESTINATION include/PySide6/QtDBus)
qfp_sbom_add_files(QtDBus
    FILES ${PYSIDE_PACKAGE_SOURCE_DIR}/qtdbushelper.h
    FILE_TYPE "SOURCE"
    INSTALL_PATH "include/PySide6/QtDBus"
)
