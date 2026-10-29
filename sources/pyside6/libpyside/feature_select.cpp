// Copyright (C) 2020 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR LGPL-3.0-only OR GPL-2.0-only OR GPL-3.0-only
// Qt-Security score:significant reason:default

#include "feature_select.h"
#include "basewrapper.h"
#include "pysidestaticstrings.h"
#include "class_property.h"
#include "pysideglobals_p.h"

#include <autodecref.h>
#include <sbkfeature_base.h>
#include <sbkpep.h>
#include <sbkpydictrange.h>
#include <sbkpytuplerange.h>
#include <sbkstaticstrings.h>
#include <sbkstring.h>
#include <signature_p.h>

#include <QtCore/qstringlist.h>

#include <cstring>

//////////////////////////////////////////////////////////////////////////////
//
// PYSIDE-1019: Support switchable extensions
//
// This functionality is no longer implemented in the signature module, since
// the PyCFunction getsets do not have to be modified any longer.
// Instead, we simply exchange the complete class dicts. This is done in the
// basewrapper.cpp file and in every generated `tp_(get|set)attro`.
//
// This is the general framework of the switchable extensions.
// A maximum of eight features is planned so far. This seems to be enough.
// More features are possible, but then we must somehow register the
// extra `select_id`s above 255.
//

/*****************************************************************************

    How Does This Feature Selection Work?
    -------------------------------------

The basic idea is to replace the `tp_dict` of a QObject derived type.
This way, we can replace the methods of the class in no time.

The crucial point to understand is how the `tp_dict` is actually accessed:
When you type "QObject.__dict__", the descriptor of `SbkObjectType_Type`
is called. This descriptor is per default unassigned, so the base class
PyType_Type provides the tp_getset method `type_dict`:

    static PyObject *
    type_dict(PyTypeObject *type, void *context)
    {
        if (type->tp_dict == NULL) {
            Py_RETURN_NONE;
        }
        return PyDictProxy_New(type->tp_dict);
    }

In order to change that, we need to insert our own version into SbkObjectType:

    static PyObject *Sbk_TypeGet___dict__(PyTypeObject *type, void *context)
    {
        auto dict = type->tp_dict;
        if (dict == NULL)
            Py_RETURN_NONE;
        if (SelectFeatureSet != nullptr)
            dict = SelectFeatureSet(type);
        return PyDictProxy_New(dict);
    }

This way, the Python function `type_ready()` does not fill in the default,
but uses our modified version. It a similar way, we overwrite type_getattro
with our own version, again in SbkObjectType, replacing the default of
PyType_Type.

Now we can exchange the dict with a customized version.
We have our own derived type `ChameleonDict` with additional attributes.
These allow us to create a ring of dicts which can be rotated to the actual
needed dict version:

Every dict has a field `select_id` which is selected by the `from __feature__`
import. The dicts are cyclic connected by the `dict_ring` field.

When a class dict is required, now always `SelectFeatureSet` is called, which
looks into the `__name__` attribute of the active module and decides which
version of `tp_dict` is needed. Then the right dict is searched in the ring
and created if not already there.

Furthermore, we need to overwrite every `tp_(get|set)attro`  with a version
that switches dicts right before looking up methods.
The dict changing must walk the whole `tp_mro` in order to change all names.

This is everything that the following code does.

*****************************************************************************/


