// The setup screen: the navigation bar on the left (sections, then what acts at once), the page of
// the selected section in the middle, the live picture of the game and the help on the right.
// Pages are lists of DlRow declared by the kit and by the game, or lists made by the program (save
// slots, screenshots); this file draws them and handles the keys and the mouse (the controller arrives
// as keys, see dl_input.c). The pause menu too: a panel over the paused game.
#include "dl_int.h"
#include <math.h>
#include <shellapi.h>

int dl_mode = M_SIDE, dl_side, dl_cur = -1, dl_sel;
bool dl_dirty = true;
DlPage dl_pages[DL_MAX_PAGES];
int dl_page_n;
DlSection dl_secs[DL_MAX_SECTIONS];
int dl_sec_n, dl_sec_top;
static int stack[8], stack_sel[8], stack_n; // pages opened from a page (DL_PAGE rows, a list's row), to come back to, and the row selected there
static int list_read = -1;    // list page whose contents were read last (-1: read again when shown)

int dl_lang(void) { return dl_cfg.ui_lang; }
// A text missing in this language is shown in English (--ui-audit reports it).
const wchar_t *dl_tr(const wchar_t *const *t) {
  if (!t) return L"";
  if (t[dl_cfg.ui_lang]) return t[dl_cfg.ui_lang];
  ui_audit_note("no translation", t[DL_LANG_EN] ? t[DL_LANG_EN] : L"");
  return t[DL_LANG_EN] ? t[DL_LANG_EN] : L"";
}
void dl_redraw(void) { dl_dirty = true; }

// ---- declaration of pages and sections
int dl_page(const wchar_t *const *tab, const DlRow *rows, int count) {
  if (dl_page_n >= DL_MAX_PAGES) return -1;
  DlPage *p = &dl_pages[dl_page_n];
  p->tab = tab;
  for (int k = 0; k < count && p->n < DL_MAX_ROWS; k++) p->rows[p->n++] = &rows[k];
  return dl_page_n++;
}

int dl_list_page(const wchar_t *const *tab, const DlList *list) {
  int page = dl_page(tab, NULL, 0);
  if (page >= 0) dl_pages[page].list = list;
  return page;
}

void dl_page_insert(int page, int at, const DlRow *row) {
  if (page < 0 || page >= dl_page_n || dl_pages[page].list) return;
  DlPage *p = &dl_pages[page];
  if (p->n >= DL_MAX_ROWS || at < 0 || at > p->n) return;
  memmove(&p->rows[at + 1], &p->rows[at], sizeof p->rows[0] * (size_t)(p->n - at));
  p->rows[at] = row;
  p->n++;
}

void dl_add_rows(int page, const DlRow *rows, int count) {
  if (page < 0 || page >= dl_page_n || dl_pages[page].list) return;
  DlPage *p = &dl_pages[page];
  if (count > DL_MAX_ROWS - p->n) count = DL_MAX_ROWS - p->n;
  memmove(&p->rows[count], &p->rows[0], sizeof p->rows[0] * (size_t)p->n);
  for (int k = 0; k < count; k++) p->rows[k] = &rows[k];
  p->n += count;
}

static void add_section(int kind, int page, unsigned icon, const wchar_t *const *label, const wchar_t *const *help) {
  if (dl_sec_n >= DL_MAX_SECTIONS) return;
  dl_secs[dl_sec_n++] = (DlSection){kind, page, icon, label, help};
}

void dl_section(int page, unsigned icon, const wchar_t *const *label, const wchar_t *const *help) {
  add_section(SEC_PAGE, page, icon, label, help);
}

// ---- the kit's own pages, made in the order of PG_* (dl_int.h)

// Only the game's languages (DlGame.langs) are offered: the others are stepped over, in the direction of
// the change (the value before it is the one last shown).
static int lang_shown;
static const wchar_t *show_lang(int v) {
  lang_shown = v;
  return dl_lang_names[v < 0 || v >= DL_LANGS ? 0 : v];
}
static void lang_changed(void) {
  int step = dl_cfg.ui_lang == (lang_shown + 1) % DL_LANGS ? 1 : DL_LANGS - 1;
  for (int k = 0; k < DL_LANGS && !dl_lang_ok(dl_cfg.ui_lang); k++) dl_cfg.ui_lang = (dl_cfg.ui_lang + step) % DL_LANGS;
}
static void full_changed(void) { dl_toggle_fullscreen(); }
static void try_it(void) { dl_mode = M_TRY; }
static void picture_defaults(void) { dl_defaults(true); }
static void gpu_changed(void) { dl_gpu_restart(); }
#ifdef _WIN32 // HDR: Windows only; on Linux the desktop shows the picture on an HDR screen itself
static void hdr_changed(void) { dl_gpu_follow(true); }
static const wchar_t *hdr_more(void) { return dl_windows_hdr() ? TX(T_HDR_WIN_ON) : TX(T_HDR_WIN_OFF); }
#endif
// The speed is 0 (max), 2 or 4, as in Kahna's settings file: the odd values are stepped over, in the
// direction of the change (the value before it is the one last shown).
static int turbo_shown;
static const wchar_t *show_turbo(int v) {
  turbo_shown = v;
  return v == 2 ? L"2x" : v == 4 ? L"4x" : TX(T_MAX);
}
static void turbo_changed(void) {
  if (dl_cfg.turbo & 1) dl_cfg.turbo = (dl_cfg.turbo + (dl_cfg.turbo > turbo_shown ? 1 : -1) + 5) % 5;
}
static const wchar_t *const *const filter_names[] = {dl_texts[T_SHARP], dl_texts[T_SMOOTH], dl_texts[T_EDGES]};
static const wchar_t *const *const gpu_names[] = {dl_texts[T_GPU_OFF], dl_texts[T_GPU_ON]};
static const wchar_t *const *const crt_names[] = {dl_texts[T_NO], dl_texts[T_CRT_FLAT], dl_texts[T_CRT_CURVED]};

