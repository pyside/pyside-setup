# Copyright (C) 2023 The Qt Company Ltd.
# SPDX-License-Identifier: BSD-3-Clause

qt_wrap_cpp(QPYTEXTOBJECT_MOC "${PYSIDE_PACKAGE_SOURCE_DIR}/qpytextobject.h")

set(QtGui_DROPPED_ENTRIES)

# Exclude sources that have clashing static helper functions named "iterator_..."
set(QtGui_SRC_UNITY_EXCLUDED_SRC
    ${QtGui_GEN_DIR}/qtextframe_iterator_wrapper.cpp
    ${QtGui_GEN_DIR}/qtextblock_iterator_wrapper.cpp
    ${QtGui_GEN_DIR}/qshaderversion_wrapper.cpp
)

set_property(SOURCE ${QtGui_SRC_UNITY_EXCLUDED_SRC}
             PROPERTY SKIP_UNITY_BUILD_INCLUSION ON)

set(QtGui_SRC_RHI
${QtGui_GEN_DIR}/qrhi_wrapper.cpp
${QtGui_GEN_DIR}/qrhiadapter_wrapper.cpp
${QtGui_GEN_DIR}/qrhibuffer_wrapper.cpp
${QtGui_GEN_DIR}/qrhicolorattachment_wrapper.cpp
${QtGui_GEN_DIR}/qrhicommandbuffer_wrapper.cpp
${QtGui_GEN_DIR}/qrhicomputepipeline_wrapper.cpp
${QtGui_GEN_DIR}/qrhidepthstencilclearvalue_wrapper.cpp
${QtGui_GEN_DIR}/qrhidriverinfo_wrapper.cpp
${QtGui_GEN_DIR}/qrhigraphicspipeline_targetblend_wrapper.cpp
${QtGui_GEN_DIR}/qrhigraphicspipeline_wrapper.cpp
${QtGui_GEN_DIR}/qrhigraphicspipeline_stencilopstate_wrapper.cpp
${QtGui_GEN_DIR}/qrhiinitparams_wrapper.cpp
${QtGui_GEN_DIR}/qrhinativehandles_wrapper.cpp
${QtGui_GEN_DIR}/qrhinullinitparams_wrapper.cpp
${QtGui_GEN_DIR}/qrhireadbackdescription_wrapper.cpp
${QtGui_GEN_DIR}/qrhireadbackresult_wrapper.cpp
${QtGui_GEN_DIR}/qrhirenderbuffer_wrapper.cpp
${QtGui_GEN_DIR}/qrhirenderpassdescriptor_wrapper.cpp
${QtGui_GEN_DIR}/qrhirendertarget_wrapper.cpp
${QtGui_GEN_DIR}/qrhiresource_wrapper.cpp
${QtGui_GEN_DIR}/qrhiresourceupdatebatch_wrapper.cpp
${QtGui_GEN_DIR}/qrhisampler_wrapper.cpp
${QtGui_GEN_DIR}/qrhiscissor_wrapper.cpp
${QtGui_GEN_DIR}/qrhishaderresourcebinding_textureandsampler_wrapper.cpp
${QtGui_GEN_DIR}/qrhishaderresourcebinding_data_storagebufferdata_wrapper.cpp
${QtGui_GEN_DIR}/qrhishaderresourcebinding_data_storageimagedata_wrapper.cpp
${QtGui_GEN_DIR}/qrhishaderresourcebinding_data_wrapper.cpp
${QtGui_GEN_DIR}/qrhishaderresourcebinding_wrapper.cpp
${QtGui_GEN_DIR}/qrhishaderresourcebindings_wrapper.cpp
${QtGui_GEN_DIR}/qrhishaderstage_wrapper.cpp
${QtGui_GEN_DIR}/qrhistats_wrapper.cpp
${QtGui_GEN_DIR}/qrhiswapchain_wrapper.cpp
${QtGui_GEN_DIR}/qrhiswapchainrendertarget_wrapper.cpp
${QtGui_GEN_DIR}/qrhitexture_wrapper.cpp
${QtGui_GEN_DIR}/qrhitexturecopydescription_wrapper.cpp
${QtGui_GEN_DIR}/qrhitexturerendertarget_wrapper.cpp
${QtGui_GEN_DIR}/qrhitexturerendertargetdescription_wrapper.cpp
${QtGui_GEN_DIR}/qrhitexturesubresourceuploaddescription_wrapper.cpp
${QtGui_GEN_DIR}/qrhitextureuploaddescription_wrapper.cpp
${QtGui_GEN_DIR}/qrhitextureuploadentry_wrapper.cpp
${QtGui_GEN_DIR}/qrhitexture_viewformat_wrapper.cpp
${QtGui_GEN_DIR}/qrhivertexinputattribute_wrapper.cpp
${QtGui_GEN_DIR}/qrhivertexinputbinding_wrapper.cpp
${QtGui_GEN_DIR}/qrhivertexinputlayout_wrapper.cpp
${QtGui_GEN_DIR}/qrhiviewport_wrapper.cpp
${QtGui_GEN_DIR}/qshader_wrapper.cpp
${QtGui_GEN_DIR}/qshadercode_wrapper.cpp
${QtGui_GEN_DIR}/qshaderkey_wrapper.cpp
)

