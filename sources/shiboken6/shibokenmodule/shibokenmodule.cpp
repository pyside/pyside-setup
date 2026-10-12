// Copyright (C) 2024 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR LGPL-3.0-only OR GPL-2.0-only OR GPL-3.0-only
// Qt-Security score:significant reason:default

// @snippet isvalid
bool isValid = Shiboken::Object::isValid(%1, false);
%PYARG_0 = %CONVERTTOPYTHON[bool](isValid);
// @snippet isvalid

// @snippet wrapinstance
auto *pyType = reinterpret_cast<PyTypeObject *>(%2);
if (Shiboken::ObjectType::checkType(pyType)) {
    auto *ptr = reinterpret_cast<void *>(%1);
    if (auto *wrapper = Shiboken::BindingManager::instance().retrieveWrapper(ptr)) {
        %PYARG_0 = reinterpret_cast<PyObject *>(wrapper);
        Py_INCREF(%PYARG_0);
    } else {
        %PYARG_0 = Shiboken::Object::newObject(pyType, ptr, false, true);
    }
} else {
    PyErr_SetString(PyExc_TypeError, "You need a shiboken-based type.");
}
// @snippet wrapinstance

// @snippet getcpppointer
if (Shiboken::Object::checkType(%1)) {
    std::vector<void*> ptrs = Shiboken::Object::cppPointers(reinterpret_cast<SbkObject *>(%1));
    %PYARG_0 = PyTuple_New(ptrs.size());
    for (std::size_t i = 0; i < ptrs.size(); ++i)
        PyTuple_SetItem(%PYARG_0, i, PyLong_FromVoidPtr(ptrs[i]));
} else {
    PyErr_SetString(PyExc_TypeError, "You need a shiboken-based type.");
}
// @snippet getcpppointer

// @snippet delete
if (Shiboken::Object::checkType(%1)) {
    Shiboken::Object::callCppDestructors(reinterpret_cast<SbkObject *>(%1));
} else {
    PyErr_SetString(PyExc_TypeError, "You need a shiboken-based type.");
}
// @snippet delete

// @snippet ownedbypython
if (Shiboken::Object::checkType(%1)) {
    bool hasOwnership = Shiboken::Object::hasOwnership(reinterpret_cast<SbkObject *>(%1));
    %PYARG_0 = %CONVERTTOPYTHON[bool](hasOwnership);
} else {
    PyErr_SetString(PyExc_TypeError, "You need a shiboken-based type.");
}
// @snippet ownedbypython

// @snippet createdbypython
if (Shiboken::Object::checkType(%1)) {
    bool wasCreatedByPython = Shiboken::Object::wasCreatedByPython(reinterpret_cast<SbkObject *>(%1));
    %PYARG_0 = %CONVERTTOPYTHON[bool](wasCreatedByPython);
} else {
    PyErr_SetString(PyExc_TypeError, "You need a shiboken-based type.");
}
// @snippet createdbypython

// @snippet disassembleframe
Shiboken::AutoDecRef label(PyObject_Str(%1));
const char *marker = Shiboken::String::toCString(label);
disassembleFrame(marker);
Py_INCREF(Py_None);
%PYARG_0 = Py_None;
// @snippet disassembleframe

// @snippet dump
if (!Shiboken::Object::checkType(%1)) {
    %PYARG_0 = Shiboken::String::fromCString("Ordinary Python type.");
} else {
    std::string str = Shiboken::Object::info(reinterpret_cast<SbkObject *>(%1));
    %PYARG_0 = Shiboken::String::fromCString(str.c_str());
}
// @snippet dump


// @snippet dump-tree
if (!Shiboken::Object::checkType(%1)) {
    %PYARG_0 = Shiboken::String::fromCString("Ordinary Python type.");
} else {
    std::string str = Shiboken::Object::dumpTree(reinterpret_cast<SbkObject *>(%1));
    %PYARG_0 = Shiboken::String::fromCString(str.c_str());
}
// @snippet dump-tree

// @snippet replacemoduledict
const bool ok = Shiboken::Module::replaceModuleDict(%1, %2, %3);
%PYARG_0 = %CONVERTTOPYTHON[bool](ok);
// @snippet replacemoduledict

