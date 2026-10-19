// What DarkLauncher's shared code takes from Windows, for Linux: the types and constants, the part of
// GDI the setup screen draws with (shapes, pictures, gradients, text, paths, clipping: dl_gdi.c draws them
// itself into pixels, with FreeType for the text and fontconfig for the fonts), and wide strings. The
// shared code is written once: on Windows it is GDI itself.
#ifndef DL_WIN32_H
#define DL_WIN32_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#include <wctype.h>

#define WINAPI
#define CALLBACK
#define MAX_PATH 260
#ifndef TRUE
#define TRUE 1
#define FALSE 0
#endif
typedef int BOOL;
typedef uint8_t BYTE;
typedef uint16_t WORD, USHORT;
typedef uint32_t DWORD, UINT, ULONG;
typedef int32_t LONG;
typedef uint32_t COLORREF; // 0x00BBGGRR, as on Windows
typedef intptr_t LPARAM, LRESULT;
typedef uintptr_t WPARAM;
typedef void *HANDLE, *HWND, *HICON, *HINSTANCE, *HMODULE;
typedef struct GdiObj *HGDIOBJ, *HBRUSH, *HPEN, *HFONT, *HBITMAP, *HRGN;
typedef struct GdiDc *HDC;
typedef struct { LONG left, top, right, bottom; } RECT;
typedef struct { LONG x, y; } POINT;
typedef struct { LONG cx, cy; } SIZE;
typedef struct { DWORD dwLowDateTime, dwHighDateTime; } FILETIME;
typedef struct { WORD wYear, wMonth, wDayOfWeek, wDay, wHour, wMinute, wSecond, wMilliseconds; } SYSTEMTIME;

#define RGB(r, g, b) ((COLORREF)(((BYTE)(r)) | ((WORD)((BYTE)(g)) << 8) | (((DWORD)(BYTE)(b)) << 16)))
#define GetRValue(c) ((BYTE)(c))
#define GetGValue(c) ((BYTE)((c) >> 8))
#define GetBValue(c) ((BYTE)((c) >> 16))

// ---- pictures
typedef struct {
  DWORD biSize;
  LONG biWidth, biHeight; // a negative height: the rows from the top
  WORD biPlanes, biBitCount;
  DWORD biCompression, biSizeImage;
  LONG biXPelsPerMeter, biYPelsPerMeter;
  DWORD biClrUsed, biClrImportant;
} BITMAPINFOHEADER;
typedef struct { BITMAPINFOHEADER bmiHeader; DWORD bmiColors[1]; } BITMAPINFO;
typedef struct { LONG bmType, bmWidth, bmHeight, bmWidthBytes; WORD bmPlanes, bmBitsPixel; void *bmBits; } BITMAP;
typedef struct { BYTE BlendOp, BlendFlags, SourceConstantAlpha, AlphaFormat; } BLENDFUNCTION;
typedef struct { LONG x, y; USHORT Red, Green, Blue, Alpha; } TRIVERTEX;
typedef struct { ULONG UpperLeft, LowerRight; } GRADIENT_RECT;
typedef struct { LONG tmHeight, tmAscent, tmDescent, tmInternalLeading, tmExternalLeading, tmAveCharWidth, tmMaxCharWidth, tmWeight; } TEXTMETRICW;
#define DIB_RGB_COLORS 0
#define SRCCOPY 0x00CC0020
#define HALFTONE 4
#define COLORONCOLOR 3
#define AC_SRC_OVER 0
#define AC_SRC_ALPHA 1
#define GRADIENT_FILL_RECT_H 0
#define GRADIENT_FILL_RECT_V 1

// ---- drawing state
#define TRANSPARENT 1
#define OPAQUE 2
#define TA_LEFT 0
#define TA_TOP 0
#define TA_RIGHT 2
#define TA_CENTER 6
#define TA_BASELINE 24
#define DT_WORDBREAK 0x10
#define DT_CALCRECT 0x400
#define DT_NOPREFIX 0x800
#define PS_SOLID 0
#define RGN_COPY 5
enum { WHITE_BRUSH = 0, BLACK_BRUSH = 4, NULL_BRUSH = 5, NULL_PEN = 8 };
#define FW_NORMAL 400
#define FW_BOLD 700
#define FW_BLACK 900
#define DEFAULT_CHARSET 1
#define ANTIALIASED_QUALITY 4

