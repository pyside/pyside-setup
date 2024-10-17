# Copyright (C) 2022 The Qt Company Ltd.
# SPDX-License-Identifier: LicenseRef-Qt-Commercial OR LGPL-3.0-only OR GPL-2.0-only OR GPL-3.0-only
from __future__ import annotations

import ast
import enum
import keyword
import os
import re
import sys
import typing
import warnings

from types import SimpleNamespace
from shibokensupport.signature.mapping import (type_map, update_mapping,
    namespace, _NotCalled, ResultVariable, ArrayLikeVariable)  # noqa E:128
from shibokensupport.signature.lib.tool import build_brace_pattern

_DEBUG = False
LIST_KEYWORDS = False

"""
parser.py

This module parses the signature text and creates properties for the
signature objects.

PySide has a new function 'CppGenerator::writeSignatureInfo()'
that extracts the gathered information about the function arguments
and defaults as good as it can. But what PySide generates is still
very C-ish and has many constants that Python doesn't understand.

The function 'try_to_guess()' below understands a lot of PySide's
peculiar way to assume local context. If it is able to do the guess,
then the result is inserted into the dict, so the search happens
not again. For everything that is not covered by these automatic
guesses, we provide an entry in 'type_map' that resolves it.

In effect, 'type_map' maps text to real Python objects.
"""


def _get_flag_enum_option():
    from shiboken6 import (__version_info__ as ver,  # noqa F:401
                           __minimum_python_version__ as pyminver,
                           __maximum_python_version__ as pymaxver)

    # PYSIDE-1735: Use the new Enums per default if version is >= 6.4
    #              This decides between delivered vs. dev versions.
    #              When 6.4 is out, the switching mode will be gone.
    flag = ver[:2] >= (6, 4)
    envname = "PYSIDE6_OPTION_PYTHON_ENUM"
    sysname = envname.lower()
    opt = os.environ.get(envname)
    if opt:
        opt = opt.lower()
        if opt in ("yes", "on", "true"):
            flag = True
        elif opt in ("no", "off", "false"):
            flag = False
        else:
            # instead of a simple int() conversion, let's allow for "0xf" or "0b1111"
            try:
                flag = ast.literal_eval(opt)
            except Exception:
                flag = False    # turn a forbidden option into an error
    elif hasattr(sys, sysname):
        opt2 = flag = getattr(sys, sysname)
        if not isinstance(flag, int):
            flag = False        # turn a forbidden option into an error
    p = f"\n    *** Python is at version {'.'.join(map(str, pyminver or (0,)))} now."
    q = f"\n    *** PySide is at version {'.'.join(map(str, ver[:2]))} now."
    # _PepUnicode_AsString: Fix a broken promise
    if pyminver and pyminver >= (3, 10):
        warnings.warn(f"{p} _PepUnicode_AsString can now be replaced by PyUnicode_AsUTF8! ***")
    # PYSIDE-1960: Emit a warning when we may remove bufferprocs_py37.(cpp|h)
    if pyminver and pyminver >= (3, 11):
        warnings.warn(f"{p} The files bufferprocs_py37.(cpp|h) should be removed ASAP! ***")
    # PYSIDE-1735: Emit a warning when we should maybe evict forgiveness mode
    if ver[:2] >= (7, 0):
        warnings.warn(f"{q} Please drop Enum forgiveness mode in `mangled_type_getattro` ***")
    # PYSIDE-2404: Emit a warning when we should drop uppercase offset constants
    if ver[:2] >= (7, 0):
        warnings.warn(f"{q} Please drop uppercase type offsets in `copyOffsetEnumStream` ***")
    # normalize the sys attribute
    setattr(sys, sysname, flag)
    os.environ[envname] = str(flag)
    if int(flag) == 0:
        raise RuntimeError(f"Old Enums are no longer supported. int({opt or opt2}) evaluates to 0)")
    return flag


