# Copyright (C) 2023 The Qt Company Ltd.
# SPDX-License-Identifier: BSD-3-Clause

set(CMAKE_AUTOMOC ON)

set(QtCore_DROPPED_ENTRIES )

set(QtCore_static_sources
    "${CMAKE_CURRENT_SOURCE_DIR}/glue/qeasingcurve_glue.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/glue/core_snippets.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/glue/qtcorehelper.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/glue/qiopipe.cpp"
    "${PYSIDE_PACKAGE_SOURCE_DIR}/qiopipe.h"
)

if(CMAKE_SYSTEM_NAME STREQUAL "Windows")
    set(SPECIFIC_OS_FILES
      ${QtCore_GEN_DIR}/qwineventnotifier_wrapper.cpp
    )
endif()

set(QtCore_SRC
${QtCore_GEN_DIR}/qabstractanimation_wrapper.cpp
${QtCore_GEN_DIR}/qabstracteventdispatcher_timerinfo_wrapper.cpp
${QtCore_GEN_DIR}/qabstracteventdispatcher_wrapper.cpp
${QtCore_GEN_DIR}/qabstractitemmodel_wrapper.cpp
${QtCore_GEN_DIR}/qabstractlistmodel_wrapper.cpp
${QtCore_GEN_DIR}/qabstractnativeeventfilter_wrapper.cpp
${QtCore_GEN_DIR}/qabstractproxymodel_wrapper.cpp
${QtCore_GEN_DIR}/qabstracttablemodel_wrapper.cpp
${QtCore_GEN_DIR}/qanimationgroup_wrapper.cpp
${QtCore_GEN_DIR}/qbasicmutex_wrapper.cpp
${QtCore_GEN_DIR}/qbasicreadwritelock_wrapper.cpp
${QtCore_GEN_DIR}/qbasictimer_wrapper.cpp
${QtCore_GEN_DIR}/qbitarray_wrapper.cpp
${QtCore_GEN_DIR}/qbuffer_wrapper.cpp
${QtCore_GEN_DIR}/qbytearray_frombase64result_wrapper.cpp
${QtCore_GEN_DIR}/qbytearray_wrapper.cpp
${QtCore_GEN_DIR}/qbytearraymatcher_wrapper.cpp
${QtCore_GEN_DIR}/qcalendar_systemid_wrapper.cpp
${QtCore_GEN_DIR}/qcalendar_wrapper.cpp
${QtCore_GEN_DIR}/qcalendar_yearmonthday_wrapper.cpp
${QtCore_GEN_DIR}/qcborarray_wrapper.cpp
${QtCore_GEN_DIR}/qcborerror_wrapper.cpp
${QtCore_GEN_DIR}/qcbormap_wrapper.cpp
${QtCore_GEN_DIR}/qcborparsererror_wrapper.cpp
${QtCore_GEN_DIR}/qcborstreamreader_wrapper.cpp
${QtCore_GEN_DIR}/qcborstreamwriter_wrapper.cpp
${QtCore_GEN_DIR}/qcborstringresultbytearray_wrapper.cpp
${QtCore_GEN_DIR}/qcborstringresultstring_wrapper.cpp
${QtCore_GEN_DIR}/qcborvalue_wrapper.cpp
${QtCore_GEN_DIR}/qchildevent_wrapper.cpp
${QtCore_GEN_DIR}/qcollator_wrapper.cpp
${QtCore_GEN_DIR}/qcollatorsortkey_wrapper.cpp
${QtCore_GEN_DIR}/qcommandlineoption_wrapper.cpp
${QtCore_GEN_DIR}/qcommandlineparser_wrapper.cpp
${QtCore_GEN_DIR}/qconcatenatetablesproxymodel_wrapper.cpp
${QtCore_GEN_DIR}/qcoreapplication_wrapper.cpp
${QtCore_GEN_DIR}/qcryptographichash_wrapper.cpp
${QtCore_GEN_DIR}/qdatastream_wrapper.cpp
${QtCore_GEN_DIR}/qdate_wrapper.cpp
${QtCore_GEN_DIR}/qdatetime_wrapper.cpp
${QtCore_GEN_DIR}/qdeadlinetimer_wrapper.cpp
${QtCore_GEN_DIR}/qdir_wrapper.cpp
${QtCore_GEN_DIR}/qdiriterator_wrapper.cpp
${QtCore_GEN_DIR}/qdirlisting_wrapper.cpp
${QtCore_GEN_DIR}/qdirlisting_direntry_wrapper.cpp
${QtCore_GEN_DIR}/qdynamicpropertychangeevent_wrapper.cpp
${QtCore_GEN_DIR}/qeasingcurve_wrapper.cpp
${QtCore_GEN_DIR}/qelapsedtimer_wrapper.cpp
${QtCore_GEN_DIR}/qevent_wrapper.cpp
${QtCore_GEN_DIR}/qeventloop_wrapper.cpp
${QtCore_GEN_DIR}/qfactoryinterface_wrapper.cpp
${QtCore_GEN_DIR}/qfile_wrapper.cpp
${QtCore_GEN_DIR}/qfiledevice_wrapper.cpp
${QtCore_GEN_DIR}/qfileinfo_wrapper.cpp
${QtCore_GEN_DIR}/qfileselector_wrapper.cpp
${QtCore_GEN_DIR}/qfilesystemwatcher_wrapper.cpp
${QtCore_GEN_DIR}/qfutureinterfacebase_wrapper.cpp
${QtCore_GEN_DIR}/qgenericargument_wrapper.cpp
${QtCore_GEN_DIR}/qrangemodel_wrapper.cpp
${QtCore_GEN_DIR}/qgenericreturnargument_wrapper.cpp
${QtCore_GEN_DIR}/qhashseed_wrapper.cpp
${QtCore_GEN_DIR}/qidentityproxymodel_wrapper.cpp
${QtCore_GEN_DIR}/qiodevice_wrapper.cpp
${QtCore_GEN_DIR}/qiodevicebase_wrapper.cpp
${QtCore_GEN_DIR}/qitemselection_wrapper.cpp
${QtCore_GEN_DIR}/qitemselectionmodel_wrapper.cpp
${QtCore_GEN_DIR}/qitemselectionrange_wrapper.cpp
${QtCore_GEN_DIR}/qjsonarray_wrapper.cpp
${QtCore_GEN_DIR}/qjsondocument_wrapper.cpp
${QtCore_GEN_DIR}/qjsonparseerror_wrapper.cpp
${QtCore_GEN_DIR}/qjsonvalue_wrapper.cpp
${QtCore_GEN_DIR}/qkeycombination_wrapper.cpp
${QtCore_GEN_DIR}/qlibrary_wrapper.cpp
${QtCore_GEN_DIR}/qlibraryinfo_wrapper.cpp
${QtCore_GEN_DIR}/qline_wrapper.cpp
${QtCore_GEN_DIR}/qlinef_wrapper.cpp
${QtCore_GEN_DIR}/qlocale_wrapper.cpp
${QtCore_GEN_DIR}/qlockfile_wrapper.cpp
${QtCore_GEN_DIR}/qloggingcategory_wrapper.cpp
${QtCore_GEN_DIR}/qmargins_wrapper.cpp
${QtCore_GEN_DIR}/qmarginsf_wrapper.cpp
${QtCore_GEN_DIR}/qmessageauthenticationcode_wrapper.cpp
${QtCore_GEN_DIR}/qmessagelogcontext_wrapper.cpp
${QtCore_GEN_DIR}/qmetaclassinfo_wrapper.cpp
${QtCore_GEN_DIR}/qmetaenum_wrapper.cpp
${QtCore_GEN_DIR}/qmetamethod_wrapper.cpp
${QtCore_GEN_DIR}/qmetaobject_connection_wrapper.cpp
${QtCore_GEN_DIR}/qmetaobject_wrapper.cpp
${QtCore_GEN_DIR}/qmetaproperty_wrapper.cpp
${QtCore_GEN_DIR}/qmetatype_wrapper.cpp
${QtCore_GEN_DIR}/qmimedata_wrapper.cpp
${QtCore_GEN_DIR}/qmimedatabase_wrapper.cpp
${QtCore_GEN_DIR}/qmimetype_wrapper.cpp
${QtCore_GEN_DIR}/qmodelindex_wrapper.cpp
${QtCore_GEN_DIR}/qmodelroledata_wrapper.cpp
${QtCore_GEN_DIR}/qmodelroledataspan_wrapper.cpp
${QtCore_GEN_DIR}/qmutex_wrapper.cpp
${QtCore_GEN_DIR}/qnativeipckey_wrapper.cpp
${QtCore_GEN_DIR}/qobject_wrapper.cpp
${QtCore_GEN_DIR}/qoperatingsystemversion_wrapper.cpp
${QtCore_GEN_DIR}/qoperatingsystemversionbase_wrapper.cpp
${QtCore_GEN_DIR}/qparallelanimationgroup_wrapper.cpp
${QtCore_GEN_DIR}/qpauseanimation_wrapper.cpp
${QtCore_GEN_DIR}/qpersistentmodelindex_wrapper.cpp
${QtCore_GEN_DIR}/qpluginloader_wrapper.cpp
${QtCore_GEN_DIR}/qpoint_wrapper.cpp
${QtCore_GEN_DIR}/qpointf_wrapper.cpp
${QtCore_GEN_DIR}/qpropertyanimation_wrapper.cpp
${QtCore_GEN_DIR}/qrandomgenerator64_wrapper.cpp
${QtCore_GEN_DIR}/qrandomgenerator_wrapper.cpp
${QtCore_GEN_DIR}/qreadlocker_wrapper.cpp
${QtCore_GEN_DIR}/qreadwritelock_wrapper.cpp
${QtCore_GEN_DIR}/qrect_wrapper.cpp
${QtCore_GEN_DIR}/qrectf_wrapper.cpp
${QtCore_GEN_DIR}/qrecursivemutex_wrapper.cpp
${QtCore_GEN_DIR}/qregularexpression_wrapper.cpp
${QtCore_GEN_DIR}/qregularexpressionmatch_wrapper.cpp
${QtCore_GEN_DIR}/qregularexpressionmatchiterator_wrapper.cpp
${QtCore_GEN_DIR}/qresource_wrapper.cpp
${QtCore_GEN_DIR}/qrunnable_wrapper.cpp
${QtCore_GEN_DIR}/qsavefile_wrapper.cpp
${QtCore_GEN_DIR}/qsemaphore_wrapper.cpp
${QtCore_GEN_DIR}/qsemaphorereleaser_wrapper.cpp
${QtCore_GEN_DIR}/qsequentialanimationgroup_wrapper.cpp
${QtCore_GEN_DIR}/qsettings_wrapper.cpp
${QtCore_GEN_DIR}/qsignalblocker_wrapper.cpp
${QtCore_GEN_DIR}/qsignalmapper_wrapper.cpp
${QtCore_GEN_DIR}/qsize_wrapper.cpp
${QtCore_GEN_DIR}/qsizef_wrapper.cpp
${QtCore_GEN_DIR}/qsocketdescriptor_wrapper.cpp
${QtCore_GEN_DIR}/qsocketnotifier_wrapper.cpp
${QtCore_GEN_DIR}/qsortfilterproxymodel_wrapper.cpp
${QtCore_GEN_DIR}/qstandardpaths_wrapper.cpp
${QtCore_GEN_DIR}/qstorageinfo_wrapper.cpp
${QtCore_GEN_DIR}/qstringconverter_wrapper.cpp
${QtCore_GEN_DIR}/qstringconverterbase_state_wrapper.cpp
${QtCore_GEN_DIR}/qstringconverterbase_wrapper.cpp
${QtCore_GEN_DIR}/qstringdecoder_wrapper.cpp
${QtCore_GEN_DIR}/qstringencoder_wrapper.cpp
${QtCore_GEN_DIR}/qstringlistmodel_wrapper.cpp
${QtCore_GEN_DIR}/qsysinfo_wrapper.cpp
${QtCore_GEN_DIR}/qsystemsemaphore_wrapper.cpp
${QtCore_GEN_DIR}/qt_wrapper.cpp
${QtCore_GEN_DIR}/qtcorehelper_qdirlistingiterator_wrapper.cpp
${QtCore_GEN_DIR}/qtcorehelper_qgenericargumentholder_wrapper.cpp
${QtCore_GEN_DIR}/qtcorehelper_qgenericreturnargumentholder_wrapper.cpp
${QtCore_GEN_DIR}/qtcorehelper_qiopipe_wrapper.cpp
${QtCore_GEN_DIR}/qtcorehelper_qmutexlocker_wrapper.cpp
${QtCore_GEN_DIR}/qtemporarydir_wrapper.cpp
${QtCore_GEN_DIR}/qtemporaryfile_wrapper.cpp
${QtCore_GEN_DIR}/qtextboundaryfinder_wrapper.cpp
${QtCore_GEN_DIR}/qtextstream_wrapper.cpp
${QtCore_GEN_DIR}/qtextstreammanipulator_wrapper.cpp
${QtCore_GEN_DIR}/qthread_wrapper.cpp
${QtCore_GEN_DIR}/qthreadpool_wrapper.cpp
${QtCore_GEN_DIR}/qtime_wrapper.cpp
${QtCore_GEN_DIR}/qtimeline_wrapper.cpp
${QtCore_GEN_DIR}/qtimer_wrapper.cpp
${QtCore_GEN_DIR}/qtimerevent_wrapper.cpp
${QtCore_GEN_DIR}/qtimezone_offsetdata_wrapper.cpp
${QtCore_GEN_DIR}/qtimezone_wrapper.cpp
${QtCore_GEN_DIR}/qtranslator_wrapper.cpp
${QtCore_GEN_DIR}/qtransposeproxymodel_wrapper.cpp
${QtCore_GEN_DIR}/qurl_wrapper.cpp
${QtCore_GEN_DIR}/qurlquery_wrapper.cpp
${QtCore_GEN_DIR}/quuid_wrapper.cpp
${QtCore_GEN_DIR}/qvariantanimation_wrapper.cpp
${QtCore_GEN_DIR}/qversionnumber_wrapper.cpp
${QtCore_GEN_DIR}/qwaitcondition_wrapper.cpp
${QtCore_GEN_DIR}/qwritelocker_wrapper.cpp
${QtCore_GEN_DIR}/qxmlstreamattribute_wrapper.cpp
${QtCore_GEN_DIR}/qxmlstreamattributes_wrapper.cpp
${QtCore_GEN_DIR}/qxmlstreamentitydeclaration_wrapper.cpp
${QtCore_GEN_DIR}/qxmlstreamentityresolver_wrapper.cpp
${QtCore_GEN_DIR}/qxmlstreamnamespacedeclaration_wrapper.cpp
${QtCore_GEN_DIR}/qxmlstreamnotationdeclaration_wrapper.cpp
${QtCore_GEN_DIR}/qxmlstreamreader_wrapper.cpp
${QtCore_GEN_DIR}/qxmlstreamwriter_wrapper.cpp
${QtCore_GEN_DIR}/qmessagelogger_wrapper.cpp

${SPECIFIC_OS_FILES}
# module is always needed
${QtCore_GEN_DIR}/qtcore_module_wrapper.cpp
)

