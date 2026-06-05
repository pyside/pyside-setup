# Copyright (C) 2023 The Qt Company Ltd.
# SPDX-License-Identifier: BSD-3-Clause

qt_wrap_cpp(QtDesigner_static_src "${PYSIDE_PACKAGE_SOURCE_DIR}/qpydesignerextensions.h")
list(APPEND QtDesigner_static_src "${CMAKE_CURRENT_SOURCE_DIR}/qpydesignercustomwidgetcollection.cpp")

set(QtDesigner_SRC
${QtDesigner_GEN_DIR}/qabstractextensionfactory_wrapper.cpp
${QtDesigner_GEN_DIR}/qabstractextensionmanager_wrapper.cpp
${QtDesigner_GEN_DIR}/qabstractformbuilder_wrapper.cpp
${QtDesigner_GEN_DIR}/qdesigneractioneditorinterface_wrapper.cpp
${QtDesigner_GEN_DIR}/qdesignercontainerextension_wrapper.cpp
${QtDesigner_GEN_DIR}/qdesignercustomwidgetcollectioninterface_wrapper.cpp
${QtDesigner_GEN_DIR}/qdesignercustomwidgetinterface_wrapper.cpp
${QtDesigner_GEN_DIR}/qdesignerdnditeminterface_wrapper.cpp
${QtDesigner_GEN_DIR}/qdesignerdynamicpropertysheetextension_wrapper.cpp
${QtDesigner_GEN_DIR}/qdesignerformeditorinterface_wrapper.cpp
${QtDesigner_GEN_DIR}/qdesignerformwindowcursorinterface_wrapper.cpp
${QtDesigner_GEN_DIR}/qdesignerformwindowinterface_wrapper.cpp
${QtDesigner_GEN_DIR}/qdesignerformwindowmanagerinterface_wrapper.cpp
${QtDesigner_GEN_DIR}/qdesignerformwindowtoolinterface_wrapper.cpp
${QtDesigner_GEN_DIR}/qdesignermembersheetextension_wrapper.cpp
${QtDesigner_GEN_DIR}/qdesignerobjectinspectorinterface_wrapper.cpp
${QtDesigner_GEN_DIR}/qdesignerpropertyeditorinterface_wrapper.cpp
${QtDesigner_GEN_DIR}/qdesignerpropertysheetextension_wrapper.cpp
${QtDesigner_GEN_DIR}/qdesignertaskmenuextension_wrapper.cpp
${QtDesigner_GEN_DIR}/qdesignerwidgetboxinterface_wrapper.cpp
${QtDesigner_GEN_DIR}/qdesignerwidgetboxinterface_category_wrapper.cpp
${QtDesigner_GEN_DIR}/qdesignerwidgetboxinterface_widget_wrapper.cpp
${QtDesigner_GEN_DIR}/qextensionmanager_wrapper.cpp
${QtDesigner_GEN_DIR}/qextensionfactory_wrapper.cpp
${QtDesigner_GEN_DIR}/qformbuilder_wrapper.cpp
${QtDesigner_GEN_DIR}/qpydesignercustomwidgetcollection_wrapper.cpp
${QtDesigner_GEN_DIR}/qpydesignercontainerextension_wrapper.cpp
${QtDesigner_GEN_DIR}/qpydesignermembersheetextension_wrapper.cpp
${QtDesigner_GEN_DIR}/qpydesignerpropertysheetextension_wrapper.cpp
${QtDesigner_GEN_DIR}/qpydesignertaskmenuextension_wrapper.cpp

# module is always needed
${QtDesigner_GEN_DIR}/qtdesigner_module_wrapper.cpp
)

configure_file("${CMAKE_CURRENT_SOURCE_DIR}/QtDesigner_global.pre.h.in"
               "${CMAKE_CURRENT_BINARY_DIR}/QtDesigner_global.pre.h" @ONLY)
configure_file("${CMAKE_CURRENT_SOURCE_DIR}/QtDesigner_global.post.h.in"
               "${CMAKE_CURRENT_BINARY_DIR}/QtDesigner_global.post.h" @ONLY)

set(QtDesigner_include_dirs
    ${QtCore_GEN_DIR}
    ${QtGui_GEN_DIR}
    ${QtWidgets_GEN_DIR}
)

set(QtDesigner_deps
    QtWidgets
)

set(QtDesigner_libraries
    Qt::Designer
)

create_pyside_module(NAME QtDesigner
                     INCLUDE_DIRS QtDesigner_include_dirs
                     LIBRARIES QtDesigner_libraries
                     DEPS QtDesigner_deps
                     TYPESYSTEM_PATH CMAKE_CURRENT_SOURCE_DIR
                     SOURCES QtDesigner_SRC
                     STATIC_SOURCES QtDesigner_static_src
                     TYPESYSTEM_NAME "${CMAKE_CURRENT_BINARY_DIR}/typesystem_designer.xml")

install(FILES ${PYSIDE_PACKAGE_SOURCE_DIR}/qpydesignerextensions.h DESTINATION include/PySide6/QtDesigner)
qfp_sbom_add_files(QtDesigner
    FILES ${PYSIDE_PACKAGE_SOURCE_DIR}/qpydesignerextensions.h
    FILE_TYPE "SOURCE"
    INSTALL_PATH "include/PySide6/QtDesigner"
)