/*!
    \namespace PySide::Feature
    \brief Namespace for feature switching (true property/snake case).
    \inmodule libpyside
*/
namespace PySide::Feature {

using namespace Shiboken;

using FeatureProc = bool(*)(PyTypeObject *type, PyObject *prev_dict, int id);

static FeatureProc *featurePointer = nullptr;

// Create a derived dict class
static PyTypeObject *
createDerivedDictType()
{
    // It is not easy to create a compatible dict object with the
    // limited API. Easier is to use Python to create a derived
    // type and to modify that a bit from the C code.
    PyObject *ChameleonDict = PepRun_GetResult(R"CPP(if True:

        class ChameleonDict(dict):
            __slots__ = ("dict_ring", "select_id", "orig_dict")

        result = ChameleonDict

        )CPP");
    return reinterpret_cast<PyTypeObject *>(ChameleonDict);
}

static PyTypeObject *ensureNewDictType()
{
    auto *globals = PySide::globals();
    if (globals->newFeatureDictType == nullptr) {
        globals->newFeatureDictType = createDerivedDictType();
        if (globals->newFeatureDictType == nullptr) {
            PyErr_Print();
            Py_FatalError("libshiboken: Problem creating ChameleonDict");
        }
    }
    return globals->newFeatureDictType;
}

static inline PyObject *nextInCircle(PyObject *dict)
{
    // returns a borrowed ref
    AutoDecRef next_dict(PyObject_GetAttr(dict, PySideName::dict_ring()));
    return next_dict;
}

static inline void setNextDict(PyObject *dict, PyObject *next_dict)
{
    PyObject_SetAttr(dict, PySideName::dict_ring(), next_dict);
}

static inline void setSelectId(PyObject *dict, int select_id)
{
    PyObject_SetAttr(dict, PySideName::select_id(), PyLong_FromLong(select_id));
}

static inline int getSelectId(PyObject *dict)
{
    auto *py_select_id = PyObject_GetAttr(dict, PyName::select_id());
    if (py_select_id == nullptr) {
        PyErr_Clear();
        return 0;
    }
    int ret = PyLong_AsLong(py_select_id);
    Py_DECREF(py_select_id);
    return ret;
}

static bool replaceClassDict(PyTypeObject *type)
{
    /*
     * Replace the type dict by the derived ChameleonDict.
     * This is mandatory for all type dicts when they are touched.
     */
    auto *ob_ndt = reinterpret_cast<PyObject *>(ensureNewDictType());
    AutoDecRef dict(PepType_GetDict(type));
    auto *new_dict = PyObject_CallObject(ob_ndt, nullptr);
    if (new_dict == nullptr || PyDict_Update(new_dict, dict) < 0)
        return false;
    // Insert the default id. Cannot fail for small numbers.
    setSelectId(new_dict, 0);
    // insert the dict into itself as ring
    setNextDict(new_dict, new_dict);
    // We have now an exact copy of the dict with a new type.
    PepType_SetDict(type, new_dict);
    // PYSIDE-2404: Retain the original dict for easy late init.
    PyObject_SetAttr(new_dict, PySideName::orig_dict(), dict);
    return true;
}

static bool addNewDict(PyTypeObject *type, int select_id)
{
    /*
     * Add a new dict to the ring and set it as `type->tp_dict`.
     * A 'false' return is fatal.
     */
    AutoDecRef dict(PepType_GetDict(type));
    AutoDecRef orig_dict(PyObject_GetAttr(dict, PySideName::orig_dict()));
    auto *ob_ndt = reinterpret_cast<PyObject *>(ensureNewDictType());
    auto *new_dict = PyObject_CallObject(ob_ndt, nullptr);
    if (new_dict == nullptr)
        return false;
    setSelectId(new_dict, select_id);
    // insert the dict into the ring
    auto *next_dict = nextInCircle(dict);
    setNextDict(dict, new_dict);
    setNextDict(new_dict, next_dict);
    PepType_SetDict(type, new_dict);
    // PYSIDE-2404: Retain the original dict for easy late init.
    PyObject_SetAttr(new_dict, PySideName::orig_dict(), orig_dict);
    return true;
}

static inline bool moveToFeatureSet(PyTypeObject *type, int select_id)
{
    /*
     * Rotate the ring to the given `select_id` and return `true`.
     * If not found, stay at the current position and return `false`.
     */
    AutoDecRef tpDict(PepType_GetDict(type));
    auto *initial_dict = tpDict.object();
    auto *dict = initial_dict;
    do {
        int current_id = getSelectId(dict);
        // This works because small numbers are singleton objects.
        if (current_id == select_id) {
            PepType_SetDict(type, dict);
            return true;
        }
        dict = nextInCircle(dict);
    } while (dict != initial_dict);
    PepType_SetDict(type, initial_dict);
    return false;
}

static bool createNewFeatureSet(PyTypeObject *type, int select_id)
{
    /*
     * Create a new feature set.
     * A `false` return value is a fatal error.
     *
     * A FeatureProc sees an empty `type->tp_dict` and the previous dict
     * content in `prev_dict`. It is responsible of filling `type->tp_dict`
     * with modified content.
     */

    bool ok = moveToFeatureSet(type, 0);
    Q_UNUSED(ok);
    assert(ok);

    AutoDecRef prev_dict(PepType_GetDict(type));
    if (!addNewDict(type, select_id))
        return false;
    int id = select_id;
    if (id == -1)
        return false;
    FeatureProc *proc = featurePointer;
    for (int idx = id; *proc != nullptr; ++proc, idx >>= 1) {
        if (idx & 1) {
            // clear the tp_dict that will get new content
            AutoDecRef tpDict(PepType_GetDict(type));
            PyDict_Clear(tpDict);
            // let the proc re-fill the tp_dict
            if (!(*proc)(type, prev_dict, id))
                return false;
            // if there is still a step, prepare `prev_dict`
            if (idx >> 1) {
                prev_dict.reset(PyDict_Copy(tpDict.object()));
                if (prev_dict.isNull())
                    return false;
            }
        }
    }
    return true;
}

static inline void SelectFeatureSetSubtype(PyTypeObject *type, int select_id)
{
    /*
     * This is the selector for one sublass. We need to call this for
     * every subclass until no more subclasses or reaching the wanted id.
     */
    static auto *pyTypeType_tp_dict = PepType_GetDict(&PyType_Type);
    AutoDecRef tpDict(PepType_GetDict(type));
    if (Py_TYPE(tpDict.object()) == Py_TYPE(pyTypeType_tp_dict)) {
        // On first touch, we initialize the dynamic naming.
        // The dict type will be replaced after the first call.
        if (!replaceClassDict(type)) {
            Py_FatalError("libshiboken: failed to replace class dict!");
            return;
        }
    }
    if (!moveToFeatureSet(type, select_id)) {
        if (!createNewFeatureSet(type, select_id)) {
            PyErr_Print();
            Py_FatalError("libshiboken: failed to create a new feature set!");
            return;
        }
    }
 }

static inline int getFeatureSelectId()
{
    static auto *undef = PyLong_FromLong(-1);

    auto *libGlobals = PySide::globals();
    PyObject *&feature_dict = PySide::globals()->featureDict;
    if (feature_dict == nullptr)
        feature_dict = GetFeatureDict();
    PyObject *&cached_globals = libGlobals->cachedFeatureGlobals;
    int &last_select_id = libGlobals->lastSelectedFeatureId;

    Shiboken::AutoDecRef globals(PepEval_GetFrameGlobals());
    if (globals.isNull() || globals.object() == cached_globals)
        return last_select_id;

    auto *modname = PyDict_GetItem(globals.object(), PyMagicName::name());
    if (modname == nullptr)
        return last_select_id;

    auto *py_select_id = PyDict_GetItem(feature_dict, modname);
    if (py_select_id == nullptr
        || !PyLong_Check(py_select_id)
        || py_select_id == undef)
        return last_select_id;

    cached_globals = globals;
    last_select_id = PyLong_AsLong(py_select_id) & 0xff;
    return last_select_id;
}

static inline void SelectFeatureSet(PyTypeObject *type)
{
    /*
     * This is the main function of the module.
     * The purpose of this function is to switch the dict of a class right
     * before a (get|set)attro call is performed.
     *
     * Generated functions call this directly.
     * Shiboken will assign it via a public hook of `basewrapper.cpp`.
     */
    static auto *pyTypeType_tp_dict = PepType_GetDict(&PyType_Type);
    AutoDecRef tpDict(PepType_GetDict(type));
    if (Py_TYPE(tpDict.object()) == Py_TYPE(pyTypeType_tp_dict)) {
        // We initialize the dynamic features by using our own dict type.
        if (!replaceClassDict(type)) {
            Py_FatalError("libshiboken: failed to replace class dict!");
            return;
        }
    }

    int select_id = getFeatureSelectId();
    static int last_select_id{};
    PyTypeObject *&last_type = PySide::globals()->lastFeatureType;

    // PYSIDE-2029: Implement a very simple but effective cache that cannot fail.
    if (type == last_type && select_id == last_select_id)
        return;
    last_type = type;
    last_select_id = select_id;

    // We leave 'Shiboken.Object' and 'object' alone, therefore "n - 2".
    for (PyTypeObject *sub_type : Shiboken::PyTupleTypeRange(type->tp_mro).chop(2))
        SelectFeatureSetSubtype(sub_type, select_id);
    // PYSIDE-1436: Clear all caches for the type and subtypes.
    PyType_Modified(type);
}

// For cppgenerator:
void Select(PyObject *obj)
{
    if (featurePointer == nullptr)
        return;
    auto *type = Py_TYPE(obj);
    SelectFeatureSet(type);
}

void Select(PyTypeObject *type)
{
    if (featurePointer != nullptr)
        SelectFeatureSet(type);
}

static bool feature_01_addLowerNames(PyTypeObject *type, PyObject *prev_dict, int id);
static bool feature_02_true_property(PyTypeObject *type, PyObject *prev_dict, int id);
static bool feature_04_addDummyNames(PyTypeObject *type, PyObject *prev_dict, int id);
static bool feature_08_addDummyNames(PyTypeObject *type, PyObject *prev_dict, int id);
static bool feature_10_addDummyNames(PyTypeObject *type, PyObject *prev_dict, int id);
static bool feature_20_addDummyNames(PyTypeObject *type, PyObject *prev_dict, int id);
static bool feature_40_addDummyNames(PyTypeObject *type, PyObject *prev_dict, int id);
static bool feature_80_addDummyNames(PyTypeObject *type, PyObject *prev_dict, int id);

static FeatureProc featureProcArray[] = {
    feature_01_addLowerNames,
    feature_02_true_property,
    feature_04_addDummyNames,
    feature_08_addDummyNames,
    feature_10_addDummyNames,
    feature_20_addDummyNames,
    feature_40_addDummyNames,
    feature_80_addDummyNames,
    nullptr
};

static bool patch_property_impl();
static bool is_initialized = false;

static void featureEnableCallback(bool enable)
{
    featurePointer = enable ? featureProcArray : nullptr;
}

void init()
{
    // This function can be called multiple times.
    if (!is_initialized) {
        featurePointer = featureProcArray;
        initSelectableFeature(SelectFeatureSet);
        setSelectableFeatureCallback(featureEnableCallback);
        patch_property_impl();
        is_initialized = true;
    }

    // Reset the cache. This is called at any "from __feature__ import".
    auto *globals = PySide::globals();
    globals->lastSelectedFeatureId = 0;
    globals->cachedFeatureGlobals = nullptr;
}

void Enable(bool enable)
{
    if (!is_initialized)
        return;
    featurePointer = enable ? featureProcArray : nullptr;
    initSelectableFeature(enable ? SelectFeatureSet : nullptr);
}

//////////////////////////////////////////////////////////////////////////////
//
// PYSIDE-1019: Support switchable extensions
//
//     Feature 0x01: Allow snake_case instead of camelCase
//
// This functionality is no longer implemented in the signature module, since
// the PyCFunction getsets do not have to be modified any longer.
// Instead, we simply exchange the complete class dicts. This is done in the
// basewrapper.cpp file.
//

static PyObject *methodWithNewName(PyTypeObject *type,
                                   PyMethodDef *meth,
                                   const char *new_name)
{
    /*
     * Create a method with a lower case name.
     */
    auto *obtype = reinterpret_cast<PyObject *>(type);
    const auto len = std::strlen(new_name);
    auto *name = new char[len + 1];
    std::strcpy(name, new_name);
    auto *new_meth = new PyMethodDef;
    new_meth->ml_name = name;
    new_meth->ml_meth = meth->ml_meth;
    new_meth->ml_flags = meth->ml_flags;
    new_meth->ml_doc = meth->ml_doc;
    if ((new_meth->ml_flags & METH_STATIC) != 0) {
        AutoDecRef cfunc(PyCFunction_NewEx(new_meth, obtype, nullptr));
        return cfunc.isNull() ? nullptr : PyStaticMethod_New(cfunc);
    }
    if ((new_meth->ml_flags & METH_CLASS) != 0)
        return PyDescr_NewClassMethod(type, new_meth);
    return PyDescr_NewMethod(type, new_meth);
}

static bool feature_01_addLowerNames(PyTypeObject *type, PyObject *prev_dict, int /* id */)
{
    PyMethodDef *meth = type->tp_methods;
    AutoDecRef tpDict(PepType_GetDict(type));
    PyObject *lower_dict = tpDict.object();

    // PYSIDE-1702: A user-defined class in Python has no internal method list.
    //              We are not going to change anything.
    if (!meth)
        return PyDict_Update(lower_dict, prev_dict) >= 0;

    /*
     * Add objects with lower names to `type->tp_dict` from 'prev_dict`.
     */
    // We first copy the things over which will not be changed:
    for (auto [key, value] : Shiboken::PyDictRange(prev_dict)) {
        if (Py_TYPE(value) != PepMethodDescr_TypePtr
            && Py_TYPE(value) != PepStaticMethod_TypePtr) {
            if (PyDict_SetItem(lower_dict, key, value))
                return false;
            continue;
        }
    }

    // Then we walk over the tp_methods to get all methods and insert
    // them with changed names.

    for (; meth != nullptr && meth->ml_name != nullptr; ++meth) {
        PyObject *oldMethod = prev_dict ? PyDict_GetItemString(prev_dict, meth->ml_name) : nullptr;
        const char *newName = String::toCString(String::getSnakeCaseName(meth->ml_name, true));
        if (oldMethod != nullptr && PyCallable_Check(oldMethod) != 0) {
            if (PyDict_SetItemString(lower_dict, newName, oldMethod) < 0)
                return false;
        } else {
            // Code path appears to be obsolete?
            AutoDecRef new_method(methodWithNewName(type, meth, newName));
            if (new_method.isNull())
                return false;
            if (PyDict_SetItemString(lower_dict, newName, new_method) < 0)
                return false;
        }
    }
    return true;
}

//////////////////////////////////////////////////////////////////////////////
//
// PYSIDE-1019: Support switchable extensions
//
//     Feature 0x02: Use true properties instead of getters and setters
//

// This is the Python 2 version for inspection of m_ml, only.
// The actual Python 3 version is larget.

struct PyCFunctionObject {
    PyObject_HEAD
    PyMethodDef *m_ml; /* Description of the C function to call */
    PyObject    *m_self; /* Passed as 'self' arg to the C func, can be NULL */
    PyObject    *m_module; /* The __module__ attribute, can be anything */
};

static PyObject *modifyStaticToClassMethod(PyTypeObject *type, PyObject *sm)
{
    AutoDecRef func_ob(PyObject_GetAttr(sm, PyMagicName::func()));
    if (func_ob.isNull())
        return nullptr;
    auto *func = reinterpret_cast<PyCFunctionObject *>(func_ob.object());
    auto *new_func = new PyMethodDef;
    new_func->ml_name = func->m_ml->ml_name;
    new_func->ml_meth = func->m_ml->ml_meth;
    new_func->ml_flags = (func->m_ml->ml_flags & ~METH_STATIC) | METH_CLASS;
    new_func->ml_doc = func->m_ml->ml_doc;
    PyCFunction_NewEx(new_func, nullptr, nullptr);
    return PyDescr_NewClassMethod(type, new_func);
}

static PyObject *createProperty(PyTypeObject *type, PyObject *getter, PyObject *setter)
{
    assert(getter != nullptr);
    if (setter == nullptr)
        setter = Py_None;
    auto *ptype = &PyProperty_Type;
    if (Py_TYPE(getter) == PepStaticMethod_TypePtr) {
        ptype = PyClassProperty_TypeF();
        getter = modifyStaticToClassMethod(type, getter);
        if (setter != Py_None)
            setter = modifyStaticToClassMethod(type, setter);
    }
    auto *obtype = reinterpret_cast<PyObject *>(ptype);
    PyObject *prop = PyObject_CallFunctionObjArgs(obtype, getter, setter, nullptr);
    return prop;
}

static QByteArrayList parseFields(const char *propStr, bool *stdWrite)
{
    /*
     * Break the string into subfields at ':' and add defaults.
     */
    if (stdWrite)
        *stdWrite = true;
    QByteArray s = QByteArray(propStr);
    auto list = s.split(u':');
    assert(list.size() == 2 || list.size() == 3);
    auto name = list[0];
    auto read = list[1];
    if (read.isEmpty())
        list[1] = name;
    if (list.size() == 2)
        return list;
    auto write = list[2];
    if (stdWrite)
        *stdWrite = write.isEmpty();
    if (write.isEmpty()) {
        list[2] = "set" + name;
        list[2][3] = std::toupper(list[2][3]);
    }
    return list;
}

static PyObject *make_snake_case(const QByteArray &s, bool lower)
{
    if (s.isNull())
        return nullptr;
    return String::getSnakeCaseName(s.constData(), lower);
}

PyObject *adjustPropertyName(PyObject *dict, PyObject *name)
{
    // PYSIDE-1670: If this is a function with multiple arity or with
    // parameters, we use a mangled name for the property.
    PyObject *existing = PyDict_GetItem(dict, name);    // borrowed
    if (existing) {
        Shiboken::AutoDecRef sig(get_signature_intern(existing, nullptr));
        if (sig.object()) {
            bool name_clash = false;
            if (PyList_CheckExact(sig)) {
                name_clash = true;
            } else {
                Shiboken::AutoDecRef params(PyObject_GetAttr(sig, PySideName::parameters()));
                // Are there parameters except self or cls?
                if (PyObject_Size(params.object()) > 1)
                    name_clash = true;
            }
            if (name_clash) {
                // PyPy has no PyUnicode_AppendAndDel function, yet
                Shiboken::AutoDecRef hold(name);
                Shiboken::AutoDecRef under(Py_BuildValue("s", "_"));
                name = PyUnicode_Concat(hold, under);
            }
        }
    }
    return name;
}

static QByteArrayList GetPropertyStringsMro(PyTypeObject *type)
{
    /*
     * PYSIDE-2042: There are possibly more methods which should become properties,
     *              because the wrapping process does not obey inheritance.
     *              Therefore, we need to walk the mro to find property strings.
     */
    auto res = QByteArrayList();

    // We leave 'Shiboken.Object' and 'object' alone, therefore "n - 2".
    for (PyTypeObject *subType : Shiboken::PyTupleTypeRange(type->tp_mro).chop(2)) {
        auto *props = SbkObjectType_GetPropertyStrings(subType);
        if (props != nullptr)
            for (; *props != nullptr; ++props)
                res << QByteArray(*props);
    }
    return res;
}

static bool feature_02_true_property(PyTypeObject *type, PyObject *prev_dict, int id)
{
    /*
     * Use the property info to create true Python property objects.
     */

    PyMethodDef *meth = type->tp_methods;
    AutoDecRef tpDict(PepType_GetDict(type));
    PyObject *prop_dict = tpDict.object();

    // The empty `tp_dict` gets populated by the previous dict.
    if (PyDict_Update(prop_dict, prev_dict) < 0)
        return false;

    // PYSIDE-1702: A user-defined class in Python has no internal method list.
    //              We are not going to change anything.
    if (!meth)
        return true;

    // For speed, we establish a helper dict that maps the removed property
    // method names to property name.
    PyObject *prop_methods = PyDict_GetItem(prop_dict, PyMagicName::property_methods());
    if (prop_methods == nullptr) {
        prop_methods = PyDict_New();
        if (prop_methods == nullptr
            || PyDict_SetItem(prop_dict, PyMagicName::property_methods(), prop_methods))
            return false;
    }
    // We then replace methods by properties.
    bool lower = (id & 0x01) != 0;
    auto props = GetPropertyStringsMro(type);
    if (props.isEmpty())
        return true;

    for (const auto &propStr : std::as_const(props)) {
        bool isStdWrite{};
        auto fields = parseFields(propStr, &isStdWrite);
        bool haveWrite = fields.size() == 3;
        PyObject *name = make_snake_case(fields[0], lower);
        PyObject *read = make_snake_case(fields[1], lower);
        PyObject *write = haveWrite ? make_snake_case(fields[2], lower) : nullptr;
        PyObject *getter = PyDict_GetItem(prev_dict, read);
        if (getter == nullptr || !(Py_TYPE(getter) == PepMethodDescr_TypePtr ||
                                   Py_TYPE(getter) == PepStaticMethod_TypePtr))
            continue;
        PyObject *setter = haveWrite ? PyDict_GetItem(prev_dict, write) : nullptr;

        // PYSIDE-1670: If multiple arities exist as a property name, rename it.
        name = adjustPropertyName(prop_dict, name);

        AutoDecRef PyProperty(createProperty(type, getter, setter));
        if (PyProperty.isNull())
            return false;
        if (PyDict_SetItem(prop_dict, name, PyProperty) < 0
            || PyDict_SetItem(prop_methods, read, name) < 0
            || (setter != nullptr && PyDict_SetItem(prop_methods, write, name) < 0))
            return false;
        if (fields[0] != fields[1] && PyDict_GetItem(prop_dict, read))
            if (PyDict_DelItem(prop_dict, read) < 0)
                return false;
        // Theoretically, we need to check for multiple signatures to be exact.
        // But we don't do so intentionally because it would be confusing.
        if (haveWrite && PyDict_GetItem(prop_dict, write) && isStdWrite) {
            if (PyDict_DelItem(prop_dict, write) < 0)
                return false;
        }
    }
    return true;
}

//////////////////////////////////////////////////////////////////////////////
//
// These are a number of patches to make Python's property object better
// suitable for us.
// We turn `__doc__` into a lazy attribute saving signature initialization.
//
// There is now also a class property implementation which inherits
// from this one.
//

static PyObject *property_doc_get(PyObject *self, void *)
{
    auto *po = reinterpret_cast<propertyobject *>(self);

    if (po->prop_doc != nullptr && po->prop_doc != Py_None) {
        Py_INCREF(po->prop_doc);
        return po->prop_doc;
    }
    if (po->prop_get) {
        // PYSIDE-1019: Fetch the default `__doc__` from fget. We do it late.
        auto *txt = PyObject_GetAttr(po->prop_get, PyMagicName::doc());
        if (txt != nullptr) {
            Py_INCREF(txt);
            po->prop_doc = txt;
            Py_INCREF(txt);
            return txt;
        }
        PyErr_Clear();
    }
    Py_RETURN_NONE;
}

static int property_doc_set(PyObject *self, PyObject *value, void *)
{
    auto *po = reinterpret_cast<propertyobject *>(self);

    Py_INCREF(value);
    po->prop_doc = value;
    return 0;
}

static PyGetSetDef property_getset[] = {
    // This gets added to the existing getsets
    {"__doc__", property_doc_get, property_doc_set, nullptr, nullptr},
    {nullptr, nullptr, nullptr, nullptr, nullptr}
};

static bool patch_property_impl()
{
    // Turn `__doc__` into a computed attribute without changing writability.
    auto *gsp = property_getset;
    auto *type = &PyProperty_Type;
    AutoDecRef dict(PepType_GetDict(type));
    AutoDecRef descr(PyDescr_NewGetSet(type, gsp));
    if (descr.isNull())
        return false;
    if (PyDict_SetItemString(dict.object(), gsp->name, descr) < 0)
        return false;
    return true;
}

//////////////////////////////////////////////////////////////////////////////
//
// PYSIDE-1019: Support switchable extensions
//
//     Feature 0x04..0x80: A fake switchable option for testing
//

#define SIMILAR_FEATURE(xx)  \
static bool feature_##xx##_addDummyNames(PyTypeObject *type, PyObject *prev_dict, int /* id */) \
{ \
    AutoDecRef tpDict(PepType_GetDict(type)); \
    PyObject *dict = tpDict.object(); \
    if (PyDict_Update(dict, prev_dict) < 0) \
        return false; \
    if (PyDict_SetItemString(dict, "fake_feature_" #xx, Py_None) < 0) \
        return false; \
    return true; \
}

SIMILAR_FEATURE(04)
SIMILAR_FEATURE(08)
SIMILAR_FEATURE(10)
SIMILAR_FEATURE(20)
SIMILAR_FEATURE(40)
SIMILAR_FEATURE(80)

} // namespace PySide::Feature
