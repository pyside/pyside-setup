# Copyright (C) 2022 The Qt Company Ltd.
# SPDX-License-Identifier: LicenseRef-Qt-Commercial OR LGPL-3.0-only OR GPL-2.0-only OR GPL-3.0-only
from __future__ import annotations

# flake8: noqa E:203

"""
mapping.py

This module has the mapping from the pyside C-modules view of signatures
to the Python representation.

The PySide modules are not loaded in advance, but only after they appear
in sys.modules. This minimizes the loading overhead.
"""

import os
import struct
import sys
import typing

from pathlib import Path
from typing import TypeVar, Generic
from _imp import is_builtin


class ellipsis(object):
    def __repr__(self):
        return "..."


ellipsis = ellipsis()
Point = typing.Tuple[int, int]
Variant = typing.Any
QImageCleanupFunction = typing.Callable[..., typing.Any]

# unfortunately, typing.Optional[t] expands to typing.Union[t, NoneType]
# Until we can force it to create Optional[t] again, we use this.
NoneType = type(None)

# PYSIDE-2517: findChild/findChildren type hints:
# Placeholder so it does not trigger an UNDEFINED error while building.
# Later it will be bound to a QObject, within the QtCore types extensions
PlaceHolderType = TypeVar("PlaceHolderType")

_S = TypeVar("_S")

MultiMap = typing.DefaultDict[str, typing.List[str]]

# ulong_max is only 32 bit on windows.
ulong_max = 2 * sys.maxsize + 1 if len(struct.pack("L", 1)) != 4 else 0xffffffff
ushort_max = 0xffff

GL_COLOR_BUFFER_BIT = 0x00004000
GL_NEAREST = 0x2600

WId = int

# from 5.9
GL_TEXTURE_2D = 0x0DE1
GL_RGBA = 0x1908


class _NotCalled(str):
    """
    Wrap some text with semantics

    This class is wrapped around text in order to avoid calling it.
    There are three reasons for this:

      - some instances cannot be created since they are abstract,
      - some can only be created after qApp was created,
      - some have an ugly __repr__ with angle brackets in it.

    By using derived classes, good looking instances can be created
    which can be used to generate source code or .pyi files. When the
    real object is needed, the wrapper can simply be called.
    """
    def __repr__(self):
        return f"{type(self).__name__}({self})"

    def __call__(self):
        from shibokensupport.signature.mapping import __dict__ as namespace
        text = self if self.endswith(")") else self + "()"
        return eval(text, namespace)


USE_PEP563 = False
# Note: we cannot know if this feature has been imported.
# Otherwise it would be "sys.version_info[:2] >= (3, 7)".
# We *can* eventually inspect sys.modules and look if
# the calling module has this future statement set,
# but should we do that?


# Some types are abstract. They just show their name.
class Virtual(_NotCalled):
    pass


# Other types I simply could not find.
class Missing(_NotCalled):
    # The string must be quoted, because the object does not exist.
    def __repr__(self):
        if USE_PEP563:
            return _NotCalled.__repr__(self)
        return f'{type(self).__name__}("{self}")'


class Invalid(_NotCalled):
    pass


# Helper types
class Default(_NotCalled):
    pass


class Instance(_NotCalled):
    pass


# Parameterized primitive variables
class _Parameterized(object):
    def __init__(self, type):
        self.type = type
        self.__name__ = self.__class__.__name__

    def __repr__(self):
        return f"{type(self).__name__}({self.type.__name__})"


# Mark the primitive variables to be moved into the result.
class ResultVariable(_Parameterized):
    pass


# Mark the primitive variables to become Sequence, Iterable or List
# (decided in the parser).
class ArrayLikeVariable(_Parameterized):
    pass


StringList = ArrayLikeVariable(str)


