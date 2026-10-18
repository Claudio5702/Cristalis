// Small self-contained installer, after the one of Kahna (Bahamut Lagoon Recomp). The files to
// install are appended to this executable by tools\make_installer.ps1:
//   [file data ...][table: count x {char name[260]; u64 size; u64 offset}][u64 table offset][u32 count]["CDSETUP1"]
//   A file whose stored length (up to the next offset) is smaller than its size is compressed with the
//   Windows Compression API (LZMS, cabinet.dll); otherwise it is stored as it is.
// The game's data is NOT part of the payload. When the player's own copy (the console's
// "CRYSTAL DEFENDERS" or "58410989" folder) sits next to the installer, the game is installed from it
// in one go; otherwise the last window asks for that folder. Either way the game program itself
// does the work (crystal_defenders.exe --install-from <folder>).
//
//   Setup.exe                     interactive: one window (folder, shortcuts, what happens with the game's data)
//   Setup.exe /S /D=C:\some\dir   silent; add /NOSHORTCUTS to skip the shortcuts and the Windows entry
//
// The installer also drops a copy of itself without payload as Uninstall.exe. Started without
// payload it removes what was installed ("/S" silent; "/ALL" also removes saves, settings and game data).
#define COBJMACROS
#include <windows.h>
#include <shlobj.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stdbool.h>

#define APP "Crystal Defenders"
#define EXE "crystal_defenders.exe"

typedef struct { char name[260]; uint64_t size, offset; } Entry;

// Decompression by Windows itself (Compression API, Windows 8 and later), loaded by name so that
// the program still starts where cabinet.dll does not offer it.
static bool unpack(const char *in, size_t in_size, char *out, size_t out_size) {
  typedef BOOL(WINAPI * CreateFn)(DWORD, void *, HANDLE *);
  typedef BOOL(WINAPI * RunFn)(HANDLE, const void *, SIZE_T, void *, SIZE_T, SIZE_T *);
  typedef BOOL(WINAPI * CloseFn)(HANDLE);
  HMODULE cab = LoadLibraryA("cabinet.dll");
  if (!cab) return false;
  CreateFn create = (CreateFn)(void *)GetProcAddress(cab, "CreateDecompressor");
  RunFn run = (RunFn)(void *)GetProcAddress(cab, "Decompress");
  CloseFn close_it = (CloseFn)(void *)GetProcAddress(cab, "CloseDecompressor");
  HANDLE h = NULL;
  SIZE_T got = 0;
  if (!create || !run || !close_it || !create(5 /* LZMS */, NULL, &h)) return false;
  bool ok = run(h, in, in_size, out, out_size, &got) && got == out_size;
  close_it(h);
  return ok;
}

static bool silent, no_system; // no_system: leave shortcuts and the Windows entry alone

#include "setup_text.h"

// A line in the terminal the installer was started from: written as text of the console itself, so
// that accents and other scripts come out right whatever its code page; UTF-8 when redirected.
static void say(HANDLE out, const wchar_t *text) {
  DWORD mode, done;
  if (!out || out == INVALID_HANDLE_VALUE) // attached to the parent's console without standard handles
    out = CreateFileW(L"CONOUT$", GENERIC_READ | GENERIC_WRITE, FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, NULL);
  if (out == INVALID_HANDLE_VALUE) return;
  if (GetConsoleMode(out, &mode)) {
    WriteConsoleW(out, text, (DWORD)wcslen(text), &done, NULL);
    WriteConsoleW(out, L"\n", 1, &done, NULL);
    return;
  }
  char utf8[4096];
  int n = WideCharToMultiByte(CP_UTF8, 0, text, -1, utf8, sizeof utf8 - 1, NULL, NULL);
  if (n > 0) { utf8[n - 1] = '\n'; WriteFile(out, utf8, (DWORD)n, &done, NULL); }
}

// Message box with the text `id`, formatted with a path (unused when the text has none).
static int box(int title, int id, UINT flags, const char *path) {
  wchar_t text[2048];
  _snwprintf(text, 2047, T(id), path ? path : "");
  text[2047] = 0;
  if (silent) { say(GetStdHandle(STD_ERROR_HANDLE), text); return IDYES; }
  return MessageBoxW(NULL, text, T(title), flags);
}

static void fail_id(int id, const char *path) {
  box(T_TITLE_INSTALL, id, MB_ICONERROR, path);
  ExitProcess(1);
}