HDC CreateCompatibleDC(HDC dc);
BOOL DeleteDC(HDC dc);
HDC GetDC(HWND w);
int ReleaseDC(HWND w, HDC dc);
HGDIOBJ GetStockObject(int which);
HGDIOBJ SelectObject(HDC dc, HGDIOBJ o);
BOOL DeleteObject(HGDIOBJ o);
int GetObjectA(HGDIOBJ o, int size, void *out);
HBRUSH CreateSolidBrush(COLORREF c);
HPEN CreatePen(int style, int width, COLORREF c);
HBITMAP CreateDIBSection(HDC dc, const BITMAPINFO *bi, UINT usage, void **bits, HANDLE section, DWORD offset);
HBITMAP CreateCompatibleBitmap(HDC dc, int w, int h);
HBITMAP CreateBitmap(int w, int h, UINT planes, UINT bits, const void *data);
HFONT CreateFontW(int height, int width, int escapement, int orientation, int weight, DWORD italic, DWORD underline, DWORD strike,
                  DWORD charset, DWORD out, DWORD clip, DWORD quality, DWORD pitch, const wchar_t *face);
HRGN CreateRectRgn(int x0, int y0, int x1, int y1);
HRGN CreateRoundRectRgn(int x0, int y0, int x1, int y1, int w, int h);
int SelectClipRgn(HDC dc, HRGN r);

int FillRect(HDC dc, const RECT *r, HBRUSH b);
BOOL Rectangle(HDC dc, int x0, int y0, int x1, int y1);
BOOL RoundRect(HDC dc, int x0, int y0, int x1, int y1, int w, int h);
BOOL Ellipse(HDC dc, int x0, int y0, int x1, int y1);
BOOL Polygon(HDC dc, const POINT *p, int n);
BOOL MoveToEx(HDC dc, int x, int y, POINT *old);
BOOL LineTo(HDC dc, int x, int y);
BOOL GradientFill(HDC dc, TRIVERTEX *v, ULONG nv, void *mesh, ULONG nm, ULONG mode);
BOOL BitBlt(HDC dst, int x, int y, int w, int h, HDC src, int sx, int sy, DWORD rop);
BOOL StretchBlt(HDC dst, int x, int y, int w, int h, HDC src, int sx, int sy, int sw, int sh, DWORD rop);
int StretchDIBits(HDC dst, int x, int y, int w, int h, int sx, int sy, int sw, int sh, const void *bits, const BITMAPINFO *bi, UINT usage, DWORD rop);
BOOL AlphaBlend(HDC dst, int x, int y, int w, int h, HDC src, int sx, int sy, int sw, int sh, BLENDFUNCTION bf);
int SetStretchBltMode(HDC dc, int mode);
BOOL SetBrushOrgEx(HDC dc, int x, int y, POINT *old);
BOOL GdiFlush(void);

int SetBkMode(HDC dc, int mode);
COLORREF SetTextColor(HDC dc, COLORREF c);
UINT SetTextAlign(HDC dc, UINT align);
int SetTextCharacterExtra(HDC dc, int extra);
BOOL TextOutW(HDC dc, int x, int y, const wchar_t *s, int n);
BOOL GetTextExtentPoint32W(HDC dc, const wchar_t *s, int n, SIZE *out);
BOOL GetTextMetricsW(HDC dc, TEXTMETRICW *out);
int GetTextFaceW(HDC dc, int n, wchar_t *out);
int DrawTextW(HDC dc, const wchar_t *s, int n, RECT *r, UINT format);
BOOL BeginPath(HDC dc);
BOOL EndPath(HDC dc);
BOOL FillPath(HDC dc);
BOOL StrokeAndFillPath(HDC dc);
BOOL SelectClipPath(HDC dc, int mode);

