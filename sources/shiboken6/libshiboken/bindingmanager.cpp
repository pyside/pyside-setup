// Copyright (C) 2016 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR LGPL-3.0-only OR GPL-2.0-only OR GPL-3.0-only
// Qt-Security score:significant reason:default

#include "bindingmanager.h"
#ifdef Py_GIL_DISABLED
#  include "sbkfailpoint.h"
#  include "sbkftoptions.h"
#  include "sbkheldlocks.h"
#endif

#include "autodecref.h"
#include "basewrapper.h"
#include "basewrapper_p.h"
#include "helper.h"
#include "sbkfeature_base.h"
#include "sbkmodule.h"
#include "sbkpep.h"
#include "sbkpytuplerange.h"
#include "sbkstaticstrings.h"
#include "sbkstring.h"

#include <algorithm>
#include <cstddef>
#include <cstring>
#include <fstream>
#include <iostream>
#include <mutex>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#ifdef Py_GIL_DISABLED
#  include <vector>
#endif

// GraphNode for the dependency graph. It keeps a pointer to
// the TypeInitStruct to be able to lazily create the type and hashes
// by the full type name.
struct GraphNode
{
    explicit GraphNode(Shiboken::Module::TypeInitStruct *i) : name(i->fullName), initStruct(i) {}
    explicit GraphNode(const char *n) : name(n), initStruct(nullptr) {} // Only for searching

    std::string_view name;
    Shiboken::Module::TypeInitStruct *initStruct;

    friend bool operator==(const GraphNode &n1, const GraphNode &n2) { return n1.name == n2.name; }
    friend bool operator!=(const GraphNode &n1, const GraphNode &n2) { return n1.name != n2.name; }
};

template <>
struct std::hash<GraphNode> {
    size_t operator()(const GraphNode &n) const noexcept
    {
        return std::hash<std::string_view>{}(n.name);
    }
};

namespace Shiboken
{

#ifdef Py_GIL_DISABLED
// The map holds borrowed references, or every wrapper would stay alive.
// A lookup upgrades with PyUnstable_TryIncRef(), which does not revive a
// wrapper whose count another thread just took to zero.
class WrapperEntry
{
public:
    explicit WrapperEntry(SbkObject *obj) : m_obj(obj)
    {
        // Without it, TryIncRef fails for objects no other thread touched yet.
        PyUnstable_EnableTryIncRef(reinterpret_cast<PyObject *>(obj));
    }

    SbkObject *borrowed() const { return m_obj; }

    // A new reference, or nullptr while the wrapper is deallocated.
    SbkObject *acquire() const
    {
        auto *obj = reinterpret_cast<PyObject *>(m_obj);
        if (!FreeThreading::optionEnabled(FreeThreading::Option::OwnedWrapperLookup)) {
            // Bit cleared: as in the base.
            Py_INCREF(obj);
            return m_obj;
        }
        return PyUnstable_TryIncRef(obj) != 0 ? m_obj : nullptr;
    }

private:
    SbkObject *m_obj;
};

using WrapperMap = std::unordered_multimap<const void *, WrapperEntry>;

using WrapperMapGuard = TrackedGuard<std::recursive_mutex, RawLock::WrapperMap>;
using MainThreadDeleteGuard = TrackedGuard<std::mutex, RawLock::MainThreadDelete>;
#else // Py_GIL_DISABLED
// Mapping of C++ address to wrapper. We use a multimap to allow for co-located
// objects, which happens for example for the first field of a struct.
using WrapperMap = std::unordered_multimap<const void *, SbkObject *>;
#endif // Py_GIL_DISABLED

template <class NodeType>
class BaseGraph
{
public:
    using NodeList = std::vector<NodeType>;
    using NodeSet = std::unordered_set<NodeType>;

    using Edges = std::unordered_map<NodeType, NodeList>;

    Edges m_edges;

    BaseGraph() = default;

    void addEdge(NodeType from, NodeType to)
    {
        m_edges[from].push_back(to);
    }

