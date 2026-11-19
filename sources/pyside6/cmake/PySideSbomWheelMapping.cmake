# Copyright (C) 2026 The Qt Company Ltd.
# SPDX-License-Identifier: BSD-3-Clause

# The following lists mirror the contents of the PySide6_Essentials, PySide6_Addons,
# PySide6_WebEngine and PySide6_Pdf wheels, as defined in build_scripts/wheel_files.py
# They are intentionally distinct from ALL_ESSENTIAL_MODULES / ALL_OPTIONAL_MODULES
# (see PySideHelpers.cmake). Those decide which Qt packages are searched for
# and built. And the smaller "essential" set keeps the required modules minimal for development.
# Whereas these decide how the built modules are split across the per-wheel SBOM documents, so that
# each SBOM matches what actually ships in the corresponding wheel.
#
# Keep these in sync with build_scripts/wheel_files.py. The create_wheels script will check
# these for consistency.
# Order is not relevant.

set(PYSIDE_ESSENTIALS_WHEEL_MODULES
    Concurrent
    Core
    DBus
    Designer
    Gui
    Help
    JsonRpc
    LabsStyleKit
    LanguageServer
    Network
    OpenGL
    OpenGLWidgets
    PrintSupport
    Qml
    QmlFeatures
    Quick
    QuickControls2
    QuickTest
    QuickWidgets
    Sql
    Svg
    SvgWidgets
    Test
    UiTools
    Wayland
    Widgets
    Xml
)

set(PYSIDE_ADDONS_WHEEL_MODULES
    3DAnimation
    3DCore
    3DExtras
    3DInput
    3DLogic
    3DRender
    Asyncio
    AxContainer
    Bluetooth
    CanvasPainter
    Charts
    DataVisualization
    Graphs
    GraphsWidgets
    HttpServer
    Location
    Multimedia
    MultimediaWidgets
    NetworkAuth
    Nfc
    Positioning
    Quick3D
    RemoteObjects
    Scxml
    Sensors
    SerialBus
    SerialPort
    SpatialAudio
    StateMachine
    TextToSpeech
    VirtualKeyboard
    WebChannel
    WebSockets
    WebView
    WebViewQuick
)

set(PYSIDE_WEBENGINE_WHEEL_MODULES
    WebEngineCore
    WebEngineQuick
    WebEngineWidgets
)

set(PYSIDE_PDF_WHEEL_MODULES
    Pdf
    PdfWidgets
)

# The wheels, besides essentials and addons, that get their own SBOM document. Each one needs a
# PYSIDE_<WHEEL>_WHEEL_MODULES list, where <WHEEL> is the upper-case wheel name.
set(PYSIDE_SEPARATE_SBOM_WHEELS
    WebEngine
    Pdf
)

# The Qt module of each Qt tool installed by pyside-tools, in <tool>:<module> form. A tool is
# shipped in the wheel of its module (see the module_Qt<module>() functions in
# build_scripts/wheel_files.py), which decides the pyside-tools SBOM document that lists it.
set(PYSIDE_QT_TOOLS_WHEEL_MODULES
    assistant:Core
    balsam:Quick3D
    balsamui:Quick3D
    designer:Designer
    linguist:Core
    lrelease:Core
    lupdate:Core
    qmlcachegen:Qml
    qmlformat:Qml
    qmlimportscanner:Qml
    qmllint:Qml
    qmlls:Qml
    qmltyperegistrar:Qml
    qsb:Quick3D
    rcc:Core
    svgtoqml:Quick
    uic:Widgets
)
