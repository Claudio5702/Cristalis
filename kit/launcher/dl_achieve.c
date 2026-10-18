// Achievements: the game's list (dl_achievements), which are unlocked and when (saves\<FILE_STEM>.achievements
// next to the program, one "id time" line each), the few words shown over the game when one is
// unlocked, and the Achievements page (a list, with the selected one drawn large beside it).
#include "dl_int.h"
#include <time.h>

#define ACH_MAX 128
static const DlAchievement *ach;
static int ach_n;
static long long ach_time[ACH_MAX]; // unlocked at (seconds since 1970), 0: not yet

static void ach_path(char *out) { snprintf(out, MAX_PATH, "%ssaves" DL_SEP FILE_STEM ".achievements", dl_base_dir); }

static void ach_read(void) {
  char p[MAX_PATH], line[160], id[96];
  long long t;
  ach_path(p);
  FILE *f = fopen(p, "r");
  if (!f) return;
  while (fgets(line, sizeof line, f))
    if (sscanf(line, "%95s %lld", id, &t) == 2)
      for (int k = 0; k < ach_n; k++)
        if (!strcmp(id, ach[k].id)) ach_time[k] = t;
  fclose(f);
}

static void ach_write(void) {
  char p[MAX_PATH], dir[MAX_PATH];
  snprintf(dir, sizeof dir, "%ssaves", dl_base_dir);
  CreateDirectoryA(dir, NULL);
  ach_path(p);
  FILE *f = fopen(p, "w");
  if (!f) return;
  for (int k = 0; k < ach_n; k++)
    if (ach_time[k]) fprintf(f, "%s %lld\n", ach[k].id, ach_time[k]);
  fclose(f);
}

static int ach_find(const char *id) {
  for (int k = 0; id && k < ach_n; k++)
    if (!strcmp(id, ach[k].id)) return k;
  return -1;
}

bool dl_unlocked(const char *id) {
  int k = ach_find(id);
  return k >= 0 && ach_time[k];
}

// ---- "Achievement unlocked" over the game, for a few seconds
static wchar_t toast_name[96];
static DWORD toast_until;

static void toast(const wchar_t *name) {
  lstrcpynW(toast_name, name, 96);
  toast_until = GetTickCount() + 4500;
  if (!toast_until) toast_until = 1;
  dl_dirty = true;
}

const wchar_t *dl_toast_now(void) {
  if (toast_until && (int)(toast_until - GetTickCount()) > 0) return toast_name;
  toast_until = 0;
  return NULL;
}

void dl_toast_sample(void) { // --gpu-check: the first achievement's, without unlocking it
  if (ach_n) toast(dl_tr(ach[0].name));
}

void dl_unlock(const char *id) {
  int k = ach_find(id);
  if (k < 0 || ach_time[k]) return;
  ach_time[k] = (long long)time(NULL);
  ach_write();
  toast(dl_tr(ach[k].name));
  dl_beep(1);
}

static int toast_fh(int h) { return h / 26 < 12 ? 12 : h / 26; }

static HFONT toast_font(int h, int which) { // 0 the title, 1 the name, 2 the star
  static HFONT f[3];
  static int made_h = -1, made_lang = -1;
  if (made_h != h || made_lang != dl_cfg.ui_lang) {
    for (int k = 0; k < 3; k++)
      if (f[k]) DeleteObject(f[k]);
    int fh = toast_fh(h);
    const wchar_t *face = dl_cfg.ui_lang == DL_LANG_JP ? L"Yu Gothic UI" : L"Verdana";
    f[0] = CreateFontW(-(fh * 4 / 5), 0, 0, 0, FW_BOLD, 0, 0, 0, DEFAULT_CHARSET, 0, 0, ANTIALIASED_QUALITY, 0, face);
    f[1] = CreateFontW(-fh, 0, 0, 0, FW_BOLD, 0, 0, 0, DEFAULT_CHARSET, 0, 0, ANTIALIASED_QUALITY, 0, face);
    f[2] = CreateFontW(-(fh * 2), 0, 0, 0, FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET, 0, 0, ANTIALIASED_QUALITY, 0, L"Segoe MDL2 Assets");
    made_h = h;
    made_lang = dl_cfg.ui_lang;
  }
  return f[which];
}

// Size of the box for a picture h pixels high.
void dl_toast_size(HDC dc, int h, int *w, int *bh) {
  int fh = toast_fh(h);
  SIZE a = {0, 0}, b = {0, 0};
  const wchar_t *title = TX(T_ACH_UNLOCKED);
  HGDIOBJ of = SelectObject(dc, toast_font(h, 0));
  GetTextExtentPoint32W(dc, title, lstrlenW(title), &a);
  SelectObject(dc, toast_font(h, 1));
  GetTextExtentPoint32W(dc, toast_name, lstrlenW(toast_name), &b);
  SelectObject(dc, of);
  *w = (a.cx > b.cx ? a.cx : b.cx) + fh * 4;
  *bh = fh * 3;
}

