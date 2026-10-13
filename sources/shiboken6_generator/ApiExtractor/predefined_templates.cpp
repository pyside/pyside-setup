// Copyright (C) 2021 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0
// Qt-Security score:significant reason:build-tool

#include "predefined_templates.h"

using namespace Qt::StringLiterals;

static QString pySequenceToCppContainer(const QString &insertFunc,
                                        bool reserve)
{
    QString result = u"(%out).clear();\n"_s;
    if (reserve) {
        result += uR"(if (PyList_Check(%in)) {
    const Py_ssize_t size = PySequence_Size(%in);
    if (size > 10)
        (%out).reserve(size);
}

)"_s;
    }

    result += uR"(Shiboken::AutoDecRef it(PyObject_GetIter(%in));
while (true) {
    Shiboken::AutoDecRef pyItem(PyIter_Next(it.object()));
    if (pyItem.isNull()) {
        if (PyErr_Occurred() && PyErr_ExceptionMatches(PyExc_StopIteration))
            PyErr_Clear();
        break;
    }
    %OUTTYPE_0 cppItem = %CONVERTTOCPP[%OUTTYPE_0](pyItem);
    (%out).)"_s;

    result += insertFunc;
    result += uR"((cppItem);
}
)"_s;
    return result;
}

// Convert a sequence to a limited/fixed array
static QString pySequenceToCppArray()
{
    return uR"(Shiboken::AutoDecRef it(PyObject_GetIter(%in));
for (auto oit = std::begin(%out), oend = std::end(%out); oit != oend; ++oit) {
    Shiboken::AutoDecRef pyItem(PyIter_Next(it.object()));
    if (pyItem.isNull()) {
        if (PyErr_Occurred() && PyErr_ExceptionMatches(PyExc_StopIteration))
            PyErr_Clear();
        break;
    }
    %OUTTYPE_0 cppItem = %CONVERTTOCPP[%OUTTYPE_0](pyItem);
    *oit = cppItem;
}
)"_s;
}

static constexpr auto stlMapKeyAccessor = "->first"_L1;
static constexpr auto stlMapValueAccessor = "->second"_L1;
static constexpr auto qtMapKeyAccessor = ".key()"_L1;
static constexpr auto qtMapValueAccessor = ".value()"_L1;

static QString cppMapToPyDict(bool isQMap)
{
    return uR"(PyObject *%out = PyDict_New();
for (auto it = std::cbegin(%in), end = std::cend(%in); it != end; ++it) {
    const auto &key = it)"_s
        + (isQMap ? qtMapKeyAccessor : stlMapKeyAccessor)
        + uR"(;
    const auto &value = it)"_s
        + (isQMap ? qtMapValueAccessor : stlMapValueAccessor)
        + uR"(;
    PyObject *pyKey = %CONVERTTOPYTHON[%INTYPE_0](key);
    PyObject *pyValue = %CONVERTTOPYTHON[%INTYPE_1](value);
    PyDict_SetItem(%out, pyKey, pyValue);
    Py_DECREF(pyKey);
    Py_DECREF(pyValue);
}
return %out;
)"_s;
}

static QString pyDictToCppMap(bool isQMap)
{
    return uR"(PyObject *key{};
PyObject *value{};
%out.clear();
Py_ssize_t pos = 0;
#ifdef Py_GIL_DISABLED
Shiboken::AutoDecRef dictItems(PepDict_IterationSnapshot(%in));
while (!dictItems.isNull() && PyDict_Next(dictItems.object(), &pos, &key, &value)) {
#else
while (PyDict_Next(%in, &pos, &key, &value)) {
#endif
    %OUTTYPE_0 cppKey = %CONVERTTOCPP[%OUTTYPE_0](key);
    %OUTTYPE_1 cppValue = %CONVERTTOCPP[%OUTTYPE_1](value);
    %out.insert()"_s
    // STL needs a pair
    + (isQMap ? u"cppKey, cppValue"_s : u"{cppKey, cppValue}"_s) + uR"();
}
)"_s;
}