// ---------------------------------------------------------------- window
// Every interactive step is one plain window built from this description: a heading, a paragraph,
// optionally a folder field, check boxes and a note, then one or two buttons.
typedef struct {
  const wchar_t *caption, *heading, *body;
  char *path;                  // folder field with a Browse button (MAX_PATH buffer), or NULL
  const wchar_t *path_label;
  const wchar_t *check[2];
  bool *checked[2];
  const wchar_t *note;         // grey explanatory text, or NULL
  const wchar_t *ok, *cancel;  // ok may be NULL (a window that only shows something); cancel too
} Dlg;

enum { ID_EDIT = 100, ID_BROWSE, ID_CHECK };
static Dlg *dlg;
static bool dlg_ok, dlg_open;
static HWND dlg_edit, dlg_chk[2], dlg_head, dlg_note;
static HFONT f_text, f_head;
static int dpi = 96;
static int S(int v) { return MulDiv(v, dpi, 96); }

static int CALLBACK browse_cb(HWND h, UINT msg, LPARAM lp, LPARAM data) {
  (void)lp;
  if (msg == BFFM_INITIALIZED && data) SendMessageA(h, BFFM_SETSELECTIONA, TRUE, data);
  return 0;
}

// Folder picker; false when cancelled.
static bool pick_folder(HWND owner, const wchar_t *title, const char *start, char *picked) {
  BROWSEINFOW bi = {0};
  bi.hwndOwner = owner;
  bi.lpszTitle = title;
  bi.ulFlags = BIF_RETURNONLYFSDIRS | BIF_NEWDIALOGSTYLE;
  bi.lpfn = browse_cb;
  bi.lParam = (LPARAM)start;
  LPITEMIDLIST id = SHBrowseForFolderW(&bi);
  bool ok = id && SHGetPathFromIDListA(id, picked);
  if (id) CoTaskMemFree(id);
  return ok;
}

static void browse(HWND owner) {
  char cur[MAX_PATH], picked[MAX_PATH];
  GetWindowTextA(dlg_edit, cur, sizeof cur);
  if (!pick_folder(owner, T(T_BROWSE), cur, picked)) return;
  size_t n = strlen(picked);
  while (n > 3 && picked[n - 1] == '\\') picked[--n] = 0;
  // the game gets its own folder inside the chosen one, unless that folder is already named after it
  const char *leaf = strrchr(picked, '\\');
  if (!leaf || _stricmp(leaf + 1, APP)) snprintf(picked + n, sizeof picked - n, "%s" APP, picked[n - 1] == '\\' ? "" : "\\");
  SetWindowTextA(dlg_edit, picked);
}

static LRESULT CALLBACK dlg_proc(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
  switch (msg) {
    case WM_COMMAND:
      switch (LOWORD(wp)) {
        case ID_BROWSE: browse(h); return 0;
        case IDOK:
          if (dlg->path) {
            char tmp[MAX_PATH];
            GetWindowTextA(dlg_edit, tmp, sizeof tmp);
            size_t n = strlen(tmp);
            while (n && (tmp[n - 1] == ' ' || (n > 3 && tmp[n - 1] == '\\'))) tmp[--n] = 0;
            if (n < 3 || tmp[1] != ':') { SetFocus(dlg_edit); MessageBeep(MB_ICONWARNING); return 0; } // needs a full path
            snprintf(dlg->path, MAX_PATH, "%s", tmp);
          }
          for (int k = 0; k < 2; k++)
            if (dlg_chk[k]) *dlg->checked[k] = SendMessageW(dlg_chk[k], BM_GETCHECK, 0, 0) == BST_CHECKED;
          dlg_ok = true;
          DestroyWindow(h);
          return 0;
        case IDCANCEL: DestroyWindow(h); return 0;
      }
      break;
    case WM_CTLCOLORSTATIC: {
      HDC dc = (HDC)wp;
      SetBkMode(dc, TRANSPARENT);
      SetTextColor(dc, (HWND)lp == dlg_head ? RGB(14, 74, 104) : (HWND)lp == dlg_note ? RGB(70, 76, 84) : RGB(0, 0, 0));
      return (LRESULT)GetSysColorBrush(COLOR_WINDOW);
    }
    case WM_CLOSE: if (dlg->ok || dlg->cancel) DestroyWindow(h); return 0;
    case WM_DESTROY: dlg_open = false; return 0;
  }
  return DefWindowProcW(h, msg, wp, lp);
}

static HWND ctl(HWND parent, const wchar_t *cls, const wchar_t *text, DWORD style, int x, int y, int w, int h, int id, HFONT f) {
  HWND c = CreateWindowExW(!lstrcmpW(cls, L"EDIT") ? WS_EX_CLIENTEDGE : 0, cls, text, WS_CHILD | WS_VISIBLE | style, x, y, w, h, parent,
                           (HMENU)(INT_PTR)id, GetModuleHandleW(NULL), NULL);
  SendMessageW(c, WM_SETFONT, (WPARAM)f, TRUE);
  return c;
}