class Reloader(object):
    """
    Reloder class

    This is a singleton class which provides the update function for the
    shiboken and PySide classes.
    """
    def __init__(self):
        self.sys_module_count = 0

    @staticmethod
    def module_valid(mod):
        if getattr(mod, "__file__", None) and not Path(mod.__file__).is_dir():
            ending = Path(mod.__file__).suffix
            return ending not in (".py", ".pyc", ".pyo", ".pyi")
        return bool(hasattr(mod, "__name__") and is_builtin(mod.__name__))

    def update(self):
        """
        'update' imports all binary modules which are already in sys.modules.
        The reason is to follow all user imports without introducing new ones.
        This function is called by pyside_type_init to adapt imports
        when the number of imported modules has changed.
        """
        if self.sys_module_count == len(sys.modules):
            return
        self.sys_module_count = len(sys.modules)
        g = globals()
        # PYSIDE-1009: Try to recognize unknown modules in errorhandler.py
        candidates = list(mod_name for mod_name in sys.modules.copy()
                          if self.module_valid(sys.modules[mod_name]))
        for mod_name in candidates:
            # 'top' is PySide6 when we do 'import PySide.QtCore'
            # or Shiboken if we do 'import Shiboken'.
            # Convince yourself that these two lines below have the same
            # global effect as "import Shiboken" or "import PySide6.QtCore".
            top = __import__(mod_name)
            g[top.__name__] = top
            proc_name = "init_" + mod_name.replace(".", "_")
            if proc_name in g:
                # Modules are in place, we can update the type_map.
                g.update(g.pop(proc_name)())


def check_module(mod):
    # During a build, there exist the modules already as directories,
    # although the '*.so' was not yet created. This causes a problem
    # in Python 3, because it accepts folders as namespace modules
    # without enforcing an '__init__.py'.
    if not Reloader.module_valid(mod):
        mod_name = mod.__name__
        raise ImportError(f"Module '{mod_name}' is not a binary module!")


update_mapping = Reloader().update
type_map = {}
namespace = globals()  # our module's __dict__

type_map.update({
    "...": ellipsis,
    "Any": typing.Any,
    "bool": bool,
    "char": int,
    "double": float,
    "float": float,
    "int": int,
    "List": ArrayLikeVariable,
    "Optional": typing.Optional,
    "Iterable": typing.Iterable,
    "long": int,
    "long long": int,
    "nullptr": None,
    "PyCallable": typing.Callable[..., typing.Any],
    "PyObject": object,
    "PyObject*": object,
    "PyArrayObject": ArrayLikeVariable(typing.Any),  # numpy
    "PyPathLike": typing.Union[str, bytes, os.PathLike[str]],
    "PySequence": typing.Iterable,  # important for numpy
    "PyTypeObject": type,
    "QChar": str,
    "QHash": typing.Dict,
    "qint16": int,
    "qint32": int,
    "qint64": int,
    "qint8": int,
    "int16_t": int,
    "int32_t": int,
    "int64_t": int,
    "int8_t": int,
    "intptr_t": int,
    "uintptr_t": int,
    "qintptr": int,
    "qsizetype": int,
    "QFunctionPointer": int,
    "QList": ArrayLikeVariable,
    "qlonglong": int,
    "QMap": typing.Dict,
    "QMultiHash": typing.Dict,
    "QMultiMap": typing.Dict,
    "QPair": typing.Tuple,
    "qptrdiff": int,
    "qreal": float,
    "QSet": typing.Set,
    "QString": str,
    "QLatin1String": str,
    "QStringView": str,
    "QStringList": StringList,
    "quint16": int,
    "quint32": int,
    "quint32": int,
    "quint64": int,
    "quint8": int,
    "uint16_t": int,
    "uint32_t": int,
    "uint64_t": int,
    "uint8_t": int,
    "Union": typing.Union,
    "quintptr": int,
    "qulonglong": int,
    "QVariant": Variant,
    "QVector": typing.List,
    "QSharedPointer": typing.Tuple,
    "real": float,
    "short": int,
    "signed char": int,
    "signed long": int,
    "std.list": typing.List,
    "std.map": typing.Dict,
    "std.nullptr_t": NoneType,
    "std.pair": typing.Tuple,
    "std.string": str,
    "std.wstring": str,
    "std.vector": typing.List,
    "str": str,
    "true": True,
    "Tuple": typing.Tuple,
    "uchar": int,
    "uchar*": str,
    "uint": int,
    "ulong": int,
    "ULONG_MAX": ulong_max,
    "UINT64_MAX": 0xffffffff,
    "unsigned char": int,  # 5.9
    "unsigned char*": str,
    "unsigned int": int,
    "unsigned long int": int,  # 5.6, RHEL 6.6
    "unsigned long long": int,
    "unsigned long": int,
    "unsigned short int": int,  # 5.6, RHEL 6.6
    "unsigned short": int,
    "ushort": int,
    "void": int,  # be more specific?
    "WId": WId,
    "zero(bytes)": b"",
    "zero(Char)": 0,
    "zero(float)": 0,
    "zero(int)": 0,
    "zero(object)": None,
    "zero(str)": "",
    "zero(typing.Any)": None,
    "zero(Any)": None,
    # This can be refined by importing numpy.typing optionally, but better than nothing.
    "numpy.ndarray": typing.List[typing.Any],
    "std.array[int, 4]": typing.List[int],
    "std.array[float, 4]": typing.List[float],
})

