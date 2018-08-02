/**
 * This file has no copyright assigned and is placed in the Public Domain.
 * This file is part of the mingw-w64 runtime package.
 * No warranty is given; refer to the file DISCLAIMER.PD within this package.
 */

#include <sect_attribs.h>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include <assert.h>
#include <stdio.h>
#include <corecrt_startup.h>


typedef void (__thiscall * dtor_fn)(void*);
int __cxa_atexit(dtor_fn dtor, void *obj, void *dso);
int __cxa_thread_atexit(dtor_fn dtor, void *obj, void *dso);

typedef struct dtor_obj dtor_obj;
struct dtor_obj {
  dtor_fn dtor;
  void *obj;
  dtor_obj *next;
};

HANDLE __dso_handle;

static CRITICAL_SECTION lock;
static int inited = 0;
static dtor_obj *global_dtors = NULL;
static __thread dtor_obj *tls_dtors = NULL;

int __cxa_atexit(dtor_fn dtor, void *obj, void *dso) {
  if (!inited)
    return 1;
  assert(!dso || dso == &__dso_handle);
  dtor_obj *handler = (dtor_obj *) calloc(1, sizeof(*handler));
  if (!handler)
    return 1;
  handler->dtor = dtor;
  handler->obj = obj;
  EnterCriticalSection(&lock);
  handler->next = global_dtors;
  global_dtors = handler;
  LeaveCriticalSection(&lock);
  return 0;
}

static void run_dtor_list(dtor_obj **ptr) {
  dtor_obj *list = *ptr;
  while (list) {
    list->dtor(list->obj);
    dtor_obj *next = list->next;
    free(list);
    list = next;
  }
  *ptr = NULL;
}

int __cxa_thread_atexit(dtor_fn dtor, void *obj, void *dso) {
  if (!inited)
    return 1;
  assert(!dso || dso == &__dso_handle);
  dtor_obj *handler = (dtor_obj *) calloc(1, sizeof(*handler));
  if (!handler)
    return 1;
  handler->dtor = dtor;
  handler->obj = obj;
  handler->next = tls_dtors;
  tls_dtors = handler;
  return 0;
}

static void WINAPI tls_callback(HANDLE hDllHandle, DWORD dwReason, LPVOID __UNUSED_PARAM(lpReserved)) {
  switch (dwReason) {
  case DLL_PROCESS_ATTACH:
    if (inited == 0) {
      InitializeCriticalSection(&lock);
      __dso_handle = hDllHandle;
    }
    inited = 1;
    break;
  case DLL_PROCESS_DETACH:
    /*
     * If there are other threads still running that haven't been detached,
     * we don't attempt to run their destructors (MSVC doesn't either), but
     * simply leak the destructor list and whatever resources the destructors
     * would have released.
     *
     * From Vista onwards, we could have used FlsAlloc to get a TLS key that
     * runs a destructor on each thread that has a value attached ot it, but
     * since MSVC doesn't run destructors on other threads in this case,
     * users shouldn't assume it and we don't attempt to do anything potentially
     * risky about it. TL;DR, threads with pending TLS destructors for a DLL
     * need to be joined before unloading the DLL.
     */
    run_dtor_list(&tls_dtors);
    run_dtor_list(&global_dtors);
    if (inited == 1) {
      inited = 0;
      DeleteCriticalSection(&lock);
    }
    break;
  case DLL_THREAD_ATTACH:
    break;
  case DLL_THREAD_DETACH:
    run_dtor_list(&tls_dtors);
    break;
  }
}

_CRTALLOC(".CRT$XLE") PIMAGE_TLS_CALLBACK __xl_e = (PIMAGE_TLS_CALLBACK) tls_callback;