class EnumSelect(enum.Enum):
    # PYSIDE-1735: Here we could save object.value expressions by using IntEnum.
    #              But it is nice to use just an Enum for selecting Enum version.
    OLD = 1
    NEW = 2
    SELECTION = NEW if _get_flag_enum_option() else OLD


def dprint(*args, **kw):
    if _DEBUG:
        import pprint
        for arg in args:
            pprint.pprint(arg)
            sys.stdout.flush()


_cache = {}


def _parse_arglist(argstr):
    # The following is a split re. The string is broken into pieces which are
    # between the recognized strings. Because the re has groups, both the
    # strings and the separators are returned, where the strings are not
    # interesting at all: They are just the commata.
    key = "_parse_arglist"
    if key not in _cache:
        regex = build_brace_pattern(level=3, separators=",")
        _cache[key] = re.compile(regex, flags=re.VERBOSE)
    split = _cache[key].split
    # Note: this list is interspersed with "," and surrounded by ""
    return [x.strip() for x in split(argstr) if x.strip() not in ("", ",")]


def _parse_line(line):
    line_re = r"""
        ((?P<multi> ([0-9]+)) : )?    # the optional multi-index
        (?P<funcname> \w+(\.\w+)*)    # the function name
        \( (?P<arglist> .*?) \)       # the argument list
        ( -> (?P<returntype> .*) )?   # the optional return type
        $
        """
    matches = re.match(line_re, line, re.VERBOSE)
    if not matches:
        raise SystemError("Error parsing line:", repr(line))
    ret = SimpleNamespace(**matches.groupdict())
    # PYSIDE-1095: Handle arbitrary default expressions
    argstr = ret.arglist.replace("->", ".deref.")
    arglist = _parse_arglist(argstr)
    args = []
    for idx, arg in enumerate(arglist):
        tokens = arg.split(":")
        if len(tokens) < 2 and idx == 0 and tokens[0] in ("self", "cls"):
            tokens = 2 * tokens     # "self: self"
        if len(tokens) != 2:
            # This should never happen again (but who knows?)
            raise SystemError(f'Invalid argument "{arg}" in "{line}".')
        name, ann = tokens
        if name in keyword.kwlist:
            if LIST_KEYWORDS:
                print("KEYWORD", ret)
            name = name + "_"
        if "=" in ann:
            ann, default = ann.split("=", 1)
            tup = name, ann, default
        else:
            tup = name, ann
        args.append(tup)
    ret.arglist = args
    multi = ret.multi
    if multi is not None:
        ret.multi = int(multi)
    funcname = ret.funcname
    parts = funcname.split(".")
    if parts[-1] in keyword.kwlist:
        ret.funcname = funcname + "_"
    return vars(ret)


def _using_snake_case():
    # Note that this function should stay here where we use snake_case.
    if "PySide6.QtCore" not in sys.modules:
        return False
    from PySide6.QtCore import QDir
    return hasattr(QDir, "cd_up")


def _handle_instance_fixup(thing):
    """
    Default expressions using instance methods like
        (...,device=QPointingDevice.primaryPointingDevice())
    need extra handling for snake_case. These are:
        QPointingDevice.primaryPointingDevice()
        QInputDevice.primaryKeyboard()
        QKeyCombination.fromCombined(0)
        QSslConfiguration.defaultConfiguration()
    """
    match = re.search(r"\w+\(", thing)
    if not match:
        return thing
    start, stop = match.start(), match.end() - 1
    pre, func, args = thing[:start], thing[start:stop], thing[stop:]
    if func[0].isupper() or func.startswith("gl") and func[2:3].isupper():
        return thing
    # Now convert this string to snake case.
    snake_func = ""
    for idx, char in enumerate(func):
        if char.isupper():
            if idx and func[idx - 1].isupper():
                # two upper chars are forbidden
                return thing
            snake_func += f"_{char.lower()}"
        else:
            snake_func += char
    return f"{pre}{snake_func}{args}"


