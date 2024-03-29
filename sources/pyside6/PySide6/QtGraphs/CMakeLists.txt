# Copyright (C) 2023 The Qt Company Ltd.
# SPDX-License-Identifier: BSD-3-Clause

project(QtGraphs)

list(APPEND QtGraphs_src "${QtGraphs_SOURCE_DIR}/qtgraphs_helper.cpp")

set(QtGraphs_SRC
${QtGraphs_GEN_DIR}/qabstract3daxis_wrapper.cpp
${QtGraphs_GEN_DIR}/qcategory3daxis_wrapper.cpp
${QtGraphs_GEN_DIR}/qlogvalue3daxisformatter_wrapper.cpp
${QtGraphs_GEN_DIR}/qvalue3daxis_wrapper.cpp
${QtGraphs_GEN_DIR}/qvalue3daxisformatter_wrapper.cpp
${QtGraphs_GEN_DIR}/qabstract3dseries_wrapper.cpp
${QtGraphs_GEN_DIR}/qabstractdataproxy_wrapper.cpp
${QtGraphs_GEN_DIR}/qbar3dseries_wrapper.cpp
${QtGraphs_GEN_DIR}/qbardataitem_wrapper.cpp
${QtGraphs_GEN_DIR}/qbardataproxy_wrapper.cpp
${QtGraphs_GEN_DIR}/qcustom3ditem_wrapper.cpp
${QtGraphs_GEN_DIR}/qcustom3dlabel_wrapper.cpp
${QtGraphs_GEN_DIR}/qcustom3dvolume_wrapper.cpp
${QtGraphs_GEN_DIR}/qheightmapsurfacedataproxy_wrapper.cpp
${QtGraphs_GEN_DIR}/qitemmodelbardataproxy_wrapper.cpp
${QtGraphs_GEN_DIR}/qitemmodelscatterdataproxy_wrapper.cpp
${QtGraphs_GEN_DIR}/qitemmodelsurfacedataproxy_wrapper.cpp
${QtGraphs_GEN_DIR}/qscatter3dseries_wrapper.cpp
${QtGraphs_GEN_DIR}/qscatterdataitem_wrapper.cpp
${QtGraphs_GEN_DIR}/qscatterdataproxy_wrapper.cpp
${QtGraphs_GEN_DIR}/qsurface3dseries_wrapper.cpp
${QtGraphs_GEN_DIR}/qsurfacedataitem_wrapper.cpp
${QtGraphs_GEN_DIR}/qsurfacedataproxy_wrapper.cpp
${QtGraphs_GEN_DIR}/q3dbars_wrapper.cpp
${QtGraphs_GEN_DIR}/q3dcamera_wrapper.cpp
${QtGraphs_GEN_DIR}/q3dlight_wrapper.cpp
${QtGraphs_GEN_DIR}/q3dobject_wrapper.cpp
${QtGraphs_GEN_DIR}/q3dscatter_wrapper.cpp
${QtGraphs_GEN_DIR}/q3dscene_wrapper.cpp
${QtGraphs_GEN_DIR}/q3dsurface_wrapper.cpp
${QtGraphs_GEN_DIR}/qabstract3dgraph_wrapper.cpp
${QtGraphs_GEN_DIR}/q3dinputhandler_wrapper.cpp
${QtGraphs_GEN_DIR}/qabstract3dinputhandler_wrapper.cpp
${QtGraphs_GEN_DIR}/qtouch3dinputhandler_wrapper.cpp
${QtGraphs_GEN_DIR}/q3dtheme_wrapper.cpp
# module is always needed
${QtGraphs_GEN_DIR}/qtgraphs_module_wrapper.cpp
)

set(QtGraphs_include_dirs ${QtGraphs_SOURCE_DIR}
                          ${QtGraphs_BINARY_DIR}
                          ${Qt${QT_MAJOR_VERSION}Core_INCLUDE_DIRS}
                          ${Qt${QT_MAJOR_VERSION}QtNetwork_INCLUDE_DIRS}
                          ${Qt${QT_MAJOR_VERSION}Gui_INCLUDE_DIRS}
                          ${Qt${QT_MAJOR_VERSION}Widgets_INCLUDE_DIRS}
                          ${Qt${QT_MAJOR_VERSION}Qml_INCLUDE_DIRS}
                          ${Qt${QT_MAJOR_VERSION}OpenGL_INCLUDE_DIRS}
                          ${Qt${QT_MAJOR_VERSION}Quick_INCLUDE_DIRS}
                          ${Qt${QT_MAJOR_VERSION}QuickWidgets_INCLUDE_DIRS}
                          ${Qt${QT_MAJOR_VERSION}Graphs_INCLUDE_DIRS}
                          ${Qt${QT_MAJOR_VERSION}Quick3D_INCLUDE_DIRS}
                          ${libpyside_SOURCE_DIR}
                          ${QtCore_GEN_DIR}
                          ${QtNetwork_GEN_DIR}
                          ${QtGui_GEN_DIR}
                          ${QtWidgets_GEN_DIR}
                          ${QtQml_GEN_DIR}
                          ${QtOpenGL_GEN_DIR}
                          ${QtQuick_GEN_DIR}
                          ${QtQuickWidgets_GEN_DIR}
                          ${QtQuick3D_GEN_DIR})

set(QtGraphs_libraries    pyside6
                          ${Qt${QT_MAJOR_VERSION}Graphs_LIBRARIES}
                          ${Qt${QT_MAJOR_VERSION}Core_LIBRARIES}
                          ${Qt${QT_MAJOR_VERSION}Network_LIBRARIES}
                          ${Qt${QT_MAJOR_VERSION}Gui_LIBRARIES}
                          ${Qt${QT_MAJOR_VERSION}Widgets_LIBRARIES}
                          ${Qt${QT_MAJOR_VERSION}Qml_LIBRARIES}
                          ${Qt${QT_MAJOR_VERSION}OpenGL_LIBRARIES}
                          ${Qt${QT_MAJOR_VERSION}Quick_LIBRARIES}
                          ${Qt${QT_MAJOR_VERSION}QuickWidgets_LIBRARIES}
                          ${Qt${QT_MAJOR_VERSION}Quick3D_LIBRARIES})

set(QtGraphs_deps QtCore QtNetwork QtGui QtWidgets QtQml QtOpenGL QtQuick QtQuickWidgets QtQuick3D)

create_pyside_module(NAME QtGraphs
                     INCLUDE_DIRS QtGraphs_include_dirs
                     LIBRARIES QtGraphs_libraries
                     DEPS QtGraphs_deps
                     TYPESYSTEM_PATH QtGraphs_SOURCE_DIR
                     SOURCES QtGraphs_SRC
                     STATIC_SOURCES QtGraphs_src)

install(FILES ${pyside6_SOURCE_DIR}/qtgraphs_helper.h
        DESTINATION include/PySide6/QtGraphs)
