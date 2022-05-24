// Copyright (C) 2016 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#ifndef LIST_H
#define LIST_H

#include <list>
#include "libsamplemacros.h"
#include "point.h"

class ObjectType;

template<class T>
class List : public std::list<T>
{
};

class IntList : public List<int>
{
public:
    enum CtorEnum {
        NoParamsCtor,
        IntCtor,
        CopyCtor,
        ListOfIntCtor
    };

    inline IntList() : m_ctorUsed(NoParamsCtor) {}
    inline explicit IntList(int val) : m_ctorUsed(IntCtor) { push_back(val); }
    inline IntList(const IntList& lst) : List<int>(lst), m_ctorUsed(CopyCtor) {}
    inline IntList(const List<int>& lst) : List<int>(lst), m_ctorUsed(ListOfIntCtor) {}

    inline void append(int v) { insert(end(), v); }
    CtorEnum constructorUsed() { return m_ctorUsed; }
private:
    CtorEnum m_ctorUsed;
};

class PointValueList : public List<Point>
{
public:
    enum CtorEnum {
        NoParamsCtor,
        PointCtor,
        CopyCtor,
        ListOfPointValuesCtor
    };

    inline PointValueList() : m_ctorUsed(NoParamsCtor) {}
    inline explicit PointValueList(Point val) : m_ctorUsed(PointCtor) { push_back(val); }
    inline PointValueList(const PointValueList& lst) : List<Point>(lst), m_ctorUsed(CopyCtor) {}
    inline PointValueList(const List<Point>& lst) : List<Point>(lst), m_ctorUsed(ListOfPointValuesCtor) {}

    inline void append(Point v) { insert(end(), v); }
    CtorEnum constructorUsed() { return m_ctorUsed; }
private:
    CtorEnum m_ctorUsed;
};

class ObjectTypePtrList : public List<ObjectType*>
{
public:
    enum CtorEnum {
        NoParamsCtor,
        ObjectTypeCtor,
        CopyCtor,
        ListOfObjectTypePtrCtor
    };

    inline ObjectTypePtrList() : m_ctorUsed(NoParamsCtor) {}
    inline explicit ObjectTypePtrList(ObjectType* val) : m_ctorUsed(ObjectTypeCtor) { push_back(val); }
    inline ObjectTypePtrList(const ObjectTypePtrList& lst) : List<ObjectType*>(lst), m_ctorUsed(CopyCtor) {}
    inline ObjectTypePtrList(const List<ObjectType*>& lst) : List<ObjectType*>(lst), m_ctorUsed(ListOfObjectTypePtrCtor) {}

    inline void append(ObjectType* v) { insert(end(), v); }
    CtorEnum constructorUsed() { return m_ctorUsed; }
private:
    CtorEnum m_ctorUsed;
};

#endif // LIST_H
