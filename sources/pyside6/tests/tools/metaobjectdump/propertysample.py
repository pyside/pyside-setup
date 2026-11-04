# Copyright (C) 2026 The Qt Company Ltd.
# SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

from PySide6 import QtCore
from PySide6.QtCore import Property, QObject, Signal
from PySide6.QtQml import QmlElement

QML_IMPORT_NAME = "Properties"
QML_IMPORT_MAJOR_VERSION = 1


@QmlElement
class PropertySample(QObject):
    changed = Signal()

    # Properties declared with decorators
    @Property(str, constant=True)
    def constant_property(self) -> str:
        return ""

    @Property(int, notify=changed)
    def notifying_property(self) -> int:
        return 0

    @Property("QString", notify=changed)
    def decorated_string_type_property(self) -> str:
        return ""

    @QtCore.Property(int, notify=changed)
    def decorated_qualified_property(self) -> int:
        return 0

    def _get_text(self) -> str:
        return ""

    def _set_text(self, value: str) -> None:
        pass

    # Properties declared as class attributes
    positional_property = Property(str, _get_text, _set_text, notify=changed)
    qualified_property = QtCore.Property(str, _get_text, _set_text, notify=changed)
    keyword_property = Property(str, fget=_get_text, fset=_set_text, notify=changed)
    keyword_setter_property = Property(str, _get_text, fset=_set_text, notify=changed)
    none_setter_property = Property(str, _get_text, None, notify=changed)
    getter_lambda_property = Property(str, lambda self: "", notify=changed)
    lambda_property = Property(str, lambda self: "", lambda self, value: None, notify=changed)
    string_type_property = Property("QString", _get_text, constant=True)
