// Save states, screenshots and the notes shown over the game, taken from Kahna.
//
// Save states: ten slots (by default in the "saves" folder, <FILE_STEM>.state0 to .state9; the game may
// say where), each with the picture of the game when it was saved beside it (.png) and maybe a name
// (.name); the game's automatic states; the session kept at closing time; the rewind. The game writes
// and reads the state itself (DlGame.save_state, load_state, state_to_mem...). Screenshots: PNG files in the "captures" folder, the picture as it is
// shown (filters included) when the graphics card draws it, the game's own picture otherwise. Both
// have a page of the setup screen: a list, the picture of the selected row instead of the live preview.
#include "dl_int.h"
#include <shellapi.h>

int dl_slot;
bool dl_state_taken; // a state was loaded since the game was last started or stopped

// ---- notes over the game ("state 3 saved"...), for a few seconds
static wchar_t note_text[160];
static DWORD note_until;

void dl_notef(const wchar_t *fmt, int n) {
  _snwprintf(note_text, 159, fmt, n);
  note_text[159] = 0;
  note_until = GetTickCount() + 2500;
  if (!note_until) note_until = 1;
  dl_dirty = true;
}

void dl_note(const wchar_t *text) {
  lstrcpynW(note_text, text, 160);
  note_until = GetTickCount() + 2500;
  if (!note_until) note_until = 1;
  dl_dirty = true;
}

const wchar_t *dl_note_now(void) {
  if (note_until && (int)(note_until - GetTickCount()) > 0) return note_text;
  note_until = 0;
  return NULL;
}

static HFONT note_font(int h) {
  static HFONT f;
  static int fh;
  int want = h / 22 < 14 ? 14 : h / 22;
  if (want != fh) { if (f) DeleteObject(f); fh = want; f = CreateFontW(-fh, 0, 0, 0, FW_BOLD, 0, 0, 0, DEFAULT_CHARSET, 0, 0, ANTIALIASED_QUALITY, 0, dl_cfg.ui_lang ? L"Verdana" : L"Yu Gothic UI"); }
  return f;
}

// Size of the note's box for a picture h pixels high.
void dl_note_size(HDC dc, int h, int *w, int *bh) {
  const wchar_t *s = dl_note_now();
  int fh = h / 22 < 14 ? 14 : h / 22;
  SIZE sz = {0, 0};
  if (s) {
    HGDIOBJ of = SelectObject(dc, note_font(h));
    GetTextExtentPoint32W(dc, s, lstrlenW(s), &sz);
    SelectObject(dc, of);
  }
  *w = sz.cx + fh;
  *bh = fh * 2;
}

// The note in its box, whose top left corner is x,y (a darkened band, then the text).
void dl_draw_note(HDC dc, int x, int y, int h) {
  const wchar_t *s = dl_note_now();
  if (!s) return;
  int w, bh, fh = h / 22 < 14 ? 14 : h / 22;
  dl_note_size(dc, h, &w, &bh);
  static HDC src;
  static u32 *px;
  if (!src) {
    BITMAPINFO bi = {{sizeof(BITMAPINFOHEADER), 1, 1, 1, 32}};
    src = CreateCompatibleDC(dc);
    SelectObject(src, CreateDIBSection(src, &bi, DIB_RGB_COLORS, (void **)&px, NULL, 0));
  }
  COLORREF band = ui_tint(RGB(0x0A, 0x1A, 0x40));
  *px = (u32)GetRValue(band) << 16 | (u32)GetGValue(band) << 8 | GetBValue(band);
  BLENDFUNCTION bf = {AC_SRC_OVER, 0, 170, 0};
  AlphaBlend(dc, x, y, w, bh, src, 0, 0, 1, 1, bf);
  HGDIOBJ of = SelectObject(dc, note_font(h));
  SetBkMode(dc, TRANSPARENT);
  SetTextAlign(dc, TA_LEFT | TA_TOP);
  SetTextColor(dc, RGB(0, 0, 0));
  TextOutW(dc, x + fh / 2 + 1, y + fh / 2 + 1, s, lstrlenW(s));
  SetTextColor(dc, RGB(255, 255, 255));
  TextOutW(dc, x + fh / 2, y + fh / 2, s, lstrlenW(s));
  SelectObject(dc, of);
}

