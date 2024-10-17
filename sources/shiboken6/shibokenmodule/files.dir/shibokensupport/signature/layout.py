# Copyright (C) 2022 The Qt Company Ltd.
# SPDX-License-Identifier: LicenseRef-Qt-Commercial OR LGPL-3.0-only OR GPL-2.0-only OR GPL-3.0-only
from __future__ import annotations

"""
layout.py

The signature module now has the capability to configure
differently formatted versions of signatures. The default
layout is known from the "__signature__" attribute.

The function "get_signature(ob, modifier=None)" produces the same
signatures by default. By passing different modifiers, you
can select different layouts.

This module configures the different layouts which can be used.
It also implements them in this file. The configurations are
used literally as strings like "signature", "existence", etc.
"""

import inspect
import typing

from types import SimpleNamespace
from textwrap import dedent
from shibokensupport.signature.mapping import ellipsis


class SignatureLayout(SimpleNamespace):
    """
    Configure a signature.

    The layout of signatures can have different layouts which are
    controlled by keyword arguments:

    definition=True         Determines if self will generated.
    defaults=True
    ellipsis=False          Replaces defaults by "...".
    return_annotation=True
    parameter_names=True    False removes names before ":".
    """
    allowed_keys = SimpleNamespace(definition=True,
                                   defaults=True,
                                   ellipsis=False,
                                   return_annotation=True,
                                   parameter_names=True)
    allowed_values = True, False

    def __init__(self, **kwds):
        args = SimpleNamespace(**self.allowed_keys.__dict__)
        args.__dict__.update(kwds)
        self.__dict__.update(args.__dict__)
        err_keys = list(set(self.__dict__) - set(self.allowed_keys.__dict__))
        if err_keys:
            self._attributeerror(err_keys)
        err_values = list(set(self.__dict__.values()) - set(self.allowed_values))
        if err_values:
            self._valueerror(err_values)

    def __setattr__(self, key, value):
        if key not in self.allowed_keys.__dict__:
            self._attributeerror([key])
        if value not in self.allowed_values:
            self._valueerror([value])
        self.__dict__[key] = value

    def _attributeerror(self, err_keys):
        err_keys = ", ".join(err_keys)
        allowed_keys = ", ".join(self.allowed_keys.__dict__.keys())
        raise AttributeError(dedent(f"""\
            Not allowed: '{err_keys}'.
            The only allowed keywords are '{allowed_keys}'.
            """))

    def _valueerror(self, err_values):
        err_values = ", ".join(map(str, err_values))
        allowed_values = ", ".join(map(str, self.allowed_values))
        raise ValueError(dedent(f"""\
            Not allowed: '{err_values}'.
            The only allowed values are '{allowed_values}'.
            """))


# The following names are used literally in this module.
# This way, we avoid the dict hashing problem.
signature = SignatureLayout()

existence = SignatureLayout(definition=False,
                            defaults=False,
                            return_annotation=False,
                            parameter_names=False)

hintingstub = SignatureLayout(ellipsis=True)

typeerror = SignatureLayout(definition=False,
                            return_annotation=False,
                            parameter_names=False)


def define_nameless_parameter():
    """
    Create Nameless Parameters

    A nameless parameter has a reduced string representation.
    This is done by cloning the parameter type and overwriting its
    __str__ method. The inner structure is still a valid parameter.
    """
    def __str__(self):
        # for Python 2, we must change self to be an instance of P
        klass = self.__class__
        self.__class__ = P
        txt = P.__str__(self)
        self.__class__ = klass
        txt = txt[txt.index(":") + 1:].strip() if ":" in txt else txt
        return txt

    P = inspect.Parameter
    newname = "NamelessParameter"
    bases = P.__bases__
    body = dict(P.__dict__)  # get rid of mappingproxy
    if "__slots__" in body:
        # __slots__ would create duplicates
        for name in body["__slots__"]:
            del body[name]
    body["__str__"] = __str__
    return type(newname, bases, body)


