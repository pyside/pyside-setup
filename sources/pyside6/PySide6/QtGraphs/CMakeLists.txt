# Copyright (C) 2023 The Qt Company Ltd.
# SPDX-License-Identifier: BSD-3-Clause

set(QtGraphs_DROPPED_ENTRIES)

list(APPEND QtGraphs_src "${CMAKE_CURRENT_SOURCE_DIR}/qtgraphs_helper.cpp")

set(QtGraphs_SRC
# 3D
${QtGraphs_GEN_DIR}/qabstract3daxis_wrapper.cpp
${QtGraphs_GEN_DIR}/qcategory3daxis_wrapper.cpp
${QtGraphs_GEN_DIR}/qtgraphs3d_wrapper.cpp
${QtGraphs_GEN_DIR}/qgraphsline_wrapper.cpp
${QtGraphs_GEN_DIR}/qgraphstheme_wrapper.cpp
${QtGraphs_GEN_DIR}/qgraphsthemedirtybitfield_wrapper.cpp
${QtGraphs_GEN_DIR}/qlogvalue3daxisformatter_wrapper.cpp
${QtGraphs_GEN_DIR}/qvalue3daxis_wrapper.cpp
${QtGraphs_GEN_DIR}/qvalue3daxisformatter_wrapper.cpp
${QtGraphs_GEN_DIR}/qabstract3dseries_wrapper.cpp
${QtGraphs_GEN_DIR}/qabstractdataproxy_wrapper.cpp
${QtGraphs_GEN_DIR}/qbar3dseries_wrapper.cpp
${QtGraphs_GEN_DIR}/qbardataitem_wrapper.cpp
${QtGraphs_GEN_DIR}/qbardataproxy_wrapper.cpp
${QtGraphs_GEN_DIR}/qcustomseries_wrapper.cpp
${QtGraphs_GEN_DIR}/qcustom3ditem_wrapper.cpp
${QtGraphs_GEN_DIR}/qcustom3dlabel_wrapper.cpp
${QtGraphs_GEN_DIR}/qcustom3dvolume_wrapper.cpp
${QtGraphs_GEN_DIR}/qheightmapsurfacedataproxy_wrapper.cpp
${QtGraphs_GEN_DIR}/qitemmodelbardataproxy_wrapper.cpp
${QtGraphs_GEN_DIR}/qitemmodelscatterdataproxy_wrapper.cpp
${QtGraphs_GEN_DIR}/qitemmodelsurfacedataproxy_wrapper.cpp
${QtGraphs_GEN_DIR}/qlegenddata_wrapper.cpp
${QtGraphs_GEN_DIR}/qscatter3dseries_wrapper.cpp
${QtGraphs_GEN_DIR}/qscatterdataitem_wrapper.cpp
${QtGraphs_GEN_DIR}/qscatterdataproxy_wrapper.cpp
${QtGraphs_GEN_DIR}/qspline3dseries_wrapper.cpp
${QtGraphs_GEN_DIR}/qsurface3dseries_wrapper.cpp
${QtGraphs_GEN_DIR}/qsurfacedataitem_wrapper.cpp
${QtGraphs_GEN_DIR}/qsurfacedataproxy_wrapper.cpp
${QtGraphs_GEN_DIR}/q3dscene_wrapper.cpp
# 2D
${QtGraphs_GEN_DIR}/qabstractaxis_wrapper.cpp
${QtGraphs_GEN_DIR}/qabstractseries_wrapper.cpp
${QtGraphs_GEN_DIR}/qareaseries_wrapper.cpp
${QtGraphs_GEN_DIR}/qbarcategoryaxis_wrapper.cpp
${QtGraphs_GEN_DIR}/qbarmodelmapper_wrapper.cpp
${QtGraphs_GEN_DIR}/qbarseries_wrapper.cpp
${QtGraphs_GEN_DIR}/qbarset_wrapper.cpp
${QtGraphs_GEN_DIR}/qdatetimeaxis_wrapper.cpp
${QtGraphs_GEN_DIR}/qlineseries_wrapper.cpp
${QtGraphs_GEN_DIR}/qlogvalueaxis_wrapper.cpp
${QtGraphs_GEN_DIR}/qpiemodelmapper_wrapper.cpp
${QtGraphs_GEN_DIR}/qpieseries_wrapper.cpp
${QtGraphs_GEN_DIR}/qpieslice_wrapper.cpp
${QtGraphs_GEN_DIR}/qscatterseries_wrapper.cpp
${QtGraphs_GEN_DIR}/qsplineseries_wrapper.cpp
${QtGraphs_GEN_DIR}/qvalueaxis_wrapper.cpp
${QtGraphs_GEN_DIR}/qxymodelmapper_wrapper.cpp
${QtGraphs_GEN_DIR}/qxyseries_wrapper.cpp
# module is always needed
${QtGraphs_GEN_DIR}/qtgraphs_module_wrapper.cpp
)

set(QtGraphs_include_dirs
    ${QtCore_GEN_DIR}
    ${QtNetwork_GEN_DIR}
    ${QtGui_GEN_DIR}
    ${QtQml_GEN_DIR}
    ${QtQuick_GEN_DIR}
    ${QtQuick3D_GEN_DIR}
)

set(QtGraphs_deps
    QtCore
    QtNetwork
    QtGui
    QtQml
    QtQuick
    QtQuick3D
)

set(QtGraphs_libraries
    Qt::Graphs
)

check_qt_opengl("Graphs" QtGraphs_include_dirs QtGraphs_deps
                QtGraphs_DROPPED_ENTRIES)

create_pyside_module(NAME QtGraphs
                     INCLUDE_DIRS QtGraphs_include_dirs
                     LIBRARIES QtGraphs_libraries
                     DEPS QtGraphs_deps
                     TYPESYSTEM_PATH CMAKE_CURRENT_SOURCE_DIR
                     SOURCES QtGraphs_SRC
                     STATIC_SOURCES QtGraphs_src
                     DROPPED_ENTRIES QtGraphs_DROPPED_ENTRIES)

install(FILES ${PYSIDE_PACKAGE_SOURCE_DIR}/qtgraphs_helper.h
        DESTINATION include/PySide6/QtGraphs)
qfp_sbom_add_files(QtGraphs
    FILES ${PYSIDE_PACKAGE_SOURCE_DIR}/qtgraphs_helper.h
    FILE_TYPE "SOURCE"
    INSTALL_PATH "include/PySide6/QtGraphs"
)