// ---- pictures read from files (GDI+, loaded on first use)
typedef struct { UINT32 version; void *debug_cb; BOOL no_thread; BOOL no_codecs; } GpStartupInput;
HBITMAP dl_picture_load(const char *path, int *w, int *h) {
  static bool tried;
  static int(WINAPI * from_file)(const WCHAR *, void **);
  static int(WINAPI * to_hbitmap)(void *, HBITMAP *, DWORD);
  static int(WINAPI * dispose)(void *);
  if (!tried) {
    tried = true;
    HMODULE gp = LoadLibraryA("gdiplus.dll");
    int(WINAPI * startup)(ULONG_PTR *, const GpStartupInput *, void *) = NULL;
    if (gp) {
      startup = (void *)GetProcAddress(gp, "GdiplusStartup");
      from_file = (void *)GetProcAddress(gp, "GdipCreateBitmapFromFile");
      to_hbitmap = (void *)GetProcAddress(gp, "GdipCreateHBITMAPFromBitmap");
      dispose = (void *)GetProcAddress(gp, "GdipDisposeImage");
    }
    ULONG_PTR token;
    GpStartupInput in = {1, NULL, FALSE, FALSE};
    if (!startup || !from_file || !to_hbitmap || !dispose || startup(&token, &in, NULL) != 0) from_file = NULL;
  }
  *w = *h = 0;
  if (!from_file) return NULL;
  wchar_t wp[MAX_PATH];
  MultiByteToWideChar(CP_ACP, 0, path, -1, wp, MAX_PATH);
  void *img = NULL;
  HBITMAP out = NULL;
  if (from_file(wp, &img) == 0 && img) {
    to_hbitmap(img, &out, 0xFF000000);
    dispose(img); // the file is not kept open: it can be deleted
  }
  BITMAP bm;
  if (out && GetObjectA(out, sizeof bm, &bm)) { *w = bm.bmWidth; *h = bm.bmHeight; }
  return out;
}

// A folder beside the program, made only when something is written there.
static void folder(char *out, const char *name, bool make) {
  snprintf(out, MAX_PATH, "%s%s", dl_base_dir, name);
  if (make) CreateDirectoryA(out, NULL);
}

// The folder of a file, made if it is not there yet (one level).
static void make_parent(const char *path) {
  char dir[MAX_PATH];
  snprintf(dir, sizeof dir, "%s", path);
  char *slash = strrchr(dir, '\\');
  if (slash) { *slash = 0; CreateDirectoryA(dir, NULL); }
}

static bool file_exists(const char *p) { return GetFileAttributesA(p) != INVALID_FILE_ATTRIBUTES; }

// ---- save states: n 0-9 the player's slots, DL_SLOTS + k the game's automatic states, -1 the session
void dl_slot_path(char *out, int n, const char *ext) {
  char base[MAX_PATH];
  if (dl_game->state_path) dl_game->state_path(n, base);
  else {
    char dir[MAX_PATH];
    folder(dir, "saves", false);
    if (n < 0) snprintf(base, sizeof base, "%s\\" FILE_STEM ".session", dir);
    else if (n >= DL_SLOTS) snprintf(base, sizeof base, "%s\\" FILE_STEM ".auto%d", dir, n - DL_SLOTS);
    else snprintf(base, sizeof base, "%s\\" FILE_STEM ".state%d", dir, n);
  }
  snprintf(out, MAX_PATH, "%s%s", base, ext);
}

bool dl_slot_used(int n) {
  char p[MAX_PATH];
  dl_slot_path(p, n, "");
  return file_exists(p);
}

void dl_state_loaded(void) {
  dl_state_taken = true;
  if (dl_game->state_loaded) dl_game->state_loaded();
}

bool dl_state_save(int n) {
  char p[MAX_PATH], pic[MAX_PATH];
  dl_slot_path(p, n, "");
  dl_slot_path(pic, n, ".png");
  make_parent(p);
  if (!dl_game->save_state || !dl_game->save_state(p)) return false;
  dl_save_png(pic, dl_pic, dl_pic_w, dl_pic_h);
  return true;
}

bool dl_state_load(int n) {
  char p[MAX_PATH];
  dl_slot_path(p, n, "");
  if (!dl_game->load_state || !file_exists(p) || !dl_game->load_state(p)) return false;
  dl_state_loaded();
  return true;
}