// Convert a STL or Qt multi map to Dict of Lists using upperBound()
static QString cppMultiMapToPyDict(bool isQMultiMap)
{
    return uR"(PyObject *%out = PyDict_New();
    for (auto it = std::cbegin(%in), end = std::cend(%in); it != end; ) {
        const auto &key = it)"_s
        + (isQMultiMap ? qtMapKeyAccessor : stlMapKeyAccessor)
        + uR"(;
        PyObject *pyKey = %CONVERTTOPYTHON[%INTYPE_0](key);
        auto upper = %in.)"_s
       + (isQMultiMap ? u"upperBound"_s : u"upper_bound"_s)
       + uR"((key);
        const auto count = Py_ssize_t(std::distance(it, upper));
        PyObject *pyValues = PyList_New(count);
        Py_ssize_t idx = 0;
        for (; it != upper; ++it, ++idx) {
            const auto &cppItem = it.value();
            PyList_SetItem(pyValues, idx, %CONVERTTOPYTHON[%INTYPE_1](cppItem));
        }
        PyDict_SetItem(%out, pyKey, pyValues);
        Py_DECREF(pyKey);
    }
    return %out;
)"_s;
}

// Convert a STL or Qt multi hash to Dict of Lists using equalRange()
static QString cppMultiHashToPyDict(bool isQMultiHash)
{
    return uR"(PyObject *%out = PyDict_New();
    for (auto it = std::cbegin(%in), end = std::cend(%in); it != end; ) {
        const auto &key = it)"_s
           + (isQMultiHash ? qtMapKeyAccessor : stlMapKeyAccessor)
           + uR"(;
        PyObject *pyKey = %CONVERTTOPYTHON[%INTYPE_0](key);
        auto range = %in.equal_range(key);
        const auto count = Py_ssize_t(std::distance(range.first, range.second));
        PyObject *pyValues = PyList_New(count);
        Py_ssize_t idx = 0;
        for (; it != range.second; ++it, ++idx) {
            const auto &cppItem = it.value();
            PyList_SetItem(pyValues, idx, %CONVERTTOPYTHON[%INTYPE_1](cppItem));
        }
        PyDict_SetItem(%out, pyKey, pyValues);
        Py_DECREF(pyKey);
    }
    return %out;
)"_s;
}

// Convert Dict of Lists to a STL or Qt multi hash/map
static QString pyDictToCppMultiHash(bool isQMultiHash)
{
    return uR"(PyObject *key{};
    PyObject *values{};
    %out.clear();
    Py_ssize_t pos = 0;
#ifdef Py_GIL_DISABLED
    Shiboken::AutoDecRef dictItems(PepDict_IterationSnapshot(%in));
    while (!dictItems.isNull() && PyDict_Next(dictItems.object(), &pos, &key, &values)) {
#else
    while (PyDict_Next(%in, &pos, &key, &values)) {
#endif
        %OUTTYPE_0 cppKey = %CONVERTTOCPP[%OUTTYPE_0](key);
        const Py_ssize_t size = PySequence_Size(values);
        for (Py_ssize_t i = 0; i < size; ++i) {
            Shiboken::AutoDecRef value(PySequence_GetItem(values, i));
            %OUTTYPE_1 cppValue = %CONVERTTOCPP[%OUTTYPE_1](value);
            %out.insert()"_s
        + (isQMultiHash ? u"cppKey, cppValue"_s : u"{cppKey, cppValue}"_s)
        + uR"();
        }
    }
)"_s;
}