// ---- the window's icon and a file's presence (the rest of Windows comes with the step after: dl_win32.c)
typedef struct { BOOL fIcon; DWORD xHotspot, yHotspot; HBITMAP hbmMask, hbmColor; } ICONINFO;
#define WM_SETICON 0x0080
#define ICON_SMALL 0
#define ICON_BIG 1
#define SM_CXICON 11
#define SM_CXSMICON 49
#define INVALID_FILE_ATTRIBUTES ((DWORD)-1)
LRESULT SendMessageW(HWND w, UINT msg, WPARAM wp, LPARAM lp);
int GetSystemMetrics(int which);
HICON CreateIconIndirect(ICONINFO *ii);
DWORD GetFileAttributesA(const char *path);

// For --ui-audit and tests: the pixels selected into a memory DC (0x00RRGGBB, rows from the top).
uint32_t *dl_gdi_bits(HDC dc, int *w, int *h);

// ======== the rest of Windows the shared code uses, made with SDL3 and POSIX (dl_win32.c, dl_pads.c)
typedef int16_t SHORT;
typedef int64_t LONGLONG;
typedef uintptr_t ULONG_PTR, DWORD_PTR;
typedef uint32_t UINT32;
typedef wchar_t WCHAR;
typedef char *LPSTR;
typedef const char *LPCSTR;
typedef const wchar_t *LPCWSTR;
typedef void *HWAVEOUT, *HMONITOR;
typedef union { LONGLONG QuadPart; } LARGE_INTEGER;
#define INVALID_HANDLE_VALUE ((HANDLE)(intptr_t)-1)
#define ERROR_SUCCESS 0
#define LOWORD(l) ((WORD)((DWORD_PTR)(l) & 0xFFFF))
#define HIWORD(l) ((WORD)(((DWORD_PTR)(l) >> 16) & 0xFFFF))
#define MAKEINTRESOURCEW(i) ((LPCWSTR)(uintptr_t)(WORD)(i))

// ---- time and the program
BOOL QueryPerformanceCounter(LARGE_INTEGER *c);
BOOL QueryPerformanceFrequency(LARGE_INTEGER *f);
DWORD GetTickCount(void);
void Sleep(DWORD ms);
UINT timeBeginPeriod(UINT ms);
UINT timeEndPeriod(UINT ms);
#define CREATE_WAITABLE_TIMER_HIGH_RESOLUTION 0x00000002
#define TIMER_ALL_ACCESS 0x1F0003
#define INFINITE 0xFFFFFFFF
#define WAIT_OBJECT_0 0
#define WAIT_TIMEOUT 258
#define QS_ALLINPUT 0x04FF
HANDLE CreateWaitableTimerExW(void *attributes, LPCWSTR name, DWORD flags, DWORD access);
BOOL SetWaitableTimer(HANDLE h, const LARGE_INTEGER *due, LONG period, void *routine, void *arg, BOOL resume);
DWORD MsgWaitForMultipleObjects(DWORD n, const HANDLE *handles, BOOL all, DWORD ms, DWORD wake);
HANDLE CreateEventW(void *attributes, BOOL manual, BOOL initial, LPCWSTR name); // auto-reset, for the sound
// errors that end the program (signals), the module an address is in (dladdr)
typedef struct { DWORD ExceptionCode, ExceptionFlags; void *ExceptionRecord, *ExceptionAddress; DWORD NumberParameters; ULONG_PTR ExceptionInformation[15]; } EXCEPTION_RECORD;
typedef struct { EXCEPTION_RECORD *ExceptionRecord; void *ContextRecord; } EXCEPTION_POINTERS;
typedef LONG (*LPTOP_LEVEL_EXCEPTION_FILTER)(EXCEPTION_POINTERS *ep);
#define EXCEPTION_CONTINUE_SEARCH 0
LPTOP_LEVEL_EXCEPTION_FILTER SetUnhandledExceptionFilter(LPTOP_LEVEL_EXCEPTION_FILTER f);
#define GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT 2
#define GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS 4
BOOL GetModuleHandleExA(DWORD flags, const char *addr, HMODULE *out);
// a thread (pthread), and a value changed at once for every thread
typedef DWORD (*LPTHREAD_START_ROUTINE)(void *arg);
HANDLE CreateThread(void *attributes, size_t stack, LPTHREAD_START_ROUTINE fn, void *arg, DWORD flags, DWORD *id);
LONG InterlockedExchange(volatile LONG *at, LONG value);
// the clipboard (SDL's), text only
#define CF_UNICODETEXT 13
#define GMEM_MOVEABLE 2
HANDLE GlobalAlloc(UINT flags, size_t n);
void *GlobalLock(HANDLE h);
BOOL GlobalUnlock(HANDLE h);
BOOL OpenClipboard(HWND w);
BOOL EmptyClipboard(void);
HANDLE SetClipboardData(UINT format, HANDLE h);
BOOL CloseClipboard(void);
DWORD GetCurrentProcessId(void);
#define PROCESS_QUERY_LIMITED_INFORMATION 0x1000
#define STILL_ACTIVE 259
HANDLE OpenProcess(DWORD access, BOOL inherit, DWORD pid);
BOOL GetExitCodeProcess(HANDLE h, DWORD *code);
BOOL QueryFullProcessImageNameW(HANDLE h, DWORD flags, wchar_t *out, DWORD *n);
BOOL CloseHandle(HANDLE h);
DWORD GetModuleFileNameA(HMODULE m, char *out, DWORD n);
DWORD GetModuleFileNameW(HMODULE m, wchar_t *out, DWORD n);
HMODULE GetModuleHandleW(LPCWSTR name);
const wchar_t *GetCommandLineW(void);
typedef struct { DWORD cb; } STARTUPINFOW;
typedef struct { HANDLE hProcess, hThread; DWORD dwProcessId, dwThreadId; } PROCESS_INFORMATION;
BOOL CreateProcessW(const wchar_t *exe, const wchar_t *cmd, void *pa, void *ta, BOOL inherit, DWORD flags, void *env, const wchar_t *dir,
                    STARTUPINFOW *si, PROCESS_INFORMATION *pi);