void dl_quick_save(void) {
  if (!dl_game->save_state) return;
  dl_notef(dl_state_save(dl_slot) ? TX(T_M_SAVED) : TX(T_M_SAVE_FAIL), dl_slot);
}

void dl_quick_load(void) {
  if (!dl_game->load_state) return;
  if (!dl_slot_used(dl_slot)) dl_notef(TX(T_M_SLOT_EMPTY), dl_slot);
  else dl_notef(dl_state_load(dl_slot) ? TX(T_M_LOADED) : TX(T_M_LOAD_FAIL), dl_slot);
}

void dl_slot_step(int dir) {
  dl_slot = (dl_slot + DL_SLOTS + dir) % DL_SLOTS;
  dl_notef(dl_slot_used(dl_slot) ? TX(T_M_SLOT) : TX(T_M_SLOT_FREE), dl_slot);
}

// ---- the session: the game in progress kept when the program closes, taken up by "Continue"
bool dl_session_exists(void) {
  if (!dl_game->resume || !dl_game->save_state || !dl_game->load_state) return false;
  char p[MAX_PATH];
  dl_slot_path(p, -1, "");
  return file_exists(p);
}

bool dl_session_resume(void) {
  if (!dl_session_exists()) return false;
  char p[MAX_PATH];
  dl_slot_path(p, -1, "");
  if (!dl_game->load_state(p)) return false;
  dl_state_loaded();
  dl_play();
  dl_note(TX(T_M_RESUMED));
  return true;
}

void dl_session_keep(void) {
  if (!dl_game->resume || !dl_game->save_state || !dl_started) return;
  char p[MAX_PATH];
  dl_slot_path(p, -1, "");
  make_parent(p);
  dl_game->save_state(p);
}

// The help of "Continue": when the session dates from, and the time played if the game counts it.
// The newest of the session and the game's automatic states: -1 the session, DL_SLOTS + k an automatic
// state, -2 none.
int dl_latest_auto(void) {
  if (!dl_game->load_state) return -2;
  int best = -2;
  FILETIME best_t = {0, 0};
  for (int i = -1; i < dl_game->auto_slots && i < 8; i++) {
    int n = i < 0 ? -1 : DL_SLOTS + i;
    if (n < 0 && !dl_game->resume) continue;
    char p[MAX_PATH];
    dl_slot_path(p, n, "");
    WIN32_FILE_ATTRIBUTE_DATA a;
    if (!GetFileAttributesExA(p, GetFileExInfoStandard, &a)) continue;
    if (best == -2 || CompareFileTime(&a.ftLastWriteTime, &best_t) > 0) { best = n; best_t = a.ftLastWriteTime; }
  }
  return best;
}

const wchar_t *dl_session_help(void) {
  static wchar_t out[1024];
  char p[MAX_PATH];
  WIN32_FILE_ATTRIBUTE_DATA fa;
  dl_slot_path(p, -1, "");
  lstrcpynW(out, TX(T_H_CONTINUE), 1024);
  if (!GetFileAttributesExA(p, GetFileExInfoStandard, &fa)) return out;
  FILETIME lt;
  SYSTEMTIME st;
  wchar_t day[48], hour[24], when[80], tail[256];
  FileTimeToLocalFileTime(&fa.ftLastWriteTime, &lt);
  FileTimeToSystemTime(&lt, &st);
  GetDateFormatW(LOCALE_USER_DEFAULT, DATE_SHORTDATE, &st, NULL, day, 48);
  GetTimeFormatW(LOCALE_USER_DEFAULT, TIME_NOSECONDS, &st, NULL, hour, 24);
  _snwprintf(when, 79, L"%ls %ls", day, hour);
  when[79] = 0;
  if (dl_game->play_seconds) {
    int secs = dl_game->play_seconds();
    _snwprintf(tail, 255, TX(T_SESSION), when, secs / 3600, secs / 60 % 60);
  } else _snwprintf(tail, 255, TX(T_SESSION_DATE), when);
  tail[255] = 0;
  _snwprintf(out, 1023, L"%ls%ls", TX(T_H_CONTINUE), tail);
  out[1023] = 0;
  return out;
}