// @snippet getallvalidwrappers
const auto setAll = Shiboken::BindingManager::instance().getAllPyObjects();
PyObject* listAll = PyList_New(0);
if (listAll == nullptr)
    return nullptr;
for (auto *o : setAll) {
    if (o != nullptr) {
        if (PyList_Append(listAll, o) != 0) {
            Py_DECREF(listAll);
            return nullptr;
        }
    }
}
return listAll;
// @snippet getallvalidwrappers

// @snippet dumptypegraph
const bool ok = Shiboken::BindingManager::instance().dumpTypeGraph(%1);
%PYARG_0 = %CONVERTTOPYTHON[bool](ok);
// @snippet dumptypegraph

// @snippet dumpwrappermap
Shiboken::BindingManager::instance().dumpWrapperMap();
// @snippet dumpwrappermap

// @snippet dumpconverters
Shiboken::Conversions::dumpConverters();
// @snippet dumpconverters

// @snippet init
// Add __version__ and __version_info__ attributes to the module
PyObject* version = PyTuple_New(5);
PyTuple_SetItem(version, 0, PyLong_FromLong(SHIBOKEN_MAJOR_VERSION));
PyTuple_SetItem(version, 1, PyLong_FromLong(SHIBOKEN_MINOR_VERSION));
PyTuple_SetItem(version, 2, PyLong_FromLong(SHIBOKEN_MICRO_VERSION));
PyTuple_SetItem(version, 3, Shiboken::String::fromCString(SHIBOKEN_RELEASE_LEVEL));
PyTuple_SetItem(version, 4, PyLong_FromLong(SHIBOKEN_SERIAL));
PepModule_Add(module, "__version_info__", version);
PyModule_AddStringConstant(module, "__version__", SHIBOKEN_VERSION);
VoidPtr::addVoidPtrToModule(module);
Shiboken::initShibokenSupport(module);
#ifdef Py_GIL_DISABLED
// Free-threaded test hooks; an add-function would change the GIL build
static PyMethodDef freeThreadingFunctions[] = {
    {"ftOptionFlags", [](PyObject *, PyObject *) -> PyObject * {
        return PyLong_FromUnsignedLongLong(Shiboken::FreeThreading::optionFlags());
    }, METH_NOARGS, nullptr},
#ifndef SBK_NO_FAILPOINTS
    {"armFailpoint", [](PyObject *, PyObject *args) -> PyObject * {
        const char *name = nullptr;
        int timeoutMs = 5000;
        if (PyArg_ParseTuple(args, "s|i:armFailpoint", &name, &timeoutMs) == 0)
            return nullptr;
        return PyBool_FromLong(Shiboken::armFailpoint(name, timeoutMs));
    }, METH_VARARGS, nullptr},
    {"releaseFailpoint", [](PyObject *, PyObject *arg) -> PyObject * {
        const char *name = PyUnicode_AsUTF8(arg);
        if (name == nullptr)
            return nullptr;
        return PyBool_FromLong(Shiboken::releaseFailpoint(name));
    }, METH_O, nullptr},
    {"failpointReached", [](PyObject *, PyObject *arg) -> PyObject * {
        const char *name = PyUnicode_AsUTF8(arg);
        if (name == nullptr)
            return nullptr;
        return PyBool_FromLong(Shiboken::failpointReached(name));
    }, METH_O, nullptr},
    {"clearFailpoints", [](PyObject *, PyObject *) -> PyObject * {
        Shiboken::clearFailpoints();
        Py_RETURN_NONE;
    }, METH_NOARGS, nullptr},
    {"failpointNames", [](PyObject *, PyObject *) -> PyObject * {
        return PyUnicode_FromString(Shiboken::failpointNames());
    }, METH_NOARGS, nullptr},
#endif // SBK_NO_FAILPOINTS
    {nullptr, nullptr, 0, nullptr}
};
if (PyModule_AddFunctions(module, freeThreadingFunctions) < 0)
    return -1;
#endif // Py_GIL_DISABLED
// @snippet init
