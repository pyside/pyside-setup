// Copyright (C) 2016 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR LGPL-3.0-only OR GPL-2.0-only OR GPL-3.0-only
// Qt-Security score:significant reason:default

#include "debugfreehook.h"
#include "bindingmanager.h"
#include "gilstate.h"

#if defined(_WIN32) && defined(_DEBUG)
#  include <sbkwindows.h>
#  include <crtdbg.h>
#endif

#ifdef __GLIBC__
#include <malloc.h>
#endif

#ifdef __APPLE__
#include <malloc/malloc.h>
#include <mach/mach.h>
#if TARGET_OS_OSX
#include <mach/mach_vm.h>
#endif
#endif

#ifdef SHIBOKEN_INSTALL_FREE_DEBUG_HOOK
extern "C" {

static int testPointerBeingFreed(void *ptr)
{
    // It is an error for a deleted pointer address to still be registered
    // in the BindingManager
    if (Shiboken::BindingManager::instance().hasWrapper(ptr)) {
        Shiboken::GilState state;

        auto wrapper = Shiboken::BindingManager::instance().wrapperRef(ptr);

        fprintf(stderr, "SbkObject still in binding map when deleted: ");
        PyObject_Print(wrapper.pyObject(), stderr, 0);
        fprintf(stderr, "\n");

#ifdef _WIN32
        DebugBreak();
#else
        assert(0);
#endif
        return FALSE;
    }

    return TRUE;
}

#if defined(_WIN32) && defined(_DEBUG)
static _CRT_ALLOC_HOOK lastCrtAllocHook;
static int DebugAllocHook(int nAllocType, void *pvData,
                          size_t nSize, int nBlockUse, long lRequest,
                          const unsigned char * szFileName, int nLine)
{
    // It is an error for a deleted pointer address to still be registered
    // in the BindingManager
    if ( nAllocType == _HOOK_FREE) {
        if ( !testPointerBeingFreed(pvData) ) {
            return 0;
        }
    }

    if ( lastCrtAllocHook != NULL ) {
        return lastCrtAllocHook(nAllocType, pvData, nSize, nBlockUse, lRequest,
                                szFileName, nLine);
    }

    return 1;
}
#endif // _WIN32 && _DEBUG

#ifdef __GLIBC__
static void (*lastFreeHook)(void *ptr, const void *caller);
static void DebugFreeHook(void *ptr, const void *caller)
{
    testPointerBeingFreed(ptr);

    if ( lastFreeHook != NULL )
        lastFreeHook(ptr, caller);
}
#endif // __GLIBC__

#ifdef __APPLE__
static malloc_zone_t lastMallocZone;
static void DebugFreeHook(malloc_zone_t *zone, void *ptr)
{
    testPointerBeingFreed(ptr);

    if ( lastMallocZone.free != NULL )
        lastMallocZone.free(zone, ptr);
}
static void DebugFreeDefiniteSizeHook(malloc_zone_t *zone, void *ptr, size_t size)
{
    testPointerBeingFreed(ptr);

    if ( lastMallocZone.free_definite_size != NULL )
        lastMallocZone.free_definite_size(zone, ptr, size);
}
#endif __APPLE__

void debugInstallFreeHook(void)
{
#if defined(_WIN32) && defined(_DEBUG)
    lastCrtAllocHook = _CrtSetAllocHook(DebugAllocHook);
#endif

#ifdef __GLIBC__
    // __free_hook is not thread safe so it marked as deprecated.  Use here
    // is hopefully safe and should catch errors in a single threaded program
    // and only miss some in a multithreaded program
    lastFreeHook = __free_hook;
    __free_hook = DebugFreeHook;
#endif

#ifdef __APPLE__
    malloc_zone_t *zone = malloc_default_zone();
    assert(zone != NULL);
    //remove the write protection from the zone struct
    if (zone->version >= 8) {
      vm_protect(mach_task_self(), (uintptr_t)zone, sizeof(*zone), 0, VM_PROT_READ | VM_PROT_WRITE);
    }
    lastMallocZone = *zone;
    zone->free = DebugFreeHook;
    zone->free_definite_size = DebugFreeDefiniteSizeHook;
    if (zone->version >= 8) {
      vm_protect(mach_task_self(), (uintptr_t)zone, sizeof(*zone), 0, VM_PROT_READ);
    }
#endif
}

void debugRemoveFreeHook(void)
{
#if defined(_WIN32) && defined(_DEBUG)
    _CrtSetAllocHook(lastCrtAllocHook);
#endif

#ifdef __GLIBC__
    __free_hook = lastFreeHook;
#endif

#ifdef __APPLE__
    malloc_zone_t *zone = malloc_default_zone();
    assert(zone != NULL);
    //remove the write protection from the zone struct
    if (zone->version >= 8) {
      vm_protect(mach_task_self(), (uintptr_t)zone, sizeof(*zone), 0, VM_PROT_READ | VM_PROT_WRITE);
    }
    zone->free = lastMallocZone.free;
    zone->free_definite_size = lastMallocZone.free_definite_size;
    if (zone->version >= 8) {
      vm_protect(mach_task_self(), (uintptr_t)zone, sizeof(*zone), 0, VM_PROT_READ);
    }
#endif
}

} // extern "C"
#endif // SHIBOKEN_INSTALL_DEBUG_FREE_HOOK