static const DlRow langs_rows[] = {
    {DL_CHOICE, dl_texts[T_UILANG], dl_texts[T_H_UILANG], &dl_cfg.ui_lang, DL_LANGS - 1, NULL, show_lang, lang_changed},
};
// ---- picture presets: several rows of the Image page at once; "Custom" while they match none
typedef struct { int integer_scale, smooth, scanlines, crt; } Preset; // smooth: 0 sharp, 1 smooth, 2 smooth edges
static const Preset presets[4] = {{1, 0, 0, 0}, {0, 2, 0, 0}, {0, 1, 2, 2}, {0, 0, 0, 0}}; // original, large, CRT, performance
static const int preset_texts[4] = {T_PRE_ORIGINAL, T_PRE_LARGE, T_PRE_CRT, T_PRE_FAST};
// the presets offered, in order (without "CRT" when the game hides the CRT filter); preset_pick is one of them
static int preset_ids[4] = {0, 1, 2, 3}, preset_n = 4;
static int preset_pick; // the last preset chosen (< > go on from it)
static int preset_match(void) { // -1: none
  bool no_crt = dl_game->hide & DL_HIDE_CRT, no_scan = dl_game->hide & DL_HIDE_SCANLINES; // a hidden row is left aside
  for (int k = 0; k < preset_n; k++) {
    const Preset *p = &presets[preset_ids[k]];
    if (dl_cfg.integer_scale == p->integer_scale && dl_cfg.smooth == p->smooth && (no_scan || dl_cfg.scanlines == p->scanlines) &&
        (no_crt || dl_cfg.crt == p->crt) && (preset_ids[k] != 3 || !dl_cfg.ahead)) // Performance: no run-ahead either
      return k;
  }
  return -1;
}
static const wchar_t *preset_show(int v) {
  (void)v;
  int m = preset_match();
  if (m < 0) return TX(T_PRE_CUSTOM);
  preset_pick = m;
  return TX(preset_texts[preset_ids[m]]);
}
static void preset_changed(void) {
  int id = preset_ids[preset_pick < 0 || preset_pick >= preset_n ? 0 : preset_pick];
  const Preset *p = &presets[id];
  dl_cfg.integer_scale = p->integer_scale;
  dl_cfg.smooth = p->smooth;
  if (!(dl_game->hide & DL_HIDE_SCANLINES)) dl_cfg.scanlines = p->scanlines;
  if (!(dl_game->hide & DL_HIDE_CRT)) dl_cfg.crt = p->crt;
  if (id == 3) dl_cfg.ahead = 0;
  dl_dirty = true;
}
static DlRow image_rows[] = { // not const: the presets' count is set at start (DlGame.hide)
    {DL_CHOICE, dl_texts[T_PRESET], dl_texts[T_H_PRESET], &preset_pick, 3, NULL, preset_show, preset_changed},
    {DL_SWITCH, dl_texts[T_FULL], dl_texts[T_H_FULL], &dl_cfg.fullscreen, 1, NULL, NULL, full_changed},
    {DL_SWITCH, dl_texts[T_INT], dl_texts[T_H_INT], &dl_cfg.integer_scale, 1},
    {DL_CHOICE, dl_texts[T_GPU], dl_texts[T_H_GPU], &dl_cfg.gpu, 1, gpu_names, NULL, gpu_changed},
    {DL_CHOICE, dl_texts[T_FILTER], dl_texts[T_H_FILTER], &dl_cfg.smooth, 2, filter_names},
    {DL_BAR, dl_texts[T_SCAN], dl_texts[T_H_SCAN], &dl_cfg.scanlines, 4},
    {DL_CHOICE, dl_texts[T_CRT], dl_texts[T_H_CRT], &dl_cfg.crt, 2, crt_names},
#ifdef _WIN32
    {DL_CHOICE, dl_texts[T_HDR], dl_texts[T_H_HDR], &dl_cfg.hdr, 4, NULL, dl_hdr_value, hdr_changed, 0, hdr_more},
#endif
    {DL_SWITCH, dl_texts[T_VSYNC], dl_texts[T_H_VSYNC], &dl_cfg.vsync, 1},
    {DL_SWITCH, dl_texts[T_FPS], dl_texts[T_H_FPS], &dl_cfg.show_fps, 1},
    {DL_ACTION, dl_texts[T_TRY], dl_texts[T_H_TRY], NULL, 0, NULL, NULL, try_it},
    {DL_ACTION, dl_texts[T_GFXDEF], dl_texts[T_H_GFXDEF], NULL, 0, NULL, NULL, picture_defaults},
    {.kind = DL_PAGE, .label = dl_texts[T_A11Y], .help = dl_texts[T_H_A11Y], .page = PG_A11Y},
};
static const wchar_t *const *const cvd_names[] = {dl_texts[T_NO], dl_texts[T_CVD_PROTAN], dl_texts[T_CVD_DEUTAN], dl_texts[T_CVD_TRITAN]};
static const DlRow a11y_rows[] = {
    {DL_SWITCH, dl_texts[T_CONTRAST], dl_texts[T_H_CONTRAST], &dl_cfg.contrast, 1},
    {DL_CHOICE, dl_texts[T_CVD], dl_texts[T_H_CVD], &dl_cfg.cvd, 3, cvd_names},
    {DL_SWITCH, dl_texts[T_STILL], dl_texts[T_H_STILL], &dl_cfg.still, 1},
    {DL_SWITCH, dl_texts[T_BIG_HELP], dl_texts[T_H_BIG_HELP], &dl_cfg.big_help, 1},
};
static const DlRow sound_rows[] = {
    {DL_BAR, dl_texts[T_VOL], dl_texts[T_H_VOL], &dl_cfg.volume, 10},
    {DL_SWITCH, dl_texts[T_MENUSND], dl_texts[T_H_MENUSND], &dl_cfg.menu_sound, 1},
};
static const wchar_t *ps_more(void) { // PlayStation pads, under the help of "Controller buttons"
  static wchar_t out[400];
  _snwprintf(out, 399, L"\n\n%ls", TX(T_PS_NOTE));
  out[399] = 0;
  return out;
}
static const DlRow rumble_row = {DL_SWITCH, dl_texts[T_RUMBLE], dl_texts[T_H_RUMBLE], &dl_cfg.rumble, 1};
static const wchar_t *const *const ahead_names[] = {dl_texts[T_NO], dl_texts[T_AHEAD_1], dl_texts[T_AHEAD_2]};
static const DlRow ahead_row = {DL_CHOICE, dl_texts[T_AHEAD], dl_texts[T_H_AHEAD], &dl_cfg.ahead, 2, ahead_names};
static const DlRow ctrl_rows[] = {
    {.kind = DL_PAGE, .label = dl_texts[T_KEYS], .help = dl_texts[T_H_KEYS], .page = PG_KEYS},
    {.kind = DL_PAGE, .label = dl_texts[T_PAD], .help = dl_texts[T_H_PAD], .page = PG_PAD, .more = ps_more},
    {.kind = DL_PAGE, .label = dl_texts[T_HOT], .help = dl_texts[T_H_HOT], .page = PG_HOT},
    {.kind = DL_CHOICE, .label = dl_texts[T_TURBO], .help = dl_texts[T_H_TURBO], .value = &dl_cfg.turbo, .max = 4, .show = show_turbo, .run = turbo_changed},
    {.kind = DL_SWITCH, .label = dl_texts[T_PAUSEMENU], .help = dl_texts[T_H_PAUSEMENU], .value = &dl_cfg.pause_menu, .max = 1},
    {.kind = DL_SWITCH, .label = dl_texts[T_BGPAUSE], .help = dl_texts[T_H_BGPAUSE], .value = &dl_cfg.bg_pause, .max = 1},
};
#define KEY_ROW(k) {.kind = DL_KEY, .label = dl_texts[T_BTN_UP + (k)], .help = dl_texts[T_KH_BTN], .value = &dl_key_vk[k]}
static const DlRow keys_rows[] = {
    KEY_ROW(0), KEY_ROW(1), KEY_ROW(2), KEY_ROW(3), KEY_ROW(4), KEY_ROW(5), KEY_ROW(6), KEY_ROW(7), KEY_ROW(8), KEY_ROW(9), KEY_ROW(10), KEY_ROW(11),
    {.kind = DL_KEY, .label = dl_texts[T_BTN_TURBO], .help = dl_texts[T_KH_TURBO], .value = &dl_key_vk[DL_BTNS]},
    {.kind = DL_ACTION, .label = dl_texts[T_GFXDEF], .help = dl_texts[T_KH_DEFAULTS], .run = dl_keys_default},
};
#define PAD_ROW(k) {.kind = DL_PADKEY, .label = dl_texts[T_BTN_UP + (k)], .help = dl_texts[T_PH_BTN], .value = &dl_pad_code[k]}
static DlRow pad_rows[] = { // not const: the help of "Default values" follows the game's choices
    PAD_ROW(0), PAD_ROW(1), PAD_ROW(2), PAD_ROW(3), PAD_ROW(4), PAD_ROW(5), PAD_ROW(6), PAD_ROW(7), PAD_ROW(8), PAD_ROW(9), PAD_ROW(10), PAD_ROW(11),
    {.kind = DL_PAGE, .label = dl_texts[T_PADFN], .help = dl_texts[T_PH_FN], .page = PG_PADFN},
    {.kind = DL_ACTION, .label = dl_texts[T_GFXDEF], .help = dl_texts[T_PH_DEFAULTS], .run = dl_pads_default},
};
#define FN_ROW(k, t, h) {.kind = DL_PADKEY, .label = dl_texts[t], .help = dl_texts[h], .value = &dl_pad_code[k], .max = 1}
static DlRow padfn_rows[] = { // the save-state rows come second, left out when the game has none
    FN_ROW(FN_TURBO, T_BTN_TURBO, T_PH_TURBO), FN_ROW(FN_SAVE, T_HK_SAVE, T_PH_ACT), FN_ROW(FN_LOAD, T_HK_LOAD, T_PH_ACT),
    FN_ROW(FN_SHOT, T_HK_SHOT, T_PH_ACT), FN_ROW(FN_MENU, T_FN_MENU, T_PH_ACT),
};
#define HOT_ROW(k) {.kind = DL_KEY, .label = dl_texts[T_HK_SAVE + (k)], .help = dl_texts[T_HKH], .value = &dl_hot_vk[k]}
static const DlRow hot_rows[] = { // the save-state rows and the rewind are left out when the game has none
    HOT_ROW(HK_SAVE), HOT_ROW(HK_LOAD), HOT_ROW(HK_PREV), HOT_ROW(HK_NEXT), HOT_ROW(HK_SHOT), HOT_ROW(HK_FULL),
    {.kind = DL_KEY, .label = dl_texts[T_HK_REWIND], .help = dl_texts[T_HKH], .value = &dl_hot_vk[HK_REWIND]},
    {.kind = DL_ACTION, .label = dl_texts[T_GFXDEF], .help = dl_texts[T_H_GFXDEF], .run = dl_hot_default},
};