def make_good_value(thing, valtype):
    # PYSIDE-1019: Handle instance calls (which are really seldom)
    if "(" in thing and _using_snake_case():
        thing = _handle_instance_fixup(thing)
    try:
        if thing.endswith("()"):
            thing = f'Default("{thing[:-2]}")'
        else:
            # PYSIDE-1735: Use explicit globals and locals because of a bug in VsCode
            ret = eval(thing, globals(), namespace)
            if valtype and repr(ret).startswith("<"):
                thing = f'Instance("{thing}")'
        return eval(thing, globals(), namespace)
    except Exception:
        pass


def try_to_guess(thing, valtype):
    if "." not in thing and "(" not in thing:
        text = f"{valtype}.{thing}"
        ret = make_good_value(text, valtype)
        if ret is not None:
            return ret
    typewords = valtype.split(".")
    valwords = thing.split(".")
    braceless = valwords[0]    # Yes, not -1. Relevant is the overlapped word.
    if "(" in braceless:
        braceless = braceless[:braceless.index("(")]
    for idx, w in enumerate(typewords):
        if w == braceless:
            text = ".".join(typewords[:idx] + valwords)
            ret = make_good_value(text, valtype)
            if ret is not None:
                return ret
    return None


def get_name(thing):
    if isinstance(thing, type):
        return getattr(thing, "__qualname__", thing.__name__)
    else:
        return thing.__name__


def _resolve_value(thing, valtype, line):
    if thing in ("0", "None") and valtype:
        if valtype.startswith("PySide6.") or valtype.startswith("typing."):
            return None
        map = type_map[valtype]
        # typing.Any: '_SpecialForm' object has no attribute '__name__'
        name = get_name(map) if hasattr(map, "__name__") else str(map)
        thing = f"zero({name})"
    if thing in type_map:
        return type_map[thing]
    res = make_good_value(thing, valtype)
    if res is not None:
        type_map[thing] = res
        return res
    res = try_to_guess(thing, valtype) if valtype else None
    if res is not None:
        type_map[thing] = res
        return res
    warnings.warn(f"""pyside_type_init:_resolve_value

        UNRECOGNIZED:   {thing!r}
        OFFENDING LINE: {line!r}
        """, RuntimeWarning)
    return thing


def _resolve_arraytype(thing, line):
    search = re.search(r"\[(\d*)\]$", thing)
    thing = thing[:search.start()]
    if thing.endswith("]"):
        thing = _resolve_arraytype(thing, line)
    if search.group(1):
        # concrete array, use a tuple
        nelem = int(search.group(1))
        thing = ", ".join([thing] * nelem)
        thing = "Tuple[" + thing + "]"
    else:
        thing = "QList[" + thing + "]"
    return thing


def to_string(thing):
    # This function returns a string that creates the same object.
    # It is absolutely crucial that str(eval(thing)) == str(thing),
    # i.e. it must be an idempotent mapping.
    if isinstance(thing, str):
        return thing
    # PYSIDE-2517: findChild/findChildren type hints:
    # TypeVar doesn't have a __qualname__ attribute,
    # so we fall back to use __name__ before the next condition.
    if isinstance(thing, typing.TypeVar):
        return get_name(thing)
    if hasattr(thing, "__name__") and thing.__module__ != "typing":
        m = thing.__module__
        dot = "." in str(thing) or m not in (thing.__qualname__, "builtins")
        name = get_name(thing)
        ret = m + "." + name if dot else name
        assert (eval(ret, globals(), namespace))
        return ret
    # Note: This captures things from the typing module:
    return str(thing)


matrix_pattern = "PySide6.QtGui.QGenericMatrix"


def handle_matrix(arg):
    n, m, typstr = tuple(map(lambda x: x.strip(), arg.split(",")))
    assert typstr == "float"
    result = f"PySide6.QtGui.QMatrix{n}x{m}"
    return eval(result, globals(), namespace)