type_map.update({
    # Handling variables declared as array:
    "array double*"         : ArrayLikeVariable(float),
    "array float*"          : ArrayLikeVariable(float),
    "array GLint*"          : ArrayLikeVariable(int),
    "array GLuint*"         : ArrayLikeVariable(int),
    "array int*"            : ArrayLikeVariable(int),
    "array long long*"      : ArrayLikeVariable(int),
    "array long*"           : ArrayLikeVariable(int),
    "array short*"          : ArrayLikeVariable(int),
    "array signed char*"    : typing.Union[bytes, bytearray, memoryview],
    "array unsigned char*"  : typing.Union[bytes, bytearray, memoryview],
    "array unsigned int*"   : ArrayLikeVariable(int),
    "array unsigned short*" : ArrayLikeVariable(int),
    # PYSIDE-1646: New macOS primitive types
    "array int8_t*"         : ArrayLikeVariable(int),
    "array uint8_t*"        : ArrayLikeVariable(int),
    "array int16_t*"        : ArrayLikeVariable(int),
    "array uint16_t*"       : ArrayLikeVariable(int),
    "array int32_t*"        : ArrayLikeVariable(int),
    "array uint32_t*"       : ArrayLikeVariable(int),
    "array intptr_t*"       : ArrayLikeVariable(int),
})

type_map.update({
    # Special cases:
    "char*"         : typing.Union[bytes, bytearray, memoryview],
    "QChar*"        : typing.Union[bytes, bytearray, memoryview],
    "quint32*"      : int,        # only for QRandomGenerator
    "quint8*"       : bytearray,  # only for QCborStreamReader and QCborValue
    "uchar*"        : typing.Union[bytes, bytearray, memoryview],
    "unsigned char*": typing.Union[bytes, bytearray, memoryview],
})

type_map.update({
    # Handling variables that are returned, eventually as Tuples:
    "PySide6.QtQml.atomic[bool]": ResultVariable(bool),  # QmlIncubationController::incubateWhile()
    "bool*"         : ResultVariable(bool),
    "float*"        : ResultVariable(float),
    "int*"          : ResultVariable(int),
    "long long*"    : ResultVariable(int),
    "long*"         : ResultVariable(int),
    "PStr*"         : ResultVariable(str),  # module sample
    "qint32*"       : ResultVariable(int),
    "qint64*"       : ResultVariable(int),
    "qreal*"        : ResultVariable(float),
    "qsizetype*"    : ResultVariable(int),
    "QString*"      : ResultVariable(str),
    "qintptr*"      : ResultVariable(int),
    "quintptr*"     : ResultVariable(int),
    "quint16*"      : ResultVariable(int),
    "uint*"         : ResultVariable(int),
    "unsigned int*" : ResultVariable(int),
    "QStringList*"  : ResultVariable(StringList),
})


type_map.update({
    # Hack, until improving the parser:
    "[typing.Any]"  : [typing.Any],
    "[typing.Any,typing.Any]"  : [typing.Any, typing.Any],
    "None" : None,
})


# PYSIDE-1328: We need to handle "self" explicitly.
type_map.update({
    "self" : "self",
    "cls"  : "cls",
})

# PYSIDE-1538: We need to treat "std::optional" accordingly.
type_map.update({
    "std.optional": typing.Optional,
    })


# The Shiboken Part
def init_Shiboken():
    type_map.update({
        "PyType": type,
        "shiboken6.bool": bool,
        "size_t": int,
    })
    return locals()


def init_minimal():
    type_map.update({
        "MinBool": bool,
    })
    return locals()