const PredefinedTemplates &predefinedTemplates()
{
    static const PredefinedTemplates result{
    {u"shiboken_conversion_pylong_to_cpp"_s,
     u"%out = %OUTTYPE(PyLong_AsLong(%in));\n"_s},

    // QPair/std::pair
    {u"shiboken_conversion_pysequence_to_cpppair"_s,
     uR"(Shiboken::AutoDecRef objfirst(PySequence_GetItem(%in, 0));
Shiboken::AutoDecRef objsecond(PySequence_GetItem(%in, 1));
%out.first = %CONVERTTOCPP[%OUTTYPE_0](objfirst);
%out.second = %CONVERTTOCPP[%OUTTYPE_1](objsecond);
)"_s},

    {u"shiboken_conversion_cpppair_to_pytuple"_s,
    uR"(PyObject *%out = PyTuple_New(2);
PyTuple_SetItem(%out, 0, %CONVERTTOPYTHON[%INTYPE_0](%in.first));
PyTuple_SetItem(%out, 1, %CONVERTTOPYTHON[%INTYPE_1](%in.second));
return %out;
)"_s},

    // Sequential containers
    {u"shiboken_conversion_cppsequence_to_pylist"_s,
     uR"(PyObject *%out = PyList_New(Py_ssize_t(%in.size()));
Py_ssize_t idx = 0;
for (auto it = std::cbegin(%in), end = std::cend(%in); it != end; ++it, ++idx) {
    ShibokenContainerStdVectorValueType<%INTYPE_0>::Type cppItem = *it;
    PyList_SetItem(%out, idx, %CONVERTTOPYTHON[%INTYPE_0](cppItem));
}
return %out;)"_s},

    // PySet
    {u"shiboken_conversion_cppsequence_to_pyset"_s,
     uR"(PyObject *%out = PySet_New(nullptr);
for (const auto &cppItem : %in) {
    PySet_Add(%out, %CONVERTTOPYTHON[%INTYPE_0](cppItem));
}
return %out;)"_s},

    {u"shiboken_conversion_pyiterable_to_cppsequentialcontainer"_s,
     pySequenceToCppContainer(u"push_back"_s, false)},
    {u"shiboken_conversion_pyiterable_to_cppsequentialcontainer_reserve"_s,
     pySequenceToCppContainer(u"push_back"_s, true)},
    {u"shiboken_conversion_pyiterable_to_cpparray"_s,
     pySequenceToCppArray()},
    {u"shiboken_conversion_pyiterable_to_cppsetcontainer"_s,
     pySequenceToCppContainer(u"insert"_s, false)},

    // Maps
    {u"shiboken_conversion_stdmap_to_pydict"_s,
     cppMapToPyDict(false)},
    {u"shiboken_conversion_qmap_to_pydict"_s,
     cppMapToPyDict(true)},
    {u"shiboken_conversion_pydict_to_stdmap"_s,
     pyDictToCppMap(false)},
    {u"shiboken_conversion_pydict_to_qmap"_s,
     pyDictToCppMap(true)},

    // Multi maps
    {u"shiboken_conversion_stdmultimap_to_pydict"_s,
     cppMultiMapToPyDict(false)},
    {u"shiboken_conversion_qmultimap_to_pydict"_s,
     cppMultiMapToPyDict(true)},

    // Multi hashes
    {u"shiboken_conversion_stdunorderedmultimap_to_pydict"_s,
     cppMultiHashToPyDict(false)},
    {u"shiboken_conversion_qmultihash_to_pydict"_s,
     cppMultiHashToPyDict(true)},

    // STL multi hash/map
    {u"shiboken_conversion_pydict_to_stdmultimap"_s,
     pyDictToCppMultiHash(false)},
    {u"shiboken_conversion_pydict_to_qmultihash"_s,
     pyDictToCppMultiHash(true)}
    };

    return result;
}

QByteArray containerTypeSystemSnippet(const char *name, const char *type,
                                      const char *include,
                                      const char *nativeToTarget,
                                      const char *targetToNativeType,
                                      const char *targetToNative)
{
    QByteArray result = "<container-type name=\""_ba
            + name + "\" type=\""_ba + type + R"(">
    <include file-name=")" + include + R"(" location="global"/>
    <conversion-rule>
        <native-to-target>
            <insert-template name=")" + nativeToTarget + R"("/>
        </native-to-target>
)";
    if (targetToNativeType != nullptr) {
        result += R"(        <target-to-native>
            <add-conversion type=")"_ba + targetToNativeType
                + R"(">
                <insert-template name=")"_ba + targetToNative + R"("/>
            </add-conversion>
        </target-to-native>
)"_ba;
        }
result += R"(    </conversion-rule>
</container-type>
)"_ba;
    return result;
}