#define STD_OUTPUT_HANDLE ((DWORD)-11)
#define FILE_TYPE_UNKNOWN 0
#define FILE_TYPE_CHAR 2
#define ATTACH_PARENT_PROCESS ((DWORD)-1)
HANDLE GetStdHandle(DWORD which);
DWORD GetFileType(HANDLE h);
BOOL AttachConsole(DWORD pid);
void SetProcessDPIAware(void);
WORD GetUserDefaultUILanguage(void);
HMODULE LoadLibraryA(const char *name);
void *GetProcAddress(HMODULE m, const char *name);
#define CP_ACP 0
#define CP_UTF8 65001
int MultiByteToWideChar(UINT cp, DWORD flags, const char *s, int n, wchar_t *out, int cap);
int WideCharToMultiByte(UINT cp, DWORD flags, const wchar_t *s, int n, char *out, int cap, const char *def, BOOL *used);

// ---- files and folders (paths with "/", the system's)
#define FILE_ATTRIBUTE_DIRECTORY 0x10
#define FILE_ATTRIBUTE_NORMAL 0x80
typedef struct { DWORD dwFileAttributes; FILETIME ftCreationTime, ftLastAccessTime, ftLastWriteTime; DWORD nFileSizeHigh, nFileSizeLow; } WIN32_FILE_ATTRIBUTE_DATA;
typedef struct {
  DWORD dwFileAttributes;
  FILETIME ftCreationTime, ftLastAccessTime, ftLastWriteTime;
  DWORD nFileSizeHigh, nFileSizeLow, dwReserved0, dwReserved1;
  char cFileName[MAX_PATH], cAlternateFileName[14];
} WIN32_FIND_DATAA;
typedef struct {
  DWORD dwFileAttributes;
  FILETIME ftCreationTime, ftLastAccessTime, ftLastWriteTime;
  DWORD nFileSizeHigh, nFileSizeLow, dwReserved0, dwReserved1;
  wchar_t cFileName[MAX_PATH], cAlternateFileName[14];
} WIN32_FIND_DATAW;
enum { GetFileExInfoStandard };
BOOL GetFileAttributesExA(const char *path, int level, void *out);
HANDLE FindFirstFileA(const char *pattern, WIN32_FIND_DATAA *fd);
BOOL FindNextFileA(HANDLE h, WIN32_FIND_DATAA *fd);
HANDLE FindFirstFileW(const wchar_t *pattern, WIN32_FIND_DATAW *fd);
BOOL FindNextFileW(HANDLE h, WIN32_FIND_DATAW *fd);
BOOL FindClose(HANDLE h);
BOOL CreateDirectoryA(const char *path, void *sa);
BOOL CreateDirectoryW(const wchar_t *path, void *sa);
BOOL DeleteFileA(const char *path);
#define FO_DELETE 3
#define FOF_SILENT 0x4
#define FOF_NOCONFIRMATION 0x10
#define FOF_ALLOWUNDO 0x40
#define FOF_NOERRORUI 0x400
typedef struct { HWND hwnd; UINT wFunc; const char *pFrom, *pTo; WORD fFlags; BOOL fAnyOperationsAborted; void *hNameMappings; const char *lpszProgressTitle; } SHFILEOPSTRUCTA;
int SHFileOperationA(SHFILEOPSTRUCTA *op); // FO_DELETE with FOF_ALLOWUNDO: into the desktop's trash
#define SW_SHOWNORMAL 1
#define SW_SHOW 5
#define SW_SHOWNOACTIVATE 4
HINSTANCE ShellExecuteA(HWND w, const char *op, const char *file, const char *params, const char *dir, int show);
HINSTANCE ShellExecuteW(HWND w, const wchar_t *op, const wchar_t *file, const wchar_t *params, const wchar_t *dir, int show);
BOOL FileTimeToLocalFileTime(const FILETIME *ft, FILETIME *local);
BOOL FileTimeToSystemTime(const FILETIME *ft, SYSTEMTIME *st);
LONG CompareFileTime(const FILETIME *a, const FILETIME *b);
void GetLocalTime(SYSTEMTIME *st);
#define LOCALE_USER_DEFAULT 0x0400
#define DATE_SHORTDATE 1
#define TIME_NOSECONDS 2
int GetDateFormatW(DWORD locale, DWORD flags, const SYSTEMTIME *st, const wchar_t *fmt, wchar_t *out, int n);
int GetTimeFormatW(DWORD locale, DWORD flags, const SYSTEMTIME *st, const wchar_t *fmt, wchar_t *out, int n);