get_property(QtCore_disabled_features TARGET Qt${QT_MAJOR_VERSION}::Core
             PROPERTY QT_DISABLED_PUBLIC_FEATURES)

if("permissions" IN_LIST QtCore_disabled_features)
    list(APPEND QtCore_DROPPED_ENTRIES QPermission
         QBluetoothPermission QCalendarPermission QCameraPermission
         QContactsPermission QLocationPermission QMicrophonePermission)
    message(STATUS "Qt${QT_MAJOR_VERSION}Core: Dropping Permissions classes")
else()
    list(APPEND QtCore_SRC
         ${QtCore_GEN_DIR}/qbluetoothpermission_wrapper.cpp
         ${QtCore_GEN_DIR}/qcalendarpermission_wrapper.cpp
         ${QtCore_GEN_DIR}/qcamerapermission_wrapper.cpp
         ${QtCore_GEN_DIR}/qcontactspermission_wrapper.cpp
         ${QtCore_GEN_DIR}/qlocationpermission_wrapper.cpp
         ${QtCore_GEN_DIR}/qmicrophonepermission_wrapper.cpp
         ${QtCore_GEN_DIR}/qpermission_wrapper.cpp)
endif()

if("sharedmemory" IN_LIST QtCore_disabled_features)
    list(APPEND QtCore_DROPPED_ENTRIES QSharedMemory)
    message(STATUS "Qt${QT_MAJOR_VERSION}Core: Dropping QSharedMemory")