// ---- the game's news: one row a line of DlGame.news, shown the first time its version starts
int dl_news_seen;
static int news_hash(void) { // of DlGame.version, kept as a setting (0: none)
  unsigned h = 2166136261u;
  for (const char *c = dl_game->version; c && *c; c++) h = (h ^ (unsigned char)*c) * 16777619u;
  return (int)(h & 0x7FFFFFFF) | 1;
}
static const wchar_t *news_text(void) { return dl_game->news ? dl_tr(dl_game->news) : L""; }
static int news_line(int k, const wchar_t **at) { // line k of the news: where it starts, how long
  const wchar_t *s = news_text();
  for (int i = 0; i < k && s; i++) {
    s = wcschr(s, L'\n');
    if (s) s++;
  }
  if (!s || !*s) return -1;
  const wchar_t *end = wcschr(s, L'\n');
  *at = s;
  return end ? (int)(end - s) : lstrlenW(s);
}
static void news_open_list(void) {}
static int news_count(void) {
  const wchar_t *at;
  int n = 0;
  while (news_line(n, &at) >= 0) n++;
  return n;
}
static int news_title(const wchar_t *at, int len) { // its title's length: up to "|"
  for (int i = 0; i < len; i++)
    if (at[i] == L'|') return i;
  return len;
}
static void news_row(int k, wchar_t *label, wchar_t *value) {
  const wchar_t *at;
  int len = news_line(k, &at), t = news_title(at, len);
  lstrcpynW(label, at, (t < 63 ? t : 63) + 1);
  value[0] = 0;
}
static const wchar_t *news_help(int k) { // what it is, then which version
  static wchar_t buf[1024], tail[256];
  const wchar_t *at;
  int len = news_line(k, &at), t = news_title(at, len);
  _snwprintf(tail, 255, TX(T_H_NEWS), dl_game->version ? dl_game->version : "");
  tail[255] = 0;
  if (t < len) _snwprintf(buf, 1023, L"%.*ls\n\n%ls", len - t - 1, at + t + 1, tail);
  else _snwprintf(buf, 1023, L"%ls", tail);
  buf[1023] = 0;
  return buf;
}
static const DlList news_list = {news_open_list, news_count, news_row, news_help, NULL, NULL, NULL, NULL, NULL};

bool dl_news_open(void) {
  if (!dl_game->version || !dl_game->news || dl_news_seen == news_hash()) return false;
  dl_news_seen = news_hash();
  dl_open_page(PG_NEWS);
  return true;
}

// ---- the first start: the language, the controls, the display, then the game
static void welcome_ctrl(void) { dl_open_page(PG_WELCOME_CTRL); }
static void welcome_screen(void) { dl_open_page(PG_WELCOME_SCREEN); }
static const DlRow welcome_rows[] = {
    {DL_CHOICE, dl_texts[T_UILANG], dl_texts[T_H_UILANG], &dl_cfg.ui_lang, DL_LANGS - 1, NULL, show_lang, lang_changed},
    {DL_ACTION, dl_texts[T_W_NEXT], dl_texts[T_H_W_LANG], NULL, 0, NULL, NULL, welcome_ctrl},
};
static const DlRow welcome_ctrl_rows[] = {
    {.kind = DL_PAGE, .label = dl_texts[T_KEYS], .help = dl_texts[T_H_KEYS], .page = PG_KEYS},
    {.kind = DL_PAGE, .label = dl_texts[T_PAD], .help = dl_texts[T_H_PAD], .page = PG_PAD},
    {DL_ACTION, dl_texts[T_W_NEXT], dl_texts[T_H_W_CTRL], NULL, 0, NULL, NULL, welcome_screen},
};
static DlRow welcome_screen_rows[] = { // not const, as image_rows
    {DL_SWITCH, dl_texts[T_FULL], dl_texts[T_H_FULL], &dl_cfg.fullscreen, 1, NULL, NULL, full_changed},
    {DL_CHOICE, dl_texts[T_PRESET], dl_texts[T_H_PRESET], &preset_pick, 3, NULL, preset_show, preset_changed},
    {DL_ACTION, dl_texts[T_W_PLAY], dl_texts[T_H_W_PLAY], NULL, 0, NULL, NULL, dl_play},
};

void dl_welcome_open(void) {
  if (dl_news_seen == 0 && dl_game->version && dl_game->news) dl_news_seen = news_hash(); // a first start has no news to show
  dl_open_page(PG_WELCOME);
}

// ---- Internet (both off at first): the status on Discord, the look for a newer version
static const wchar_t *updates_more(void) {
  static wchar_t out[160];
  const wchar_t *t = dl_update_text();
  _snwprintf(out, 159, L"\n\n%ls", t ? t : L"");
  out[159] = 0;
  return t ? out : L"";
}
static const DlRow net_discord_row = {DL_SWITCH, dl_texts[T_DISCORD], dl_texts[T_H_DISCORD], &dl_cfg.discord, 1, NULL, NULL, dl_discord_changed};
static const DlRow net_updates_row = {DL_SWITCH, dl_texts[T_UPDATES], dl_texts[T_H_UPDATES], &dl_cfg.updates, 1, NULL, NULL, dl_online_start, 0, updates_more};
static const DlRow net_open_row = {DL_ACTION, dl_texts[T_UPD_OPEN], dl_texts[T_H_UPD_OPEN], NULL, 0, NULL, NULL, dl_update_open};
static const DlRow net_row = {.kind = DL_PAGE, .label = dl_texts[T_NET], .help = dl_texts[T_H_NET], .page = PG_NET};

// ---- Settings: one entry of the bar for the kit's pages and its rarer actions
static void settings_folder(void) { ShellExecuteA(NULL, "open", dl_base_dir, NULL, NULL, SW_SHOWNORMAL); }
static void settings_defaults(void) { dl_defaults(false); }
static const DlRow settings_rows[] = {
    {.kind = DL_PAGE, .label = dl_texts[T_LANGS], .help = dl_texts[T_H_LANGS], .page = DL_PAGE_LANGS},
    {.kind = DL_PAGE, .label = dl_texts[T_IMAGE], .help = dl_texts[T_H_IMAGE], .page = DL_PAGE_IMAGE},
    {.kind = DL_PAGE, .label = dl_texts[T_SOUND], .help = dl_texts[T_H_SOUND], .page = DL_PAGE_SOUND},
    {.kind = DL_PAGE, .label = dl_texts[T_CTRL], .help = dl_texts[T_H_CTRL], .page = PG_CTRL},
    {DL_ACTION, dl_texts[T_FOLDER], dl_texts[T_H_FOLDER], NULL, 0, NULL, NULL, settings_folder},
    {DL_ACTION, dl_texts[T_DEFAULTS], dl_texts[T_H_DEFAULTS], NULL, 0, NULL, NULL, settings_defaults},
};
static const DlRow settings_reset_row = {DL_ACTION, dl_texts[T_RESET], dl_texts[T_H_RESET], NULL, 0, NULL, NULL, dl_restart};

