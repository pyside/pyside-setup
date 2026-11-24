# Copyright (C) 2022 The Qt Company Ltd.
# SPDX-License-Identifier: LicenseRef-Qt-Commercial OR LGPL-3.0-only OR GPL-2.0-only OR GPL-3.0-only
# Qt-Security score:significant reason:default
from __future__ import annotations

"""
enum_sig.py

Enumerate all signatures of a class.

This module separates the enumeration process from the formatting.
It is not easy to adhere to this protocol, but in the end, it paid off
by producing a lot of clarity.
"""

import inspect
import sys
import types
import collections
from abc import ABC, abstractmethod
from shibokensupport.signature import get_signature as get_sig
from shibokensupport.signature.layout import DEFAULT_PARAM_KIND
from enum import Enum


"""
PYSIDE-1599: Making sure that pyi files always are tested.

A new problem popped up when supporting true properties:
When there exists an item named "property", then we cannot use
the builtin `property` as decorator, but need to prefix it with "builtins".

We scan for such a name in a class, and if there should a property be
declared in the same class, we use `builtins.property` in the class and
all sub-classes. The same consideration holds for "overload".
"""

_normal_functions = (types.BuiltinFunctionType, types.FunctionType)
if hasattr(sys, "pypy_version_info"):
    # In PyPy, there are more types because our builtin functions
    # are created differently in C++ and not in PyPy.
    _normal_functions += (type(get_sig),)


def is_inconsistent_overload(signatures):
    if not isinstance(signatures, list):
        return False
    count = 0
    for sig in signatures:
        count += 1 if "self" in sig.parameters else 0
    return count != 0 and count != len(signatures)


def is_relevant_type(thing):
    t = str(type(thing))
    return (("PySide" in t or "getset_descriptor" in t)
            and "QMetaObject" not in t)


class BaseFormatter(ABC):
    def __init__(self) -> None:
        self.level: int = 0
        self.mod_name: str = ""
        self.class_name: str | None = None
        self.have_body: bool = False
        self.is_method: collections.abc.Callable[[], bool] = lambda: False

    @abstractmethod
    def module(self, mod_name):
        ...

    @abstractmethod
    def klass(self, class_name, class_str, has_misc_error=False):
        ...

    @abstractmethod
    def function(self, func_name, signature, decorator=None, aug_ass=None, incon_err=None):
        ...


class SectionFormatter(ABC):
    @abstractmethod
    def section(self) -> None:
        ...


class EnumFormatter(ABC):
    @abstractmethod
    def enum(self, class_name, enum_name, value):
        ...


class AttributeFormatter(ABC):
    @abstractmethod
    def attribute(self, attr_name, attr_value):
        ...


class SignalFormatter(ABC):
    @abstractmethod
    def signal(self, class_name, sig_name, sig_str):
        ...