static int text_h(HFONT f, const wchar_t *s, int w) {
  HDC dc = GetDC(NULL);
  HGDIOBJ old = SelectObject(dc, f);
  RECT r = {0, 0, w, 0};
  DrawTextW(dc, s, -1, &r, DT_CALCRECT | DT_WORDBREAK | DT_EDITCONTROL | DT_NOPREFIX);
  SelectObject(dc, old);
  ReleaseDC(NULL, dc);
  return r.bottom;
}

// Builds the window of a step and shows it.
static HWND dialog_open(Dlg *d) {
  static bool ready;
  HINSTANCE inst = GetModuleHandleW(NULL);
  if (!ready) {
    ready = true;
    // current look of buttons and check boxes: use the common-controls manifest that Windows ships in shell32.dll
    wchar_t sys[MAX_PATH];
    GetSystemDirectoryW(sys, MAX_PATH);
    ACTCTXW ac = {sizeof ac};
    ac.dwFlags = ACTCTX_FLAG_RESOURCE_NAME_VALID | ACTCTX_FLAG_ASSEMBLY_DIRECTORY_VALID | ACTCTX_FLAG_SET_PROCESS_DEFAULT;
    ac.lpSource = L"shell32.dll";
    ac.lpAssemblyDirectory = sys;
    ac.lpResourceName = MAKEINTRESOURCEW(124);
    CreateActCtxW(&ac);
    LoadLibraryW(L"comctl32.dll");
    SetProcessDPIAware();
    HDC dc = GetDC(NULL);
    dpi = GetDeviceCaps(dc, LOGPIXELSY);
    ReleaseDC(NULL, dc);
    NONCLIENTMETRICSW m = {sizeof m};
    SystemParametersInfoW(SPI_GETNONCLIENTMETRICS, sizeof m, &m, 0);
    LOGFONTW lf = m.lfMessageFont;
    lf.lfHeight = -S(13);
    f_text = CreateFontIndirectW(&lf);
    lf.lfHeight = -S(21);
    lf.lfWeight = FW_SEMIBOLD;
    f_head = CreateFontIndirectW(&lf);
    WNDCLASSW wc = {0};
    wc.lpfnWndProc = dlg_proc;
    wc.hInstance = inst;
    wc.lpszClassName = L"CrystalDefendersSetup";
    wc.hCursor = LoadCursorW(NULL, (LPCWSTR)IDC_ARROW);
    wc.hIcon = LoadIconW(inst, MAKEINTRESOURCEW(1));
    wc.hbrBackground = GetSysColorBrush(COLOR_WINDOW);
    RegisterClassW(&wc);
  }
  dlg = d;
  dlg_ok = false;
  dlg_open = true;
  dlg_edit = dlg_chk[0] = dlg_chk[1] = dlg_note = NULL;
  HWND h = CreateWindowExW(0, L"CrystalDefendersSetup", d->caption, WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX, 0, 0, 100, 100,
                           NULL, NULL, inst, NULL);
  const int W = S(540), M = S(24), TW = W - 2 * M;
  int y = S(20), th;
  th = text_h(f_head, d->heading, TW);
  dlg_head = ctl(h, L"STATIC", d->heading, SS_NOPREFIX | SS_EDITCONTROL, M, y, TW, th, -1, f_head);
  y += th + S(12);
  th = text_h(f_text, d->body, TW);
  ctl(h, L"STATIC", d->body, SS_NOPREFIX | SS_EDITCONTROL, M, y, TW, th, -1, f_text);
  y += th + S(18);
  if (d->path) {
    th = text_h(f_text, d->path_label, TW);
    ctl(h, L"STATIC", d->path_label, SS_NOPREFIX, M, y, TW, th, -1, f_text);
    y += th + S(5);
    dlg_edit = ctl(h, L"EDIT", L"", ES_AUTOHSCROLL | WS_TABSTOP, M, y, TW - S(118), S(25), ID_EDIT, f_text);
    SetWindowTextA(dlg_edit, d->path);
    ctl(h, L"BUTTON", T(T_BROWSE_BTN), BS_PUSHBUTTON | WS_TABSTOP, W - M - S(110), y - S(1), S(110), S(27), ID_BROWSE, f_text);
    y += S(25) + S(18);
  }
  for (int k = 0; k < 2; k++)
    if (d->check[k]) {
      // long texts go on several lines (the box itself takes about 22 pixels on the left)
      int ch = text_h(f_text, d->check[k], TW - S(22)) + S(4);
      if (ch < S(21)) ch = S(21);
      dlg_chk[k] = ctl(h, L"BUTTON", d->check[k], BS_AUTOCHECKBOX | BS_MULTILINE | WS_TABSTOP, M, y, TW, ch, ID_CHECK + k, f_text);
      SendMessageW(dlg_chk[k], BM_SETCHECK, *d->checked[k] ? BST_CHECKED : BST_UNCHECKED, 0);
      y += ch + S(4);
    }
  if (d->check[0]) y += S(8);
  if (d->note) {
    th = text_h(f_text, d->note, TW);
    dlg_note = ctl(h, L"STATIC", d->note, SS_NOPREFIX | SS_EDITCONTROL, M, y, TW, th, -1, f_text);
    y += th + S(18);
  }
  HWND ok = NULL;
  if (d->ok || d->cancel) {
    y += S(4);
    int bx = W - M;
    if (d->cancel) {
      bx -= S(110);
      ctl(h, L"BUTTON", d->cancel, BS_PUSHBUTTON | WS_TABSTOP, bx, y, S(110), S(30), IDCANCEL, f_text);
      bx -= S(10);
    }
    if (d->ok) {
      bx -= S(190);
      ok = ctl(h, L"BUTTON", d->ok, BS_DEFPUSHBUTTON | WS_TABSTOP, bx, y, S(190), S(30), IDOK, f_text);
    }
    y += S(30) + S(20);
  }

  RECT r = {0, 0, W, y};
  AdjustWindowRect(&r, (DWORD)GetWindowLongW(h, GWL_STYLE), FALSE);
  int ww = r.right - r.left, wh = r.bottom - r.top;
  SetWindowPos(h, NULL, (GetSystemMetrics(SM_CXSCREEN) - ww) / 2, (GetSystemMetrics(SM_CYSCREEN) - wh) / 2, ww, wh, SWP_NOZORDER);
  ShowWindow(h, SW_SHOW);
  SetForegroundWindow(h);
  if (ok) SetFocus(ok);
  return h;
}