void dl_menu_init(void) {
  bool saves = dl_game->save_state && dl_game->load_state;
  if (dl_game->pad_layout || dl_game->pad_menu) pad_rows[sizeof pad_rows / sizeof pad_rows[0] - 1].help = dl_texts[T_PH_DEFAULTS_GAME];
  if (dl_game->pad_menu == DL_MENU_HOLD) padfn_rows[4].help = dl_texts[T_PH_MENU_HOLD]; // FN_MENU's row
  dl_page(dl_texts[T_TAB_LANGS], langs_rows, 1);
  // the rows the game does not offer (DlGame.hide), and the "CRT" preset with the CRT filter
  if (dl_game->hide & DL_HIDE_CRT) {
    preset_n = 0;
    for (int k = 0; k < 4; k++)
      if (k != 2) preset_ids[preset_n++] = k;
  }
  image_rows[0].max = welcome_screen_rows[1].max = preset_n - 1;
  if (dl_game->hide & DL_HIDE_CRT) image_rows[0].help = welcome_screen_rows[1].help = dl_texts[T_H_PRESET_NOCRT];
  dl_page(dl_texts[T_TAB_IMAGE], image_rows, sizeof image_rows / sizeof image_rows[0]);
  for (int k = dl_pages[DL_PAGE_IMAGE].n - 1; k >= 0; k--) {
    const int *v = dl_pages[DL_PAGE_IMAGE].rows[k]->value;
    if ((v == &dl_cfg.crt && (dl_game->hide & DL_HIDE_CRT)) || (v == &dl_cfg.scanlines && (dl_game->hide & DL_HIDE_SCANLINES))) {
      DlPage *p = &dl_pages[DL_PAGE_IMAGE];
      memmove(&p->rows[k], &p->rows[k + 1], sizeof p->rows[0] * (size_t)(p->n - k - 1));
      p->n--;
    }
  }
  dl_page(dl_texts[T_TAB_SOUND], sound_rows, sizeof sound_rows / sizeof sound_rows[0]);
  dl_page(dl_texts[T_TAB_CTRL], ctrl_rows, sizeof ctrl_rows / sizeof ctrl_rows[0]);
  if (dl_game->rumble) dl_page_insert(PG_CTRL, 3, &rumble_row); // after the three pages of assignments
  if (dl_ahead_ok()) dl_page_insert(PG_CTRL, 3 + (dl_game->rumble ? 1 : 0), &ahead_row);
  dl_page(dl_texts[T_TAB_KEYS], keys_rows, sizeof keys_rows / sizeof keys_rows[0]);
  dl_page(dl_texts[T_TAB_PAD], pad_rows, sizeof pad_rows / sizeof pad_rows[0]);
  DlPage *pf = &dl_pages[dl_page(dl_texts[T_TAB_PADFN], padfn_rows, 1)];
  for (int k = 1; k < (int)(sizeof padfn_rows / sizeof padfn_rows[0]); k++)
    if (saves || (k != 1 && k != 2)) pf->rows[pf->n++] = &padfn_rows[k];
  DlPage *ph = &dl_pages[dl_page(dl_texts[T_TAB_HOT], NULL, 0)];
  for (int k = 0; k < (int)(sizeof hot_rows / sizeof hot_rows[0]); k++)
    if ((saves || k > HK_NEXT) && (k != HK_REWIND || dl_rewind_ok())) ph->rows[ph->n++] = &hot_rows[k];
  dl_list_page(dl_texts[T_TAB_SAVES], &dl_saves_list);
  dl_list_page(dl_texts[T_TAB_SHOTS], &dl_shots_list);
  dl_page(dl_texts[T_TAB_A11Y], a11y_rows, sizeof a11y_rows / sizeof a11y_rows[0]);
  dl_list_page(dl_texts[T_TAB_BACKUPS], &dl_backups_list);
  dl_list_page(dl_texts[T_TAB_NEWS], &news_list);
  dl_page(dl_texts[T_TAB_WELCOME], welcome_rows, sizeof welcome_rows / sizeof welcome_rows[0]);
  dl_page(dl_texts[T_TAB_CTRL], welcome_ctrl_rows, sizeof welcome_ctrl_rows / sizeof welcome_ctrl_rows[0]);
  dl_page(dl_texts[T_TAB_IMAGE], welcome_screen_rows, sizeof welcome_screen_rows / sizeof welcome_screen_rows[0]);
  DlPage *pn = &dl_pages[dl_page(dl_texts[T_TAB_NET], NULL, 0)];
  if (DISCORD_APP_ID[0]) pn->rows[pn->n++] = &net_discord_row;
  if (UPDATE_URL[0]) { pn->rows[pn->n++] = &net_updates_row; pn->rows[pn->n++] = &net_open_row; }
  if (pn->n) dl_page_insert(PG_CTRL, dl_pages[PG_CTRL].n, &net_row); // the last row of the Controls page
  dl_page(dl_texts[T_TAB_SETTINGS], settings_rows, sizeof settings_rows / sizeof settings_rows[0]);
  if (dl_game->reset) dl_page_insert(PG_SETTINGS, dl_pages[PG_SETTINGS].n, &settings_reset_row); // a game that cannot restart has no such row
  add_section(SEC_PLAY, -1, 0xE768, dl_texts[T_PLAY], dl_texts[T_H_PLAY]);
  if (saves) add_section(SEC_PAGE, PG_SAVES, 0xE74E, dl_texts[T_SAVES], dl_texts[T_H_SAVES]);
  add_section(SEC_PAGE, PG_SHOTS, 0xE722, dl_texts[T_SHOTS], dl_texts[T_H_SHOTS]);
}

// ---- safe mode: a page shown at the start of a run after one that did not close normally (dl_app.c)
static int safe_page = -1;
static void safe_keep(void) { dl_mode = M_SIDE; dl_side = 0; }
static void safe_load(void) {
  int n = dl_latest_auto();
  if (n == -1) dl_session_resume();
  else if (n >= 0 && dl_state_load(n)) dl_play();
}
static const wchar_t *report_where(void) { // under the help of "Copy the crash report": its file
  static wchar_t out[MAX_PATH + 8];
  _snwprintf(out, MAX_PATH + 7, L"\n%hs", dl_report_file());
  out[MAX_PATH + 7] = 0;
  return out;
}
static const DlRow safe_rows[] = {
    {DL_ACTION, dl_texts[T_SAFE_KEEP], dl_texts[T_H_SAFE_KEEP], NULL, 0, NULL, NULL, safe_keep},
    {DL_ACTION, dl_texts[T_SAFE_RESTART], dl_texts[T_H_SAFE_RESTART], NULL, 0, NULL, NULL, dl_safe_restart},
    {DL_ACTION, dl_texts[T_SAFE_REPORT], dl_texts[T_H_SAFE_REPORT], NULL, 0, NULL, NULL, dl_report_copy, 0, report_where},
};
static const DlRow safe_load_row = {DL_ACTION, dl_texts[T_SAFE_LOAD], dl_texts[T_H_SAFE_LOAD], NULL, 0, NULL, NULL, safe_load};

void dl_safe_page_open(void) {
  if (safe_page < 0) return;
  if (dl_latest_auto() >= -1) dl_page_insert(safe_page, 1, &safe_load_row); // only when there is one
  dl_open_page(safe_page);
}

void dl_menu_finish(void) {
  // the kit's pages and rarer actions behind one entry (Settings), then Quit alone at the bottom
  add_section(SEC_PAGE, PG_SETTINGS, 0xE713, dl_texts[T_PA_SETTINGS], dl_texts[T_H_SETTINGS]);
  dl_sec_top = dl_sec_n;
  add_section(SEC_QUIT, -1, 0xE7E8, dl_texts[T_QUIT], dl_texts[T_H_QUIT]);
  // the safe mode page, reached only after a crash (made always, so that --ui-audit sees it)
  safe_page = dl_page(dl_texts[T_SAFE_TAB], safe_rows, sizeof safe_rows / sizeof safe_rows[0]);
  if (dl_audit_dir) dl_page_insert(safe_page, 1, &safe_load_row);
}

// ---- rows
#define LIST_VIS ((int)((PANEL_BOTTOM - 6 - ROW_Y0) / ROW_H) - 1) // rows of a list shown at once, "Back" under them

static const DlList *page_list(int page) { return page >= 0 && page < dl_page_n ? dl_pages[page].list : NULL; }

// The contents of a list page, read when it is first shown.
static void list_follow(int page) {
  const DlList *l = page_list(page);
  if (!l) return;
  if (list_read != page) { l->open(); list_read = page; dl_pages[page].top = 0; }
  DlPage *p = &dl_pages[page];
  int n = l->count();
  if (dl_mode == M_PAGE && dl_cur == page && dl_sel < n) { // the selected row stays in sight
    if (dl_sel < p->top) p->top = dl_sel;
    if (dl_sel >= p->top + LIST_VIS) p->top = dl_sel - LIST_VIS + 1;
  }
  if (p->top > n - LIST_VIS) p->top = n - LIST_VIS;
  if (p->top < 0) p->top = 0;
}

static int page_rows(int page) { // + "Back"
  if (page < 0 || page >= dl_page_n) return 0;
  const DlList *l = dl_pages[page].list;
  if (l) { list_follow(page); return l->count() + 1; }
  return dl_pages[page].n + 1;
}

static double row_h(int page) {
  if (page_list(page)) return ROW_H;
  int n = page_rows(page);
  double fit = (PANEL_BOTTOM - 6 - ROW_Y0) / (n > 0 ? n : 1);
  return fit < ROW_H ? fit : ROW_H;
}

static const wchar_t *row_value(const DlRow *r) {
  static const wchar_t empty[] = L"", dots[] = L"...";
  switch (r->kind) {
    case DL_ACTION: return r->show ? r->show(r->value ? *r->value : 0) : NULL;
    case DL_CHOICE: {
      int v = r->value ? *r->value : 0;
      if (r->names) return dl_tr(r->names[v < 0 ? 0 : v > r->max ? r->max : v]);
      if (r->show) return r->show(v);
      return empty;
    }
    case DL_BAR: return empty;
    case DL_KEY: return !r->value ? empty : dl_wait == r->value ? dots : dl_key_name(*r->value);
    case DL_PADKEY: return !r->value ? empty : dl_wait == r->value ? dots : dl_pad_name(r->value);
    default: return NULL;
  }
}

static void open_page(int page) {
  if (dl_mode == M_PAGE && stack_n < 8) { stack_sel[stack_n] = dl_sel; stack[stack_n++] = dl_cur; }
  dl_mode = M_PAGE;
  dl_cur = page;
  dl_sel = 0;
  list_read = -1;
}

static void page_back(void) {
  if (stack_n) {
    int from = dl_cur;
    dl_cur = stack[--stack_n];
    dl_sel = stack_sel[stack_n];
    for (int k = 0; k < dl_pages[dl_cur].n; k++) // the row that opened the page we come from
      if (dl_pages[dl_cur].rows[k]->kind == DL_PAGE && dl_pages[dl_cur].rows[k]->page == from) dl_sel = k;
  } else dl_mode = M_SIDE;
}