// ---- rewind: a snapshot every 6 pictures, the last 200 of them (about 20 seconds at 60 pictures a second)
#define RW_SLOTS 200
static u8 *rw_buf;
static size_t rw_size;
static int rw_head, rw_count, rw_total, rw_tick;
static DWORD rw_last;
static bool rw_used;

bool dl_rewind_ok(void) { return dl_game->state_size && dl_game->state_to_mem && dl_game->state_from_mem; }

void dl_rewind_snap(void) {
  if (!dl_rewind_ok() || ++rw_tick % 6) return;
  size_t need = dl_game->state_size();
  if (need != rw_size || !rw_buf) {
    free(rw_buf);
    rw_size = need;
    rw_buf = need ? malloc(need * RW_SLOTS) : NULL;
    rw_head = rw_count = 0;
    if (!rw_buf) { rw_size = 0; return; }
  }
  dl_game->state_to_mem(rw_buf + (size_t)rw_head * rw_size);
  rw_head = (rw_head + 1) % RW_SLOTS;
  if (rw_count < RW_SLOTS) rw_count++;
  rw_total = rw_count;
}

// One step back every 90 ms while the key is held; true when there is a new picture to show.
bool dl_rewind_step(void) {
  static s16 sink[8192];
  DWORD t = GetTickCount();
  if (!rw_buf || rw_count <= 0 || t - rw_last < 90) return false;
  rw_head = (rw_head + RW_SLOTS - 1) % RW_SLOTS;
  rw_count--;
  const u8 *snap = rw_buf + (size_t)rw_head * rw_size;
  dl_game->state_from_mem(snap);
  dl_game->frame(&(DlInput){0}, dl_pic, sink, 4096); // the picture of that moment
  dl_game->state_from_mem(snap);
  dl_notef(TX(T_M_REWIND), (rw_total - rw_count) * 6 / 60);
  rw_last = t;
  rw_used = true;
  return true;
}

void dl_rewind_end(void) {
  if (!rw_used) return;
  rw_used = false;
  rw_total = rw_count; // the timeline goes on from here
  dl_state_loaded();
  dl_input_release();
}

// ---- screenshots
void dl_screenshot(void) {
  char dir[MAX_PATH], p[MAX_PATH];
  folder(dir, "captures", true);
  SYSTEMTIME t;
  GetLocalTime(&t);
  snprintf(p, sizeof p, "%s\\" FILE_STEM "_%04d%02d%02d_%02d%02d%02d.png", dir, t.wYear, t.wMonth, t.wDay, t.wHour, t.wMinute, t.wSecond);
  for (int k = 2; file_exists(p) && k < 100; k++)
    snprintf(p, sizeof p, "%s\\" FILE_STEM "_%04d%02d%02d_%02d%02d%02d_%d.png", dir, t.wYear, t.wMonth, t.wDay, t.wHour, t.wMinute, t.wSecond, k);
  bool ok = false;
  if (dl_gpu_ready()) { // the picture as shown, drawn once more for it
    snprintf(dl_shot_path, sizeof dl_shot_path, "%s", p);
    dl_shot_game = true;
    dl_shot_ok = false;
    ok = dl_gpu_show(true, false) && dl_shot_ok;
    dl_shot_path[0] = 0;
    dl_shot_game = false;
  }
  if (!ok) ok = dl_save_png(p, dl_pic, dl_pic_w, dl_pic_h);
  dl_notef(ok ? TX(T_M_SHOT) : TX(T_M_SHOT_FAIL), 0);
}

// ---- the pages: lists drawn by dl_menu.c
static HBITMAP pic_bmp; // the picture of the selected row, from a file
static int pic_w, pic_h, pic_for = -1;
static u32 *state_px;   // or made by loading the state for one picture
static int state_w, state_h, state_for = -1;
static double state_aspect;
static int confirm = -1; // row waiting for the second press of Delete

static void pic_forget(void) {
  if (pic_bmp) DeleteObject(pic_bmp);
  pic_bmp = NULL;
  pic_w = pic_h = 0;
  pic_for = state_for = -1;
}

static HBITMAP pic_of(const char *path, int row, int *w, int *h) {
  if (pic_for != row) {
    if (pic_bmp) DeleteObject(pic_bmp);
    pic_bmp = file_exists(path) ? dl_picture_load(path, &pic_w, &pic_h) : NULL;
    pic_for = row;
  }
  *w = pic_w;
  *h = pic_h;
  return pic_bmp;
}