// Shows the window and waits; true when the main button was chosen.
static bool dialog(Dlg *d) {
  HWND h = dialog_open(d);
  MSG msg;
  while (dlg_open && GetMessageW(&msg, NULL, 0, 0) > 0) {
    if (IsDialogMessageW(h, &msg)) continue;
    TranslateMessage(&msg);
    DispatchMessageW(&msg);
  }
  return dlg_ok;
}

static void make_dirs(const char *path) { // creates every folder of a file path
  char tmp[MAX_PATH];
  snprintf(tmp, sizeof tmp, "%s", path);
  for (char *p = tmp + 3; *p; p++)
    if (*p == '\\') { *p = 0; CreateDirectoryA(tmp, NULL); *p = '\\'; }
}

static bool shortcut(const char *lnk, const char *target, const char *workdir) {
  IShellLinkA *sl = NULL;
  IPersistFile *pf = NULL;
  bool ok = false;
  if (CoCreateInstance(&CLSID_ShellLink, NULL, CLSCTX_INPROC_SERVER, &IID_IShellLinkA, (void **)&sl) != S_OK) return false;
  IShellLinkA_SetPath(sl, target);
  IShellLinkA_SetWorkingDirectory(sl, workdir);
  IShellLinkA_SetDescription(sl, APP);
  if (IShellLinkA_QueryInterface(sl, &IID_IPersistFile, (void **)&pf) == S_OK) {
    wchar_t w[MAX_PATH];
    MultiByteToWideChar(CP_ACP, 0, lnk, -1, w, MAX_PATH);
    ok = IPersistFile_Save(pf, w, TRUE) == S_OK;
    IPersistFile_Release(pf);
  }
  IShellLinkA_Release(sl);
  return ok;
}

static bool is_dir(const char *path) {
  DWORD a = GetFileAttributesA(path);
  return a != INVALID_FILE_ATTRIBUTES && (a & FILE_ATTRIBUTE_DIRECTORY);
}

static bool exists(const char *path) { return GetFileAttributesA(path) != INVALID_FILE_ATTRIBUTES; }

// The player's copy of the game next to the installer: the console's folder, named after the game or
// after its title id, or a folder of files already taken out of it. Empty when there is none.
static void find_game_copy(const char *dir, char *out) {
  static const char *const names[] = {"CRYSTAL DEFENDERS", "58410989", "game"};
  char path[MAX_PATH];
  out[0] = 0;
  for (int k = 0; k < 3; k++) {
    snprintf(path, sizeof path, "%s%s", dir, names[k]);
    if (!is_dir(path)) continue;
    if (k == 2) { // only with the game's files in it
      char xex[MAX_PATH];
      snprintf(xex, sizeof xex, "%s\\default.xex", path);
      if (!exists(xex)) continue;
    }
    snprintf(out, MAX_PATH, "%s", path);
    return;
  }
}