if (CMAKE_SYSTEM_NAME STREQUAL "Windows")
    list(APPEND QtGui_SRC_RHI
         ${QtGui_GEN_DIR}/qrhid3d11initparams_wrapper.cpp
         ${QtGui_GEN_DIR}/qrhid3d11nativehandles_wrapper.cpp
         ${QtGui_GEN_DIR}/qrhid3d12initparams_wrapper.cpp
         ${QtGui_GEN_DIR}/qrhid3d12nativehandles_wrapper.cpp)
elseif (CMAKE_SYSTEM_NAME STREQUAL "Darwin")
    list(APPEND QtGui_SRC_RHI
         ${QtGui_GEN_DIR}/qrhimetalinitparams_wrapper.cpp)
endif()

set(QtGui_SRC
${QtGui_SRC_UNITY_EXCLUDED_SRC}
${QtGui_GEN_DIR}/qabstractfileiconprovider_wrapper.cpp
${QtGui_GEN_DIR}/qabstracttextdocumentlayout_paintcontext_wrapper.cpp
${QtGui_GEN_DIR}/qabstracttextdocumentlayout_selection_wrapper.cpp
${QtGui_GEN_DIR}/qabstracttextdocumentlayout_wrapper.cpp
${QtGui_GEN_DIR}/qaccessibilityhints_wrapper.cpp
${QtGui_GEN_DIR}/qaccessible_wrapper.cpp
${QtGui_GEN_DIR}/qaccessibleactioninterface_wrapper.cpp
${QtGui_GEN_DIR}/qaccessibleannouncementevent_wrapper.cpp
${QtGui_GEN_DIR}/qaccessibleattributesinterface_wrapper.cpp
${QtGui_GEN_DIR}/qaccessibleeditabletextinterface_wrapper.cpp
${QtGui_GEN_DIR}/qaccessibleevent_wrapper.cpp
${QtGui_GEN_DIR}/qaccessibleobject_wrapper.cpp
${QtGui_GEN_DIR}/qaccessibleselectioninterface_wrapper.cpp
${QtGui_GEN_DIR}/qaccessiblestatechangeevent_wrapper.cpp
${QtGui_GEN_DIR}/qaccessibletablecellinterface_wrapper.cpp
${QtGui_GEN_DIR}/qaccessibletablemodelchangeevent_wrapper.cpp
${QtGui_GEN_DIR}/qaccessibletextcursorevent_wrapper.cpp
${QtGui_GEN_DIR}/qaccessibletextinsertevent_wrapper.cpp
${QtGui_GEN_DIR}/qaccessibletextinterface_wrapper.cpp
${QtGui_GEN_DIR}/qaccessibletextremoveevent_wrapper.cpp
${QtGui_GEN_DIR}/qaccessibletextselectionevent_wrapper.cpp
${QtGui_GEN_DIR}/qaccessibletextupdateevent_wrapper.cpp
${QtGui_GEN_DIR}/qaccessiblevaluechangeevent_wrapper.cpp
${QtGui_GEN_DIR}/qaccessiblevalueinterface_wrapper.cpp
${QtGui_GEN_DIR}/qaction_wrapper.cpp
${QtGui_GEN_DIR}/qactionevent_wrapper.cpp
${QtGui_GEN_DIR}/qactiongroup_wrapper.cpp
${QtGui_GEN_DIR}/qbackingstore_wrapper.cpp
${QtGui_GEN_DIR}/qbitmap_wrapper.cpp
${QtGui_GEN_DIR}/qbrush_wrapper.cpp
${QtGui_GEN_DIR}/qchildwindowevent_wrapper.cpp
${QtGui_GEN_DIR}/qclipboard_wrapper.cpp
${QtGui_GEN_DIR}/qcloseevent_wrapper.cpp
${QtGui_GEN_DIR}/qcolor_wrapper.cpp
${QtGui_GEN_DIR}/qcolorspace_wrapper.cpp
${QtGui_GEN_DIR}/qcolorconstants_wrapper.cpp
${QtGui_GEN_DIR}/qcolorconstants_svg_wrapper.cpp
${QtGui_GEN_DIR}/qcolortransform_wrapper.cpp
${QtGui_GEN_DIR}/qconicalgradient_wrapper.cpp
${QtGui_GEN_DIR}/qcontextmenuevent_wrapper.cpp
${QtGui_GEN_DIR}/qcursor_wrapper.cpp
${QtGui_GEN_DIR}/qdesktopservices_wrapper.cpp
${QtGui_GEN_DIR}/qdoublevalidator_wrapper.cpp
${QtGui_GEN_DIR}/qdrag_wrapper.cpp
${QtGui_GEN_DIR}/qdragenterevent_wrapper.cpp
${QtGui_GEN_DIR}/qdragleaveevent_wrapper.cpp
${QtGui_GEN_DIR}/qdragmoveevent_wrapper.cpp
${QtGui_GEN_DIR}/qdropevent_wrapper.cpp
${QtGui_GEN_DIR}/qenterevent_wrapper.cpp
${QtGui_GEN_DIR}/qeventpoint_wrapper.cpp
${QtGui_GEN_DIR}/qexposeevent_wrapper.cpp
${QtGui_GEN_DIR}/qfileopenevent_wrapper.cpp
${QtGui_GEN_DIR}/qfocusevent_wrapper.cpp
${QtGui_GEN_DIR}/qfont_wrapper.cpp
${QtGui_GEN_DIR}/qfont_tag_wrapper.cpp
${QtGui_GEN_DIR}/qfontdatabase_wrapper.cpp
${QtGui_GEN_DIR}/qfontinfo_wrapper.cpp
${QtGui_GEN_DIR}/qfontmetrics_wrapper.cpp
${QtGui_GEN_DIR}/qfontmetricsf_wrapper.cpp
${QtGui_GEN_DIR}/qfontvariableaxis_wrapper.cpp
${QtGui_GEN_DIR}/qglyphrun_wrapper.cpp
${QtGui_GEN_DIR}/qgradient_wrapper.cpp
${QtGui_GEN_DIR}/qguiapplication_wrapper.cpp
${QtGui_GEN_DIR}/qhelpevent_wrapper.cpp
${QtGui_GEN_DIR}/qhideevent_wrapper.cpp
${QtGui_GEN_DIR}/qhoverevent_wrapper.cpp
${QtGui_GEN_DIR}/qicon_wrapper.cpp
${QtGui_GEN_DIR}/qicondragevent_wrapper.cpp
${QtGui_GEN_DIR}/qiconengine_scaledpixmapargument_wrapper.cpp
${QtGui_GEN_DIR}/qiconengine_wrapper.cpp
${QtGui_GEN_DIR}/qimage_wrapper.cpp
${QtGui_GEN_DIR}/qimageiohandler_wrapper.cpp
${QtGui_GEN_DIR}/qimagereader_wrapper.cpp
${QtGui_GEN_DIR}/qimagewriter_wrapper.cpp
${QtGui_GEN_DIR}/qinputdevice_wrapper.cpp
${QtGui_GEN_DIR}/qinputevent_wrapper.cpp
${QtGui_GEN_DIR}/qinputmethod_wrapper.cpp
${QtGui_GEN_DIR}/qinputmethodevent_attribute_wrapper.cpp
${QtGui_GEN_DIR}/qinputmethodevent_wrapper.cpp
${QtGui_GEN_DIR}/qinputmethodqueryevent_wrapper.cpp
${QtGui_GEN_DIR}/qintvalidator_wrapper.cpp
${QtGui_GEN_DIR}/qkeyevent_wrapper.cpp
${QtGui_GEN_DIR}/qkeysequence_wrapper.cpp
${QtGui_GEN_DIR}/qlineargradient_wrapper.cpp
${QtGui_GEN_DIR}/qmatrix2x2_wrapper.cpp
${QtGui_GEN_DIR}/qmatrix2x3_wrapper.cpp
${QtGui_GEN_DIR}/qmatrix2x4_wrapper.cpp
${QtGui_GEN_DIR}/qmatrix3x2_wrapper.cpp
${QtGui_GEN_DIR}/qmatrix3x3_wrapper.cpp
${QtGui_GEN_DIR}/qmatrix3x4_wrapper.cpp
${QtGui_GEN_DIR}/qmatrix4x2_wrapper.cpp
${QtGui_GEN_DIR}/qmatrix4x3_wrapper.cpp
${QtGui_GEN_DIR}/qmatrix4x4_wrapper.cpp
${QtGui_GEN_DIR}/qmouseevent_wrapper.cpp
${QtGui_GEN_DIR}/qmoveevent_wrapper.cpp
${QtGui_GEN_DIR}/qmovie_wrapper.cpp
${QtGui_GEN_DIR}/qnativegestureevent_wrapper.cpp
${QtGui_GEN_DIR}/qnativeinterface_wrapper.cpp
${QtGui_GEN_DIR}/qoffscreensurface_wrapper.cpp
${QtGui_GEN_DIR}/qpagedpaintdevice_wrapper.cpp
${QtGui_GEN_DIR}/qpagelayout_wrapper.cpp
${QtGui_GEN_DIR}/qpageranges_wrapper.cpp
${QtGui_GEN_DIR}/qpageranges_range_wrapper.cpp
${QtGui_GEN_DIR}/qpagesize_wrapper.cpp
${QtGui_GEN_DIR}/qpaintdevice_wrapper.cpp
${QtGui_GEN_DIR}/qpaintdevicewindow_wrapper.cpp
${QtGui_GEN_DIR}/qpaintengine_wrapper.cpp
${QtGui_GEN_DIR}/qpaintenginestate_wrapper.cpp
${QtGui_GEN_DIR}/qpainter_pixmapfragment_wrapper.cpp
${QtGui_GEN_DIR}/qpainter_wrapper.cpp
${QtGui_GEN_DIR}/qpainterpath_element_wrapper.cpp
${QtGui_GEN_DIR}/qpainterpath_wrapper.cpp
${QtGui_GEN_DIR}/qpainterpathstroker_wrapper.cpp
${QtGui_GEN_DIR}/qpainterstateguard_wrapper.cpp
${QtGui_GEN_DIR}/qpaintevent_wrapper.cpp
${QtGui_GEN_DIR}/qpalette_wrapper.cpp
${QtGui_GEN_DIR}/qpdfoutputintent_wrapper.cpp
${QtGui_GEN_DIR}/qpdfwriter_wrapper.cpp
${QtGui_GEN_DIR}/qpen_wrapper.cpp
${QtGui_GEN_DIR}/qpicture_wrapper.cpp
${QtGui_GEN_DIR}/qpixmap_wrapper.cpp
${QtGui_GEN_DIR}/qpixmapcache_key_wrapper.cpp
${QtGui_GEN_DIR}/qpixmapcache_wrapper.cpp
${QtGui_GEN_DIR}/qpixelformat_wrapper.cpp
${QtGui_GEN_DIR}/qplatformsurfaceevent_wrapper.cpp
${QtGui_GEN_DIR}/qpointerevent_wrapper.cpp
${QtGui_GEN_DIR}/qpointingdevice_wrapper.cpp
${QtGui_GEN_DIR}/qpointingdeviceuniqueid_wrapper.cpp
${QtGui_GEN_DIR}/qpolygon_wrapper.cpp
${QtGui_GEN_DIR}/qpolygonf_wrapper.cpp
${QtGui_GEN_DIR}/qpytextobject_wrapper.cpp
${QtGui_GEN_DIR}/qquaternion_wrapper.cpp
${QtGui_GEN_DIR}/qquaternion_axes_wrapper.cpp
${QtGui_GEN_DIR}/qradialgradient_wrapper.cpp
${QtGui_GEN_DIR}/qregularexpressionvalidator_wrapper.cpp
${QtGui_GEN_DIR}/qrgba64_wrapper.cpp
${QtGui_GEN_DIR}/qregion_wrapper.cpp
${QtGui_GEN_DIR}/qresizeevent_wrapper.cpp
${QtGui_GEN_DIR}/qsessionmanager_wrapper.cpp
${QtGui_GEN_DIR}/qshortcutevent_wrapper.cpp
${QtGui_GEN_DIR}/qshowevent_wrapper.cpp
${QtGui_GEN_DIR}/qsinglepointevent_wrapper.cpp
${QtGui_GEN_DIR}/qstandarditem_wrapper.cpp
${QtGui_GEN_DIR}/qstandarditemmodel_wrapper.cpp
${QtGui_GEN_DIR}/qstatustipevent_wrapper.cpp
${QtGui_GEN_DIR}/qaccessible_state_wrapper.cpp
${QtGui_GEN_DIR}/qaccessibleinterface_wrapper.cpp
${QtGui_GEN_DIR}/qscreen_wrapper.cpp
${QtGui_GEN_DIR}/qshortcut_wrapper.cpp
${QtGui_GEN_DIR}/qrasterwindow_wrapper.cpp
${QtGui_GEN_DIR}/qrawfont_wrapper.cpp
${QtGui_SRC_RHI}
${QtGui_GEN_DIR}/qscrollevent_wrapper.cpp
${QtGui_GEN_DIR}/qscrollprepareevent_wrapper.cpp
${QtGui_GEN_DIR}/qstatictext_wrapper.cpp
${QtGui_GEN_DIR}/qstylehints_wrapper.cpp
${QtGui_GEN_DIR}/qsurface_wrapper.cpp
${QtGui_GEN_DIR}/qsurfaceformat_wrapper.cpp
${QtGui_GEN_DIR}/qsyntaxhighlighter_wrapper.cpp
${QtGui_GEN_DIR}/qtabletevent_wrapper.cpp
${QtGui_GEN_DIR}/qtextblock_wrapper.cpp
${QtGui_GEN_DIR}/qtextblockformat_wrapper.cpp
${QtGui_GEN_DIR}/qtextblockgroup_wrapper.cpp
${QtGui_GEN_DIR}/qtextblockuserdata_wrapper.cpp
${QtGui_GEN_DIR}/qtextcharformat_wrapper.cpp
${QtGui_GEN_DIR}/qtextcursor_wrapper.cpp
${QtGui_GEN_DIR}/qtextdocument_wrapper.cpp
${QtGui_GEN_DIR}/qtextdocumentfragment_wrapper.cpp
${QtGui_GEN_DIR}/qtextdocumentwriter_wrapper.cpp
${QtGui_GEN_DIR}/qtextformat_wrapper.cpp
${QtGui_GEN_DIR}/qtextfragment_wrapper.cpp
${QtGui_GEN_DIR}/qtextframe_wrapper.cpp
${QtGui_GEN_DIR}/qtextframeformat_wrapper.cpp
${QtGui_GEN_DIR}/qtextimageformat_wrapper.cpp
${QtGui_GEN_DIR}/qtextinlineobject_wrapper.cpp
${QtGui_GEN_DIR}/qtextitem_wrapper.cpp
${QtGui_GEN_DIR}/qtextlayout_formatrange_wrapper.cpp
${QtGui_GEN_DIR}/qtextlayout_wrapper.cpp
${QtGui_GEN_DIR}/qtextlength_wrapper.cpp
${QtGui_GEN_DIR}/qtextline_wrapper.cpp
${QtGui_GEN_DIR}/qtextlist_wrapper.cpp
${QtGui_GEN_DIR}/qtextlistformat_wrapper.cpp
${QtGui_GEN_DIR}/qtextobject_wrapper.cpp
${QtGui_GEN_DIR}/qtextobjectinterface_wrapper.cpp
${QtGui_GEN_DIR}/qtextoption_tab_wrapper.cpp
${QtGui_GEN_DIR}/qtextoption_wrapper.cpp
${QtGui_GEN_DIR}/qtexttable_wrapper.cpp
${QtGui_GEN_DIR}/qtexttablecell_wrapper.cpp
${QtGui_GEN_DIR}/qtexttablecellformat_wrapper.cpp
${QtGui_GEN_DIR}/qtexttableformat_wrapper.cpp
${QtGui_GEN_DIR}/qtguihelper_qoverridecursorguard_wrapper.cpp
${QtGui_GEN_DIR}/qtoolbarchangeevent_wrapper.cpp
${QtGui_GEN_DIR}/qtouchevent_wrapper.cpp
${QtGui_GEN_DIR}/qtransform_wrapper.cpp
${QtGui_GEN_DIR}/qt_wrapper.cpp
${QtGui_GEN_DIR}/qundocommand_wrapper.cpp
${QtGui_GEN_DIR}/qundogroup_wrapper.cpp
${QtGui_GEN_DIR}/qundostack_wrapper.cpp
${QtGui_GEN_DIR}/qvalidator_wrapper.cpp
${QtGui_GEN_DIR}/qvector2d_wrapper.cpp
${QtGui_GEN_DIR}/qvector3d_wrapper.cpp
${QtGui_GEN_DIR}/qvector4d_wrapper.cpp
${QtGui_GEN_DIR}/qwhatsthisclickedevent_wrapper.cpp
${QtGui_GEN_DIR}/qwheelevent_wrapper.cpp
${QtGui_GEN_DIR}/qwindow_wrapper.cpp
${QtGui_GEN_DIR}/qwindowstatechangeevent_wrapper.cpp
# module is always needed
${QtGui_GEN_DIR}/qtgui_module_wrapper.cpp
)