static void row_date(const FILETIME *ft, wchar_t *out, bool seconds) {
  FILETIME lt;
  SYSTEMTIME st;
  FileTimeToLocalFileTime(ft, &lt);
  FileTimeToSystemTime(&lt, &st);
  bool month_first = dl_cfg.ui_lang == 0 || dl_cfg.ui_lang == 2; // Japanese and English
  int a = month_first ? st.wMonth : st.wDay, b = month_first ? st.wDay : st.wMonth;
  if (seconds) wsprintfW(out, L"%02d/%02d/%04d  %02d:%02d:%02d", a, b, st.wYear, st.wHour, st.wMinute, st.wSecond);
  else wsprintfW(out, L"%02d/%02d  %02d:%02d", a, b, st.wHour, st.wMinute);
}

static void empty_text(HDC dc, double x0, double y0, double x1, double y1) {
  ui_text(dc, f_item, (x0 + x1) / 2, (y0 + y1) / 2 - 8, TX(T_EMPTY), ui_tint(RGB(110, 124, 210)), TA_CENTER);
}

// save states: the ten slots, then the game's automatic ones
#define SV_MAX (DL_SLOTS + 8)
#define NAME_MAX 28
static bool sv_used[SV_MAX];
static FILETIME sv_time[SV_MAX];
static wchar_t sv_name[DL_SLOTS][NAME_MAX + 1], edit_buf[NAME_MAX + 1];
static int edit_slot = -1; // slot whose name is being typed (F2)

static int saves_count(void) { return DL_SLOTS + (dl_game->auto_slots < 8 ? dl_game->auto_slots : 8); }

// A slot's name is kept beside it in "<state>.name" (UTF-8).
static void name_read(int n, wchar_t *out) {
  char p[MAX_PATH], s[NAME_MAX * 4 + 1];
  dl_slot_path(p, n, ".name");
  out[0] = 0;
  FILE *f = fopen(p, "rb");
  if (!f) return;
  size_t len = fread(s, 1, sizeof s - 1, f);
  fclose(f);
  s[len] = 0;
  if (!MultiByteToWideChar(CP_UTF8, 0, s, -1, out, NAME_MAX + 1)) out[0] = 0;
  out[NAME_MAX] = 0;
}

static void name_write(int n, const wchar_t *name) {
  char p[MAX_PATH], s[NAME_MAX * 4 + 1];
  dl_slot_path(p, n, ".name");
  int len = name[0] ? WideCharToMultiByte(CP_UTF8, 0, name, -1, s, sizeof s, NULL, NULL) : 0;
  if (len <= 1) { DeleteFileA(p); return; } // no name any more
  FILE *f = fopen(p, "wb");
  if (f) { fwrite(s, 1, (size_t)len - 1, f); fclose(f); }
}

static void saves_open(void) {
  pic_forget();
  confirm = edit_slot = -1;
  for (int k = 0; k < saves_count(); k++) {
    char p[MAX_PATH];
    WIN32_FILE_ATTRIBUTE_DATA fa;
    dl_slot_path(p, k, "");
    sv_used[k] = GetFileAttributesExA(p, GetFileExInfoStandard, &fa) != 0;
    if (sv_used[k]) sv_time[k] = fa.ftLastWriteTime;
    if (k < DL_SLOTS) name_read(k, sv_name[k]);
  }
}
static void saves_row(int k, wchar_t *label, wchar_t *value) {
  if (k == edit_slot) _snwprintf(label, 63, L"%d. %ls_", k, edit_buf);
  else if (k < DL_SLOTS && sv_name[k][0]) _snwprintf(label, 63, L"%d. %ls", k, sv_name[k]);
  else if (k < DL_SLOTS) wsprintfW(label, TX(T_SLOT_N), k);
  else if (dl_game->auto_labels) lstrcpynW(label, dl_tr(dl_game->auto_labels[k - DL_SLOTS]), 64);
  else wsprintfW(label, L"Auto %d", k - DL_SLOTS + 1);
  label[63] = 0;
  if (sv_used[k]) row_date(&sv_time[k], value, false);
  else lstrcpyW(value, TX(T_EMPTY));
}
static const wchar_t *saves_help(int k) {
  static wchar_t buf[512];
  if (edit_slot >= 0) return TX(T_SVH_NAME);
  if (confirm == k) return TX(T_SVH_CONFIRM);
  if (k >= DL_SLOTS) return sv_used[k] ? TX(T_SVH_AUTO) : TX(T_SVH_AUTO_EMPTY);
  if (!sv_used[k]) return dl_started ? TX(T_SVH_SAVE) : TX(T_SVH_EMPTY);
  _snwprintf(buf, 511, L"%ls%ls", TX(T_SVH_LOAD), TX(T_SVH_F2));
  buf[511] = 0;
  return buf;
}

