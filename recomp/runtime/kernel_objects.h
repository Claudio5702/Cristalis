// Host-side kernel objects reachable from guest handles.
#pragma once

#include "guest.h"

#include <windows.h>

enum class KObjectType
{
    Thread,
    Event,
    Semaphore,
    File,
};

struct KObject
{
    KObjectType type{};
    HANDLE host{};
    uint32_t guestAddress{}; // set once the guest references the object by pointer
};

uint32_t ObjectCreateHandle(KObject* object);
KObject* ObjectFromHandle(uint32_t handle);
void ObjectCloseHandle(uint32_t handle);