// Changes the selected row: dir -1 / +1 (Left / Right), or enter (Enter, a click).
static void row_adjust(int dir, bool enter) {
  DlPage *p = &dl_pages[dl_cur];
  if (p->list) {
    if (!enter) return;
    if (dl_sel >= p->list->count()) page_back();
    else if (p->list->enter) p->list->enter(dl_sel);
    return;
  }
  if (dl_sel >= p->n) { if (enter) page_back(); return; } // "Back"
  const DlRow *r = p->rows[dl_sel];
  switch (r->kind) {
    case DL_ACTION:
      if (enter && r->run) r->run();
      return;
    case DL_PAGE:
      if (enter) open_page(r->page);
      return;
    case DL_KEY:
    case DL_PADKEY:
      if (enter && r->value) dl_wait_for(r->value, r->kind == DL_PADKEY);
      return;
    case DL_SWITCH:
      if (r->value) *r->value = !*r->value;
      break;
    case DL_CHOICE:
      if (r->value) *r->value = (*r->value + (dir < 0 ? r->max : 1)) % (r->max + 1);
      break;
    case DL_BAR:
      if (!r->value) return;
      if (enter) *r->value = *r->value >= r->max ? 0 : *r->value + 1;
      else {
        int v = *r->value + dir;
        if (v < 0 || v > r->max) return;
        *r->value = v;
      }
      break;
  }
  if (r->run) r->run();
}

// Delete (Y on the controller): erases a row of a list, or leaves a controller function unassigned.
static void row_erase(void) {
  DlPage *p = &dl_pages[dl_cur];
  if (p->list) {
    if (dl_sel < p->list->count() && p->list->erase) p->list->erase(dl_sel);
    return;
  }
  if (dl_sel < p->n && p->rows[dl_sel]->kind == DL_PADKEY && p->rows[dl_sel]->max == 1) *dl_pad_value(p->rows[dl_sel]->value) = -1;
}

static void side_act(void) {
  const DlSection *s = &dl_secs[dl_side];
  switch (s->kind) {
    case SEC_PAGE: stack_n = 0; open_page(s->page); break;
    case SEC_PLAY: if (dl_started || !dl_session_resume()) dl_play(); break;
    case SEC_FOLDER: ShellExecuteA(NULL, "open", dl_base_dir, NULL, NULL, SW_SHOWNORMAL); break;
    case SEC_DEFAULTS: dl_defaults(false); break;
    case SEC_RESET: dl_restart(); break;
    case SEC_QUIT: dl_running = false; break;
  }
}

// ---- the pause menu: a panel over the paused game
enum { PA_RESUME, PA_SAVE, PA_LOAD, PA_SHOT, PA_SETTINGS, PA_QUIT, PA_COUNT };
static int pa_items[PA_COUNT + DL_MAX_FUNCS], pa_n; // the rows shown (without save states: no save and load); PA_COUNT + k: the game's function k
#define PA_X0 192.0
#define PA_X1 448.0
#define PA_VAL0 (PA_X1 - 64)

static void pause_rows(void) {
  pa_n = 0;
  for (int k = 0; k < PA_COUNT; k++) {
    if (k == PA_SETTINGS) // the game's functions before the settings
      for (int f = 0; f < dl_func_n; f++)
        if (dl_funcs[f].in_pause) pa_items[pa_n++] = PA_COUNT + f;
    if ((k != PA_SAVE && k != PA_LOAD) || (dl_game->save_state && dl_game->load_state)) pa_items[pa_n++] = k;
  }
}

void dl_pause_open(void) {
  if (!dl_cfg.pause_menu) { dl_menu_open(); return; }
  pause_rows();
  dl_mode = M_PAUSE;
  dl_sel = 0;
  dl_dirty = true;
}

static double pa_y0(void) { return 200 - (pa_n * ROW_H + 80) / 2 + 34; } // top of the first row

static void pause_act(int dir, bool enter) {
  int it = pa_items[dl_sel < 0 || dl_sel >= pa_n ? 0 : dl_sel];
  if (it == PA_SAVE || it == PA_LOAD) {
    if (!enter) { dl_slot = (dl_slot + DL_SLOTS + (dir < 0 ? -1 : 1)) % DL_SLOTS; return; }
  } else if (!enter) return;
  if (it >= PA_COUNT) { dl_funcs[it - PA_COUNT].run(); return; }
  switch (it) {
    case PA_RESUME: dl_play(); break;
    case PA_SAVE: dl_notef(dl_state_save(dl_slot) ? TX(T_M_SAVED) : TX(T_M_SAVE_FAIL), dl_slot); break;
    case PA_LOAD:
      if (!dl_slot_used(dl_slot)) dl_notef(TX(T_M_SLOT_EMPTY), dl_slot);
      else if (!dl_state_load(dl_slot)) dl_notef(TX(T_M_LOAD_FAIL), dl_slot);
      else { dl_play(); dl_notef(TX(T_M_LOADED), dl_slot); }
      break;
    case PA_SHOT: dl_screenshot(); break; // the picture of the game alone, without this menu
    case PA_SETTINGS: dl_menu_open(); break;
    case PA_QUIT: dl_running = false; break;
  }
}

static void draw_pause(HDC dc, int cw, int ch) {
  RECT all = {0, 0, cw, ch};
  FillRect(dc, &all, GetStockObject(dl_layer == 2 ? WHITE_BRUSH : BLACK_BRUSH));
  if (!dl_layer) { // the paused picture (the graphics card draws it itself under the layer)
    int pw, ph, keep = dl_cfg.show_fps;
    dl_fit(cw, ch, dl_cfg.integer_scale, &pw, &ph);
    dl_cfg.show_fps = 0;
    dl_blit(dc, (cw - pw) / 2, (ch - ph) / 2, pw, ph);
    dl_cfg.show_fps = keep;
  }
  ui_rect_alpha(dc, -ui_ox / ui_s, -ui_oy / ui_s, (cw - ui_ox) / ui_s + 1, (ch - ui_oy) / ui_s + 1, ui_tint(RGB(0, 6, 30)), 120);
  double y0 = pa_y0(), y1 = y0 + pa_n * ROW_H + 46;
  ui_panel(dc, PA_X0, y0 - 20, PA_X1, y1, COL_PANEL, TX(T_TAB_PAUSE));
  wchar_t num[8];
  for (int r = 0; r < pa_n; r++) {
    int it = pa_items[r];
    double y = y0 + r * ROW_H;
    bool sel = dl_sel == r, slot_row = it == PA_SAVE || it == PA_LOAD;
    const wchar_t *label = it >= PA_COUNT ? dl_tr(dl_funcs[it - PA_COUNT].label) : it == PA_RESUME ? TX(T_RESUME) : it == PA_SAVE ? TX(T_HK_SAVE)
                         : it == PA_LOAD ? TX(T_HK_LOAD) : it == PA_SHOT ? TX(T_HK_SHOT) : it == PA_SETTINGS ? TX(T_PA_SETTINGS) : TX(T_QUIT);
    if (sel) ui_cursor(dc, PA_X0 + 3, y - 1, PA_X1 - 3, y + ROW_H - 2);
    ui_text_fit(dc, PA_X0 + 12, y, label, sel ? COL_SEL : ui_tint(RGB(240, 242, 255)), TA_LEFT, (slot_row ? PA_VAL0 : PA_X1 - 6) - PA_X0 - 16);
    if (slot_row) { // the slot, changed with Left and Right
      ui_box(dc, PA_VAL0, y, PA_X1 - 8, y + ROW_H - 4, ui_tint(RGB(6, 18, 58)), 4);
      wsprintfW(num, L"%d", dl_slot);
      ui_text(dc, f_item, (PA_VAL0 + PA_X1 - 8) / 2, y, num, COL_VALUE, TA_CENTER);
      if (sel) {
        ui_text(dc, f_item, PA_VAL0 + 5, y, L"<", RGB(255, 255, 255), TA_LEFT);
        ui_text(dc, f_item, PA_X1 - 13, y, L">", RGB(255, 255, 255), TA_RIGHT);
      }
    }
  }
  // under the rows: the result of the last action, or what the selected slot holds, or how to go back
  static wchar_t line[160];
  const wchar_t *note = dl_note_now();
  int it = pa_items[dl_sel < 0 || dl_sel >= pa_n ? 0 : dl_sel];
  if (!note && (it == PA_SAVE || it == PA_LOAD)) {
    _snwprintf(line, 159, dl_slot_used(dl_slot) ? TX(T_M_SLOT) : TX(T_M_SLOT_FREE), dl_slot);
    line[159] = 0;
    note = line;
  }
  if (!note) note = TX(T_PAH);
  RECT r = {ui_ox + PX(PA_X0 + 12), ui_oy + PX(y0 + pa_n * ROW_H + 6), ui_ox + PX(PA_X1 - 10), ui_oy + PX(y1 - 4)};
  SelectObject(dc, f_help_small);
  SetBkMode(dc, TRANSPARENT);
  SetTextAlign(dc, TA_LEFT | TA_TOP);
  SetTextColor(dc, ui_tint(RGB(200, 216, 250)));
  RECT need = r;
  DrawTextW(dc, note, -1, &need, DT_WORDBREAK | DT_NOPREFIX | DT_CALCRECT);
  if (need.bottom > r.bottom) ui_audit_note("pause note overflows", note);
  DrawTextW(dc, note, -1, &r, DT_WORDBREAK | DT_NOPREFIX);
  dl_prev_w = 0;
}