NamelessParameter = define_nameless_parameter()

"""
Note on the "Optional" feature:

When an annotation has a default value that is None, then the
type has to be wrapped into "typing.Optional".

Note that only the None value creates an Optional expression,
because the None leaves the domain of the variable.
Defaults like integer values are ignored: They stay in the domain.

That information would be lost when we use the "..." convention.

Note that the typing module has the remarkable expansion

    Optional[T]    is    Union[T, NoneType]

We want to avoid that when generating the .pyi file.
This is done by a regex in pyi_generator.py .
The following would work in Python 3, but this is a version-dependent
hack that also won't work in Python 2 and would be _very_ complex.
"""
# import sys
# if sys.version_info[0] == 3:
#     class hugo(list):pass
#     typing._normalize_alias["hugo"] = "Optional"
#     Optional = typing._alias(hugo, typing.T, inst=False)
# else:
#     Optional = typing.Optional


def make_signature_nameless(signature):
    """
    Make a Signature Nameless

    We use an existing signature and change the type of its parameters.
    The signature looks different, but is totally intact.
    """
    for key in signature.parameters.keys():
        signature.parameters[key].__class__ = NamelessParameter


_POSITIONAL_ONLY         = inspect.Parameter.POSITIONAL_ONLY  # noqa E:201
_POSITIONAL_OR_KEYWORD   = inspect.Parameter.POSITIONAL_OR_KEYWORD  # noqa E:201
_VAR_POSITIONAL          = inspect.Parameter.VAR_POSITIONAL  # noqa E:201
_KEYWORD_ONLY            = inspect.Parameter.KEYWORD_ONLY  # noqa E:201
_VAR_KEYWORD             = inspect.Parameter.VAR_KEYWORD  # noqa E:201
_empty                   = inspect.Parameter.empty  # noqa E:201


default_weights = {
    typing.Any: 1000,   # noqa E:241
    bool:        101,   # noqa E:241
    int:         102,   # noqa E:241
    float:       103,   # noqa E:241
}


def get_ordering_key(anno):
    """
    This is the main sorting algorithm for annotations.
    For a normal type, we use the tuple

        (- length of mro(anno), 1, name)

    For Union expressions, we use the minimum

        (- minlen of mro(anno), len(getargs(anno)), name)

    This way, Union annotations are always sorted behind normal types.
    Addition of a `name` field ensures a unique ordering.

    A special case are numeric types, which have also an ordering between them.
    They can be handled separately, since they are all of the shortest mro.
    """
    typing_type = typing.get_origin(anno)
    is_union = typing_type is typing.Union
    if is_union:
        # This is some Union-like construct.
        typing_args = typing.get_args(anno)
        parts = len(typing_args)

        if defaults := list(ann for ann in typing_args if ann in default_weights):
            # Special: look into the default weights and use the largest.
            leng = 0
            for ann in defaults:
                w = default_weights[ann]
                if w > leng:
                    leng = w
                    anno = ann
        else:
            # Normal: Use the union arg with the shortest mro().
            leng = 9999
            for ann in typing_args:
                lng = len(ann.mro())
                if lng < leng:
                    leng = lng
                    anno = ann
    else:
        leng = len(anno.mro()) if anno not in (type, None, typing.Any) else 0
        parts = 1
    if anno in default_weights:
        leng = - default_weights[anno]
    # In 3.10 only None has no name. 3.9 is worse concerning typing constructs.
    name = anno.__name__ if hasattr(anno, "__name__") else "None"
    # Put typing containers after the plain type.
    if typing_type and not is_union:
        return (-leng + 100, parts, name)
    return (-leng, parts, name)