    NodeSet nodeSet() const
    {
        NodeSet result;
        for (const auto &p : m_edges) {
            result.insert(p.first);
            for (const auto node2 : p.second)
                result.insert(node2);
        }
        return result;
    }
};

class Graph : public BaseGraph<GraphNode>
{
public:
    using TypeCptrPair = BindingManager::TypeCptrPair;

    // Find derived class by trying to find a child node that accepts the base class in
    // its type discovery function, return type and cptr cast to derived class.
    TypeCptrPair identifyType(void *cptr, PyTypeObject *type, PyTypeObject *baseType) const
    {
        return identifyType(cptr, GraphNode(type->tp_name), type, baseType);
    }

    bool dumpTypeGraph(const char *fileName) const;

private:
    TypeCptrPair identifyType(void *cptr, GraphNode typeNode, PyTypeObject *type,
                              PyTypeObject *baseType) const;
};

Graph::TypeCptrPair Graph::identifyType(void *cptr,
                                        GraphNode typeNode, PyTypeObject *type,
                                        PyTypeObject *baseType) const
{
    assert(typeNode.initStruct != nullptr || type != nullptr);
    auto edgesIt = m_edges.find(typeNode);
    if (edgesIt != m_edges.end()) {
        typeNode = edgesIt->first;
        const NodeList &adjNodes = edgesIt->second;
        for (const auto &node : adjNodes) {
            auto newType = identifyType(cptr, node, nullptr, baseType);
            if (newType.first != nullptr)
                return newType;
        }
    }

    if (type == nullptr) {
        if (typeNode.initStruct->type == nullptr) // Layzily create type
            type = Shiboken::Module::get(*typeNode.initStruct);
        else
            type = typeNode.initStruct->type;
    }

    if (void *derivedCPtr = BindingManager::runTypeDiscovery(cptr, type, baseType))
        return {type, derivedCPtr};
    return {nullptr, nullptr};
}

static void formatDotNode(std::string_view name, std::ostream &file)
{
    auto lastDot = name.rfind('.');
    file << "    \"" << name << "\" [ label=";
    if (lastDot != std::string::npos) {
        file << '"' << name.substr(lastDot + 1) << "\" tooltip=\""
             << name.substr(0, lastDot) << '"';
    } else {
        file << '"' << name << '"';
    }
    file << " ]\n";
}

bool Graph::dumpTypeGraph(const char *fileName) const
{
    std::ofstream file(fileName);
    if (!file.good())
        return false;

    file << "digraph D {\n";

    // Define nodes with short names
    for (const auto &node : nodeSet())
        formatDotNode(node.name, file);

    // Write edges
    for (const auto &p : m_edges) {
        const auto &node1 = p.first;
        const NodeList &nodeList = p.second;
        for (const auto &node2 : nodeList)
            file << "    \"" << node2.name << "\" -> \"" << node1.name << "\"\n";
    }
    file << "}\n";
    return true;
}

struct BindingManager::BindingManagerPrivate {
    using DestructorEntries = std::vector<DestructorEntry>;

    WrapperMap wrapperMapper;
    // Guard wrapperMapper mainly for QML which calls into the generated
    // QObject::metaObject() and elsewhere from threads without GIL, causing
    // crashes for example in retrieveWrapper(). std::shared_mutex was rejected due to:
    // https://stackoverflow.com/questions/50972345/when-is-stdshared-timed-mutex-slower-than-stdmutex-and-when-not-to-use-it
    std::recursive_mutex wrapperMapLock;
    Graph classHierarchy;
    DestructorEntries deleteInMainThread;
#ifdef Py_GIL_DISABLED
    // Filled by every thread that deallocates a delete-in-main-thread wrapper.
    std::mutex deleteInMainThreadLock;
#endif

    WrapperMap::const_iterator findSbkObject(const void *cptr, SbkObject *wrapper) const;
    WrapperMap::const_iterator findByType(const void *cptr, PyTypeObject *desiredType) const;

