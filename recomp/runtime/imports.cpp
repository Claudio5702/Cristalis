// Resolves the XEX import table at load time. Function imports come as a pair of
// records: a pointer slot in .rdata followed by its thunk in .text. XenonUtils has
// already turned the thunks into stubs the recompiler maps to __imp__ functions, so
// each slot only needs the address of its thunk. Slots without a thunk are kernel
// variables, which get backing storage here.
#include "guest.h"
#include "xex_header.h"

#include <windows.h>

#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

// Ordinal to name tables defined by XenonUtils (xex.cpp).
extern std::unordered_map<size_t, const char*> XboxKernelExports;
extern std::unordered_map<size_t, const char*> XamExports;

static bool IsCodeAddress(uint32_t address)
{
    return address >= PPC_CODE_BASE && address < PPC_CODE_BASE + PPC_CODE_SIZE;
}

static void TimeStampThread(uint32_t bundle)
{
    // KeTimeStampBundle: interrupt time, system time (100 ns units), tick count (ms).
    for (;;)
    {
        FILETIME systemTime;
        GetSystemTimeAsFileTime(&systemTime);
        uint64_t ticks = GetTickCount64();
        Store64(bundle + 0x00, ticks * 10000);
        Store64(bundle + 0x08, (uint64_t(systemTime.dwHighDateTime) << 32) | systemTime.dwLowDateTime);
        Store32(bundle + 0x10, uint32_t(ticks));
        Sleep(1);
    }
}

static uint32_t CreateKernelVariable(const std::string& name)
{
    uint32_t block = GuestAllocVirtual(0x1000, 0x1000);
    if (block == 0)
        return 0;

    if (name == "XexExecutableModuleHandle")
    {
        // Pointer to an LDR_DATA_TABLE_ENTRY; the CRT reads the XEX header base from it.
        uint32_t module = block + 0x100;
        Store32(module + 0x18, uint32_t(PPC_IMAGE_BASE));
        Store32(module + 0x1C, g_xex.entryPoint);
        Store32(module + 0x20, uint32_t(PPC_IMAGE_SIZE));
        Store32(module + 0x58, g_xex.headerAddress);
        Store32(block, module);
    }
    else if (name == "XboxHardwareInfo")
    {
        g_base[block + 4] = 6; // logical processor count
    }
    else if (name == "XboxKrnlVersion" || name == "XboxKrnlBaseVersion")
    {
        Store16(block + 0, 2);
        Store16(block + 2, 0);
        Store16(block + 4, 17559);
        Store16(block + 6, 0);
    }
    else if (name == "KeTimeStampBundle")
    {
        std::thread(TimeStampThread, block).detach();
    }
    return block;
}

void ResolveImports()
{
    uint32_t imports = XexFindHeader(0x000103FF);
    if (imports == 0)
        return;

    uint32_t stringTableSize = Load32(imports + 4);
    uint32_t libraryCount = Load32(imports + 8);

    std::vector<std::string> libraryNames;
    uint32_t cursor = imports + 12;
    for (uint32_t i = 0; i < libraryCount; i++)
    {
        libraryNames.emplace_back((const char*)g_base + cursor);
        cursor += uint32_t((libraryNames.back().size() + 1 + 3) & ~size_t(3));
    }

    size_t functionSlots = 0;
    uint32_t library = imports + 12 + stringTableSize;
    for (uint32_t i = 0; i < libraryCount; i++)
    {
        uint32_t librarySize = Load32(library);
        uint16_t nameIndex = Load16(library + 0x24);
        uint16_t recordCount = Load16(library + 0x26);
        const std::string libraryName = nameIndex < libraryNames.size() ? libraryNames[nameIndex] : "?";

        const std::unordered_map<size_t, const char*>* names = nullptr;
        if (libraryName == "xboxkrnl.exe")
            names = &XboxKernelExports;
        else if (libraryName == "xam.xex")
            names = &XamExports;

        for (uint32_t k = 0; k < recordCount; k++)
        {
            uint32_t slot = Load32(library + 0x28 + k * 4);
            if (IsCodeAddress(slot))
                continue;

            uint32_t next = k + 1 < recordCount ? Load32(library + 0x28 + (k + 1) * 4) : 0;
            if (IsCodeAddress(next))
            {
                Store32(slot, next);
                functionSlots++;
                continue;
            }

            // XenonUtils leaves the record in host byte order: type, library, ordinal.
            uint32_t record;
            memcpy(&record, g_base + slot, 4);
            uint32_t ordinal = record & 0xFFFF;

            std::string name = "?";
            if (names != nullptr)
            {
                auto it = names->find(ordinal);
                if (it != names->end())
                    name = it->second;
            }

            uint32_t variable = CreateKernelVariable(name);
            Store32(slot, variable);
            Log("Import variable %s!%s (ordinal 0x%X) at %08X -> %08X",
                libraryName.c_str(), name.c_str(), ordinal, slot, variable);
        }

        library += librarySize;
    }

    Log("Resolved %zu import function slots", functionSlots);
}