get_property(QtGui_enabled_features TARGET Qt${QT_MAJOR_VERSION}::Gui
             PROPERTY QT_ENABLED_PUBLIC_FEATURES)

if("xcb" IN_LIST QtGui_enabled_features)
    list(APPEND QtGui_SRC
         ${QtGui_GEN_DIR}/qnativeinterface_qx11application_wrapper.cpp)
endif()

if("wayland" IN_LIST QtGui_enabled_features)
    list(APPEND QtGui_SRC
         ${QtGui_GEN_DIR}/qnativeinterface_qwaylandapplication_wrapper.cpp)
endif()

if(WIN32)
    list(APPEND QtGui_SRC
         ${QtGui_GEN_DIR}/qnativeinterface_qwindowsscreen_wrapper.cpp)
endif()

if("opengl" IN_LIST QtGui_enabled_features OR "opengles2" IN_LIST QtGui_enabled_features
   OR "opengles3" IN_LIST QtGui_enabled_features)
    list(APPEND QtGui_SRC
         ${QtGui_GEN_DIR}/qopenglcontextgroup_wrapper.cpp
         ${QtGui_GEN_DIR}/qopenglextrafunctions_wrapper.cpp
         ${QtGui_GEN_DIR}/qopenglfunctions_wrapper.cpp
         ${QtGui_GEN_DIR}/qopenglcontext_wrapper.cpp
         ${QtGui_GEN_DIR}/qrhigles2initparams_wrapper.cpp
         ${QtGui_GEN_DIR}/qrhigles2nativehandles_wrapper.cpp)