// The picture of a state saved without one: the state is loaded for one picture of the game, then
// everything is put back as it was.
static bool state_picture(int k) {
  if (state_for == k) return state_px != NULL;
  state_for = k;
  if (!dl_rewind_ok() || !dl_game->load_state) return false;
  size_t n = dl_game->state_size(), px_n = (size_t)dl_game->width * dl_game->height;
  u8 *keep = malloc(n);
  u32 *keep_px = malloc(px_n * 4);
  if (!state_px) state_px = malloc(px_n * 4);
  bool ok = false;
  if (keep && keep_px && state_px) {
    static s16 sink[8192];
    int w = dl_pic_w, h = dl_pic_h;
    double a = dl_pic_aspect;
    char p[MAX_PATH];
    dl_game->state_to_mem(keep);
    memcpy(keep_px, dl_pic, px_n * 4);
    dl_slot_path(p, k, "");
    if (dl_game->load_state(p)) {
      dl_game->frame(&(DlInput){0}, dl_pic, sink, 4096);
      state_w = dl_pic_w;
      state_h = dl_pic_h;
      state_aspect = dl_pic_aspect;
      memcpy(state_px, dl_pic, (size_t)state_w * state_h * 4);
      ok = true;
    }
    dl_game->state_from_mem(keep);
    memcpy(dl_pic, keep_px, px_n * 4);
    dl_pic_w = w;
    dl_pic_h = h;
    dl_pic_aspect = a;
  }
  free(keep);
  free(keep_px);
  return ok;
}

static bool saves_preview(void *dcv, int k, double x0, double y0, double x1, double y1) {
  HDC dc = dcv;
  char p[MAX_PATH];
  int w, h;
  if (!sv_used[k]) { empty_text(dc, x0, y0, x1, y1); return true; }
  dl_slot_path(p, k, ".png");
  HBITMAP bmp = pic_of(p, k, &w, &h);
  if (bmp && w > 0 && h > 0) dl_draw_picture(dc, bmp, w, h, x0, y0, x1, y1);
  else if (state_picture(k)) dl_draw_pixels(dc, state_px, state_w, state_h, state_aspect, x0, y0, x1, y1);
  return true;
}
static void saves_enter(int k) {
  confirm = -1;
  if (sv_used[k]) { // load it and play
    if (k < DL_SLOTS) dl_slot = k;
    if (!dl_state_load(k)) { dl_notef(TX(T_M_LOAD_FAIL), k); return; }
    dl_play();
    dl_notef(TX(T_M_LOADED), k);
  } else if (dl_started && k < DL_SLOTS) { // a free slot: the game in progress goes there
    dl_slot = k;
    dl_state_save(k);
    saves_open();
  }
}
static void saves_erase(int k) {
  if (!sv_used[k]) return;
  if (confirm != k) { confirm = k; return; }
  static const char *const ext[] = {"", ".png", ".name"};
  for (int e = 0; e < 3; e++) {
    char p[MAX_PATH];
    dl_slot_path(p, k, ext[e]);
    DeleteFileA(p);
  }
  saves_open();
}
static void list_moved(void) { confirm = -1; }

// Naming a slot: F2 on a slot in use, then the keys of the name, Enter to keep it, Esc to cancel.
bool dl_saves_key(int vk) {
  if (edit_slot < 0) {
    if (vk != VK_F2 || dl_sel >= DL_SLOTS || !sv_used[dl_sel]) return false;
    edit_slot = dl_sel;
    lstrcpyW(edit_buf, sv_name[dl_sel]);
    confirm = -1;
    return true;
  }
  int len = lstrlenW(edit_buf);
  if (vk == VK_RETURN) {
    while (len && edit_buf[len - 1] == L' ') edit_buf[--len] = 0;
    lstrcpyW(sv_name[edit_slot], edit_buf);
    name_write(edit_slot, edit_buf);
    edit_slot = -1;
    dl_beep(1);
  } else if (vk == VK_ESCAPE) edit_slot = -1;
  else if (vk == VK_BACK && len) edit_buf[len - 1] = 0;
  return true;
}

