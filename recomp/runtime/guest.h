// Guest (Xbox 360) memory and import plumbing shared by the runtime.
#pragma once

#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstring>

#include <ppc_config.h>
#include <ppc_context.h>

// Host address of guest address 0. The whole 4 GB guest space is reserved here.
extern uint8_t* g_base;

inline void* GuestToHost(uint32_t address) { return address ? g_base + address : nullptr; }
inline uint32_t HostToGuest(const void* pointer) { return pointer ? uint32_t((const uint8_t*)pointer - g_base) : 0; }

inline uint16_t Load16(uint32_t a) { uint16_t v; memcpy(&v, g_base + a, 2); return __builtin_bswap16(v); }
inline uint32_t Load32(uint32_t a) { uint32_t v; memcpy(&v, g_base + a, 4); return __builtin_bswap32(v); }
inline uint64_t Load64(uint32_t a) { uint64_t v; memcpy(&v, g_base + a, 8); return __builtin_bswap64(v); }
inline void Store16(uint32_t a, uint16_t v) { v = __builtin_bswap16(v); memcpy(g_base + a, &v, 2); }
inline void Store32(uint32_t a, uint32_t v) { v = __builtin_bswap32(v); memcpy(g_base + a, &v, 4); }
inline void Store64(uint32_t a, uint64_t v) { v = __builtin_bswap64(v); memcpy(g_base + a, &v, 8); }

// Guest address space layout used by the runtime allocators.
constexpr uint32_t kVirtualHeapBase = 0x40000000;
constexpr uint32_t kVirtualHeapEnd = 0x70000000;
constexpr uint32_t kStackBase = 0x70000000;
constexpr uint32_t kStackEnd = 0x7F000000;
constexpr uint32_t kPhysicalHeapBase = 0xA0000000;
constexpr uint32_t kPhysicalHeapEnd = 0xC0000000;

// Commits [address, address + size) of guest memory. Returns false on failure.
bool GuestCommit(uint32_t address, uint32_t size);
void GuestDecommit(uint32_t address, uint32_t size);
// Allocators for the two heaps. Return 0 when exhausted; memory comes zeroed.
uint32_t GuestAllocVirtual(uint32_t size, uint32_t alignment = 0x10000);
uint32_t GuestAllocPhysical(uint32_t size, uint32_t alignment = 0x1000);
// Release an allocation by its start address. False if it is not one.
bool GuestFreeVirtual(uint32_t address);
bool GuestFreePhysical(uint32_t address);
// Thread stacks are never released: threads are few and long-lived.
uint32_t GuestAllocStack(uint32_t size);
void GuestFreeStack(uint32_t stack);

void Log(const char* format, ...);

// The single CPU context of the current guest thread, for crash diagnostics.
extern thread_local PPCContext* t_guestContext;
// Binds a guest context to the calling host thread and makes it visible to the
// crash handler and the thread sampler.
void GuestRegisterContext(PPCContext* ctx);
// Logs the guest return addresses of the calling thread. Accurate at function
// entry, where the stack pointer in the context is up to date.
void LogGuestBacktrace(const char* label);
// Must run on a guest thread before it exits: its context lives on its stack.
void GuestUnregisterContext();

// Import dispatch. imports.gen.cpp defines one __imp__ function per XEX import,
// each forwarding to the handler registered under the same name.
using ImportHandler = void (*)(PPCContext& ctx, uint8_t* base);

extern const char* const g_importNames[];
extern const size_t g_importCount;
extern ImportHandler g_importHandlers[];

void ImportDispatch(size_t index, PPCContext& ctx, uint8_t* base);
void RegisterImport(const char* name, ImportHandler handler);

struct ImportRegistrar
{
    ImportRegistrar(const char* name, ImportHandler handler) { RegisterImport(name, handler); }
};

// Defines the host implementation of a guest import. Arguments arrive in
// ctx.r3..r10 and the result is returned in ctx.r3.
#define GUEST_IMPORT(name) \
    static void name##_impl(PPCContext& ctx, uint8_t* base); \
    static ImportRegistrar name##_registrar(#name, name##_impl); \
    static void name##_impl([[maybe_unused]] PPCContext& ctx, [[maybe_unused]] uint8_t* base)

constexpr uint32_t STATUS_SUCCESS = 0x00000000;
constexpr uint32_t STATUS_UNSUCCESSFUL = 0xC0000001;
constexpr uint32_t STATUS_NO_MEMORY = 0xC0000017;