else()
    list(APPEND QtGui_DROPPED_ENTRIES
         QOpenGLContext QOpenGLContextGroup QOpenGLPaintDevice
         QOpenGLExtraFunctions QOpenGLFunctions
         QRhiGles2InitParams QRhiGles2NativeHandles)
endif()

set(QtGui_private_include_dirs
    ${Qt${QT_MAJOR_VERSION}Core_PRIVATE_INCLUDE_DIRS}
    ${Qt${QT_MAJOR_VERSION}Gui_PRIVATE_INCLUDE_DIRS})

if (${CMAKE_SYSTEM_NAME} STREQUAL "Android")
    if (QT_FEATURE_opengles2)
        # add openGL ES 2.0
        find_package(GLESv2 REQUIRED)
    else()
        message(FATAL_ERROR "QtGui requires OpenGL ES 2.0 on Android")
    endif()
endif()

configure_file("${CMAKE_CURRENT_SOURCE_DIR}/QtGui_global.post.h.in"
               "${CMAKE_CURRENT_BINARY_DIR}/QtGui_global.post.h" @ONLY)

set(QtGui_include_dirs
    ${QtCore_GEN_DIR}
)

set(QtGui_deps
    QtCore
)

set(QtGui_libraries
    Qt::Gui
    Qt::GuiPrivate
)

create_pyside_module(NAME QtGui
                     INCLUDE_DIRS QtGui_include_dirs
                     LIBRARIES QtGui_libraries
                     DEPS QtGui_deps
                     TYPESYSTEM_PATH CMAKE_CURRENT_SOURCE_DIR
                     SOURCES QtGui_SRC
                     STATIC_SOURCES QPYTEXTOBJECT_MOC
                     TYPESYSTEM_NAME "${CMAKE_CURRENT_BINARY_DIR}/typesystem_gui.xml"
                     DROPPED_ENTRIES QtGui_DROPPED_ENTRIES
                     ADDITIONAL_INCLUDE_DIRS QtGui_private_include_dirs)

install(FILES ${PYSIDE_PACKAGE_SOURCE_DIR}/qpytextobject.h
              ${PYSIDE_PACKAGE_SOURCE_DIR}/qtguihelper.h
        DESTINATION include/PySide6/QtGui/)
qfp_sbom_add_files(QtGui
    FILES
        ${PYSIDE_PACKAGE_SOURCE_DIR}/qpytextobject.h
        ${PYSIDE_PACKAGE_SOURCE_DIR}/qtguihelper.h
    FILE_TYPE "SOURCE"
    INSTALL_PATH "include/PySide6/QtGui"
)