void dl_saves_char(wchar_t c) {
  int len = lstrlenW(edit_buf);
  if (edit_slot < 0 || c < 32 || c == 127 || len >= NAME_MAX) return;
  edit_buf[len] = c;
  edit_buf[len + 1] = 0;
  dl_dirty = true;
}

const DlList dl_saves_list = {saves_open, saves_count, saves_row, saves_help, saves_enter, saves_erase, list_moved, NULL, saves_preview};

// screenshots: the pictures of the captures folder, newest first
#define SHOT_MAX 1000
typedef struct { char name[128]; FILETIME t; } Shot;
static Shot *shots;
static int shots_n;
static char shots_dir[MAX_PATH];

static int shot_newer(const void *a, const void *b) { return -CompareFileTime(&((const Shot *)a)->t, &((const Shot *)b)->t); }

static void shots_open(void) {
  pic_forget();
  confirm = -1;
  shots_n = 0;
  if (!shots) shots = malloc(sizeof *shots * SHOT_MAX);
  folder(shots_dir, "captures", false);
  if (!shots) return;
  char pat[MAX_PATH];
  snprintf(pat, sizeof pat, "%s\\*.png", shots_dir);
  WIN32_FIND_DATAA fd;
  HANDLE h = FindFirstFileA(pat, &fd);
  if (h == INVALID_HANDLE_VALUE) return;
  do {
    if ((fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) || strlen(fd.cFileName) >= sizeof shots->name) continue;
    snprintf(shots[shots_n].name, sizeof shots->name, "%s", fd.cFileName);
    shots[shots_n].t = fd.ftLastWriteTime;
    shots_n++;
  } while (shots_n < SHOT_MAX && FindNextFileA(h, &fd));
  FindClose(h);
  qsort(shots, (size_t)shots_n, sizeof *shots, shot_newer);
}
static int shots_count(void) { return shots_n; }
static void shots_row(int k, wchar_t *label, wchar_t *value) {
  row_date(&shots[k].t, label, true);
  value[0] = 0;
}
static void shot_path(int k, char *out) { snprintf(out, MAX_PATH, "%s\\%s", shots_dir, shots[k].name); }
static const wchar_t *shots_help(int k) {
  static wchar_t buf[512];
  if (confirm == k) return TX(T_SHH_CONFIRM);
  buf[0] = 0;
  if (pic_for == k && pic_w > 0) _snwprintf(buf, 200, TX(T_SHH_SIZE), pic_w, pic_h);
  buf[200] = 0;
  lstrcatW(buf, TX(T_SHH_OPEN));
  return buf;
}
static bool shots_preview(void *dcv, int k, double x0, double y0, double x1, double y1) {
  char p[MAX_PATH];
  int w, h;
  shot_path(k, p);
  HBITMAP bmp = pic_of(p, k, &w, &h);
  if (bmp && w > 0 && h > 0) dl_draw_picture(dcv, bmp, w, h, x0, y0, x1, y1);
  return true;
}
static void shots_enter(int k) {
  char p[MAX_PATH];
  shot_path(k, p);
  ShellExecuteA(dl_hwnd, "open", p, NULL, NULL, SW_SHOWNORMAL);
}
// a screenshot goes to the Recycle Bin, in two presses
static void shots_erase(int k) {
  if (confirm != k) { confirm = k; return; }
  char p[MAX_PATH + 1];
  shot_path(k, p);
  p[strlen(p) + 1] = 0; // the list of files ends with two zeros
  SHFILEOPSTRUCTA op = {0};
  op.hwnd = dl_hwnd;
  op.wFunc = FO_DELETE;
  op.pFrom = p;
  op.fFlags = FOF_ALLOWUNDO | FOF_NOCONFIRMATION | FOF_SILENT | FOF_NOERRORUI;
  SHFileOperationA(&op);
  shots_open();
}

const DlList dl_shots_list = {shots_open, shots_count, shots_row, shots_help, shots_enter, shots_erase, list_moved, dl_texts[T_SHH_NONE], shots_preview};