def init_sample():
    import datetime
    type_map.update({
        "char": int,
        "char**": typing.List[str],
        "const char*": str,
        "Complex": complex,
        "double": float,
        "ByteArray&": typing.Union[bytes, bytearray, memoryview],
        "Foo.HANDLE": int,
        "HANDLE": int,
        "Null": None,
        "ObjectType.Identifier": Missing("sample.ObjectType.Identifier"),
        "OddBool": bool,
        "PStr": str,
        "PyDate": datetime.date,
        "PyBuffer": typing.Union[bytes, bytearray, memoryview],
        "sample.bool": bool,
        "sample.char": int,
        "sample.double": float,
        "sample.int": int,
        "sample.ObjectType": object,
        "sample.OddBool": bool,
        "sample.Photon.TemplateBase[Photon.DuplicatorType]": sample.Photon.ValueDuplicator,
        "sample.Photon.TemplateBase[Photon.IdentityType]": sample.Photon.ValueIdentity,
        "sample.Point": Point,
        "sample.PStr": str,
        "SampleNamespace.InValue.ZeroIn": 0,
        "sample.unsigned char": int,
        "std.size_t": int,
        "std.string": str,
        "ZeroIn": 0,
        'Str("<unk")': "<unk",
        'Str("<unknown>")': "<unknown>",
        'Str("nown>")': "nown>",
    })
    return locals()


def init_other():
    import numbers
    type_map.update({
        "other.ExtendsNoImplicitConversion": Missing("other.ExtendsNoImplicitConversion"),
        "other.Number": numbers.Number,
    })
    return locals()


def init_smart():
    # This missing type should be defined in module smart. We cannot set it to Missing()
    # because it is a container type. Therefore, we supply a surrogate:
    global SharedPtr

    class SharedPtr(Generic[_S]):
        __module__ = "smart"
    smart.SharedPtr = SharedPtr
    type_map.update({
        "smart.Smart.Integer2": int,
    })
    return locals()


# The PySide Part
def init_PySide6_QtCore():
    from PySide6.QtCore import Qt, QUrl, QDir, QKeyCombination, QObject
    from PySide6.QtCore import QRect, QRectF, QSize, QPoint, QLocale, QByteArray
    from PySide6.QtCore import QMarginsF  # 5.9
    from PySide6.QtCore import SignalInstance
    try:
        # seems to be not generated by 5.9 ATM.
        from PySide6.QtCore import Connection
    except ImportError:
        pass

    type_map.update({
        "' '": " ",
        "'%'": "%",
        "'g'": "g",
        "4294967295UL": 4294967295,  # 5.6, RHEL 6.6
        "CheckIndexOption.NoOption": Instance(
            "PySide6.QtCore.QAbstractItemModel.CheckIndexOptions.NoOption"),  # 5.11
        "DescriptorType(-1)": int,  # Native handle of QSocketDescriptor
        "false": False,
        "list of QAbstractAnimation": typing.List[PySide6.QtCore.QAbstractAnimation],
        "long long": int,
        "size_t": int,
        "NULL": None,  # 5.6, MSVC
        "nullptr": None,  # 5.9
        # PYSIDE-2517: findChild/findChildren type hints:
        "PlaceHolderType": typing.TypeVar("PlaceHolderType", bound=PySide6.QtCore.QObject),
        "PyBuffer": typing.Union[bytes, bytearray, memoryview],
        "PyByteArray": bytearray,
        "PyBytes": typing.Union[bytes, bytearray, memoryview],
        "PyTuple": typing.Tuple,
        "QDeadlineTimer.Forever": PySide6.QtCore.QDeadlineTimer.ForeverConstant.Forever,
        "QDeadlineTimer(QDeadlineTimer.Forever)": Instance("PySide6.QtCore.QDeadlineTimer"),
        "PySide6.QtCore.QUrl.ComponentFormattingOptions":
            PySide6.QtCore.QUrl.ComponentFormattingOption,  # mismatch option/enum, why???
        "PyUnicode": typing.Text,
        "QByteArrayView": QByteArray,
        "Q_NULLPTR": None,
        "QCalendar.Unspecified": PySide6.QtCore.QCalendar.Unspecified,
        "QCborTag(-1)": ulong_max,
        "QDir.Filters(AllEntries | NoDotAndDotDot)": Instance(
            "QDir.Filters(QDir.AllEntries | QDir.NoDotAndDotDot)"),
        "QDir.SortFlags(Name | IgnoreCase)": Instance(
            "QDir.SortFlags(QDir.Name | QDir.IgnoreCase)"),
        "QEvent.Type.None": None,
        "QGenericArgument((0))": ellipsis,  # 5.6, RHEL 6.6. Is that ok?
        "QGenericArgument()": ellipsis,
        "QGenericArgument(0)": ellipsis,
        "QGenericArgument(NULL)": ellipsis,  # 5.6, MSVC
        "QGenericArgument(nullptr)": ellipsis,  # 5.10
        "QGenericArgument(Q_NULLPTR)": ellipsis,
        "QJsonObject": typing.Dict[str, PySide6.QtCore.QJsonValue],
        "QModelIndex()": Invalid("PySide6.QtCore.QModelIndex"),  # repr is btw. very wrong, fix it?!
        "QModelIndexList": typing.List[PySide6.QtCore.QModelIndex],
        "PySideSignalInstance": SignalInstance,
        "QString()": "",
        "Flag.Default": Instance("PySide6.QtCore.QStringConverterBase.Flags"),
        "QStringList()": [],
        "QStringRef": str,
        "QStringRef": str,
        "Qt.HANDLE": int,  # be more explicit with some constants?
        "QUrl.FormattingOptions(PrettyDecoded)": Instance(
            "QUrl.FormattingOptions(QUrl.PrettyDecoded)"),
        "QVariant()": Invalid(Variant),
        "QVariant.Type": type,  # not so sure here...
        "QVariantMap": typing.Dict[str, Variant],
        "std.chrono.seconds{5}" : ellipsis,
    })
    try:
        type_map.update({
            "PySide6.QtCore.QMetaObject.Connection": PySide6.QtCore.Connection,  # wrong!
        })
    except AttributeError:
        # this does not exist on 5.9 ATM.
        pass

    # special case - char* can either be 'bytes' or 'str'. The default is 'bytes'.
    # Here we manually set it to map to 'str'.
    type_map.update({("PySide6.QtCore.QObject.setProperty", "char*"): str})
    type_map.update({("PySide6.QtCore.QObject.property", "char*"): str})

    return locals()