// ---- the window and its messages
typedef LRESULT(CALLBACK *WNDPROC)(HWND, UINT, WPARAM, LPARAM);
typedef struct { UINT style; WNDPROC lpfnWndProc; int cbClsExtra, cbWndExtra; HINSTANCE hInstance; HICON hIcon; HANDLE hCursor; HBRUSH hbrBackground; LPCWSTR lpszMenuName, lpszClassName; } WNDCLASSW;
typedef struct { HWND hwnd; UINT message; WPARAM wParam; LPARAM lParam; DWORD time; POINT pt; } MSG;
typedef struct { HDC hdc; BOOL fErase; RECT rcPaint; BOOL fRestore, fIncUpdate; BYTE rgbReserved[32]; } PAINTSTRUCT;
typedef struct { UINT length, flags, showCmd; POINT ptMinPosition, ptMaxPosition; RECT rcNormalPosition; } WINDOWPLACEMENT;
typedef struct { DWORD cbSize; RECT rcMonitor, rcWork; DWORD dwFlags; } MONITORINFO;
typedef struct { DWORD cbSize; RECT rcMonitor, rcWork; DWORD dwFlags; wchar_t szDevice[32]; } MONITORINFOEXW;
typedef struct { WORD dmSize; DWORD dmDisplayFrequency; } DEVMODEW;
#define ENUM_CURRENT_SETTINGS ((DWORD)-1)
#define WM_DESTROY 0x0002
#define WM_SIZE 0x0005
#define WM_PAINT 0x000F
#define WM_CLOSE 0x0010
#define WM_ERASEBKGND 0x0014
#define WM_KEYDOWN 0x0100
#define WM_KEYUP 0x0101
#define WM_CHAR 0x0102
#define WM_SYSKEYDOWN 0x0104
#define WM_MOUSEMOVE 0x0200
#define WM_LBUTTONDOWN 0x0201
#define WM_RBUTTONDOWN 0x0204
#define WM_MOUSEWHEEL 0x020A
#define WM_DEVICECHANGE 0x0219
#define PM_REMOVE 1
#define WS_OVERLAPPEDWINDOW 0x00CF0000
#define GWL_STYLE (-16)
#define SWP_NOSIZE 0x1
#define SWP_NOMOVE 0x2
#define SWP_NOZORDER 0x4
#define SWP_FRAMECHANGED 0x20
#define SWP_NOOWNERZORDER 0x200
#define HWND_TOP ((HWND)0)
#define MONITOR_DEFAULTTONULL 0
#define MONITOR_DEFAULTTOPRIMARY 1
#define SM_CXSCREEN 0
#define SM_CYSCREEN 1
#define IDC_ARROW 32512
WORD RegisterClassW(const WNDCLASSW *wc);
HWND CreateWindowW(LPCWSTR cls, LPCWSTR title, DWORD style, int x, int y, int w, int h, HWND parent, void *menu, HINSTANCE inst, void *param);
BOOL DestroyWindow(HWND w);
BOOL ShowWindow(HWND w, int how);
LRESULT DefWindowProcW(HWND w, UINT msg, WPARAM wp, LPARAM lp);
BOOL PeekMessageW(MSG *m, HWND w, UINT lo, UINT hi, UINT remove);
BOOL TranslateMessage(const MSG *m);
LRESULT DispatchMessageW(const MSG *m);
void PostQuitMessage(int code);
BOOL InvalidateRect(HWND w, const RECT *r, BOOL erase);
HDC BeginPaint(HWND w, PAINTSTRUCT *ps);
BOOL EndPaint(HWND w, const PAINTSTRUCT *ps);
BOOL GetClientRect(HWND w, RECT *r);
BOOL GetWindowRect(HWND w, RECT *r);
BOOL AdjustWindowRect(RECT *r, DWORD style, BOOL menu);
BOOL SetWindowPos(HWND w, HWND after, int x, int y, int cx, int cy, UINT flags);
HWND GetForegroundWindow(void);
BOOL IsIconic(HWND w);
LONG GetWindowLongW(HWND w, int index);
LONG SetWindowLongW(HWND w, int index, LONG value);
BOOL GetWindowPlacement(HWND w, WINDOWPLACEMENT *p);
BOOL SetWindowPlacement(HWND w, const WINDOWPLACEMENT *p);
HMONITOR MonitorFromWindow(HWND w, DWORD flags);
HMONITOR MonitorFromRect(const RECT *r, DWORD flags);
BOOL GetMonitorInfoW(HMONITOR m, MONITORINFO *mi);
BOOL EnumDisplaySettingsW(const wchar_t *device, DWORD mode, DEVMODEW *dm);
HICON LoadIconW(HINSTANCE inst, LPCWSTR name);
HANDLE LoadCursorW(HINSTANCE inst, LPCWSTR name);
long DwmFlush(void);

