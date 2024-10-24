# Copyright (C) 2022 The Qt Company Ltd.
# SPDX-License-Identifier: LicenseRef-Qt-Commercial OR BSD-3-Clause
from __future__ import annotations


import os
import sys

from PySide6.QtCore import QUrl
from PySide6.QtGui import QGuiApplication, QSurfaceFormat
from PySide6.QtQml import QQmlApplicationEngine
from PySide6.QtQuick3D import QQuick3D

# Imports to trigger the resources and registration of QML elements
import resources_rc  # noqa: F401
from examplepoint import ExamplePointGeometry  # noqa: F401
from exampletriangle import ExampleTriangleGeometry  # noqa: F401

if __name__ == "__main__":
    os.environ["QT_QUICK_CONTROLS_STYLE"] = "Basic"
    app = QGuiApplication(sys.argv)

    QSurfaceFormat.setDefaultFormat(QQuick3D.idealSurfaceFormat())

    engine = QQmlApplicationEngine()
    engine.load(QUrl.fromLocalFile(":/main.qml"))
    if not engine.rootObjects():
        sys.exit(-1)

    sys.exit(app.exec())
