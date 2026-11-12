# Copyright (C) 2022 The Qt Company Ltd.
# SPDX-License-Identifier: LicenseRef-Qt-Commercial OR BSD-3-Clause
from __future__ import annotations

"""PySide6 WebEngine QtQuick 2 Example"""

import sys
from argparse import ArgumentParser, RawTextHelpFormatter
from pathlib import Path

from PySide6.QtCore import (QCoreApplication, QFileInfo, QLoggingCategory,
                            QObject, QUrl, Slot)
from PySide6.QtQml import QQmlApplicationEngine, QmlElement, QmlSingleton
from PySide6.QtGui import QGuiApplication
from PySide6.QtWebEngineQuick import QtWebEngineQuick

import rc_resources  # noqa: F401


# To be used on the @QmlElement decorator
# (QML_IMPORT_MINOR_VERSION is optional)
QML_IMPORT_NAME = "BrowserUtils"
QML_IMPORT_MAJOR_VERSION = 1


DEFAULT_URL = "chrome://qt?example=Quick Nano Browser"


def url_from_user_input(user_input):
    file_info = QFileInfo(user_input)
    if file_info.exists():
        return QUrl.fromLocalFile(file_info.absoluteFilePath())
    return QUrl.fromUserInput(user_input)


@QmlElement
@QmlSingleton
class Utils(QObject):

    @Slot(str, result=QUrl)
    def fromUserInput(self, user_input):
        return url_from_user_input(user_input)


if __name__ == '__main__':
    QCoreApplication.setApplicationName("Quick Nano Browser")
    QCoreApplication.setOrganizationName("QtProject")

    QLoggingCategory.setFilterRules("qt.webenginecontext.debug=true")

    QtWebEngineQuick.initialize()

    argument_parser = ArgumentParser(description="Quick Nano Browser",
                                     formatter_class=RawTextHelpFormatter)
    argument_parser.add_argument("--single-process", "-s", action="store_true",
                                 help="Run in single process mode (trouble shooting)")
    argument_parser.add_argument("url", help="The URL to open",
                                 nargs='?', type=str)
    options = argument_parser.parse_args()

    url = url_from_user_input(options.url) if options.url else QUrl(DEFAULT_URL)

    app_args = sys.argv
    if options.single_process:
        app_args.extend(["--webEngineArgs", "--single-process"])
    app = QGuiApplication(app_args)
    engine = QQmlApplicationEngine()
    engine.setInitialProperties({"startupUrl": url})
    engine.addImportPath(Path(__file__).parent)
    engine.loadFromModule("BrowserUtils", "ApplicationRoot")
    if not engine.rootObjects():
        sys.exit(-1)

    exit_code = app.exec()
    del engine
    sys.exit(exit_code)