void dl_key(int vk) {
  dl_dirty = true;
  if (dl_wait) { dl_wait_key(vk); return; }
  if (dl_mode == M_PAGE && dl_cur == PG_SAVES && dl_saves_key(vk)) return; // a slot's name being typed
  if (dl_mode == M_PAGE && dl_packs_key(vk)) return;                          // a pack moved up or down
  if (dl_mode == M_TRY) { dl_mode = M_PAGE; return; }
  if (vk == VK_UP || vk == VK_DOWN) dl_beep(0);
  else if (vk == VK_RETURN || vk == VK_LEFT || vk == VK_RIGHT) dl_beep(1);
  if (dl_mode == M_PAUSE) {
    switch (vk) {
      case VK_UP: dl_sel = (dl_sel + pa_n - 1) % pa_n; break;
      case VK_DOWN: dl_sel = (dl_sel + 1) % pa_n; break;
      case VK_LEFT: pause_act(-1, false); break;
      case VK_RIGHT: pause_act(1, false); break;
      case VK_RETURN: pause_act(0, true); break;
      case VK_ESCAPE: dl_play(); break;
    }
    return;
  }
  if (dl_mode == M_SIDE) {
    switch (vk) {
      case VK_UP: dl_side = (dl_side + dl_sec_n - 1) % dl_sec_n; list_read = -1; break;
      case VK_DOWN: dl_side = (dl_side + 1) % dl_sec_n; list_read = -1; break;
      case VK_RIGHT: if (dl_secs[dl_side].kind == SEC_PAGE) side_act(); break;
      case VK_RETURN: side_act(); break;
      case VK_ESCAPE: if (dl_started) { if (dl_cfg.pause_menu) dl_pause_open(); else dl_play(); } break;
    }
    return;
  }
  int count = page_rows(dl_cur);
  const DlList *l = page_list(dl_cur);
  if (l && vk != VK_DELETE && l->moved) l->moved();
  switch (vk) {
    case VK_UP: dl_sel = (dl_sel + count - 1) % count; break;
    case VK_DOWN: dl_sel = (dl_sel + 1) % count; break;
    case VK_LEFT: row_adjust(-1, false); break;
    case VK_RIGHT: row_adjust(1, false); break;
    case VK_RETURN: row_adjust(0, true); break;
    case VK_DELETE: row_erase(); break;
    case VK_ESCAPE: page_back(); break;
  }
}

// ---- drawing
static double side_y(int k) { return k < dl_sec_top ? ROW_Y0 + k * ROW_H : PANEL_BOTTOM - 6 - (dl_sec_n - k) * ROW_H; }

static void draw_side(HDC dc) {
  ui_panel(dc, 10, 60, 156, PANEL_BOTTOM, COL_SIDE, NULL);
  HFONT icons = ui_icons(dc);
  for (int k = 0; k < dl_sec_n; k++) {
    double y = side_y(k);
    bool focus = dl_mode == M_SIDE && dl_side == k, here = !focus && dl_secs[k].kind == SEC_PAGE && dl_side == k;
    if (focus) ui_cursor(dc, SIDE_X0, y - 1, SIDE_X1, y + ROW_H - 2);
    else if (here) ui_box(dc, SIDE_X0, y - 1, SIDE_X1, y + ROW_H - 2, ui_tint(RGB(22, 50, 118)), 4);
    COLORREF c = focus ? COL_SEL : here ? RGB(255, 255, 255) : ui_tint(RGB(196, 212, 244));
    double lx = SIDE_X0 + 8;
    if (dl_secs[k].icon && ui_theme_icon(dc, dl_secs[k].icon, SIDE_X0 + 7, y, SIDE_X0 + 23, y + 16)) lx = SIDE_X0 + 28;
    else if (icons && dl_secs[k].icon) {
      wchar_t g[2] = {(wchar_t)dl_secs[k].icon, 0};
      ui_text(dc, icons, SIDE_X0 + 8, y + 2, g, c, TA_LEFT);
      lx = SIDE_X0 + 28;
    }
    const wchar_t *label = dl_secs[k].kind == SEC_PLAY && !dl_started && dl_session_exists() ? TX(T_CONTINUE) : dl_tr(dl_secs[k].label);
    ui_text_fit(dc, lx, y, label, c, TA_LEFT, SIDE_X1 - lx - 4);
  }
}

static void draw_rows(HDC dc, int page, int sel) {
  DlPage *p = &dl_pages[page];
  if (p->list) {
    int n = p->list->count(), shown = n - p->top < LIST_VIS ? n - p->top : LIST_VIS;
    wchar_t label[64], value[64];
    for (int r = 0; r < shown; r++) {
      int k = p->top + r;
      p->list->row(k, label, value);
      ui_row(dc, ROW_Y0 + r * ROW_H, ROW_H, sel == k, label, value[0] ? value : NULL, -1, true, 0, 0);
    }
    ui_row(dc, ROW_Y0 + shown * ROW_H, ROW_H, sel == n, TX(T_BACK), NULL, -1, false, 0, 0);
    return;
  }
  double h = row_h(page);
  for (int k = 0; k <= p->n; k++) {
    double y = ROW_Y0 + k * h;
    if (k == p->n) { ui_row(dc, y, h, sel == k, TX(T_BACK), NULL, -1, false, 0, 0); continue; }
    const DlRow *r = p->rows[k];
    int sw = r->kind == DL_SWITCH && r->value ? (*r->value != 0) : -1;
    int bar = r->kind == DL_BAR && r->value ? *r->value : 0, bar_max = r->kind == DL_BAR ? r->max : 0;
    ui_row(dc, y, h, sel == k, dl_tr(r->label), row_value(r), sw, r->kind == DL_KEY || r->kind == DL_PADKEY || r->kind == DL_ACTION, bar, bar_max);
  }
}

int dl_prev_x, dl_prev_y, dl_prev_w, dl_prev_h; // the live picture inside the setup screen
u32 *dl_menu_bits;

static void draw_help(HDC dc, double x0, double y0, double x1, double y1, const wchar_t *help) {
  ui_panel(dc, x0, y0, x1, y1, COL_PANEL, NULL);
  RECT hr = {ui_ox + PX(x0 + 10), ui_oy + PX(y0 + 6), ui_ox + PX(x1 - 8), ui_oy + PX(y1 - 4)};
  SelectObject(dc, f_help);
  SetTextAlign(dc, TA_LEFT | TA_TOP);
  SetTextColor(dc, ui_tint(RGB(240, 242, 255)));
  RECT need = hr;
  DrawTextW(dc, help, -1, &need, DT_WORDBREAK | DT_NOPREFIX | DT_CALCRECT);
  if (need.bottom > hr.bottom) {
    SelectObject(dc, f_help_small);
    need = hr;
    DrawTextW(dc, help, -1, &need, DT_WORDBREAK | DT_NOPREFIX | DT_CALCRECT);
    if (need.bottom > hr.bottom) ui_audit_note("help overflows", help);
  }
  DrawTextW(dc, help, -1, &hr, DT_WORDBREAK | DT_NOPREFIX);
}

// The footer with a controller: its buttons, each with what it does.
static double pad_item(HDC dc, double x, int glyph, const wchar_t *label) {
  SIZE sz;
  ui_pad_glyph(dc, glyph, x + 6.5, 388, 13);
  SelectObject(dc, f_small);
  GetTextExtentPoint32W(dc, label, lstrlenW(label), &sz);
  ui_text(dc, f_small, x + 18, 382, label, RGB(0, 0, 0), TA_LEFT);
  ui_text(dc, f_small, x + 17, 381, label, RGB(255, 255, 255), TA_LEFT);
  return x + 17 + sz.cx / ui_s + 20;
}

static void pad_footer(HDC dc) {
  double x = pad_item(dc, 16, GL_DPAD, TX(T_FT_MOVE));
  x = pad_item(dc, x, dl_pad_glyph(true), TX(T_FT_OK));
  x = pad_item(dc, x, dl_pad_glyph(false), TX(T_FT_BACK));
  if (x > 630) ui_audit_note("footer too long", TX(T_FT_MOVE));
}

