// Facts read from the XEX header at load time.
#pragma once

#include <cstdint>

struct XexInfo
{
    uint32_t headerAddress{}; // guest copy of the XEX header
    uint32_t entryPoint{};
    uint32_t stackSize{};
    uint32_t tlsSlotCount{};
    uint32_t tlsRawDataAddress{};
    uint32_t tlsDataSize{};
    uint32_t tlsRawDataSize{};
};

extern XexInfo g_xex;

// RtlImageXexHeaderField semantics: inline value, pointer to inline value, or
// pointer to header data depending on the low byte of the key. 0 if absent.
uint32_t XexFindHeader(uint32_t key);

// Directory holding default.xex; game:\ and d:\ resolve to it.
void SetGameRoot(const char* xexPath);

// Maps the GPU register block. Returns false on failure.
bool VideoInit();

// Maps the XMA decoder registers and context array. Returns false on failure.
bool AudioInit();

// Tells the game that system UI opened or closed over it, as the console does
// for its guide: the game pauses while it is open.
void XamSystemUiChanged(bool open);

// XC_LANGUAGE value matching the host UI language.
uint32_t GuestLanguage();

// Fills import pointer slots and creates kernel variables. Needs g_xex populated.
void ResolveImports();

// Allocates stack, PCR, KTHREAD and static TLS for a guest thread. Returns the
// PCR address (the guest's r13) or 0 on failure.
uint32_t GuestCreateThreadBlock(uint32_t stackSize, uint32_t& stackTop);