def init_PySide6_QtConcurrent():
    type_map.update({
        "PySide6.QtCore.QFuture[QString]":
        PySide6.QtConcurrent.QFutureQString,
        "PySide6.QtCore.QFuture[void]":
        PySide6.QtConcurrent.QFutureVoid,
    })
    return locals()


def init_PySide6_QtGui():
    from PySide6.QtGui import QPageLayout, QPageSize  # 5.12 macOS
    type_map.update({
        "0.0f": 0.0,
        "1.0f": 1.0,
        "GL_COLOR_BUFFER_BIT": GL_COLOR_BUFFER_BIT,
        "GL_NEAREST": GL_NEAREST,
        "int32_t": int,
        "HBITMAP": int,
        "HICON": int,
        "HMONITOR": int,
        "HRGN": int,
        "QPixmap()": Default("PySide6.QtGui.QPixmap"),  # can't create without qApp
        "QPlatformSurface*": int,  # a handle
        "QVector< QTextLayout.FormatRange >()": [],  # do we need more structure?
        "uint32_t": int,
        "uint8_t": int,
        "USHRT_MAX": ushort_max,
    })

    # special case - char* can either be 'bytes' or 'str'. The default is 'bytes'.
    # Here we manually set it to map to 'str'.
    type_map.update({("PySide6.QtGui.QPixmap.save", "char*"): str})

    return locals()


def init_PySide6_QtWidgets():
    from PySide6.QtWidgets import (QWidget, QMessageBox, QStyleOption,
                                   QStyleHintReturn, QStyleOptionComplex,
                                   QGraphicsItem, QStyleOptionGraphicsItem)
    type_map.update({
        "QMessageBox.StandardButtons(Yes | No)": Instance(
            "QMessageBox.StandardButtons(QMessageBox.Yes | QMessageBox.No)"),
        "QWidget.RenderFlags(DrawWindowBackground | DrawChildren)": Instance(
            "QWidget.RenderFlags(QWidget.DrawWindowBackground | QWidget.DrawChildren)"),
        "static_cast<Qt.MatchFlags>(Qt.MatchExactly|Qt.MatchCaseSensitive)": Instance(
            "Qt.MatchFlags(Qt.MatchExactly | Qt.MatchCaseSensitive)"),
        "static_cast<Qt.MatchFlag>(Qt.MatchExactly|Qt.MatchCaseSensitive)": Instance(
            "Qt.MatchFlag(Qt.MatchExactly | Qt.MatchCaseSensitive)"),
        "QListWidgetItem.ItemType.Type": PySide6.QtWidgets.QListWidgetItem.Type,
        "QTableWidgetItem.ItemType.Type": PySide6.QtWidgets.QTableWidgetItem.Type,
        "QTreeWidgetItem.ItemType.Type": PySide6.QtWidgets.QTreeWidgetItem.Type,
    })
    return locals()