static bool game_ready(const char *dest) {
  char path[MAX_PATH];
  snprintf(path, sizeof path, "%s\\game\\default.xex", dest);
  return exists(path);
}

// Has the installed game take its data out of the player's copy, while a small window says so.
static bool install_game_data(const char *dest, const char *copy) {
  char cmd[3 * MAX_PATH], exe[MAX_PATH];
  snprintf(exe, sizeof exe, "%s\\" EXE, dest);
  snprintf(cmd, sizeof cmd, "\"%s\" --install-from \"%s\"", exe, copy);
  STARTUPINFOA si = {sizeof si};
  PROCESS_INFORMATION pi;
  if (!CreateProcessA(exe, cmd, NULL, NULL, FALSE, 0, NULL, dest, &si, &pi)) return false;
  CloseHandle(pi.hThread);
  HWND wait = NULL;
  if (!silent) {
    static Dlg working;
    working = (Dlg){T(T_TITLE_INSTALL), T(T_TITLE_INSTALL), T(T_WORKING), NULL, NULL, {NULL, NULL}, {NULL, NULL}, NULL, NULL, NULL};
    wait = dialog_open(&working);
    SetCursor(LoadCursorW(NULL, (LPCWSTR)IDC_WAIT));
  }
  while (MsgWaitForMultipleObjects(1, &pi.hProcess, FALSE, INFINITE, QS_ALLINPUT) == WAIT_OBJECT_0 + 1) {
    MSG msg;
    while (PeekMessageW(&msg, NULL, 0, 0, PM_REMOVE)) {
      TranslateMessage(&msg);
      DispatchMessageW(&msg);
    }
  }
  DWORD code = 1;
  GetExitCodeProcess(pi.hProcess, &code);
  CloseHandle(pi.hProcess);
  if (wait) DestroyWindow(wait);
  return code == 0 && game_ready(dest);
}

#define UNINST_KEY "Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\CrystalDefendersRecomp"

// Everything inside a folder, sub-folders included; the folders themselves are removed too (but not 'dir').
static int delete_tree(const char *dir) {
  char pat[MAX_PATH], path[MAX_PATH];
  WIN32_FIND_DATAA fd;
  int n = 0;
  snprintf(pat, sizeof pat, "%s\\*", dir);
  HANDLE h = FindFirstFileA(pat, &fd);
  if (h == INVALID_HANDLE_VALUE) return 0;
  do {
    if (!strcmp(fd.cFileName, ".") || !strcmp(fd.cFileName, "..")) continue;
    snprintf(path, sizeof path, "%s\\%s", dir, fd.cFileName);
    if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) { n += delete_tree(path); RemoveDirectoryA(path); }
    else if (DeleteFileA(path)) n++;
  } while (FindNextFileA(h, &fd));
  FindClose(h);
  return n;
}

static int delete_file(const char *dir, const char *name) {
  char path[MAX_PATH];
  snprintf(path, sizeof path, "%s\\%s", dir, name);
  return DeleteFileA(path) ? 1 : 0;
}

static void remove_shortcuts(void) {
  char folder[MAX_PATH], lnk[MAX_PATH];
  static const int where[] = {CSIDL_DESKTOPDIRECTORY, CSIDL_PROGRAMS};
  for (int k = 0; k < 2; k++)
    if (SHGetFolderPathA(NULL, where[k], NULL, 0, folder) == S_OK) {
      snprintf(lnk, sizeof lnk, "%s\\" APP ".lnk", folder);
      DeleteFileA(lnk);
    }
}

// A running program's file cannot be opened for writing by anyone else.
static bool game_running(const char *exe) {
  HANDLE h = CreateFileA(exe, GENERIC_WRITE, 0, NULL, OPEN_EXISTING, 0, NULL);
  if (h != INVALID_HANDLE_VALUE) { CloseHandle(h); return false; }
  DWORD e = GetLastError();
  return e == ERROR_SHARING_VIOLATION || e == ERROR_ACCESS_DENIED;
}