// The box at x,y: a dark band, a gold star, "Achievement unlocked" and the name.
void dl_draw_toast(HDC dc, int x, int y, int h) {
  if (!dl_toast_now()) return;
  int w, bh, fh = toast_fh(h);
  dl_toast_size(dc, h, &w, &bh);
  static HDC src;
  static u32 *px;
  if (!src) {
    BITMAPINFO bi = {{sizeof(BITMAPINFOHEADER), 1, 1, 1, 32}};
    src = CreateCompatibleDC(dc);
    SelectObject(src, CreateDIBSection(src, &bi, DIB_RGB_COLORS, (void **)&px, NULL, 0));
  }
  COLORREF band = ui_tint(RGB(0x0A, 0x1A, 0x40));
  *px = (u32)GetRValue(band) << 16 | (u32)GetGValue(band) << 8 | GetBValue(band);
  BLENDFUNCTION bf = {AC_SRC_OVER, 0, 210, 0};
  AlphaBlend(dc, x, y, w, bh, src, 0, 0, 1, 1, bf);
  SetBkMode(dc, TRANSPARENT);
  SetTextAlign(dc, TA_LEFT | TA_TOP);
  HGDIOBJ of = SelectObject(dc, toast_font(h, 2));
  SetTextColor(dc, RGB(255, 206, 84));
  TextOutW(dc, x + fh / 2, y + fh / 2, L"\xE735", 1);
  SelectObject(dc, toast_font(h, 0));
  COLORREF v = ui_value();
  SetTextColor(dc, v);
  TextOutW(dc, x + fh * 3, y + fh / 2, TX(T_ACH_UNLOCKED), lstrlenW(TX(T_ACH_UNLOCKED)));
  SelectObject(dc, toast_font(h, 1));
  SetTextColor(dc, RGB(0, 0, 0));
  TextOutW(dc, x + fh * 3 + 1, y + fh * 3 / 2 + 1, toast_name, lstrlenW(toast_name));
  SetTextColor(dc, RGB(255, 255, 255));
  TextOutW(dc, x + fh * 3, y + fh * 3 / 2, toast_name, lstrlenW(toast_name));
  SelectObject(dc, of);
}

// ---- the Achievements page
static bool secret(int k) { return ach[k].hidden && !ach_time[k]; }

static void ach_date(int k, wchar_t *out, int n) {
  time_t t = (time_t)ach_time[k];
  struct tm *lt = localtime(&t);
  if (!lt) { out[0] = 0; return; }
  bool month_first = dl_cfg.ui_lang == DL_LANG_EN;
  if (dl_cfg.ui_lang == DL_LANG_JP) _snwprintf(out, n, L"%04d/%02d/%02d", lt->tm_year + 1900, lt->tm_mon + 1, lt->tm_mday); // year first in Japanese
  else _snwprintf(out, n, L"%02d/%02d/%04d", month_first ? lt->tm_mon + 1 : lt->tm_mday, month_first ? lt->tm_mday : lt->tm_mon + 1, lt->tm_year + 1900);
  out[n - 1] = 0;
}

static void ach_open(void) {}
static int ach_count(void) { return ach_n; }

static void ach_row(int k, wchar_t *label, wchar_t *value) {
  lstrcpynW(label, secret(k) ? TX(T_ACH_SECRET) : dl_tr(ach[k].name), 64);
  if (ach_time[k]) ach_date(k, value, 64);
  else lstrcpynW(value, TX(T_ACH_LOCKED), 64);
}

static const wchar_t *ach_help(int k) {
  static wchar_t out[512];
  wchar_t count[96];
  int got = 0;
  for (int i = 0; i < ach_n; i++) got += ach_time[i] != 0;
  _snwprintf(count, 95, TX(T_ACH_COUNT), got, ach_n);
  count[95] = 0;
  _snwprintf(out, 511, L"%ls\n\n%ls", secret(k) ? TX(T_ACH_H_SECRET) : dl_tr(ach[k].desc), count);
  out[511] = 0;
  return out;
}

// The selected one, large: its star (gold once unlocked), its name, when it was unlocked.
static bool ach_preview(void *dcv, int k, double x0, double y0, double x1, double y1) {
  HDC dc = dcv;
  bool got = ach_time[k] != 0;
  static HFONT star;
  static int star_px;
  if (star_px != PX(46)) {
    if (star) DeleteObject(star);
    star_px = PX(46);
    star = CreateFontW(-star_px, 0, 0, 0, FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET, 0, 0, ANTIALIASED_QUALITY, 0, L"Segoe MDL2 Assets");
  }
  double cx = (x0 + x1) / 2;
  ui_text(dc, star, cx, y0 + 6, got ? L"\xE735" : L"\xE734", got ? RGB(255, 206, 84) : ui_tint(RGB(96, 112, 168)), TA_CENTER);
  ui_text_fit(dc, cx, y0 + 70, secret(k) ? TX(T_ACH_SECRET) : dl_tr(ach[k].name), RGB(255, 255, 255), TA_CENTER, x1 - x0 - 8);
  wchar_t date[48], line[96];
  if (got) {
    ach_date(k, date, 48);
    _snwprintf(line, 95, TX(T_ACH_DATE), date);
    line[95] = 0;
  } else lstrcpynW(line, TX(T_ACH_LOCKED), 96);
  ui_text(dc, f_small, cx, y0 + 96, line, got ? ui_value() : ui_tint(RGB(150, 164, 214)), TA_CENTER);
  return true;
}

static const DlList ach_list = {ach_open, ach_count, ach_row, ach_help, NULL, NULL, NULL, NULL, ach_preview};

void dl_achievements(const DlAchievement *list, int count) {
  if (ach || !list || count <= 0) return;
  ach = list;
  ach_n = count > ACH_MAX ? ACH_MAX : count;
  ach_read();
  dl_section(dl_list_page(dl_texts[T_TAB_ACH], &ach_list), 0xE734, dl_texts[T_ACH], dl_texts[T_H_ACH]);
}
