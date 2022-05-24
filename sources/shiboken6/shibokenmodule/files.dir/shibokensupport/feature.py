# Copyright (C) 2022 The Qt Company Ltd.
# SPDX-License-Identifier: LicenseRef-Qt-Commercial OR LGPL-3.0-only OR GPL-2.0-only OR GPL-3.0-only

"""
__feature__.py  (renamed to feature.py)

This is the feature file for the Qt for Python project. There is some
similarity to Python's `__future__` file, but also some distinction.

The normal usage is like

    from __feature__ import <feature_name> [, ...]
    ...

Alternatively, there is the `set_selection` function which uses select_id's
and takes an optional `mod_name` parameter.

The select id `-1` has the spectial meaning "ignore this module".
"""

import sys
from contextlib import contextmanager

all_feature_names = [
    "snake_case",
    "true_property",
    "_feature_04",
    "_feature_08",
    "_feature_10",
    "_feature_20",
    "_feature_40",
    "_feature_80",
]

__all__ = ["all_feature_names", "info", "reset", "set_selection"] + all_feature_names

snake_case = 0x01
true_property = 0x02
_feature_04 = 0x04
_feature_08 = 0x08
_feature_10 = 0x10
_feature_20 = 0x20
_feature_40 = 0x40
_feature_80 = 0x80

# let's remove the dummies for the normal user
_really_all_feature_names = all_feature_names[:]
all_feature_names = list(_ for _ in all_feature_names if not _.startswith("_"))

# Install an import hook that controls the `__feature__` import.
"""
Note: This are two imports.
>>> import dis
>>> def test():
...     from __feature__ import snake_case
...
>>> dis.dis(test)
  2           0 LOAD_CONST               1 (0)
              2 LOAD_CONST               2 (('snake_case',))
              4 IMPORT_NAME              0 (__feature__)
              6 IMPORT_FROM              1 (snake_case)
              8 STORE_FAST               0 (snake_case)
             10 POP_TOP
             12 LOAD_CONST               0 (None)
             14 RETURN_VALUE
"""

"""
The redirection of __import__
-----------------------------

This construction avoids irritating extra redirections in tracebacks.

The normal `__import__` is replaced by C function `__feature_import__`.
`__feature_import__` calls this `feature_import` function first, to
see if a feature is requested. If this function does not handle it, it returns
None to indicate that a normal import should be performed, and
`__feature_import__` calls the original import `__orig_import__`.
All these variables are transparently kept in module `builtins`.
"""

def feature_import(name, *args, **kwargs):
    # PYSIDE-1368: The `__name__` attribute does not need to exist in all modules.
    # PYSIDE-1398: sys._getframe(1) may not exist when embedding.
    # PYSIDE-1338: The "1" below is the redirection in loader.py .
    # PYSIDE-1548: Ensure that features are not affected by other imports.
    calling_frame = _cf = sys._getframe(1).f_back
    importing_module = _cf.f_globals.get("__name__", "__main__") if _cf else "__main__"
    existing = pyside_feature_dict.get(importing_module, 0)

    if name == "__feature__" and args[2]:
        __init__()

        # This is an `import from` statement that corresponds to `IMPORT_NAME`.
        # The following `IMPORT_FROM` will handle errors. (Confusing, ofc.)
        flag = get_select_id(args[2])

        flag |= existing & 255 if isinstance(existing, int) and existing >= 0 else 0
        pyside_feature_dict[importing_module] = flag

        if importing_module == "__main__":
            # We need to add all modules here which should see __feature__.
            pyside_feature_dict["rlcompleter"] = flag

        # Initialize feature (multiple times allowed) and clear cache.
        sys.modules["PySide6.QtCore"].__init_feature__()
        return sys.modules["__feature__"]

    if importing_module not in pyside_feature_dict:
        # Ignore new modules if not from PySide.
        default = 0 if name.split(".")[0] == "PySide6" else -1
        pyside_feature_dict[importing_module] = default
    # Redirect to the original import
    return None

_is_initialized = False

def __init__():
    global _is_initialized
    if not _is_initialized:
        # use _one_ recursive import...
        import PySide6.QtCore
        # Initialize all prior imported modules
        for name in sys.modules:
            pyside_feature_dict.setdefault(name, -1)
        _is_initialized = True


def set_selection(select_id, mod_name=None):
    """
    Internal use: Set the feature directly by Id.
    Id == -1: ignore this module in switching.
    """
    mod_name = mod_name or sys._getframe(1).f_globals['__name__']
    __init__()
    # Reset the features to the given id
    flag = 0
    if isinstance(select_id, int):
        flag = select_id & 255
    pyside_feature_dict[mod_name] = flag
    sys.modules["PySide6.QtCore"].__init_feature__()
    return _current_selection(flag)

# The set_section(0) case seems to be unsafe. We will migrate to
# use the opaque feature.reset() call in all test cases.
def reset():
    set_selection(0)
    pyside_feature_dict.clear()
    _is_initialized = False


def info(mod_name=None):
    """
    Internal use: Return the current selection
    """
    mod_name = mod_name or sys._getframe(1).f_globals['__name__']
    flag = pyside_feature_dict.get(mod_name, 0)
    return _current_selection(flag)


def _current_selection(flag):
    names = []
    if flag >= 0:
        for idx, name in enumerate(_really_all_feature_names):
            if (1 << idx) & flag:
                names.append(name)
    return names


def get_select_id(feature_names):
    flag = 0
    for feature in feature_names:
        if feature in _really_all_feature_names:
            flag |= globals()[feature]
        else:
            raise SyntaxError(f"PySide feature {feature} is not defined")
    return flag


@contextmanager
def force_selection(select_id, mod_name):
    """
    This function is for generating pyi files with features.
    The selection id is set globally after performing the unswitched
    import.

    """
    __init__()
    saved_feature_dict = pyside_feature_dict.copy()
    for name in pyside_feature_dict:
        set_selection(0, name)
    __import__(mod_name)
    for name in pyside_feature_dict.copy():
        set_selection(select_id, name)
    try:
        yield
    finally:
        pyside_feature_dict.update(saved_feature_dict)

#eof