def _resolve_type(thing, line, level, var_handler, func_name=None):
    # manual set of 'str' instead of 'bytes'
    if func_name:
        new_thing = (func_name, thing)
        if new_thing in type_map:
            return type_map[new_thing]

    # Capture total replacements, first. Happens in
    # "PySide6.QtCore.QCborStreamReader.StringResult[PySide6.QtCore.QByteArray]"
    if thing in type_map:
        return type_map[thing]

    # Now the nested structures are handled.
    if "[" in thing:
        # Special case: Callable[[],
        if thing == "[]":
            return thing
        # handle primitive arrays
        if re.search(r"\[\d*\]$", thing):
            thing = _resolve_arraytype(thing, line)
        # Handle a container return type. (see PYSIDE-921 in cppgenerator.cpp)
        contr, thing = re.match(r"(.*?)\[(.*?)\]$", thing).groups()
        # Special case: Handle the generic matrices.
        if contr == matrix_pattern:
            return handle_matrix(thing)
        contr = var_handler(_resolve_type(contr, line, level + 1, var_handler))
        if isinstance(contr, _NotCalled):
            raise SystemError("Container types must exist:", repr(contr))
        contr = to_string(contr)
        pieces = []
        for part in _parse_arglist(thing):
            part = var_handler(_resolve_type(part, line, level + 1, var_handler))
            if isinstance(part, _NotCalled):
                # fix the tag (i.e. "Missing") by repr
                part = repr(part)
            pieces.append(to_string(part))
        thing = ", ".join(pieces)
        result = f"{contr}[{thing}]"
        # PYSIDE-1538: Make sure that the eval does not crash.
        try:
            return eval(result, globals(), namespace)
        except Exception:
            warnings.warn(f"""pyside_type_init:_resolve_type

                UNRECOGNIZED:   {result!r}
                OFFENDING LINE: {line!r}
                """, RuntimeWarning)
    return _resolve_value(thing, None, line)


def _handle_generic(obj, repl):
    """
    Assign repl if obj is an ArrayLikeVariable

    This is a neat trick. Example:

        obj                     repl        result
        ----------------------  --------    ---------
        ArrayLikeVariable       List        List
        ArrayLikeVariable(str)  List        List[str]
        ArrayLikeVariable       Sequence    Sequence
        ArrayLikeVariable(str)  Sequence    Sequence[str]
    """
    if isinstance(obj, ArrayLikeVariable):
        return repl[obj.type]
    if isinstance(obj, type) and issubclass(obj, ArrayLikeVariable):
        # was "if obj is ArrayLikeVariable"
        return repl
    return obj


def handle_argvar(obj):
    """
    Decide how array-like variables are resolved in arguments

    Currently, the best approximation is types.Sequence.
    We want to change that to types.Iterable in the near future.
    """
    return _handle_generic(obj, typing.Sequence)


def handle_retvar(obj):
    """
    Decide how array-like variables are resolved in results

    This will probably stay typing.List forever.
    """
    return _handle_generic(obj, typing.List)


class Hashabledict(dict):
    def __hash__(self):
        return hash(frozenset(self))


def calculate_props(line):
    parsed = SimpleNamespace(**_parse_line(line.strip()))
    arglist = parsed.arglist
    annotations = {}
    _defaults = []
    for idx, tup in enumerate(arglist):
        name, ann = tup[:2]
        if ann == "...":
            name = "*args" if name.startswith("arg_") else "*" + name
            # copy the patched fields back
            ann = 'nullptr'     # maps to None
            tup = name, ann
            arglist[idx] = tup
        annotations[name] = _resolve_type(ann, line, 0, handle_argvar, parsed.funcname)
        if len(tup) == 3:
            default = _resolve_value(tup[2], ann, line)
            # PYSIDE-2846: When creating signatures, the defaults should be hashable.
            #              For that to work, we use `Hashabledict`.
            if type(default) is dict:
                default = Hashabledict(default)
            _defaults.append(default)
    defaults = tuple(_defaults)
    returntype = parsed.returntype
    if isinstance(returntype, str) and returntype.startswith("("):
        # PYSIDE-1588: Simplify the handling of returned tuples for now.
        # Later we might create named tuples, instead.
        returntype = "Tuple"
    # PYSIDE-1383: We need to handle `None` explicitly.
    annotations["return"] = (_resolve_type(returntype, line, 0, handle_retvar)
                             if returntype is not None else None)
    props = SimpleNamespace()
    props.defaults = defaults
    props.kwdefaults = {}
    props.annotations = annotations
    props.varnames = tuple(tup[0] for tup in arglist)
    funcname = parsed.funcname
    shortname = funcname[funcname.rindex(".") + 1:]
    props.name = shortname
    props.multi = parsed.multi
    fix_variables(props, line)
    return vars(props)