static int uninstall(const char *self, const char *dir, bool all) {
  char path[MAX_PATH], d[MAX_PATH];
  snprintf(d, sizeof d, "%s", dir);
  size_t n = strlen(d);
  while (n > 3 && d[n - 1] == '\\') d[--n] = 0;
  // only ever act on a folder that really holds the game
  snprintf(path, sizeof path, "%s\\" EXE, d);
  if (!exists(path)) fail_id(T_UN_NOTGAME, NULL);
  if (!silent) {
    wchar_t body[1024];
    _snwprintf(body, 1023, T(T_UN_INTRO), d);
    body[1023] = 0;
    all = false;
    Dlg ask = {T(T_TITLE_UNINSTALL), T(T_TITLE_UNINSTALL), body, NULL, NULL, {T(T_UN_CHK_ALL), NULL}, {&all, NULL}, T(T_UN_NOTE),
               T(T_BTN_UNINSTALL), T(T_BTN_CANCEL)};
    if (!dialog(&ask)) return 0;
  }
  // The game must not be running: Windows keeps a running program's file, and the uninstaller would
  // then erase itself and leave the game behind without a way to remove it.
  while (game_running(path)) {
    if (silent) { box(T_TITLE_UNINSTALL, T_UN_RUNNING, 0, NULL); return 1; }
    if (box(T_TITLE_UNINSTALL, T_UN_RUNNING, MB_RETRYCANCEL | MB_ICONWARNING, NULL) != IDRETRY) return 0;
  }
  int removed = 0;
  static const char *const program[] = {EXE, "avcodec-62.dll", "avutil-60.dll", "swresample-6.dll", "FFmpeg-LICENSE.txt",
                                        "LISEZMOI.txt", "README.txt", "crystal_defenders.log", "crystal_defenders.prev.log", "Jouer.bat",
                                        "gpu_shaders.bin"}; // DarkLauncher's compiled shaders
  for (size_t k = 0; k < sizeof program / sizeof program[0]; k++) removed += delete_file(d, program[k]);
  static const char *const data[] = {"game", "userdata", "captures"}; // the game's data, the saves and achievements, screenshots
  if (all) {
    removed += delete_file(d, "crystal_defenders.ini");
    for (int k = 0; k < 3; k++) {
      snprintf(path, sizeof path, "%s\\%s", d, data[k]);
      removed += delete_tree(path);
    }
  }
  // folders left empty go too (RemoveDirectory never removes a folder that still holds something)
  for (int k = 0; k < 3; k++) {
    snprintf(path, sizeof path, "%s\\%s", d, data[k]);
    RemoveDirectoryA(path);
  }
  if (!no_system) {
    remove_shortcuts();
    RegDeleteKeyA(HKEY_CURRENT_USER, UNINST_KEY);
  }
  if (silent) {
    wchar_t line[1024];
    _snwprintf(line, 1023, L"uninstalled: %d files removed from %hs (personal data %hs)", removed, d, all ? "removed" : "kept");
    line[1023] = 0;
    say(GetStdHandle(STD_OUTPUT_HANDLE), line);
  }
  else box(T_TITLE_UNINSTALL, all ? T_UN_DONE : T_UN_DONE_KEPT, MB_ICONINFORMATION, NULL);
  // This program cannot erase its own file while it runs. Once it has ended, a hidden helper erases it
  // (it tries again every second for up to 30 seconds), then removes the folder if nothing is left in
  // it. The helper must not be inside the folder itself, or the folder could not be removed.
  char cmd[4 * MAX_PATH], tmp[MAX_PATH];
  snprintf(cmd, sizeof cmd,
           "cmd.exe /c for /L %%i in (1,1,30) do @(if exist \"%s\" (ping -n 2 127.0.0.1 >nul & del /f /q \"%s\" 2>nul)) & rmdir \"%s\" 2>nul",
           self, self, d);
  GetTempPathA(sizeof tmp, tmp);
  SetCurrentDirectoryA(tmp);
  STARTUPINFOA si = {sizeof si};
  PROCESS_INFORMATION pi;
  if (CreateProcessA(NULL, cmd, NULL, NULL, FALSE, CREATE_NO_WINDOW, NULL, tmp, &si, &pi)) {
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
  }
  return 0;
}

