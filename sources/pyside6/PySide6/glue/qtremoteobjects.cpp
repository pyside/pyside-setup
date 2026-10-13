// Copyright (C) 2024 Ford Motor Company
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR LGPL-3.0-only OR GPL-2.0-only OR GPL-3.0-only
// Qt-Security score:significant reason:default

// @snippet qtro-init
PySide::RemoteObjects::init(module);
// @snippet qtro-init

// @snippet node-acquire
auto *typeObject = reinterpret_cast<PyTypeObject*>(%PYARG_1);
if (!PySide::inherits(typeObject, SbkPySide6_QtRemoteObjectsTypeStructs[SBK_QRemoteObjectReplica_IDX].fullName)) {
    PyErr_SetString(PyExc_TypeError, "First argument must be a type deriving from QRemoteObjectReplica.");
    return nullptr;
}

#ifdef Py_GIL_DISABLED
static Shiboken::OnceValue<PyObject *> pyConstructWithNodeOnce;
PyObject *pyConstructWithNode = pyConstructWithNodeOnce.get([] {
    return Shiboken::Enum::newItem(
        Shiboken::Module::get(SbkPySide6_QtRemoteObjectsTypeStructs[SBK_QRemoteObjectReplica_ConstructorType_IDX]),
        1 /* protected QRemoteObjectReplica::ConstructorType::ConstructWithNode */
    );
});
#else
static PyObject *pyConstructWithNode = Shiboken::Enum::newItem(
    Shiboken::Module::get(SbkPySide6_QtRemoteObjectsTypeStructs[SBK_QRemoteObjectReplica_ConstructorType_IDX]),
    1 /* protected QRemoteObjectReplica::ConstructorType::ConstructWithNode */
);
#endif

Shiboken::AutoDecRef args;
if (pyArgs[1])
    args.reset(PyTuple_Pack(3, %PYSELF, pyConstructWithNode, pyArgs[1]));
else
    args.reset(PyTuple_Pack(2, %PYSELF, pyConstructWithNode));

PyObject *instance = PyObject_CallObject(%PYARG_1, args.object());
if (!instance)
    return nullptr;  // Propagate the exception

%PYARG_0 = instance;
// @snippet node-acquire