def fix_variables(props, line):
    annos = props.annotations
    if not any(isinstance(ann, (ResultVariable, ArrayLikeVariable))
               for ann in annos.values()):
        return
    retvar = annos.get("return", None)
    if retvar and isinstance(retvar, (ResultVariable, ArrayLikeVariable)):
        # Special case: a ResultVariable which is the result will always be an array!
        annos["return"] = retvar = typing.List[retvar.type]
    varnames = list(props.varnames)
    defaults = list(props.defaults)
    diff = len(varnames) - len(defaults)

    safe_annos = annos.copy()
    retvars = [retvar] if retvar else []
    deletions = []
    for idx, name in enumerate(varnames):
        ann = safe_annos[name]
        if isinstance(ann, ArrayLikeVariable):
            ann = typing.Sequence[ann.type]
            annos[name] = ann
        if not isinstance(ann, ResultVariable):
            continue
        # We move the variable to the end and remove it.
        # PYSIDE-1409: If the variable was the first arg, we move it to the front.
        # XXX This algorithm should probably be replaced by more introspection.
        retvars.insert(0 if idx == 0 else len(retvars), ann.type)
        deletions.append(idx)
        del annos[name]
    for idx in reversed(deletions):
        # varnames:  0 1 2 3 4 5 6 7
        # defaults:        0 1 2 3 4
        # diff: 3
        del varnames[idx]
        if idx >= diff:
            del defaults[idx - diff]
        else:
            diff -= 1
    if retvars:
        retvars = list(handle_retvar(rv) if isinstance(rv, ArrayLikeVariable) else rv
                       for rv in retvars)
        if len(retvars) == 1:
            returntype = retvars[0]
        else:
            retvars_str = ", ".join(map(to_string, retvars))
            typestr = f"typing.Tuple[{retvars_str}]"
            returntype = eval(typestr, globals(), namespace)
        props.annotations["return"] = returntype
    props.varnames = tuple(varnames)
    props.defaults = tuple(defaults)


def fixup_multilines(lines):
    """
    Multilines can collapse when certain distinctions between C++ types
    vanish after mapping to Python.
    This function fixes this by re-computing multiline-ness.
    """
    res = []
    multi_lines = []
    for line in lines:
        multi = re.match(r"([0-9]+):", line)
        if multi:
            idx, rest = int(multi.group(1)), line[multi.end():]
            multi_lines.append(rest)
            if idx > 0:
                continue
            # remove duplicates
            multi_lines = sorted(set(multi_lines))
            # renumber or return a single line
            nmulti = len(multi_lines)
            if nmulti > 1:
                for idx, line in enumerate(multi_lines):
                    res.append(f"{nmulti-idx-1}:{line}")
            else:
                res.append(multi_lines[0])
            multi_lines = []
        else:
            res.append(line)
    return res


def pyside_type_init(type_key, sig_strings):
    dprint()
    dprint(f"Initialization of type key '{type_key}'")
    update_mapping()
    lines = fixup_multilines(sig_strings)
    ret = {}
    multi_props = []
    for line in lines:
        props = calculate_props(line)
        shortname = props["name"]
        multi = props["multi"]
        if multi is None:
            ret[shortname] = props
            dprint(props)
        else:
            multi_props.append(props)
            if multi > 0:
                continue
            multi_props = {"multi": multi_props}
            ret[shortname] = multi_props
            dprint(multi_props)
            multi_props = []
    return ret

# end of file