int main(int argc, char **argv) {
  char self[MAX_PATH], self_dir[MAX_PATH], dest[MAX_PATH] = "";
  const char *forced_lang = NULL;
  bool shortcuts = true, all = false;
  for (int k = 1; k < argc; k++) {
    if (!_stricmp(argv[k], "/S")) silent = true;
    else if (!_stricmp(argv[k], "/ALL")) all = true;
    else if (!_strnicmp(argv[k], "/LANG=", 6)) forced_lang = argv[k] + 6;
    else if (!_stricmp(argv[k], "/NOSHORTCUTS")) { shortcuts = false; no_system = true; }
    else if (!_strnicmp(argv[k], "/D=", 3)) snprintf(dest, sizeof dest, "%s", argv[k] + 3);
  }
  { // window program: print to the terminal it was started from, if any
    HANDLE ho = GetStdHandle(STD_OUTPUT_HANDLE);
    if (!ho || ho == INVALID_HANDLE_VALUE || GetFileType(ho) == FILE_TYPE_UNKNOWN) AttachConsole(ATTACH_PARENT_PROCESS);
  }
  pick_language(forced_lang);
  GetModuleFileNameA(NULL, self, sizeof self);
  snprintf(self_dir, sizeof self_dir, "%s", self);
  char *slash = strrchr(self_dir, '\\');
  if (slash) slash[1] = 0;

  // ---- payload table
  FILE *f = fopen(self, "rb");
  if (!f) fail_id(T_BAD_SETUP, NULL);
  uint64_t table_off = 0;
  uint32_t count = 0;
  char magic[8];
  _fseeki64(f, -20, SEEK_END);
  bool has_payload = fread(&table_off, 8, 1, f) == 1 && fread(&count, 4, 1, f) == 1 && fread(magic, 1, 8, f) == 8 &&
                     !memcmp(magic, "CDSETUP1", 8) && count > 0 && count <= 64;
  if (!has_payload) { // this is the copy left in the installation folder
    fclose(f);
    CoInitialize(NULL);
    return uninstall(self, self_dir, all);
  }
  Entry ent[64];
  _fseeki64(f, (int64_t)table_off, SEEK_SET);
  if (fread(ent, sizeof(Entry), count, f) != count) fail_id(T_BAD_SETUP, NULL);

  // ---- destination
  if (!dest[0]) {
    char base[MAX_PATH];
    SHGetFolderPathA(NULL, CSIDL_LOCAL_APPDATA, NULL, 0, base);
    snprintf(dest, sizeof dest, "%s\\Programs\\" APP, base);
  }
  CoInitialize(NULL);
  char copy[MAX_PATH];
  find_game_copy(self_dir, copy);
  bool on_desktop = shortcuts, in_menu = shortcuts;
  if (!silent) {
    // everything is asked in one window, which also says what will happen with the game's data
    wchar_t note[1024];
    if (copy[0]) _snwprintf(note, 1023, T(T_GAME_FOUND), strrchr(copy, '\\') + 1);
    else _snwprintf(note, 1023, L"%ls", T(T_GAME_MISSING));
    note[1023] = 0;
    Dlg ask = {T(T_TITLE_INSTALL), T(T_TITLE_INSTALL), T(T_INTRO), dest, T(T_DIR_LABEL), {T(T_CHK_DESKTOP), T(T_CHK_MENU)},
               {&on_desktop, &in_menu}, note, T(T_BTN_INSTALL), T(T_BTN_CANCEL)};
    if (!dialog(&ask)) return 0;
  }
  size_t dn = strlen(dest);
  while (dn > 3 && dest[dn - 1] == '\\') dest[--dn] = 0;

  // ---- files
  char path[MAX_PATH];
  snprintf(path, sizeof path, "%s\\x", dest);
  make_dirs(path);
  static char buf[1 << 16];
  for (uint32_t k = 0; k < count; k++) {
    snprintf(path, sizeof path, "%s\\%s", dest, ent[k].name);
    make_dirs(path);
    FILE *o = fopen(path, "wb");
    if (!o) fail_id(T_CANT_WRITE, path);
    _fseeki64(f, (int64_t)ent[k].offset, SEEK_SET);
    uint64_t stored = (k + 1 < count ? ent[k + 1].offset : table_off) - ent[k].offset;
    if (stored < ent[k].size) { // compressed
      char *in = malloc((size_t)stored), *out = malloc((size_t)ent[k].size);
      bool ok = in && out && fread(in, 1, (size_t)stored, f) == stored && unpack(in, (size_t)stored, out, (size_t)ent[k].size) &&
                fwrite(out, 1, (size_t)ent[k].size, o) == ent[k].size;
      free(in);
      free(out);
      if (!ok) { fclose(o); fail_id(T_COPY_ERR, NULL); }
    } else {
      uint64_t left = ent[k].size;
      while (left) {
        size_t n = left > sizeof buf ? sizeof buf : (size_t)left;
        if (fread(buf, 1, n, f) != n || fwrite(buf, 1, n, o) != n) { fclose(o); fail_id(T_COPY_ERR, NULL); }
        left -= n;
      }
    }
    fclose(o);
  }
  // uninstaller: this program without its payload
  {
    uint64_t stub = ent[0].offset;
    for (uint32_t k = 1; k < count; k++) if (ent[k].offset < stub) stub = ent[k].offset;
    snprintf(path, sizeof path, "%s\\Uninstall.exe", dest);
    FILE *o = fopen(path, "wb");
    if (o) {
      _fseeki64(f, 0, SEEK_SET);
      uint64_t left = stub;
      while (left) {
        size_t n = left > sizeof buf ? sizeof buf : (size_t)left;
        if (fread(buf, 1, n, f) != n || fwrite(buf, 1, n, o) != n) break;
        left -= n;
      }
      fclose(o);
    }
  }
  fclose(f);

  // ---- shortcuts
  char exe[MAX_PATH], lnk[MAX_PATH], folder[MAX_PATH];
  snprintf(exe, sizeof exe, "%s\\" EXE, dest);
  if (on_desktop && SHGetFolderPathA(NULL, CSIDL_DESKTOPDIRECTORY, NULL, 0, folder) == S_OK) {
    snprintf(lnk, sizeof lnk, "%s\\" APP ".lnk", folder);
    shortcut(lnk, exe, dest);
  }
  if (in_menu && SHGetFolderPathA(NULL, CSIDL_PROGRAMS, NULL, 0, folder) == S_OK) {
    snprintf(lnk, sizeof lnk, "%s\\" APP ".lnk", folder);
    shortcut(lnk, exe, dest);
  }

  if (!no_system) { // entry in the Windows list of installed applications (current user only)
    HKEY key;
    if (RegCreateKeyExA(HKEY_CURRENT_USER, UNINST_KEY, 0, NULL, 0, KEY_WRITE, NULL, &key, NULL) == ERROR_SUCCESS) {
      char un[MAX_PATH + 2];
      DWORD one = 1;
      static const char name[] = "Crystal Defenders (CrystalDefendersRecomp)", publisher[] = "CrystalDefendersRecomp (unofficial)";
      snprintf(un, sizeof un, "\"%s\\Uninstall.exe\"", dest);
      RegSetValueExA(key, "DisplayName", 0, REG_SZ, (const BYTE *)name, sizeof name);
      RegSetValueExA(key, "Publisher", 0, REG_SZ, (const BYTE *)publisher, sizeof publisher);
      RegSetValueExA(key, "UninstallString", 0, REG_SZ, (const BYTE *)un, (DWORD)strlen(un) + 1);
      RegSetValueExA(key, "InstallLocation", 0, REG_SZ, (const BYTE *)dest, (DWORD)strlen(dest) + 1);
      RegSetValueExA(key, "DisplayIcon", 0, REG_SZ, (const BYTE *)exe, (DWORD)strlen(exe) + 1);
      RegSetValueExA(key, "NoModify", 0, REG_DWORD, (const BYTE *)&one, 4);
      RegSetValueExA(key, "NoRepair", 0, REG_DWORD, (const BYTE *)&one, 4);
      RegCloseKey(key);
    }
  }

  // ---- the game's data: from the copy next to the installer, unless it is already there
  bool ready = game_ready(dest);
  if (!ready && copy[0]) ready = install_game_data(dest, copy);

  if (silent) {
    wchar_t line[1024];
    _snwprintf(line, 1023, L"installed %u files to %hs (game data %hs)", count, dest, ready ? "ready" : "missing");
    line[1023] = 0;
    say(GetStdHandle(STD_OUTPUT_HANDLE), line);
    return 0;
  }
  // ---- result: start the game when it is ready to run, otherwise ask for the player's copy; the
  // window then comes back with "start the game".
  for (;;) {
    wchar_t body[2048];
    _snwprintf(body, 2047, T(ready ? T_DONE_READY : T_DONE_NOGAME), dest);
    body[2047] = 0;
    Dlg done = {T(T_TITLE_INSTALL), T(T_DONE_TITLE), body, NULL, NULL, {NULL, NULL}, {NULL, NULL}, NULL, T(ready ? T_BTN_PLAY : T_BTN_PICK), T(T_BTN_CLOSE)};
    if (!dialog(&done)) return 0;
    if (ready) break;
    char picked[MAX_PATH];
    if (!pick_folder(NULL, T(T_PICK_TITLE), NULL, picked)) continue; // cancelled: back to the window
    ready = install_game_data(dest, picked);
    if (!ready) MessageBoxW(NULL, T(T_GAME_FAIL), T(T_TITLE_INSTALL), MB_OK | MB_ICONWARNING);
  }
  ShellExecuteA(NULL, "open", exe, NULL, dest, SW_SHOWNORMAL);
  return 0;
}