class ExactEnumerator:
    """
    ExactEnumerator enumerates all signatures in a module as they are.

    This class is used for generating complete listings of all signatures.
    An appropriate formatter should be supplied, if printable output
    is desired.
    """

    # PYSIDE-2846: This set holds functions with `__i<f>__` != `__<f>__` signature.
    mypy_aug_ass_errors = set()

    augmented_assignments = {
        "__iadd__": "__add__",
        "__isub__": "__sub__",
        "__imul__": "__mul__",
        "__idiv__": "__div__",
    }

    # PYSIDE-2846: Inheritance errors which are in QtDesigner
    mypy_misc_class_errors = set()
    mypy_misc_class_errors.add("QPyDesignerPropertySheetExtension")

    def __init__(self, formatter: BaseFormatter, result_type=dict):
        try:
            # Lazy import
            from PySide6.QtCore import Signal, SignalInstance
        except ImportError:
            self.SignalCls: tuple[type[Signal], type[SignalInstance]] | None = None
        else:
            self.SignalCls = (Signal, SignalInstance)

        self.fmt = formatter
        self.result_type = result_type
        self.fmt.is_method = self.is_method
        self.collision_candidates = {"property", "overload"}
        self.func = None

    def is_method(self):
        """
        Is this function a method?
        We check if it is a simple function.
        """
        tp = type(self.func)
        return tp not in _normal_functions

    def section(self):
        if isinstance(self.fmt, SectionFormatter):
            self.fmt.section()

    def module(self, mod_name):
        __import__(mod_name)
        self.fmt.mod_name = mod_name
        with self.fmt.module(mod_name):
            module = sys.modules[mod_name]
            members = inspect.getmembers(module, inspect.isclass)
            # PYSIDE-2846: Make sure not to get QTextFrame.iterator :(
            members = list(x for x in members if "." not in x[0])
            functions = inspect.getmembers(module, inspect.isroutine)
            ret = self.result_type()
            self.fmt.class_name = None
            for class_name, klass in members:
                self.collision_track = set()
                ret.update(self.klass(class_name, klass))
                self.section()
            for func_name, func in functions:
                ret.update(self.function(func_name, func))
            if functions or not members:
                self.section()
            return ret

    def klass(self, class_name, klass):
        ret = self.result_type()
        if "._" in class_name:
            # This happens when introspecting enum.Enum etc. Python 3.8.8 does not
            # like this, but we want to remove that, anyway.
            return ret
        if "<" in class_name:
            # This is happening in QtQuick for some reason:
            # class std::shared_ptr<QQuickItemGrabResult >:
            # We simply skip over this class.
            return ret
        bases_list = []
        for base in klass.__bases__:
            name = base.__qualname__
            if name not in ("object", "property", "type"):
                name = base.__module__ + "." + name
            bases_list.append(name)
        bases_str = ', '.join(bases_list)
        # PYSIDE-2516: see pyi_generator.
        if class_name in ("Qt.KeyboardModifier", "Qt.Modifier"):
            bases_str = "_SupportsOrKey"
        class_str = f"{class_name}" if bases_str == "object" else f"{class_name}({bases_str})"
        # class_members = inspect.getmembers(klass)
        # gives us also the inherited things.
        class_members = sorted(list(klass.__dict__.items()))
        subclasses = []
        functions = []
        enums = []
        properties = []
        signals = []
        attributes = {}

        for thing_name, thing in class_members:
            if self.SignalCls and isinstance(thing, self.SignalCls):
                signals.append((thing_name, thing))
            elif inspect.isclass(thing):
                subclass_name = ".".join((class_name, thing_name))
                subclasses.append((subclass_name, thing))
            elif inspect.isroutine(thing):
                func_name = thing_name.split(".")[0]   # remove ".overload"
                functions.append((func_name, thing))
            elif isinstance(thing, Enum):
                # take the real enum name, not what is in the dict
                if not thing_name.startswith("_"):
                    enums.append((thing_name, type(thing).__qualname__, thing))
            elif isinstance(thing, property):
                properties.append((thing_name, thing))
            # Support attributes that have PySide types as values,
            # but we skip the 'staticMetaObject' that needs
            # to be defined at a QObject level.
            # PYSIDE-3034: added public variables, extracted helper function to
            # avoid repetitive calls of str(type(thing))
            elif is_relevant_type(thing):
                if class_name not in attributes:
                    attributes[class_name] = {}
                attributes[class_name][thing_name] = thing

            if thing_name in self.collision_candidates:
                self.collision_track.add(thing_name)

        # PYSIDE-2846: Mark inconsistency between __iadd__ and __add__ etc.
        for aug_ass in self.augmented_assignments:
            if aug_ass in klass.__dict__:
                other = self.augmented_assignments[aug_ass]
                if other in klass.__dict__:
                    aug_sig = self.get_signature(klass.__dict__[aug_ass])
                    other_sig = self.get_signature(klass.__dict__[other])
                    if aug_sig != other_sig:
                        func = klass.__dict__[aug_ass]
                        self.mypy_aug_ass_errors.add(func)

        # sort by class then enum value
        enums.sort(key=lambda tup: (tup[1], tup[2].value))

        # We want to handle functions and properties together.
        func_prop = sorted(functions + properties, key=lambda tup: tup[0])

        self.fmt.have_body = False

        has_misc_error = class_name in self.mypy_misc_class_errors
        if issubclass(klass, Enum) and not len(enums):
            # PYSIDE-2846: We keep the empty enum and ignore the error.
            has_misc_error = True
        with self.fmt.klass(class_name, class_str, has_misc_error):
            self.fmt.class_name = class_name

            if isinstance(self.fmt, EnumFormatter) and enums:
                # this is an optional feature
                for enum_name, enum_class_name, value in enums:
                    with self.fmt.enum(enum_class_name, enum_name, value.value):
                        pass
                self.section()
                self.fmt.have_body = True

            if isinstance(self.fmt, SignalFormatter) and signals:
                # this is an optional feature
                for signal_name, signal in signals:
                    sig_class = type(signal)
                    sig_class_name = f"{sig_class.__qualname__}"
                    sig_str = str(signal)
                    with self.fmt.signal(sig_class_name, signal_name, sig_str):
                        pass
                self.section()
                self.fmt.have_body = True

            if isinstance(self.fmt, AttributeFormatter) and attributes:
                for class_name, attrs in attributes.items():
                    for attr_name, attr_value in attrs.items():
                        with self.fmt.attribute(attr_name, attr_value):
                            pass
                self.section()
                self.fmt.have_body = True

            for subclass_name, subclass in subclasses:
                save = self.collision_track.copy()
                ret.update(self.klass(subclass_name, subclass))
                self.collision_track = save
                self.fmt.class_name = class_name

            if ret_ := self.function("__init__", klass):
                ret.update(ret_)
                self.section()
                self.fmt.have_body = True

            has_function_section = False
            for func_name, func in func_prop:
                if func_name != "__init__":
                    if isinstance(func, property):
                        ret_ = self.fproperty(func_name, func)
                    else:
                        ret_ = self.function(func_name, func)
                    has_function_section |= bool(ret_)
                    ret.update(ret_)
            if has_function_section:
                self.section()
                self.fmt.have_body = True

        # If this class is a nested class, we may have clobbered the parent's class have_body value
        # The parent class always has a body because it contains this class.
        self.fmt.have_body = True

        return ret

    @staticmethod
    def get_signature(func):
        return get_sig(func)

    @staticmethod
    def property_setter_signature(signatures):
        orig_signatures = signatures
        # Property.setter should accept 2 arguments: self and value.
        signatures = list(filter(lambda s: len(s.parameters) == 2, signatures))
        if len(signatures) == 1:
            return signatures[0]
        elif signatures:
            # If there are several signatures, their types can be combined.
            names = set()
            types_ = []
            for signature in signatures:
                it = iter(signature.parameters.values())
                next(it)
                param = next(it)
                names.add(param.name)
                types_.append(param.annotation)
                if (param.kind not in (inspect.Parameter.POSITIONAL_ONLY,
                                       inspect.Parameter.POSITIONAL_OR_KEYWORD)
                        or signature.return_annotation is not None):
                    # Incorrect overload.
                    # We return the original overloads (currently, there are none).
                    return orig_signatures
            # If the names match, we use it;
            # otherwise, we use the name `arg__1`, since another name might cause confusion.
            if len(names) == 1:
                name = names.pop()
            else:
                name = "arg__1"
            # union types
            type_ = types_[0]
            for t in types_[1:]:
                type_ |= t
            return inspect.Signature(
                (inspect.Parameter("self", inspect.Parameter.POSITIONAL_ONLY),
                 inspect.Parameter(name, inspect.Parameter.POSITIONAL_ONLY, annotation=type_)),
                return_annotation=None)
        # If there are no suitable overloads, we return the original overloads
        # (currently, there are none).
        return orig_signatures

    def function(self, func_name, func, decorator=None, is_property_setter=False):
        self.func = func    # for is_method()
        ret = self.result_type()
        if decorator in self.collision_track:
            decorator = f"builtins.{decorator}"
        signature = self.get_signature(func, decorator)
        if is_property_setter and isinstance(signature, list):
            # PYSIDE-3530: Remove typing.overload and incorrect signatures for property.setter.
            signature = self.property_setter_signature(signature)
        incon_err = False
        if is_inconsistent_overload(signature):
            incon_err = True
        # PYSIDE-2846: Special cases of signatures which inherit from object.
        _self = inspect.Parameter("self", DEFAULT_PARAM_KIND)
        if func_name == "__dir__":
            signature = inspect.Signature([_self], return_annotation=collections.abc.Iterable[str])
        elif func_name == "__repr__":
            signature = inspect.Signature([_self], return_annotation=str)
        if signature is not None:
            aug_ass = func in self.mypy_aug_ass_errors
            with self.fmt.function(func_name, signature, decorator, aug_ass, incon_err) as key:
                ret[key] = signature
        self.func = None
        return ret

    def fproperty(self, prop_name, prop):
        ret = self.function(prop_name, prop.fget, type(prop).__qualname__)
        if prop.fset:
            ret.update(self.function(prop_name, prop.fset, f"{prop_name}.setter", True))
        if prop.fdel:
            ret.update(self.function(prop_name, prop.fdel, f"{prop_name}.deleter"))
        return ret