    bool releaseWrapper(void *cptr, SbkObject *wrapper, const int *bases = nullptr);
    bool releaseWrapperHelper(void *cptr, SbkObject *wrapper);

    void assignWrapper(SbkObject *wrapper, const void *cptr, const int *bases = nullptr);
    void assignWrapperHelper(SbkObject *wrapper, const void *cptr);
};

// Find wrapper map entry by Python instance
WrapperMap::const_iterator
    BindingManager::BindingManagerPrivate::findSbkObject(const void *cptr,
                                                         SbkObject *wrapper) const
{
    const auto end = wrapperMapper.cend();
    auto it = wrapperMapper.find(cptr);
    for (; it != end && it->first == cptr; ++it) {
#ifdef Py_GIL_DISABLED
        if (it->second.borrowed() == wrapper)
#else
        if (it->second == wrapper)
#endif
            return it;
    }
    return end;
}

// Find wrapper map entry by Python type
WrapperMap::const_iterator
    BindingManager::BindingManagerPrivate::findByType(const void *cptr,
                                                      PyTypeObject *desiredType) const
{
    const auto end = wrapperMapper.cend();
    auto it = wrapperMapper.find(cptr);
    for (; it != end && it->first == cptr; ++it) {
#ifdef Py_GIL_DISABLED
        auto *foundType = Py_TYPE(reinterpret_cast<PyObject *>(it->second.borrowed()));
#else
        auto *foundType = Py_TYPE(reinterpret_cast<PyObject *>(it->second));
#endif
        if (foundType == desiredType || PyType_IsSubtype(foundType, desiredType) != 0)
            return it;
    }
    return end;
}

bool BindingManager::BindingManagerPrivate::releaseWrapperHelper(void *cptr, SbkObject *wrapper)
{
    // The wrapper argument is checked to ensure that the correct wrapper is released.
    // Returns true if the correct wrapper is found and released.
    // If wrapper argument is NULL, no such check is performed.
    const auto it = wrapper != nullptr ? findSbkObject(cptr, wrapper) : wrapperMapper.find(cptr);
    if (it != wrapperMapper.cend()) {
        wrapperMapper.erase(it);
        return true;
    }
    return false;
}

bool BindingManager::BindingManagerPrivate::releaseWrapper(void *cptr, SbkObject *wrapper,
                                                           const int *bases)
{
    assert(cptr);
#ifdef Py_GIL_DISABLED
    WrapperMapGuard guard(wrapperMapLock);
#else
    std::lock_guard<std::recursive_mutex> guard(wrapperMapLock);
#endif
    const bool result = releaseWrapperHelper(cptr, wrapper);
    if (bases != nullptr) {
        auto *base = static_cast<uint8_t *>(cptr);
        for (const auto *offset = bases; *offset != -1; ++offset)
            releaseWrapperHelper(base + *offset, wrapper);
    }
    return result;
}

inline void BindingManager::BindingManagerPrivate::assignWrapperHelper(SbkObject *wrapper,
                                                                       const void *cptr)
{
    const auto it = findSbkObject(cptr, wrapper);
    if (it == wrapperMapper.cend())
#ifdef Py_GIL_DISABLED
        wrapperMapper.insert(std::make_pair(cptr, WrapperEntry(wrapper)));
#else
        wrapperMapper.insert(std::make_pair(cptr, wrapper));
#endif
}

void BindingManager::BindingManagerPrivate::assignWrapper(SbkObject *wrapper, const void *cptr,
                                                          const int *bases)
{
    assert(cptr);
#ifdef Py_GIL_DISABLED
    WrapperMapGuard guard(wrapperMapLock);
#else
    std::lock_guard<std::recursive_mutex> guard(wrapperMapLock);
#endif
    assignWrapperHelper(wrapper, cptr);
    if (bases != nullptr) {
        const auto *base = static_cast<const uint8_t *>(cptr);
        for (const auto *offset = bases; *offset != -1; ++offset)
            assignWrapperHelper(wrapper, base + *offset);
    }
}

/*!
    \class Shiboken::BindingManager

    \brief Maintains a set of wrappers.

    \inmodule libshiboken
*/
BindingManager::BindingManager() :
    m_d(new BindingManager::BindingManagerPrivate)
{
#ifdef SHIBOKEN_INSTALL_FREE_DEBUG_HOOK
    debugInstallFreeHook();
#endif
}

BindingManager::~BindingManager()
{
#ifdef SHIBOKEN_INSTALL_FREE_DEBUG_HOOK
    debugRemoveFreeHook();
#endif
#ifndef NDEBUG
    if (Shiboken::pyVerbose() > 0)
        dumpWrapperMap();
#endif
    /* Cleanup hanging references. We just invalidate them as when
     * the BindingManager is being destroyed the interpreter is alredy
     * shutting down. */
    if (Py_IsInitialized()) {  // ensure the interpreter is still valid
#ifdef Py_GIL_DISABLED
        // Untracked, as in the base: destroy() decrefs, which tracking forbids
        // under a raw lock; this is static destruction.
        std::lock_guard<std::recursive_mutex> guard(m_d->wrapperMapLock);
        while (!m_d->wrapperMapper.empty()) {
            Object::destroy(m_d->wrapperMapper.begin()->second.borrowed(), const_cast<void *>(m_d->wrapperMapper.begin()->first));
        }
#else
        std::lock_guard<std::recursive_mutex> guard(m_d->wrapperMapLock);
        while (!m_d->wrapperMapper.empty()) {
            Object::destroy(m_d->wrapperMapper.begin()->second, const_cast<void *>(m_d->wrapperMapper.begin()->first));
        }
#endif
        assert(m_d->wrapperMapper.empty());
    }
    delete m_d;
}

BindingManager &BindingManager::instance() {
    static BindingManager singleton;
    return singleton;
}

bool BindingManager::hasWrapper(const void *cptr) const
{
#ifdef Py_GIL_DISABLED
    WrapperMapGuard guard(m_d->wrapperMapLock);
#else
    std::lock_guard<std::recursive_mutex> guard(m_d->wrapperMapLock);
#endif
    return m_d->wrapperMapper.find(cptr) != m_d->wrapperMapper.end();
}

bool BindingManager::hasWrapper(const void *cptr, PyTypeObject *typeObject) const
{
#ifdef Py_GIL_DISABLED
    WrapperMapGuard guard(m_d->wrapperMapLock);
#else
    std::lock_guard<std::recursive_mutex> guard(m_d->wrapperMapLock);
#endif
    return m_d->findByType(cptr, typeObject) != m_d->wrapperMapper.cend();
}

void BindingManager::registerWrapper(SbkObject *pyObj, void *cptr)
{
    auto *instanceType = Shiboken::pyType(pyObj);
    auto *d = PepType_SOTP(instanceType);

    if (!d)
        return;

    if (d->mi_init && !d->mi_offsets)
        d->mi_offsets = d->mi_init(cptr);
    m_d->assignWrapper(pyObj, cptr, d->mi_offsets);
}

/*!
    Takes the object \a sbkObj out of the wrapper map, leaving its flags alone.
    Deallocation uses this to make the wrapper unreachable before it runs
    any Python code; releaseWrapper() is the same plus invalidation.
*/
void BindingManager::unregisterWrapper(SbkObject *sbkObj)
{
    const auto cptrs = Shiboken::cppPointersSpan(sbkObj);
    auto *d = PepType_SOTP(Shiboken::pyType(sbkObj));

    const int *mi_offsets = d != nullptr ? d->mi_offsets : nullptr;
    for (std::size_t i = 0, size = cptrs.size(); i < size; ++i) {
        if (cptrs[i] != nullptr)
            m_d->releaseWrapper(cptrs[i], sbkObj, mi_offsets);
    }
}

void BindingManager::releaseWrapper(SbkObject *sbkObj)
{
    unregisterWrapper(sbkObj);
    sbkObj->d->validCppObject = false;
}

/*!
    \class Shiboken::WrapperRef
    \inmodule libshiboken

    \brief A wrapper found by BindingManager::wrapperRef().

    Free-threaded builds hold a reference the holder owns, since another
    thread can drop the last one; GIL builds borrow, as retrieveWrapper()
    does. newRef() gives the caller a new reference in both. object() and
    pyObject() are deleted on a temporary.
*/

#ifdef Py_GIL_DISABLED
void BindingManager::runDeletionInMainThread()
{
    BindingManagerPrivate::DestructorEntries pending;
    {
        MainThreadDeleteGuard guard(m_d->deleteInMainThreadLock);
        pending.swap(m_d->deleteInMainThread);
    }
    // Taken over, not iterated in place: a destructor can queue more.
    for (const DestructorEntry &e : pending)
        e.destructor(e.cppInstance);
}

void BindingManager::addToDeletionInMainThread(const DestructorEntry &e)
{
    MainThreadDeleteGuard guard(m_d->deleteInMainThreadLock);
    m_d->deleteInMainThread.push_back(e);
}

/*!
    \ingroup freethreading
    \inmodule libshiboken

    Returns the wrapper of \a cptr of type \a typeObject or a subtype, with a
    reference, or an empty WrapperRef, also for a wrapper being
    deallocated: PyUnstable_TryIncRef() does not revive it. Needs an
    attached thread. GIL builds borrow the wrapper instead, as
    retrieveWrapper() does.
*/
WrapperRef BindingManager::wrapperRef(const void *cptr, PyTypeObject *typeObject) const
{
    WrapperMapGuard guard(m_d->wrapperMapLock);
    // Keep looking past an entry that fails: a live sibling may follow.
    const auto range = m_d->wrapperMapper.equal_range(cptr);
    for (auto it = range.first; it != range.second; ++it) {
        // Deallocation removes the entry under this lock first: the type may be read.
        auto *foundType = Py_TYPE(reinterpret_cast<PyObject *>(it->second.borrowed()));
        if (foundType != typeObject && PyType_IsSubtype(foundType, typeObject) == 0)
            continue;
        if (auto *wrapper = it->second.acquire())
            return WrapperRef::fromOwned(wrapper);
    }
    return {};
}

/*!
    \overload
    \ingroup freethreading
    \inmodule libshiboken

    Returns a wrapper of \a cptr of any type, with a reference taken.
*/
WrapperRef BindingManager::wrapperRef(const void *cptr) const
{
    WrapperMapGuard guard(m_d->wrapperMapLock);
    const auto range = m_d->wrapperMapper.equal_range(cptr);
    for (auto it = range.first; it != range.second; ++it) {
        if (auto *wrapper = it->second.acquire())
            return WrapperRef::fromOwned(wrapper);
    }
    return {};
}

/*!
    \ingroup freethreading
    \inmodule libshiboken

    Returns the type of the wrapper of \a cptr as a new reference, or nullptr.
    Holding the wrapper instead could make the caller run its deallocation,
    C++ deletion included, inside a call into that object.
*/
PyTypeObject *BindingManager::wrapperTypeRef(const void *cptr) const
{
    WrapperMapGuard guard(m_d->wrapperMapLock);
    // The entry leaves the map under this lock before its wrapper is freed.
    const auto it = m_d->wrapperMapper.find(cptr);
    if (it == m_d->wrapperMapper.end())
        return nullptr;
    auto *type = Py_TYPE(reinterpret_cast<PyObject *>(it->second.borrowed()));
    Py_INCREF(type);
    return type;
}

/*!
    \class Shiboken::BindingManager::Registration
    \ingroup freethreading
    \inmodule libshiboken

    \brief The result of registerWrapperUnlessPresent(): empty, or the
    wrapper that was there first.
*/

/*!
    \ingroup freethreading
    \inmodule libshiboken

    Registers \a pyObj for \a cptr unless the map already has a wrapper of
    \a typeObject there, and returns that one with a reference taken. The
    caller discards its own wrapper when the result is not empty.
*/
BindingManager::Registration
    BindingManager::registerWrapperUnlessPresent(SbkObject *pyObj, void *cptr,
                                                 PyTypeObject *typeObject)
{
    // Lookup and insertion in one hold of the map lock: as two steps, two
    // threads converting one C++ object both registered a wrapper.
    auto *d = PepType_SOTP(Shiboken::pyType(pyObj));
    if (d == nullptr)
        return {};

    if (!FreeThreading::optionEnabled(FreeThreading::Option::OneWrapperPerAddress)) {
        // Bit cleared: two holds, as in the base.
        if (auto winner = wrapperRef(cptr, typeObject))
            return {std::move(winner)};
        Shiboken::failpoint("register-after-lookup");
        registerWrapper(pyObj, cptr);
        return {};
    }

    // Recursive: the calls below lock again.
    WrapperMapGuard guard(m_d->wrapperMapLock);
    if (auto winner = wrapperRef(cptr, typeObject))
        return {std::move(winner)};
    Shiboken::failpoint("register-after-lookup");
    registerWrapper(pyObj, cptr);
    return {};
}
#else // Py_GIL_DISABLED
void BindingManager::runDeletionInMainThread()
{
    for (const DestructorEntry &e : m_d->deleteInMainThread)
        e.destructor(e.cppInstance);
    m_d->deleteInMainThread.clear();
}

void BindingManager::addToDeletionInMainThread(const DestructorEntry &e)
{
    m_d->deleteInMainThread.push_back(e);
}

SbkObject *BindingManager::retrieveWrapper(const void *cptr) const
{
    std::lock_guard<std::recursive_mutex> guard(m_d->wrapperMapLock);
    auto iter = m_d->wrapperMapper.find(cptr);
    if (iter == m_d->wrapperMapper.end())
        return nullptr;
    return iter->second;
}

SbkObject *BindingManager::retrieveWrapper(const void *cptr, PyTypeObject *typeObject) const
{
    std::lock_guard<std::recursive_mutex> guard(m_d->wrapperMapLock);
    const auto it = m_d->findByType(cptr, typeObject);
    return it != m_d->wrapperMapper.cend() ? it->second : nullptr;
}
#endif // Py_GIL_DISABLED

PyObject *BindingManager::getOverride(SbkObject *wrapper, PyObject *pyMethodName)
{
    auto *obWrapper = reinterpret_cast<PyObject *>(wrapper);

    Shiboken::AutoDecRef method(PyObject_GetAttr(obWrapper, pyMethodName));
    if (method.isNull())
        return nullptr;

    PyObject *function = nullptr;

    // PYSIDE-1523: PyMethod_Check is not accepting compiled methods, we do this rather
    // crude check for them.
    // PYSIDE-535: This macro is redefined in a compatible way in pep384
    if (PyMethod_Check(method) != 0) {
        if (PyMethod_Self(method) != obWrapper)
            return nullptr;
        function = PyMethod_Function(method);
    } else if (isCompiledMethod(method)) {
        Shiboken::AutoDecRef im_self(PyObject_GetAttr(method, PyName::im_self()));
        // Not retaining a reference inline with what PyMethod_GET_SELF does.
        if (im_self.object() != obWrapper)
            return nullptr;
        function = PyObject_GetAttr(method, PyName::im_func());
        // Not retaining a reference inline with what PyMethod_GET_FUNCTION does.
        Py_DECREF(function);
    } else {
        return nullptr;
    }

    PyTypeObject *type = Py_TYPE(obWrapper);
    bool defaultFound = false;
    // The first class in the mro (index 0) is the class being checked and it should not be tested.
    // The last class in the mro (size - 1) is the base Python object class which should not be tested also.
    for (PyTypeObject *parent : Shiboken::PyTupleTypeRange(type->tp_mro).slice(1).chop(1)) {
        AutoDecRef parentDict(PepType_GetDict(parent));
        if (parentDict) {
            if (PyObject *defaultMethod = PyDict_GetItem(parentDict.object(), pyMethodName)) {
                defaultFound = true;
                if (function != defaultMethod)
                    return function;
            }
        }
    }
    // PYSIDE-2255: If no default method was found, use the method.
    if (!defaultFound)
        return function;
    return nullptr;
}

void BindingManager::addClassInheritance(Module::TypeInitStruct *parent,
                                         Module::TypeInitStruct *child)
{
    m_d->classHierarchy.addEdge(GraphNode(parent), GraphNode(child));
}

/*!
    Tries to find the correct type of \a cptr via type discovery knowing that
    it is at least of type \a type. If a derived class is found, it returns
    the type and \a cptr cast to the type (which may be different in case of
    multiple inheritance).
*/
BindingManager::TypeCptrPair BindingManager::findDerivedType(void *cptr, PyTypeObject *type) const
{
    return m_d->classHierarchy.identifyType(cptr, type, type);
}

/*!
    If \a probeType has a type discovery function, runs it passing \a cptr
    (a pointer to the instance of type \a type) and \a type and returns the
    (cast) pointer or nullptr.
*/
void *BindingManager::runTypeDiscovery(void *cptr, PyTypeObject *probeType, PyTypeObject *type)
{
    auto *sotp = PepType_SOTP(probeType);
    assert(sotp != nullptr);
    return sotp->type_discovery != nullptr ? sotp->type_discovery(cptr, type) : nullptr;
}

// FIXME PYSIDE7: remove, just for compatibility
/*!
    Tries to find the correct type of *\a cptr knowing that it is at least
    of type \a type.

    \a cptr is a pointer to a pointer to the instance of type \a type.
    In case of multiple inheritance, this function may change the contents
    of \a cptr.

    \warning This function is slow, use it only as last resort.
*/
PyTypeObject *BindingManager::resolveType(void **cptr, PyTypeObject *type)
{
    auto result = findDerivedType(*cptr, type);
    if (result.second != nullptr)
        *cptr = result.second;
    return result.first != nullptr ? result.first : type;
}

#ifdef Py_GIL_DISABLED
/*!
    Returns every live wrapper once, each with a reference taken. A wrapper
    that is being deallocated is left out. Free-threaded builds only; a build
    with a GIL returns a set of borrowed objects.
*/
std::vector<WrapperRef> BindingManager::getAllPyObjects()
{
    // Multiple inheritance registers one wrapper under several pointers.
    std::vector<WrapperRef> result;
    std::set<const SbkObject *> seen;
    WrapperMapGuard guard(m_d->wrapperMapLock);
    for (const auto &entry : m_d->wrapperMapper) {
        auto ref = WrapperRef::fromOwned(entry.second.acquire());
        if (ref.isNull() || !seen.insert(ref.object()).second)
            continue;
        result.push_back(std::move(ref));
    }
    return result;
}
#else // Py_GIL_DISABLED
std::set<PyObject *> BindingManager::getAllPyObjects()
{
    std::set<PyObject *> pyObjects;
    std::lock_guard<std::recursive_mutex> guard(m_d->wrapperMapLock);
    const WrapperMap &wrappersMap = m_d->wrapperMapper;
    auto it = wrappersMap.begin();
    for (; it != wrappersMap.end(); ++it)
        pyObjects.insert(reinterpret_cast<PyObject *>(it->second));

    return pyObjects;
}
#endif // Py_GIL_DISABLED

/*!
    Calls the function \a visitor for each object registered on binding manager,
    passing \a data (user data) as second argument.

    \note As various C++ pointers can point to the same PyObject due to multiple
    inheritance, the visitor can be called more than once for each PyObject.
*/
void BindingManager::visitAllPyObjects(ObjectVisitor visitor, void *data)
{
#ifdef Py_GIL_DISABLED
    // Collected first, visited without the lock: a visitor runs Python and
    // C++ destructors, which change the map. The references keep the
    // collected wrappers alive until their turn.
    const std::vector<WrapperRef> wrappers = getAllPyObjects();
    Shiboken::checkNoRawLock(__func__);
    for (const auto &wrapper : wrappers)
        visitor(wrapper.object(), data);
#else
    WrapperMap copy = m_d->wrapperMapper;
    for (const auto &p : copy) {
        if (m_d->findSbkObject(p.first, p.second) != m_d->wrapperMapper.cend())
            visitor(p.second, data);
    }
#endif // Py_GIL_DISABLED
}

bool BindingManager::dumpTypeGraph(const char *fileName) const
{
    return m_d->classHierarchy.dumpTypeGraph(fileName);
}

void BindingManager::dumpWrapperMap()
{
#ifdef Py_GIL_DISABLED
    WrapperMapGuard guard(m_d->wrapperMapLock);
#endif
    const auto &wrapperMap = m_d->wrapperMapper;
    std::cerr <<  "-------------------------------\n"
        << "WrapperMap size: " << wrapperMap.size() << " Types: "
        << m_d->classHierarchy.nodeSet().size() << '\n';
    for (auto it : wrapperMap) {
#ifdef Py_GIL_DISABLED
        auto *ob = reinterpret_cast<PyObject *>(it.second.borrowed());
#else
        auto *ob = reinterpret_cast<PyObject *>(it.second);
#endif
        std::cerr << "key: " << it.first << ", value: "
            << static_cast<const void *>(ob) << " ("
            << PepType_GetFullyQualifiedNameStr(Py_TYPE(ob)) << ", refcnt: "
            << Py_REFCNT(ob) << ")\n";
    }
    std::cerr << "-------------------------------\n";
}

static bool isPythonType(PyTypeObject *type)
{
    // This is a type which should be called by multiple inheritance.
    // It is either a pure Python type or a derived PySide type.
    return !ObjectType::checkType(type) || ObjectType::isUserType(type);
}

bool callInheritedInit(PyObject *self, PyObject *args, PyObject *kwds,
                       Module::TypeInitStruct typeStruct)
{
    using Shiboken::AutoDecRef;

    static PyObject *const _init = String::createStaticString("__init__");
    static PyObject *objectInit =
        PyObject_GetAttr(reinterpret_cast<PyObject *>(&PyBaseObject_Type), _init);

    // A native C++ self cannot have multiple inheritance.
    if (!Object::isUserType(self))
        return false;

    /* No need to check the last one: it's gonna be skipped anyway.  */
    auto mroRange = Shiboken::PyTupleTypeRange(Py_TYPE(self)->tp_mro).chop(1);
    const auto mroEnd = mroRange.end();

    const char *className = typeStruct.fullName;
    auto namePred = [className](PyTypeObject *t) {
        return std::strcmp(className, PepType_GetFullyQualifiedNameStr(t)) == 0; };
    auto mroIt = std::find_if(mroRange.begin(), mroEnd, namePred);
    if (mroIt == mroEnd)
        return false;
    // We are now at the first non-Python class `QObject`.
    // mro: ('C', 'A', 'QObject', 'Object', 'B', 'object')
    // We want to catch class `B` and call its `__init__`.
    mroIt = std::find_if(++mroIt, mroEnd, isPythonType);
    if (mroIt == mroEnd)
        return false;

    auto *subType = *mroIt;
    if (subType == &PyBaseObject_Type)
        return false;
    AutoDecRef func(PyObject_GetAttr(reinterpret_cast<PyObject *>(subType), _init));
    // PYSIDE-2654: If this has no implementation then we get object.__init__
    //              but that is the same case like above.
    if (func == objectInit)
        return false;
    // PYSIDE-2294: We need to explicitly ignore positional args in a mixin class.
    SBK_UNUSED(args);
    AutoDecRef newArgs(PyTuple_New(1));
    auto *newArgsOb = newArgs.object();
    Py_INCREF(self);
    PyTuple_SetItem(newArgsOb, 0, self);
    // Note: This can fail, so please always check the error status.
    AutoDecRef result(PyObject_Call(func, newArgs, kwds));
    return true;
}

} // namespace Shiboken