else()
    list(APPEND QtCore_SRC ${QtCore_GEN_DIR}/qsharedmemory_wrapper.cpp)
endif()

if("process" IN_LIST QtCore_disabled_features)
    list(APPEND QtCore_DROPPED_ENTRIES QProcess)
    message(STATUS "Qt${QT_MAJOR_VERSION}Core: Dropping QProcess")
else()
    list(APPEND QtCore_SRC ${QtCore_GEN_DIR}/qprocess_wrapper.cpp)
    if(NOT CMAKE_SYSTEM_NAME STREQUAL "Windows")
        list(APPEND QtCore_SRC ${QtCore_GEN_DIR}/qprocess_unixprocessparameters_wrapper.cpp)
    endif()
endif()

if("processenvironment" IN_LIST QtCore_disabled_features)
    list(APPEND QtCore_DROPPED_ENTRIES QProcessEnvironment)
    message(STATUS "Qt${QT_MAJOR_VERSION}Core: Dropping QProcessEnvironment")
else()
    list(APPEND QtCore_SRC ${QtCore_GEN_DIR}/qprocessenvironment_wrapper.cpp)
endif()

configure_file("${CMAKE_CURRENT_SOURCE_DIR}/QtCore_global.post.h.in"
               "${CMAKE_CURRENT_BINARY_DIR}/QtCore_global.post.h" @ONLY)