def stringify(signature):
    if isinstance(signature, list):
        # remove duplicates which still sometimes occour:
        ret = set(stringify(sig) for sig in signature)
        return sorted(ret) if len(ret) > 1 else list(ret)[0]
    return tuple(str(pv) for pv in signature.parameters.values())


class SimplifyingEnumerator(ExactEnumerator):
    """
    SimplifyingEnumerator enumerates all signatures in a module filtered.

    There are no default values, no variable
    names and no self parameter. Only types are present after simplification.
    The functions 'next' resp. '__next__' are removed
    to make the output identical for Python 2 and 3.
    An appropriate formatter should be supplied, if printable output
    is desired.
    """

    def function(self, func_name, func, decorator=None):
        ret = self.result_type()
        signature = get_sig(func, 'existence')
        if signature is not None:
            sig = stringify(signature)
            if sig is not None:
                with self.fmt.function(func_name, signature) as key:
                    ret[key] = sig
        return ret


class HintingEnumerator(ExactEnumerator):
    """
    HintingEnumerator enumerates all signatures in a module slightly changed.

    This class is used for generating complete listings of all signatures for
    hinting stubs. Only default values are replaced by "...".
    """

    def __init__(self, *args, **kwds):
        super().__init__(*args, **kwds)
        # We need to provide default signatures for class properties.
        cls_param = inspect.Parameter("cls", DEFAULT_PARAM_KIND)
        set_param = inspect.Parameter("arg_1", DEFAULT_PARAM_KIND, annotation=object)
        self.getter_sig = inspect.Signature([cls_param], return_annotation=object)
        self.setter_sig = inspect.Signature([cls_param, set_param])
        self.deleter_sig = inspect.Signature([cls_param])

    def get_signature(self, func, decorator=None):
        # Class properties don't have signature support (yet).
        # In that case, produce a fake one.
        sig = get_sig(func, "hintingstub")
        if not sig:
            if decorator:
                if decorator.endswith(".setter"):
                    sig = self.setter_sig
                elif decorator.endswith(".deleter"):
                    sig = self.deleter_sig
                else:
                    sig = self.getter_sig
        return sig