// The game alone in the window ("Try it full size", and the game itself through dl_app.c).
static void draw_try(HDC dc, int cw, int ch) {
  RECT all = {0, 0, cw, ch};
  FillRect(dc, &all, GetStockObject(dl_layer == 2 ? WHITE_BRUSH : BLACK_BRUSH));
  int w, h;
  dl_fit(cw, ch, dl_cfg.integer_scale, &w, &h);
  if (!dl_layer) dl_blit(dc, (cw - w) / 2, (ch - h) / 2, w, h);
  ui_text(dc, f_small, 320, 384, TX(T_TRY_BACK), ui_tint(RGB(220, 230, 255)), TA_CENTER);
}

// A picture, whole and centred, in the area x0,y0 - x1,y1 of the canvas.
void dl_draw_picture(HDC dc, HBITMAP bmp, int pw, int ph, double x0, double y0, double x1, double y1) {
  int ax = ui_ox + PX(x0), ay = ui_oy + PX(y0), aw = PX(x1 - x0), ah = PX(y1 - y0);
  int sw = aw, sh = (int)((s64)aw * ph / pw);
  if (sh > ah) { sh = ah; sw = (int)((s64)ah * pw / ph); }
  HDC src = CreateCompatibleDC(dc);
  HGDIOBJ old = SelectObject(src, bmp);
  SetStretchBltMode(dc, HALFTONE);
  SetBrushOrgEx(dc, 0, 0, NULL);
  StretchBlt(dc, ax + (aw - sw) / 2, ay + (ah - sh) / 2, sw, sh, src, 0, 0, pw, ph, SRCCOPY);
  SelectObject(src, old);
  DeleteDC(src);
}

// Pixels of the game (pw x ph, shown with the proportions aspect), whole and centred in the area.
void dl_draw_pixels(HDC dc, const u32 *px, int pw, int ph, double aspect, double x0, double y0, double x1, double y1) {
  int ax = ui_ox + PX(x0), ay = ui_oy + PX(y0), aw = PX(x1 - x0), ah = PX(y1 - y0);
  int sw = aw, sh = (int)(aw / aspect);
  if (sh > ah) { sh = ah; sw = (int)(ah * aspect); }
  BITMAPINFO bi = {{sizeof(BITMAPINFOHEADER), pw, -ph, 1, 32}};
  SetStretchBltMode(dc, HALFTONE);
  SetBrushOrgEx(dc, 0, 0, NULL);
  StretchDIBits(dc, ax + (aw - sw) / 2, ay + (ah - sh) / 2, sw, sh, 0, 0, pw, ph, px, &bi, DIB_RGB_COLORS, SRCCOPY);
}

void dl_draw_menu(HDC wdc) {
  static HDC mem;
  static HBITMAP bmp;
  static int bw, bh;
  RECT rc;
  GetClientRect(dl_hwnd, &rc);
  int cw = rc.right, ch = rc.bottom;
  if (cw <= 0 || ch <= 0) return;
  if (!mem) mem = CreateCompatibleDC(wdc);
  if (cw != bw || ch != bh) {
    BITMAPINFO bi = {0};
    bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth = cw;
    bi.bmiHeader.biHeight = -ch;
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    void *bits = NULL;
    HBITMAP nb = CreateDIBSection(wdc, &bi, DIB_RGB_COLORS, &bits, NULL, 0);
    if (!nb) return;
    SelectObject(mem, nb);
    if (bmp) DeleteObject(bmp);
    bmp = nb;
    dl_menu_bits = bits;
    bw = cw;
    bh = ch;
  }
  double sx = cw / 640.0, sy = ch / 400.0;
  ui_s = sx < sy ? sx : sy;
  ui_ox = (int)((cw - 640 * ui_s) / 2);
  ui_oy = (int)((ch - 400 * ui_s) / 2);
  ui_fonts();
  HDC dc = mem;
  if (dl_mode == M_TRY || dl_mode == M_PAUSE) {
    if (dl_mode == M_TRY) draw_try(dc, cw, ch);
    else draw_pause(dc, cw, ch);
    if (dl_layer) GdiFlush();
    else BitBlt(wdc, 0, 0, cw, ch, mem, 0, 0, SRCCOPY);
    dl_prev_w = 0;
    return;
  }
  // the player's own backdrop if there is one (opaque, in the layer too); otherwise the sky, or, as a
  // layer for the graphics card, black then white, without the sky and the game's picture, which the
  // graphics card draws itself
  if (backdrop_draw(dc, cw, ch)) {
  } else if (dl_layer) {
    RECT all = {0, 0, cw, ch};
    FillRect(dc, &all, GetStockObject(dl_layer == 2 ? WHITE_BRUSH : BLACK_BRUSH));
  } else sky_draw(dc, cw, ch);
  ui_title(dc, 16, 40);
  draw_side(dc);

  // the page: the one in hand, or, with the navigation bar in hand, the page of its entry ("peek")
  int page = dl_mode == M_PAGE ? dl_cur : dl_secs[dl_side].kind == SEC_PAGE ? dl_secs[dl_side].page : -1;
  bool home = page < 0;
  const wchar_t *help;
  const DlList *list = page_list(page);
  int sel = dl_mode == M_PAGE ? dl_sel : -1;
  if (!home) {
    page_rows(page); // a list is read, and scrolled to its selected row
    ui_panel(dc, 164, 60, 428, PANEL_BOTTOM, COL_PANEL, dl_tr(dl_pages[page].tab));
    draw_rows(dc, page, sel);
    int n = list ? list->count() : dl_pages[page].n;
    if (sel < 0) help = dl_tr(dl_secs[dl_side].help);
    else if (sel >= n) help = list && !n && list->empty ? dl_tr(list->empty) : TX(T_H_BACK);
    else if (list) help = list->help(sel);
    else {
      const DlRow *r = dl_pages[page].rows[sel];
      help = dl_tr(r->help);
      if (r->more) { // the state of the moment, under the help
        static wchar_t both[1024];
        _snwprintf(both, 1023, L"%ls%ls", help, r->more());
        both[1023] = 0;
        help = both;
      }
    }
    if (dl_wait) help = dl_wait_pad() ? TX(T_PH_WAIT) : TX(T_KH_WAIT);
  } else {
    const DlSection *s = &dl_secs[dl_side];
    help = s->kind == SEC_PLAY && dl_started ? TX(T_H_RESUME) : s->kind == SEC_PLAY && dl_session_exists() ? dl_session_help() : dl_tr(s->help);
    static wchar_t with_update[1100];
    if (s->kind == SEC_PLAY && dl_update_text()) { // a newer version: told under "Play"
      _snwprintf(with_update, 1099, L"%ls\n\n%ls", help, dl_update_text());
      with_update[1099] = 0;
      help = with_update;
    }
  }

  // the game's picture: large beside the bar's actions, small beside a page; on a list with
  // pictures, the picture of the selected row instead
  double vx0 = home ? 164 : 436, vy0 = 60, vx1 = 630, vy1 = home ? 300 : 212;
  double hy0 = home ? 310 : 220;
  ui_panel(dc, vx0, vy0, vx1, vy1, RGB(0, 0, 0), TX(T_PREVIEW));
  int pw, ph, aw = PX(vx1 - vx0 - 8), ah = PX(vy1 - vy0 - 12);
  dl_fit(aw, ah, false, &pw, &ph);
  dl_prev_x = ui_ox + PX(vx0 + 4) + (aw - pw) / 2;
  dl_prev_y = ui_oy + PX(vy0 + 8) + (ah - ph) / 2;
  dl_prev_w = pw;
  dl_prev_h = ph;
  if (list && list->preview && sel >= 0 && sel < list->count() && list->preview(dc, sel, vx0 + 4, vy0 + 8, vx1 - 4, vy1 - 4)) dl_prev_w = 0;
  else if (!dl_layer) dl_blit(dc, dl_prev_x, dl_prev_y, dl_prev_w, dl_prev_h);
  draw_help(dc, vx0, hy0, 630, PANEL_BOTTOM, help);

  if (dl_input_kind == DL_IN_KEYS) {
    ui_text(dc, f_small, 17, 382, TX(T_FOOTER), RGB(0, 0, 0), TA_LEFT);
    ui_text(dc, f_small, 16, 381, TX(T_FOOTER), RGB(255, 255, 255), TA_LEFT);
  } else pad_footer(dc);
  ui_text(dc, f_small, 625, 382, L"DarkLauncher " DL_VERSION_W, RGB(0, 0, 0), TA_RIGHT); // the version, bottom right
  ui_text(dc, f_small, 624, 381, L"DarkLauncher " DL_VERSION_W, ui_tint(RGB(222, 230, 252)), TA_RIGHT);
  if (dl_layer) GdiFlush();
  else BitBlt(wdc, 0, 0, cw, ch, mem, 0, 0, SRCCOPY);
}

