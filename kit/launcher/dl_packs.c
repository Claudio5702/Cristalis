// Packs and mods (dl_packs): folders, or files of the game's kinds, that the player drops into a folder
// next to the program. A page lists them; the player turns each on or off and orders them (the ones
// above come before the ones below); the choice is kept in "<settings file>.packs", one "1 name" or
// "0 name" line each, in order. The game reads the packs turned on with dl_pack_count / dl_pack_path,
// at start and whenever its changed() is called; without changed(), the page offers to restart.
#include "dl_int.h"
#include <shellapi.h>

#define PK_MAX 64
typedef struct {
  wchar_t name[MAX_PATH];
  bool on, dir;
} Pack;
static Pack pk[PK_MAX];
static int pk_n;
static const DlPacks *pcfg;
static wchar_t pk_dir[MAX_PATH];
static char order_path[MAX_PATH];
static bool pending; // changed, to be taken at the next start (no changed() given)
static int pk_page = -1;
static char act_path[PK_MAX][MAX_PATH];
static int act_n;

static bool is_pack_file(const wchar_t *name) {
  const wchar_t *dot = wcsrchr(name, L'.');
  if (!dot || !pcfg->exts) return false;
  for (int k = 0; pcfg->exts[k]; k++) {
    wchar_t e[32];
    MultiByteToWideChar(CP_UTF8, 0, pcfg->exts[k], -1, e, 32);
    if (!lstrcmpiW(dot, e)) return true;
  }
  return false;
}

static void active_update(void) {
  act_n = 0;
  for (int k = 0; k < pk_n; k++) {
    if (!pk[k].on) continue;
    wchar_t w[MAX_PATH];
    _snwprintf(w, MAX_PATH - 1, L"%ls\\%ls", pk_dir, pk[k].name);
    w[MAX_PATH - 1] = 0;
    WideCharToMultiByte(CP_ACP, 0, w, -1, act_path[act_n], MAX_PATH, NULL, NULL);
    act_n++;
  }
}

static void order_write(void) {
  FILE *f = fopen(order_path, "w");
  if (!f) return;
  for (int k = 0; k < pk_n; k++) {
    char n[MAX_PATH * 3];
    WideCharToMultiByte(CP_UTF8, 0, pk[k].name, -1, n, sizeof n, NULL, NULL);
    fprintf(f, "%d %s\n", pk[k].on ? 1 : 0, n);
  }
  fclose(f);
}

static int by_name(const void *a, const void *b) { return lstrcmpiW(((const Pack *)a)->name, ((const Pack *)b)->name); }

