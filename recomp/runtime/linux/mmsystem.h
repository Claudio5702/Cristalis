// Linux build: the waveOut calls audio.cpp uses, implemented in win32.cpp over SDL.
#pragma once

#include "windows.h"

using HWAVEOUT = void*;
#define WAVE_FORMAT_PCM 1
#define WAVE_MAPPER 0xFFFFFFFFu
#define CALLBACK_NULL 0u
#define MMSYSERR_NOERROR 0u
#define WHDR_DONE 0x1u

struct WAVEFORMATEX
{
    WORD wFormatTag, nChannels;
    DWORD nSamplesPerSec, nAvgBytesPerSec;
    WORD nBlockAlign, wBitsPerSample, cbSize;
};
struct WAVEHDR
{
    LPSTR lpData;
    DWORD dwBufferLength, dwBytesRecorded;
    DWORD_PTR dwUser;
    DWORD dwFlags, dwLoops;
    WAVEHDR* lpNext;
    DWORD_PTR reserved;
};

UINT waveOutOpen(HWAVEOUT* device, UINT id, const WAVEFORMATEX* format, DWORD_PTR callback, DWORD_PTR instance, DWORD flags);
UINT waveOutPrepareHeader(HWAVEOUT device, WAVEHDR* header, UINT size);
UINT waveOutWrite(HWAVEOUT device, WAVEHDR* header, UINT size);
