// Included by the generated ppc_config.h when PPC_INCLUDE_DETAIL is defined.
// Routes guest indirect calls through a checked lookup so a call to an address
// with no recompiled function is reported instead of jumping to null.
#pragma once

#include <cstdint>

struct PPCContext;
typedef void PPCFunc(PPCContext& __restrict__ ctx, uint8_t* base);

PPCFunc* GuestResolveIndirect(uint8_t* base, uint32_t address);

#define PPC_CALL_INDIRECT_FUNC(x) GuestResolveIndirect(base, x)(ctx, base)