// ---- sound
#define WAVE_MAPPER ((UINT)-1)
#define WAVE_FORMAT_PCM 1
#define CALLBACK_NULL 0
#define CALLBACK_EVENT 0x00050000
#define MMSYSERR_NOERROR 0
#define WHDR_DONE 1
#define WHDR_PREPARED 2
typedef struct { WORD wFormatTag, nChannels; DWORD nSamplesPerSec, nAvgBytesPerSec; WORD nBlockAlign, wBitsPerSample, cbSize; } WAVEFORMATEX;
typedef struct wavehdr_tag { LPSTR lpData; DWORD dwBufferLength, dwBytesRecorded; DWORD_PTR dwUser; DWORD dwFlags, dwLoops; struct wavehdr_tag *lpNext; DWORD_PTR reserved; } WAVEHDR;
UINT waveOutOpen(HWAVEOUT *h, UINT device, const WAVEFORMATEX *fmt, DWORD_PTR cb, DWORD_PTR inst, DWORD flags);
UINT waveOutPrepareHeader(HWAVEOUT h, WAVEHDR *hdr, UINT size);
UINT waveOutWrite(HWAVEOUT h, WAVEHDR *hdr, UINT size);
UINT waveOutReset(HWAVEOUT h);
UINT waveOutClose(HWAVEOUT h);
#define SND_ASYNC 0x1
#define SND_NODEFAULT 0x2
#define SND_MEMORY 0x4
BOOL PlaySoundA(const char *sound, HMODULE m, DWORD flags);