set(QtCore_glue_sources
    "${CMAKE_CURRENT_SOURCE_DIR}/glue/qeasingcurve_glue.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/glue/qeasingcurve_glue.h"
)

set(QtCore_libraries
    Qt::Core
    Qt::CorePrivate
)

create_pyside_module(NAME QtCore
                     LIBRARIES QtCore_libraries
                     TYPESYSTEM_PATH CMAKE_CURRENT_SOURCE_DIR
                     SOURCES QtCore_SRC
                     STATIC_SOURCES QtCore_static_sources
                     TYPESYSTEM_NAME "${CMAKE_CURRENT_BINARY_DIR}/typesystem_core.xml"
                     GLUE_SOURCES QtCore_glue_sources
                     DROPPED_ENTRIES QtCore_DROPPED_ENTRIES
                     )

# Note: The permission API for Apple platforms only works if the Python application is packaged
# with pyside6-deploy (uses Nuitka). Read "Notes for Developers" in Qt for Python documentation
# for more information
#
# For Apple platforms, the Qt permission API implementations are in small static libraries.
# In Qt C++, the application is linked directly to these static libraries during the build when
# linking to the QtCore module as a post processing CMake step.
# Being static plugins makes it difficult to add these plugins during Nuitka packaging step.
# Thus, we link the static plugins to QtCore.abi3.so. However, to request the permissions
# it is still required to have the necessary Information Property keys eg: NSCameraUsageDescription
# in the Info.plist of the application bundle which Nuitka creates.
if (APPLE)
    set(permissions Camera Microphone Bluetooth Contacts Calendar)
    foreach(permission IN LISTS permissions)
        set(permission_plugin_name "QDarwin${permission}PermissionPlugin")
        set(permission_plugin "${QT_CMAKE_EXPORT_NAMESPACE}::${permission_plugin_name}")
        # Setting this property is necessary for Camera and Microphone. Otherwise it won't append
        # the linker flags like -Wl,-u,_QDarwinMicrophonePermissionRequest which are required to
        # link to qdarwinpermissionplugin_microphone_request.mm.o and find symbols like
        # QDarwinMicrosphonePermissionHandler which handles requesting the actual permission
        set_target_properties(QtCore PROPERTIES "_qt_has_${permission_plugin_name}_usage_description" TRUE)
        # importing the plugin
        qt6_import_plugins(QtCore INCLUDE ${permission_plugin})
    endforeach()
endif()

install(FILES ${PYSIDE_PACKAGE_SOURCE_DIR}/qtcorehelper.h ${PYSIDE_PACKAGE_SOURCE_DIR}/qiopipe.h
        DESTINATION include/PySide6/QtCore/)
qfp_sbom_add_files(QtCore
    FILES
        ${PYSIDE_PACKAGE_SOURCE_DIR}/qtcorehelper.h
        ${PYSIDE_PACKAGE_SOURCE_DIR}/qiopipe.h
    FILE_TYPE "SOURCE"
    INSTALL_PATH "include/PySide6/QtCore"
)
