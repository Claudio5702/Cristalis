// Lists the game controllers Windows exposes: XInput slots and legacy joysticks.
#include <windows.h>

#include <mmsystem.h>

#include <cstdio>
#include <initializer_list>

struct XInputState
{
    DWORD packetNumber;
    WORD buttons;
    BYTE leftTrigger, rightTrigger;
    SHORT leftX, leftY, rightX, rightY;
};

int main()
{
    for (const wchar_t* name : { L"xinput1_4.dll", L"xinput9_1_0.dll", L"xinput1_3.dll" })
    {
        HMODULE module = LoadLibraryW(name);
        auto getState = module ? (DWORD(WINAPI*)(DWORD, XInputState*))(void*)GetProcAddress(module, "XInputGetState") : nullptr;
        printf("%ls: %s\n", name, getState ? "loaded" : "not available");
        for (DWORD slot = 0; getState != nullptr && slot < 4; slot++)
        {
            XInputState state{};
            DWORD result = getState(slot, &state);
            printf("  slot %lu: %s (buttons %04X)\n", slot, result == ERROR_SUCCESS ? "CONNECTED" : "empty", state.buttons);
        }
    }

    UINT count = joyGetNumDevs();
    printf("legacy joystick slots: %u\n", count);
    for (UINT id = 0; id < count; id++)
    {
        JOYINFOEX info{};
        info.dwSize = sizeof(info);
        info.dwFlags = JOY_RETURNALL;
        if (joyGetPosEx(id, &info) != JOYERR_NOERROR)
            continue;
        JOYCAPSW caps{};
        joyGetDevCapsW(id, &caps, sizeof(caps));
        printf("  joystick %u: '%ls' vendor %04X product %04X, %u buttons, %u axes, POV %s\n", id, caps.szPname, caps.wMid,
            caps.wPid, caps.wNumButtons, caps.wNumAxes, (caps.wCaps & JOYCAPS_HASPOV) ? "yes" : "no");
    }
    return 0;
}
