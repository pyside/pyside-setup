# Copyright (C) 2026 The Qt Company Ltd.
# SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0
from __future__ import annotations

import os
import subprocess
import sys
import sysconfig
import unittest

from pathlib import Path
sys.path.append(os.fspath(Path(__file__).resolve().parents[1]))
from init_paths import init_test_paths
init_test_paths(False)


@unittest.skipUnless(sysconfig.get_config_var("Py_GIL_DISABLED"), "free-threaded builds only")
class LazyLoadingOffTest(unittest.TestCase):
    """Test that importing PySide leaves module type and import alone."""

    def check(self, what):
        # A fresh process can look before the first import
        proc = subprocess.run([sys.executable, __file__, what],
                              capture_output=True, text=True, timeout=120)
        self.assertEqual(proc.returncode, 0, proc.stdout + proc.stderr)

    def testModuleGetattrUntouched(self):
        self.check("--getattro")

    def testImportNotRedirected(self):
        self.check("--import")


def _getattro_slot():
    """PyModule_Type.tp_getattro, read through the type object's layout."""
    import ctypes
    module_type = id(type(sys))
    # PyVarObject header, then tp_name; tp_getattro is the 16th field after it
    header = object.__basicsize__ + ctypes.sizeof(ctypes.c_ssize_t)
    name = ctypes.c_char_p.from_address(module_type + header).value
    if name != b"module":
        raise RuntimeError(f"unexpected type layout: tp_name is {name!r}")
    slot = header + 15 * ctypes.sizeof(ctypes.c_void_p)
    return ctypes.c_void_p.from_address(module_type + slot).value


def _check_getattro():
    before = _getattro_slot()
    import PySide6.QtCore  # noqa: F401
    if _getattro_slot() != before:
        print("importing PySide6 replaced PyModule_Type.tp_getattro")
        return 1
    return 0


def _check_import():
    import builtins
    before = builtins.__import__
    import PySide6.QtCore  # noqa: F401
    orig = getattr(builtins, "__orig_import__", before)
    if orig is not before:
        print(f"importing PySide6 redirected the import to {orig!r}")
        return 1
    return 0


if __name__ == '__main__':
    if "--getattro" in sys.argv:
        sys.exit(_check_getattro())
    if "--import" in sys.argv:
        sys.exit(_check_import())
    unittest.main()
