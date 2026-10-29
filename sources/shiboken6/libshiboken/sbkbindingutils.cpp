// Copyright (C) 2025 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR LGPL-3.0-only OR GPL-2.0-only OR GPL-3.0-only
// Qt-Security score:significant reason:default

#include "sbkbindingutils.h"

#include "autodecref.h"
#include "sbkstring.h"
#include "sbkpep.h"
#include "sbkpydictrange.h"
#include "sbkstaticstrings_p.h"

#include <algorithm>

namespace Shiboken {

static const ArgumentNameIndexMapping *
    findMapping(const ArgumentNameIndexMapping *i1,
                const ArgumentNameIndexMapping *i2, PyObject *needle)
{
    return std::find_if(i1, i2, [needle](const ArgumentNameIndexMapping &m) {
        return PyUnicode_CompareWithASCIIString(needle, m.name) == 0;
    });
}

/*!
    Function binding helper: Parses the keyword arguments in dict \a kwds
    according to \a mapping (name->index) and stores them in array \a pyArgs
    under their index. Fails if an entry is missing or duplicate entries
    occur.

    \table
        \header
            \li Parameter
            \li Meaning
        \row
            \li \a kwds
            \li Dict of keyword arguments.
        \row
            \li \a mapping
            \li Array of name->index mappings.
        \row
            \li \a size
            \li Size of \a mapping.
        \row
            \li \a errInfo
            \li Receives error information.
        \row
            \li \a pyArgs
            \li Array of arguments to be populated.
        \row
            \li \e Return
            \li true on success, false otherwise.
    \endtable
*/
bool parseKeywordArguments(PyObject *kwds,
                           const ArgumentNameIndexMapping *mapping, size_t size,
                           Shiboken::AutoDecRef &errInfo, PyObject **pyArgs)
{
    if (kwds == nullptr || PyDict_Size(kwds) == 0)
        return true;
    const ArgumentNameIndexMapping *mappingEnd = mapping + size;
    for (auto [key, value] : PyDictRange(kwds)) {
        auto it = findMapping(mapping, mappingEnd, key);
        // Missing key: Create a new dict as error context (see signature/errorhandler.py)
        if (it == mappingEnd) {
            errInfo.reset(PyDict_New());
            PyDict_SetItem(errInfo.object(), key, value);
            return false;
        }
        if (pyArgs[it->index] != nullptr) { // duplicate entry, set string as error context
            errInfo.reset(key);
            return false;
        }
        pyArgs[it->index] = value;
    }
    return true;
}

/*!
    Function binding helper: Parses the keyword arguments of a QObject
    constructor in dict \a kwds according to \a mapping (name->index) and
    stores them in array \a pyArgs under their index. Fails if duplicate
    entries occur. Unmapped entries (QObject properties) are stored in a dict
    in \a errInfo for further processing.

    \table
        \header
            \li Parameter
            \li Meaning
        \row
            \li \a kwds
            \li Dict of keyword arguments.
        \row
            \li \a mapping
            \li Array of name->index mappings.
        \row
            \li \a size
            \li Size of \a mapping.
        \row
            \li \a errInfo
            \li Receives error information or the dict of unmapped entries.
        \row
            \li \a pyArgs
            \li Array of arguments to be populated.
        \row
            \li \e Return
            \li true on success, false otherwise.
    \endtable
*/
bool parseConstructorKeywordArguments(PyObject *kwds,
                                      const ArgumentNameIndexMapping *mapping, size_t size,
                                      Shiboken::AutoDecRef &errInfo, PyObject **pyArgs)
{
    assert(kwds);
    Shiboken::AutoDecRef result(PyDict_New());
    const ArgumentNameIndexMapping *mappingEnd = mapping + size;
    for (auto [key, value] : PyDictRange(kwds)) {
        auto it = findMapping(mapping, mappingEnd, key);
        // Ignore missing key, assuming it is a property to be handled later
        if (it != mappingEnd) {
            // duplicate entry, set string as error context (see signature/errorhandler.py)
            if (pyArgs[it->index] != nullptr) {
                errInfo.reset(key);
                return false;
            }
            pyArgs[it->index] = value;
        } else {
            PyDict_SetItem(result.object(), key, value);
        }
    }
    errInfo.reset(result.release());
    return true;
}

static bool isCompiledHelper()
{
    Shiboken::AutoDecRef globals(PepEval_GetFrameGlobals());
    if (globals.isNull())
        return false;

    if (PyDict_GetItem(globals.object(), PyMagicName::compiled()) != nullptr)
        return true;
    globals.reset(nullptr);

    // __compiled__ may not be set in initialization phases, check builtins
    static PyObject *nuitkaDir = Shiboken::String::createStaticString("__nuitka_binary_exe");
    Shiboken::AutoDecRef builtins(PepEval_GetFrameBuiltins());
    return !builtins.isNull() && PyDict_GetItem(builtins.object(), nuitkaDir) != nullptr;
}

bool isCompiled()
{
    static const bool result = isCompiledHelper();
    return result;
}

} // namespace Shiboken