// The folder read again: the packs kept in their order (the file's at the first reading), those gone
// left out, new ones turned on at the end. True when the list changed.
static bool rescan(bool from_file) {
  Pack found[PK_MAX], list[PK_MAX];
  int n = 0, m = 0;
  wchar_t pat[MAX_PATH];
  _snwprintf(pat, MAX_PATH - 1, L"%ls\\*", pk_dir);
  pat[MAX_PATH - 1] = 0;
  WIN32_FIND_DATAW fd;
  HANDLE h = FindFirstFileW(pat, &fd);
  if (h != INVALID_HANDLE_VALUE) {
    do {
      bool dir = (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
      if (fd.cFileName[0] == L'.' || n >= PK_MAX || (!dir && !is_pack_file(fd.cFileName))) continue;
      lstrcpynW(found[n].name, fd.cFileName, MAX_PATH);
      found[n].dir = dir;
      found[n].on = true;
      n++;
    } while (FindNextFileW(h, &fd));
    FindClose(h);
  }
  qsort(found, (size_t)n, sizeof found[0], by_name);
  bool taken[PK_MAX] = {0};
  if (from_file) { // the order kept in the file
    FILE *f = fopen(order_path, "r");
    char line[MAX_PATH * 3 + 8];
    while (f && fgets(line, sizeof line, f)) {
      int on;
      char *nl = strpbrk(line, "\r\n");
      if (nl) *nl = 0;
      if (sscanf(line, "%d ", &on) != 1 || !strchr(line, ' ')) continue;
      wchar_t name[MAX_PATH];
      MultiByteToWideChar(CP_UTF8, 0, strchr(line, ' ') + 1, -1, name, MAX_PATH);
      for (int k = 0; k < n; k++)
        if (!taken[k] && !lstrcmpiW(found[k].name, name)) { taken[k] = true; list[m] = found[k]; list[m++].on = on != 0; break; }
    }
    if (f) fclose(f);
  } else { // the order in memory
    for (int j = 0; j < pk_n; j++)
      for (int k = 0; k < n; k++)
        if (!taken[k] && !lstrcmpiW(found[k].name, pk[j].name)) { taken[k] = true; list[m] = found[k]; list[m++].on = pk[j].on; break; }
  }
  for (int k = 0; k < n; k++)
    if (!taken[k]) list[m++] = found[k];
  bool same = m == pk_n;
  for (int k = 0; same && k < m; k++) same = !lstrcmpW(list[k].name, pk[k].name) && list[k].on == pk[k].on;
  memcpy(pk, list, sizeof list[0] * (size_t)m);
  pk_n = m;
  return !same;
}

static void changed_now(void) {
  order_write();
  active_update();
  if (pcfg->changed) pcfg->changed();
  else pending = true;
  dl_dirty = true;
}

int dl_pack_count(void) { return act_n; }
const char *dl_pack_path(int k) { return k >= 0 && k < act_n ? act_path[k] : NULL; }

// ---- the page: the packs, then "Restart to apply" when needed, then "Open the packs folder"
static int extra_restart(void) { return pending && !pcfg->changed ? 1 : 0; }
static int pk_count(void) { return pk_n + extra_restart() + 1; }

static void pk_open(void) {
  if (rescan(false)) changed_now(); // packs added or taken away since
}

static void pk_row(int k, wchar_t *label, wchar_t *value) {
  value[0] = 0;
  if (k < pk_n) {
    lstrcpynW(label, pk[k].name, 64);
    lstrcpynW(value, pk[k].on ? TX(T_PK_ON) : TX(T_PK_OFF), 64);
  } else if (k == pk_n && extra_restart()) lstrcpynW(label, TX(T_PK_RESTART), 64);
  else lstrcpynW(label, TX(T_PK_FOLDER), 64);
}

static const wchar_t *pk_help(int k) {
  if (k < pk_n) return TX(T_H_PK);
  if (k == pk_n && extra_restart()) return TX(T_H_PK_RESTART);
  return TX(T_H_PK_FOLDER);
}

static void pk_enter(int k) {
  if (k < pk_n) {
    pk[k].on = !pk[k].on;
    changed_now();
  } else if (k == pk_n && extra_restart()) dl_relaunch();
  else {
    CreateDirectoryW(pk_dir, NULL);
    ShellExecuteW(NULL, L"open", pk_dir, NULL, NULL, SW_SHOWNORMAL);
  }
}

// A pack's picture: preview, cover or icon (.png, .jpg) inside its folder, or next to its file with the
// same name. Otherwise its kind and name, drawn.
static bool pk_preview(void *dcv, int k, double x0, double y0, double x1, double y1) {
  if (k >= pk_n) return false;
  static int pic_for = -1;
  static HBITMAP pic;
  static int pw, ph;
  if (pic_for != k) {
    if (pic) DeleteObject(pic);
    pic = NULL;
    pic_for = k;
    static const wchar_t *const names[] = {L"preview.png", L"cover.png", L"icon.png", L"preview.jpg", L"cover.jpg"};
    for (int i = 0; i < (pk[k].dir ? 5 : 2) && !pic; i++) {
      wchar_t w[MAX_PATH];
      if (pk[k].dir) _snwprintf(w, MAX_PATH - 1, L"%ls\\%ls\\%ls", pk_dir, pk[k].name, names[i]);
      else {
        _snwprintf(w, MAX_PATH - 1, L"%ls\\%ls", pk_dir, pk[k].name);
        wchar_t *dot = wcsrchr(w, L'.');
        if (dot) lstrcpyW(dot, i ? L".jpg" : L".png");
      }
      w[MAX_PATH - 1] = 0;
      char p[MAX_PATH];
      WideCharToMultiByte(CP_ACP, 0, w, -1, p, MAX_PATH, NULL, NULL);
      if (GetFileAttributesA(p) != INVALID_FILE_ATTRIBUTES) pic = dl_picture_load(p, &pw, &ph);
    }
  }
  if (pic && pw > 0 && ph > 0) {
    dl_draw_picture(dcv, pic, pw, ph, x0, y0, x1, y1);
    return true;
  }
  HDC dc = dcv;
  static HFONT big;
  static int big_px;
  if (big_px != PX(40)) {
    if (big) DeleteObject(big);
    big_px = PX(40);
    big = CreateFontW(-big_px, 0, 0, 0, FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET, 0, 0, ANTIALIASED_QUALITY, 0, L"Segoe MDL2 Assets");
  }
  double cx = (x0 + x1) / 2;
  if (ui_icons(dc)) ui_text(dc, big, cx, y0 + 10, pk[k].dir ? L"\xE8B7" : L"\xE7C3", ui_tint(RGB(150, 164, 214)), TA_CENTER);
  ui_text_fit(dc, cx, y0 + 62, pk[k].name, RGB(255, 255, 255), TA_CENTER, x1 - x0 - 8);
  ui_text(dc, f_small, cx, y0 + 88, pk[k].dir ? TX(T_PK_DIR) : TX(T_PK_FILE), pk[k].on ? ui_value() : ui_tint(RGB(150, 164, 214)), TA_CENTER);
  return true;
}

static const DlList pk_list = {pk_open, pk_count, pk_row, pk_help, pk_enter, NULL, NULL, NULL, pk_preview};

// Page Up / Page Down (LB / RB) on the page: the selected pack one place up or down.
bool dl_packs_key(int vk) {
  if (pk_page < 0 || dl_cur != pk_page || (vk != VK_PRIOR && vk != VK_NEXT)) return false;
  int k = dl_sel, to = vk == VK_PRIOR ? k - 1 : k + 1;
  if (k < 0 || k >= pk_n || to < 0 || to >= pk_n) return true;
  Pack t = pk[k];
  pk[k] = pk[to];
  pk[to] = t;
  dl_sel = to;
  dl_beep(0);
  changed_now();
  return true;
}

int dl_packs(const DlPacks *p) {
  if (pcfg || !p || !p->folder) return -1;
  pcfg = p;
  wchar_t f[MAX_PATH];
  MultiByteToWideChar(CP_UTF8, 0, p->folder, -1, f, MAX_PATH);
  if (f[0] && f[1] == L':') lstrcpynW(pk_dir, f, MAX_PATH); // a full path
  else {
    wchar_t base[MAX_PATH];
    MultiByteToWideChar(CP_ACP, 0, dl_base_dir, -1, base, MAX_PATH);
    _snwprintf(pk_dir, MAX_PATH - 1, L"%ls%ls", base, f);
    pk_dir[MAX_PATH - 1] = 0;
  }
  dl_settings_sibling(order_path, ".packs");
  rescan(true);
  active_update();
  pk_page = dl_list_page(p->tab, &pk_list);
  if (p->icon) dl_section(pk_page, p->icon, p->label, p->help);
  return pk_page;
}