def init_PySide6_QtSql():
    from PySide6.QtSql import QSqlDatabase
    type_map.update({
        "QLatin1StringView(QSqlDatabase.defaultConnection)": QSqlDatabase.defaultConnection,
        "QVariant.Invalid": Invalid("Variant"),  # not sure what I should create, here...
    })
    return locals()


def init_PySide6_QtNetwork():
    from PySide6.QtNetwork import QNetworkRequest, QHostAddress
    best_structure = typing.OrderedDict if getattr(typing, "OrderedDict", None) else typing.Dict
    type_map.update({
        "QMultiMap[PySide6.QtNetwork.QSsl.AlternativeNameEntryType, QString]":
            best_structure[PySide6.QtNetwork.QSsl.AlternativeNameEntryType, typing.List[str]],
        "DefaultTransferTimeoutConstant":
            QNetworkRequest.TransferTimeoutConstant,
        "QNetworkRequest.DefaultTransferTimeoutConstant":
            QNetworkRequest.TransferTimeoutConstant,
    })
    del best_structure
    return locals()


def init_PySide6_QtOpenGL():
    type_map.update({
        "GLbitfield": int,
        "GLenum": int,
        "GLfloat": float,  # 5.6, MSVC 15
        "GLint": int,
        "GLuint": int,
    })
    return locals()


def init_PySide6_QtQml():
    type_map.update({
        "VolatileBool": PySide6.QtQml.VolatileBool,
    })
    return locals()


def init_PySide6_QtQuick():
    type_map.update({
        "PySide6.QtQuick.QSharedPointer[PySide6.QtQuick.QQuickItemGrabResult]":
            PySide6.QtQuick.QQuickItemGrabResult,
        "QSGGeometry.Type.UnsignedShortType": int,
    })
    return locals()


def init_PySide6_QtTest():
    from PySide6.QtCore import SignalInstance
    type_map.update({
        "PySideSignalInstance": SignalInstance,
        "PySide6.QtTest.QTest.PySideQTouchEventSequence": PySide6.QtTest.QTest.QTouchEventSequence,
        "PySide6.QtTest.QTouchEventSequence": PySide6.QtTest.QTest.QTouchEventSequence,
    })
    return locals()


# from 5.12, macOS
def init_PySide6_QtDataVisualization():
    from PySide6.QtDataVisualization import (QBarDataItem, QSurfaceDataItem)
    QBarDataRow = typing.List[QBarDataItem]
    QBarDataArray = typing.List[QBarDataRow]
    QSurfaceDataRow = typing.List[QSurfaceDataItem]
    QSurfaceDataArray = typing.List[QSurfaceDataRow]
    type_map.update({
        "100.0f": 100.0,
        "QBarDataArray": QBarDataArray,
        "QBarDataArray*": QBarDataArray,
        "QSurfaceDataArray": QSurfaceDataArray,
        "QSurfaceDataArray*": QSurfaceDataArray,
    })
    return locals()


def init_PySide6_QtBluetooth():
    type_map.update({
        "QVariant*": object,
    })
    return locals()


def init_PySide6_QtGraphs():
    from PySide6.QtGraphs import (QBarDataItem, QSurfaceDataItem)
    QBarDataRow = typing.List[QBarDataItem]
    QBarDataArray = typing.List[QBarDataRow]
    QSurfaceDataRow = typing.List[QSurfaceDataItem]
    QSurfaceDataArray = typing.List[QSurfaceDataRow]
    type_map.update({
        "100.0f": 100.0,
        "QBarDataArray": QBarDataArray,
        "QBarDataArray*": QBarDataArray,
        "QSurfaceDataArray": QSurfaceDataArray,
        "QSurfaceDataArray*": QSurfaceDataArray,
    })
    return locals()


def init_PySide6_QtHttpServer():
    type_map.update({
        "qMakePair(1u, 1u)": (1, 1),
    })
    return locals()


def init_testbinding():
    type_map.update({
        "testbinding.PySideCPP2.TestObjectWithoutNamespace": testbinding.TestObjectWithoutNamespace,
        "testbinding.FlagsNamespace.Options": testbinding.Option,
        "FlagsNamespace.Option.NoOptions": 0,
        "StdIntList": typing.List[int],
        'Str("")': str(""),
    })
    return locals()

# end of file
