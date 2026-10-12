// Copyright (C) 2020 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR LGPL-3.0-only OR GPL-2.0-only OR GPL-3.0-only
// Qt-Security score:significant reason:default

////////////////////////////////////////////////////////////////////////////
//
// signature_global.cpp
//
// This file contains the global data structures and init code.
//

#include "autodecref.h"
#include "sbkpep.h"
#include "sbkstring.h"
#include "sbkstaticstrings.h"
#include "sbkstaticstrings_p.h"
#include "sbkenum.h"

#include "signature_p.h"

#include <cstring>

using namespace Shiboken;

extern "C" {

#ifndef Q_QDOC
static const char *PySide_CompressedSignaturePackage[] = {
#include "embed/signature_inc.h"
    };

static const unsigned char PySide_SignatureLoader[] = {
#include "embed/signature_bootstrap_inc.h"
    };
#endif // Q_QDOC

static void init_phase_1(safe_globals_struc *p)
{
    do {
        /*
         * Initializing module signature_bootstrap.
         * Since we now have an embedding script, we can do this without any
         * Python strings in the C code.
         */
#if defined(Py_LIMITED_API) || defined(SHIBOKEN_NO_EMBEDDING_PYC)
        // We must work for multiple versions or we are cross-building for a different
        // Python version interpreter, so use source code.
#else
        AutoDecRef marshal_module(PyImport_Import(PyName::marshal()));      // builtin
        AutoDecRef loads(PyObject_GetAttr(marshal_module, PyName::loads()));
        if (loads.isNull())
            break;
#endif
        char *bytes_cast = reinterpret_cast<char *>(
                                       const_cast<unsigned char *>(PySide_SignatureLoader));
        AutoDecRef bytes(PyBytes_FromStringAndSize(bytes_cast, sizeof(PySide_SignatureLoader)));
        if (bytes.isNull())
            break;

        AutoDecRef builtins;
#if defined(Py_LIMITED_API) || defined(SHIBOKEN_NO_EMBEDDING_PYC)
        builtins.reset(PepEval_GetFrameBuiltins());
        PyObject *compile = PyDict_GetItem(builtins.object(), PyName::compile());
        builtins.reset(nullptr);
        if (compile == nullptr)
            break;
        AutoDecRef code_obj(PyObject_CallFunction(compile, "Oss",
                                bytes.object(), "signature_bootstrap.py", "exec"));
#else
        AutoDecRef code_obj(PyObject_CallFunctionObjArgs(
                                loads, bytes.object(), nullptr));
#endif
        if (code_obj.isNull())
            break;
        p->helper_module = PyImport_ExecCodeModule("signature_bootstrap", code_obj);
        if (p->helper_module == nullptr)
            break;
        // Initialize the module
        PyObject *mdict = PyModule_GetDict(p->helper_module);
        builtins.reset(PepEval_GetFrameBuiltins());
        if (PyDict_SetItem(mdict, PyMagicName::builtins(), builtins.object()) < 0)
            break;
        builtins.reset(nullptr);

        /*********************************************************************
         *
         * Attention!
         * ----------
         *
         * We are unpacking an embedded ZIP file with more signature modules.
         * They will be loaded later with the zipimporter.
         * The file `signature_bootstrap.py` does the unpacking and starts the
         * loader. See `init_phase_2`.
         *
         * Due to MSVC's limitation to 64k strings, we needed to assemble pieces.
         */
        auto **block_ptr = reinterpret_cast<const char **>(PySide_CompressedSignaturePackage);
        PyObject *piece{};
        AutoDecRef zipped_string_sequence(PyList_New(0));
        for (; **block_ptr != 0; ++block_ptr) {
            // we avoid the string/unicode dilemma by not using PyString_XXX:
            piece = Py_BuildValue("s", *block_ptr);
            if (piece == nullptr || PyList_Append(zipped_string_sequence, piece) < 0)
                break;
        }
        if (PyDict_SetItemString(mdict, "zipstring_sequence", zipped_string_sequence) < 0)
            break;

        // build a dict for diverse mappings
        p->map_dict = PyDict_New();

        // build a dict for the prepared arguments
        p->arg_dict = PyDict_New();
        if (PyObject_SetAttrString(p->helper_module, "pyside_arg_dict", p->arg_dict) < 0)
            break;

        // build a dict for assigned signature values
        p->value_dict = PyDict_New();

        // PYSIDE-1019: build a __feature__ dict
        p->feature_dict = PyDict_New();
        if (PyObject_SetAttrString(p->helper_module, "pyside_feature_dict", p->feature_dict) < 0)
            break;

        // This function will be disabled until phase 2 is done.
        p->finish_import_func = nullptr;

        return;

    } while (false);

    PyErr_Print();
    Py_FatalError("libshiboken/signature: could not initialize part 1");
}

static int init_phase_2(safe_globals_struc *p, PyMethodDef *methods)
{
    do {
        // The single function to be called, but maybe more to come.
        for (PyMethodDef *ml = methods; ml->ml_name != nullptr; ++ml) {
            PyObject *v = PyCFunction_NewEx(ml, nullptr, nullptr);
            if (v == nullptr
                || PyObject_SetAttrString(p->helper_module, ml->ml_name, v) != 0)
                break;
            Py_DECREF(v);
        }
        // The first entry is __feature_import__, add documentation.
        Shiboken::AutoDecRef builtins(PepEval_GetFrameBuiltins());
        PyObject *imp_func = PyDict_GetItemString(builtins.object(), "__import__");
        builtins.reset(nullptr);
        PyObject *imp_doc = PyObject_GetAttrString(imp_func, "__doc__");
        signature_methods[0].ml_doc = String::toCString(imp_doc);

        PyObject *bootstrap_func = PyObject_GetAttrString(p->helper_module, "bootstrap");
        if (bootstrap_func == nullptr)
            break;

        /*********************************************************************
         *
         * Attention!
         * ----------
         *
         * This is the entry point where everything in folder
         * `shibokensupport` becomes initialized. It starts with
         * `signature_bootstrap.py` and continues from there to `loader.py`.
         *
         * The return value of the bootstrap function is the loader module.
         */
        PyObject *loader = PyObject_CallFunctionObjArgs(bootstrap_func, nullptr);
        if (loader == nullptr)
            break;

        // now the loader should be initialized
        p->pyside_type_init_func = PyObject_GetAttrString(loader, "pyside_type_init");
        if (p->pyside_type_init_func == nullptr)
            break;
        p->create_signature_func = PyObject_GetAttrString(loader, "create_signature");
        if (p->create_signature_func == nullptr)
            break;
        p->seterror_argument_func = PyObject_GetAttrString(loader, "seterror_argument");
        if (p->seterror_argument_func == nullptr)
            break;
        p->make_helptext_func = PyObject_GetAttrString(loader, "make_helptext");
        if (p->make_helptext_func == nullptr)
            break;
        p->finish_import_func = PyObject_GetAttrString(loader, "finish_import");
        if (p->finish_import_func == nullptr)
            break;
        p->feature_import_func = PyObject_GetAttrString(loader, "feature_import");
        if (p->feature_import_func == nullptr)
            break;
        p->feature_imported_func = PyObject_GetAttrString(loader, "feature_imported");
        if (p->feature_imported_func == nullptr)
            break;

        // We call stuff like the feature initialization late,
        // after all the function pointers are in place.
        PyObject *post_init_func = PyObject_GetAttrString(loader, "post_init");
        if (post_init_func == nullptr)
            break;
        PyObject *ret = PyObject_CallFunctionObjArgs(post_init_func, nullptr);
        if (ret == nullptr)
            break;
#ifdef Py_GIL_DISABLED
        // Free-threaded builds have no __feature__: take back what post_init published
        if (PyDict_PopString(PyImport_GetModuleDict(), "__feature__", nullptr) < 0)
            break;
#endif

        return 0;

    } while (false);

    PyErr_Print();
    Py_FatalError("libshiboken/signature: could not initialize part 2");
    return -1;
}

#ifndef _WIN32
////////////////////////////////////////////////////////////////////////////
// a stack trace for linux-like platforms
#include <cstdio>
#ifdef __GLIBC__
#  include <execinfo.h>
#endif
#include <signal.h>
#include <cstdlib>
#include <unistd.h>

static void handler(int sig) {
#if defined(__GLIBC__)
    void *array[30];
    // get void *'s for all entries on the stack
    const int size = backtrace(array, 30);

    // print out all the frames to stderr
#endif
    std::fprintf(stderr, "Error: signal %d:\n", sig);
#ifdef __GLIBC__
    backtrace_symbols_fd(array, size, STDERR_FILENO);
#endif
    exit(1);
}

////////////////////////////////////////////////////////////////////////////
#endif // _WIN32

safe_globals_struc *signatureGlobals()
{
    static safe_globals_struc result;
    return &result;
}

void init_shibokensupport_module(void)
{
    static int init_done = 0;

    if (!init_done) {
        init_done = 1;

        auto *pyside_globals = signatureGlobals();
        init_phase_1(pyside_globals);

#ifndef _WIN32
        // We enable the stack trace in CI, only.
        const char *testEnv = getenv("QTEST_ENVIRONMENT");
        if (testEnv && std::strstr(testEnv, "ci"))
            signal(SIGSEGV, handler);   // install our handler
#endif // _WIN32

        init_phase_2(pyside_globals, signature_methods);
        // Enum must be initialized when signatures exist, not earlier.
        init_enum();
    }
}

} // extern "C"