// A new picture of the game while the setup screen is shown: only the picture changes.
void dl_draw_preview(HDC wdc) {
  if (dl_mode == M_TRY) {
    RECT rc;
    GetClientRect(dl_hwnd, &rc);
    int w, h;
    dl_fit(rc.right, rc.bottom, dl_cfg.integer_scale, &w, &h);
    dl_blit(wdc, (rc.right - w) / 2, (rc.bottom - h) / 2, w, h);
    return;
  }
  if (dl_prev_w > 0) dl_blit(wdc, dl_prev_x, dl_prev_y, dl_prev_w, dl_prev_h);
}

// ---- mouse: hovering selects, a click acts; on a value, the left half decreases and the right half
// increases; on a bar, the click position sets the level
void dl_mouse(int mx, int my, bool click) {
  if (dl_wait) return;
  if (dl_mode == M_TRY) { if (click) dl_key(VK_RETURN); return; }
  double lx = (mx - ui_ox) / ui_s, ly = (my - ui_oy) / ui_s;
  if (dl_mode == M_PAUSE) {
    double y0 = pa_y0();
    int row = (int)floor((ly - y0 + 1) / ROW_H);
    if (lx < PA_X0 || lx > PA_X1 || ly < y0 - 1 || row < 0 || row >= pa_n) return;
    if (row != dl_sel) { dl_sel = row; dl_dirty = true; }
    if (!click) return;
    dl_dirty = true;
    dl_beep(1);
    int it = pa_items[row];
    if ((it == PA_SAVE || it == PA_LOAD) && lx >= PA_VAL0) pause_act(lx < (PA_VAL0 + PA_X1 - 8) / 2 ? -1 : 1, false);
    else pause_act(0, true);
    return;
  }
  if (lx >= SIDE_X0 && lx <= SIDE_X1) {
    for (int k = 0; k < dl_sec_n; k++) {
      double y = side_y(k);
      if (ly < y - 1 || ly >= y + ROW_H - 1) continue;
      if (click) {
        dl_mode = M_SIDE;
        dl_side = k;
        dl_dirty = true;
        dl_beep(1);
        side_act();
      } else if (dl_mode == M_SIDE && dl_side != k) { dl_side = k; list_read = -1; dl_dirty = true; }
      return;
    }
    return;
  }
  int page = dl_mode == M_PAGE ? dl_cur : dl_secs[dl_side].kind == SEC_PAGE ? dl_secs[dl_side].page : -1;
  if (page < 0) return;
  double h = row_h(page);
  int row = (int)floor((ly - ROW_Y0) / h);
  const DlList *l = page_list(page);
  if (l) { // the rows shown are a window on the list, "Back" under them
    int n = l->count(), shown = n - dl_pages[page].top < LIST_VIS ? n - dl_pages[page].top : LIST_VIS;
    if (row < 0 || row > shown) return;
    row = row == shown ? n : dl_pages[page].top + row;
  }
  if (lx < ROW_X0 || lx > ROW_X1 || ly < ROW_Y0 || row < 0 || row >= page_rows(page)) return;
  if (dl_mode == M_SIDE) { // a row of the page shown beside the bar: go into the page first
    if (!click) return;
    side_act();
  }
  if (row != dl_sel) { dl_sel = row; dl_dirty = true; if (l && l->moved) l->moved(); }
  if (!click) return;
  dl_dirty = true;
  dl_beep(1);
  const DlRow *r = !l && row < dl_pages[page].n ? dl_pages[page].rows[row] : NULL;
  if (r && r->kind == DL_BAR && r->value && lx >= VAL_X0) {
    int v = lx < VAL_X0 + 4 ? 0 : (int)((lx - VAL_X0 - 4) / ((VAL_W - 8) / r->max)) + 1;
    *r->value = v > r->max ? r->max : v;
    if (r->run) r->run();
    return;
  }
  if (r && r->kind == DL_CHOICE && lx >= VAL_X0) row_adjust(lx < (VAL_X0 + VAL_X1) / 2 ? -1 : 1, false);
  else row_adjust(0, true);
}

void dl_menu_reread(void) { list_read = -1; }

void dl_open_page(int page) {
  if (page < 0 || page >= dl_page_n) return;
  if (dl_mode != M_PAGE) stack_n = 0;
  open_page(page);
  dl_dirty = true;
}

// ---- --ui-audit DIR: every entry, page and row in every language, and the pause menu; reports the
// texts that do not fit and saves one picture of each page
int dl_audit(void) {
  SetWindowPos(dl_hwnd, NULL, 0, 0, 1280 + 16, 800 + 39, SWP_NOMOVE | SWP_NOZORDER);
  HDC wdc = GetDC(dl_hwnd);
  int keep = dl_cfg.ui_lang;
  char path[MAX_PATH];
  RECT rc;
  // the bar: its upper entries must end above its lower ones
  if (ROW_Y0 + dl_sec_top * ROW_H > PANEL_BOTTOM - 6 - (dl_sec_n - dl_sec_top) * ROW_H) ui_audit_note("navigation bar too full", L"");
  for (int l = 0; l < DL_LANGS; l++) {
    if (!dl_lang_ok(l)) continue;
    dl_cfg.ui_lang = l;
    dl_mode = M_SIDE;
    for (int k = 0; k < dl_sec_n; k++) { // the bar, each entry selected (with the page it shows)
      dl_side = k;
      list_read = -1;
      dl_draw_menu(wdc);
      if (k == 0 && dl_menu_bits) {
        GetClientRect(dl_hwnd, &rc);
        snprintf(path, sizeof path, "%s" DL_SEP "%d_home.png", dl_audit_dir, l);
        dl_save_png(path, dl_menu_bits, rc.right, rc.bottom);
      }
    }
    static const char *const kinds[] = {"", "xbox", "ps", "pad", "ps_direct"};
    for (int kind = DL_IN_XBOX; kind <= DL_IN_PS_HID; kind++) { // the footer with each kind of controller
      dl_input_kind = kind;
      dl_side = 0;
      dl_draw_menu(wdc);
      if (dl_menu_bits) {
        GetClientRect(dl_hwnd, &rc);
        snprintf(path, sizeof path, "%s" DL_SEP "%d_home_%s.png", dl_audit_dir, l, kinds[kind]);
        dl_save_png(path, dl_menu_bits, rc.right, rc.bottom);
      }
    }
    dl_input_kind = DL_IN_KEYS;
    for (int p = 0; p < dl_page_n; p++) { // every page, each row selected
      dl_mode = M_PAGE;
      dl_cur = p;
      list_read = -1;
      for (int r = 0; r < page_rows(p); r++) {
        dl_sel = r;
        dl_draw_menu(wdc);
        if (r == 0 && dl_menu_bits) {
          GetClientRect(dl_hwnd, &rc);
          snprintf(path, sizeof path, "%s" DL_SEP "%d_page%02d.png", dl_audit_dir, l, p);
          dl_save_png(path, dl_menu_bits, rc.right, rc.bottom);
        }
      }
    }
    if (dl_game->save_state && dl_game->load_state) { // the keyboard on screen (a slot named with a controller)
      dl_mode = M_PAGE;
      dl_cur = PG_SAVES;
      list_read = -1;
      dl_sel = 0;
      page_rows(PG_SAVES);
      for (int pass = 0; pass < 2; pass++) { // with an Xbox pad, then a PlayStation one (the help names their buttons)
        dl_input_kind = pass ? DL_IN_PS_HID : DL_IN_XBOX;
        dl_saves_osk_sample(true);
        dl_draw_menu(wdc);
        if (dl_menu_bits) {
          GetClientRect(dl_hwnd, &rc);
          snprintf(path, sizeof path, "%s" DL_SEP "%d_keyboard%s.png", dl_audit_dir, l, pass ? "_ps" : "");
          dl_save_png(path, dl_menu_bits, rc.right, rc.bottom);
        }
      }
      dl_input_kind = DL_IN_KEYS;
      dl_saves_osk_sample(false);
    }
    pause_rows(); // the pause menu, each row selected
    dl_mode = M_PAUSE;
    for (int r = 0; r < pa_n; r++) {
      dl_sel = r;
      dl_draw_menu(wdc);
      if (r == 1 && dl_menu_bits) {
        GetClientRect(dl_hwnd, &rc);
        snprintf(path, sizeof path, "%s" DL_SEP "%d_pause.png", dl_audit_dir, l);
        dl_save_png(path, dl_menu_bits, rc.right, rc.bottom);
      }
    }
  }
  dl_cfg.ui_lang = keep;
  ReleaseDC(dl_hwnd, wdc);
  printf("ui audit: %d issues\n", dl_audit_issues);
  return dl_audit_issues ? 2 : 0;
}