// ---- the keyboard (Windows' virtual-key codes, as in the settings file)
#define VK_BACK 0x08
#define VK_TAB 0x09
#define VK_RETURN 0x0D
#define VK_SHIFT 0x10
#define VK_ESCAPE 0x1B
#define VK_SPACE 0x20
#define VK_PRIOR 0x21
#define VK_NEXT 0x22
#define VK_END 0x23
#define VK_HOME 0x24
#define VK_LEFT 0x25
#define VK_UP 0x26
#define VK_RIGHT 0x27
#define VK_DOWN 0x28
#define VK_INSERT 0x2D
#define VK_DELETE 0x2E
#define VK_DIVIDE 0x6F
#define VK_F1 0x70
#define VK_F2 0x71
#define VK_F3 0x72
#define VK_F5 0x74
#define VK_F7 0x76
#define VK_F11 0x7A
#define VK_F12 0x7B
#define VK_LSHIFT 0xA0
#define VK_RSHIFT 0xA1
#define VK_LCONTROL 0xA2
#define VK_RCONTROL 0xA3
#define VK_LMENU 0xA4
#define VK_RMENU 0xA5
#define MAPVK_VK_TO_VSC 0
#define MAPVK_VSC_TO_VK 1
SHORT GetAsyncKeyState(int vk);
UINT MapVirtualKeyW(UINT code, UINT type);
int GetKeyNameTextW(LONG lp, wchar_t *out, int n);

// ---- controllers: XInput (dl_pads.c, from SDL's gamepads) and WinMM joysticks (none: SDL reads them all)
typedef struct { WORD wButtons; BYTE bLeftTrigger, bRightTrigger; SHORT sThumbLX, sThumbLY, sThumbRX, sThumbRY; } XINPUT_GAMEPAD;
typedef struct { DWORD dwPacketNumber; XINPUT_GAMEPAD Gamepad; } XINPUT_STATE;
typedef struct { WORD wLeftMotorSpeed, wRightMotorSpeed; } XINPUT_VIBRATION;
DWORD WINAPI dl_xinput_get(DWORD slot, XINPUT_STATE *st);
DWORD WINAPI dl_xinput_set(DWORD slot, XINPUT_VIBRATION *v);
#define JOYERR_NOERROR 0
#define JOY_RETURNALL 0xFF
typedef struct { DWORD dwSize, dwFlags, dwXpos, dwYpos, dwZpos, dwRpos, dwUpos, dwVpos, dwButtons, dwButtonNumber, dwPOV, dwReserved1, dwReserved2; } JOYINFOEX;
typedef struct { WORD wMid, wPid; wchar_t szPname[32]; } JOYCAPSW;
UINT joyGetNumDevs(void);
UINT joyGetPosEx(UINT id, JOYINFOEX *ji);
UINT joyGetDevCapsW(UINT id, JOYCAPSW *caps, UINT size);

// ---- wide strings (wchar_t is 32 bits here: the same texts, the same code)
#define lstrlenW(s) ((int)wcslen(s))
#define lstrcpyW(d, s) wcscpy(d, s)
#define lstrcmpW(a, b) wcscmp(a, b)
#define lstrcmpiW(a, b) wcscasecmp(a, b)
#define lstrcatW(d, s) wcscat(d, s)
static inline wchar_t *lstrcpynW(wchar_t *d, const wchar_t *s, int n) {
  if (n <= 0) return d;
  wcsncpy(d, s, (size_t)n - 1);
  d[n - 1] = 0;
  return d;
}
#define wsprintfW(out, ...) swprintf(out, 1024, __VA_ARGS__)
#define _snwprintf swprintf
static inline wchar_t *CharUpperW(wchar_t *s) {
  for (wchar_t *p = s; *p; p++) *p = (wchar_t)towupper((wint_t)*p);
  return s;
}
static inline wchar_t *CharLowerW(wchar_t *s) {
  for (wchar_t *p = s; *p; p++) *p = (wchar_t)towlower((wint_t)*p);
  return s;
}
#endif
