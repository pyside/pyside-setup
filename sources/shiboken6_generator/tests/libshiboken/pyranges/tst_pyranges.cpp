// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include <QtTest/QTest>

#include <QtCore/QObject>
#include <QtCore/QDebug>
#include <QtCore/QMap>

#include <sbkpython.h>
#include <sbkpydictrange.h>
#include <sbkpysequencerange.h>
#include <sbkpytuplerange.h>
#include <autodecref.h>
#include <gilstate.h>

#include <algorithm>
#include <vector>

/* Test for the tuple/sequence/dict ranges in libshiboken. The test should ideally be under
 * sources/shiboken6, however, that does not use Qt and thus does not have QTestlib. */
class PyRangesTest : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void cleanupTestCase();
    void testSequenceIterators();
    void testDictIterators();
    void testTupleIterators();
};

void PyRangesTest::initTestCase()
{
    Py_Initialize();
}

void PyRangesTest::cleanupTestCase()
{
    Py_Finalize();
}

// Exercise algorithms with the iterators to ensure operator=(), etc works
// correctly.
void PyRangesTest::testSequenceIterators()
{
    Shiboken::AutoDecRef pyList(PyList_New(4));
    PyList_SetItem(pyList.object(), 0, PyLong_FromLong(1));
    PyList_SetItem(pyList.object(), 1, PyLong_FromLong(2));
    PyList_SetItem(pyList.object(), 2, PyLong_FromLong(3));
    PyList_SetItem(pyList.object(), 3, PyLong_FromLong(4));
    Shiboken::PySequenceRange range(pyList.object());

    std::vector<long> stdVector;
    std::transform(range.begin(), range.end(),
                   std::back_inserter(stdVector), PyLong_AsLong);

    const long sum = std::accumulate(stdVector.begin(), stdVector.end(), 0);
    QCOMPARE(sum, 10);
}

void PyRangesTest::testTupleIterators()
{
    Shiboken::AutoDecRef pyTuple(PyTuple_Pack(3, PyLong_FromLong(1), PyLong_FromLong(2),
                                              PyLong_FromLong(3)));

    Shiboken::PyTupleRange range(pyTuple.object());

    std::vector<long> stdVector;
    std::transform(range.begin(), range.end(),
                   std::back_inserter(stdVector), PyLong_AsLong);

    const long sum = std::accumulate(stdVector.begin(), stdVector.end(), 0);
    QCOMPARE(sum, 6);

    Py_ssize_t enumeratedIndexSum = 0;
    long enumeratedSum = 0;
    for (auto [i, value] : Shiboken::PyTupleEnumeratedRange(pyTuple.object())) {
        enumeratedIndexSum += i;
        enumeratedSum += PyLong_AsLong(value);
    }
    QCOMPARE(enumeratedIndexSum, Py_ssize_t(3));
    QCOMPARE(enumeratedSum, 6);
}

void PyRangesTest::testDictIterators()
{
    using TestMap = QMap<long, long>;

    Shiboken::AutoDecRef pyDict(PyDict_New());
    PyDict_SetItem(pyDict.object(), PyLong_FromLong(1), PyLong_FromLong(2));
    PyDict_SetItem(pyDict.object(), PyLong_FromLong(3), PyLong_FromLong(4));
    Shiboken::PyDictRange range(pyDict.object());


    QMap<long, long> qMap;
    for (auto [key, value] : range)
        qMap.insert(PyLong_AsLong(key), PyLong_AsLong(value));

    const TestMap expected{{1, 2}, {3, 4}};
    QCOMPARE(qMap, expected);
}

QTEST_APPLESS_MAIN(PyRangesTest)

#include "tst_pyranges.moc"