def sort_by_inheritance(signatures):
    # First decorate all signatures with a key built by the mro.
    for idx, sig in enumerate(signatures):
        sort_order = []
        for param in list(sig.parameters.values()):
            sort_order.append(get_ordering_key(param.annotation))
        signatures[idx] = sort_order, sig

    # Sort the signatures and remove the key column again.
    signatures = sorted(signatures, key=lambda x: x[0])
    for idx, sig in enumerate(signatures):
        signatures[idx] = sig[1]
    return signatures


def _remove_ambiguous_signatures_body(signatures):
    # By the sorting of signatures, duplicates will always be adjacent.
    last_ann = last_sig = None
    last_idx = -1
    to_delete = []
    found = False
    for idx, sig in enumerate(signatures):
        annos = []
        for param in list(sig.parameters.values()):
            annos.append(param.annotation)
        if annos == last_ann:
            found = True
            if sig.return_annotation is last_sig.return_annotation:
                # we can use any duplicate
                to_delete.append(idx)
            else:
                # delete the one which has non-empty result
                to_delete.append(idx if not sig.return_annotation else last_idx)
        last_ann = annos
        last_sig = sig
        last_idx = idx

    if not found:
        return False, signatures
    new_sigs = []
    for idx, sig in enumerate(signatures):
        if idx not in to_delete:
            new_sigs.append(sig)
    return True, new_sigs


def remove_ambiguous_signatures(signatures):
    # This may run more than once because of indexing.
    found, new_sigs = _remove_ambiguous_signatures_body(signatures)
    if found:
        _, new_sigs = _remove_ambiguous_signatures_body(new_sigs)
    return new_sigs


def create_signature(props, key):
    if not props:
        # empty signatures string
        return
    if isinstance(props["multi"], list):
        # multi sig: call recursively.
        res = list(create_signature(elem, key) for elem in props["multi"])
        # PYSIDE-2846: Sort multi-signatures by inheritance in order to avoid shadowing.
        res = sort_by_inheritance(res)
        res = remove_ambiguous_signatures(res)
        return res if len(res) > 1 else res[0]

    if type(key) is tuple:
        _, modifier = key
    else:
        _, modifier = key, "signature"

    layout = globals()[modifier]  # lookup of the modifier in this module
    if not isinstance(layout, SignatureLayout):
        raise SystemError("Modifiers must be names of a SignatureLayout "
                          "instance")

    # this is the basic layout of a signature
    varnames = props["varnames"]
    if layout.definition:
        # PYSIDE-1328: We no longer use info from the sig_kind which is
        # more complex for multiple signatures. We now get `self` from the
        # parser.
        pass
    else:
        if varnames and varnames[0] in ("self", "cls"):
            varnames = varnames[1:]

    # calculate the modifications
    defaults = props["defaults"][:]
    if not layout.defaults:
        defaults = ()
    annotations = props["annotations"].copy()
    if not layout.return_annotation and "return" in annotations:
        del annotations["return"]

    # Build a signature.
    kind = inspect._POSITIONAL_OR_KEYWORD
    params = []
    for idx, name in enumerate(varnames):
        if name.startswith("**"):
            kind = _VAR_KEYWORD
        elif name.startswith("*"):
            kind = _VAR_POSITIONAL
        ann = annotations.get(name, _empty)
        if ann in ("self", "cls"):
            ann = _empty
        name = name.lstrip("*")
        defpos = idx - len(varnames) + len(defaults)
        default = defaults[defpos] if defpos >= 0 else _empty
        if default is None:
            ann = typing.Optional[ann]
        if default is not _empty and layout.ellipsis:
            default = ellipsis
        param = inspect.Parameter(name, kind, annotation=ann, default=default)
        params.append(param)
        if kind == _VAR_POSITIONAL:
            kind = _KEYWORD_ONLY
    sig = inspect.Signature(params,
                            return_annotation=annotations.get('return', _empty),
                            __validate_parameters__=False)

    # the special case of nameless parameters
    if not layout.parameter_names:
        make_signature_nameless(sig)
    return sig

# end of file
