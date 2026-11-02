// The setup screen: the navigation bar on the left (sections, then what acts at once), the page of
// the selected section in the middle, the live picture of the game and the help on the right.
// Pages are lists of DlRow declared by the kit and by the game, or lists made by the program (save
// slots, screenshots); this file draws them and handles the keys and the mouse (the controller arrives
// as keys, see dl_input.c). The pause menu too: a panel over the paused game.
#include "dl_int.h"
#include "dl_games.h" // the other games (DL_GAMES_URL, dl_games_list)
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
static const wchar_t *const *const filter_names[] = {dl_texts[T_SHARP], dl_texts[T_SMOOTH], dl_texts[T_EDGES], dl_texts[T_XBR]};
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
// an old console's 3D has no smooth edges: "Large and sharp" is smoothed there (DlGame.look)
static int preset_smooth(const Preset *p) { return dl_game->look == DL_LOOK_RETRO3D && p->smooth > 1 ? 1 : p->smooth; }
static int preset_match(void) { // -1: none
  bool no_crt = dl_hides() & DL_HIDE_CRT, no_scan = dl_hides() & DL_HIDE_SCANLINES; // a hidden row is left aside
  for (int k = 0; k < preset_n; k++) {
    const Preset *p = &presets[preset_ids[k]];
    if (dl_cfg.integer_scale == p->integer_scale && dl_cfg.smooth == preset_smooth(p) && (no_scan || dl_cfg.scanlines == p->scanlines) &&
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
  dl_cfg.smooth = preset_smooth(p);
  if (!(dl_hides() & DL_HIDE_SCANLINES)) dl_cfg.scanlines = p->scanlines;
  if (!(dl_hides() & DL_HIDE_CRT)) dl_cfg.crt = p->crt;
  if (id == 3) dl_cfg.ahead = 0;
  dl_dirty = true;
}
static DlRow image_rows[] = { // not const: the presets' count is set at start (DlGame.hide)
    {DL_CHOICE, dl_texts[T_PRESET], dl_texts[T_H_PRESET], &preset_pick, 3, NULL, preset_show, preset_changed},
    {DL_SWITCH, dl_texts[T_FULL], dl_texts[T_H_FULL], &dl_cfg.fullscreen, 1, NULL, NULL, full_changed},
    {DL_SWITCH, dl_texts[T_INT], dl_texts[T_H_INT], &dl_cfg.integer_scale, 1},
    {DL_CHOICE, dl_texts[T_GPU], dl_texts[T_H_GPU], &dl_cfg.gpu, 1, gpu_names, NULL, gpu_changed},
    {DL_CHOICE, dl_texts[T_FILTER], dl_texts[T_H_FILTER], &dl_cfg.smooth, 3, filter_names},
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
static const wchar_t *const *const p2_names[] = {dl_texts[T_P2_SECOND], dl_texts[T_P2_FIRST]};
static const DlRow p2_row = {DL_CHOICE, dl_texts[T_P2], dl_texts[T_H_P2], &dl_cfg.p2_pad, 1, p2_names}; // two players (DlGame.players)
static const wchar_t *const *const ahead_names[] = {dl_texts[T_NO], dl_texts[T_AHEAD_1], dl_texts[T_AHEAD_2]};
static const DlRow ahead_row = {DL_CHOICE, dl_texts[T_AHEAD], dl_texts[T_H_AHEAD], &dl_cfg.ahead, 2, ahead_names};
static DlRow ctrl_rows[] = { // not const, as the rows below: their help leaves out fast forward when the game hides it
    {.kind = DL_PAGE, .label = dl_texts[T_KEYS], .help = dl_texts[T_H_KEYS], .page = PG_KEYS},
    {.kind = DL_PAGE, .label = dl_texts[T_PAD], .help = dl_texts[T_H_PAD], .page = PG_PAD, .more = ps_more},
    {.kind = DL_PAGE, .label = dl_texts[T_HOT], .help = dl_texts[T_H_HOT], .page = PG_HOT},
    {.kind = DL_CHOICE, .label = dl_texts[T_TURBO], .help = dl_texts[T_H_TURBO], .value = &dl_cfg.turbo, .max = 4, .show = show_turbo, .run = turbo_changed},
    {.kind = DL_SWITCH, .label = dl_texts[T_PAUSEMENU], .help = dl_texts[T_H_PAUSEMENU], .value = &dl_cfg.pause_menu, .max = 1},
    {.kind = DL_SWITCH, .label = dl_texts[T_BGPAUSE], .help = dl_texts[T_H_BGPAUSE], .value = &dl_cfg.bg_pause, .max = 1},
};
#define KEY_ROW(k) {.kind = DL_KEY, .label = dl_texts[T_BTN_UP + (k)], .help = dl_texts[T_KH_BTN], .value = &dl_key_vk[k]}
static DlRow keys_rows[] = {
    KEY_ROW(0), KEY_ROW(1), KEY_ROW(2), KEY_ROW(3), KEY_ROW(4), KEY_ROW(5), KEY_ROW(6), KEY_ROW(7), KEY_ROW(8), KEY_ROW(9), KEY_ROW(10), KEY_ROW(11),
    KEY_ROW(12), KEY_ROW(13), KEY_ROW(14), KEY_ROW(15), // L2 R2 L3 R3: left out unless the game reads them (DlGame.extra_buttons)
    {.kind = DL_KEY, .label = dl_texts[T_BTN_TURBO], .help = dl_texts[T_KH_TURBO], .value = &dl_key_vk[DL_BTNS]},
    {.kind = DL_ACTION, .label = dl_texts[T_GFXDEF], .help = dl_texts[T_KH_DEFAULTS], .run = dl_keys_default},
};
#define PAD_ROW(k) {.kind = DL_PADKEY, .label = dl_texts[T_BTN_UP + (k)], .help = dl_texts[T_PH_BTN], .value = &dl_pad_code[k]}
static DlRow pad_rows[] = { // not const: the help of "Default values" follows the game's choices
    PAD_ROW(0), PAD_ROW(1), PAD_ROW(2), PAD_ROW(3), PAD_ROW(4), PAD_ROW(5), PAD_ROW(6), PAD_ROW(7), PAD_ROW(8), PAD_ROW(9), PAD_ROW(10), PAD_ROW(11),
    PAD_ROW(12), PAD_ROW(13), PAD_ROW(14), PAD_ROW(15), // as on the keys' page
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
static DlRow welcome_ctrl_rows[] = {
    {.kind = DL_PAGE, .label = dl_texts[T_KEYS], .help = dl_texts[T_H_KEYS], .page = PG_KEYS},
    {.kind = DL_PAGE, .label = dl_texts[T_PAD], .help = dl_texts[T_H_PAD], .page = PG_PAD},
    {DL_ACTION, dl_texts[T_W_NEXT], dl_texts[T_H_W_CTRL], NULL, 0, NULL, NULL, welcome_screen},
};
static DlRow welcome_screen_rows[] = { // not const, as image_rows
    {DL_SWITCH, dl_texts[T_FULL], dl_texts[T_H_FULL], &dl_cfg.fullscreen, 1, NULL, NULL, full_changed},
    {DL_CHOICE, dl_texts[T_PRESET], dl_texts[T_H_PRESET], &preset_pick, 3, NULL, preset_show, preset_changed},
    {DL_ACTION, dl_texts[T_W_PLAY], dl_texts[T_H_W_PLAY], NULL, 0, NULL, NULL, dl_play},
};
// the game outside DarkLauncher (DlGame.play): the language, then the game (its own pages say how it plays)
static DlRow welcome_out_rows[] = { // not const: its last row may lead to the game's page
    {DL_CHOICE, dl_texts[T_UILANG], dl_texts[T_H_UILANG], &dl_cfg.ui_lang, DL_LANGS - 1, NULL, show_lang, lang_changed},
    {DL_ACTION, dl_texts[T_W_PLAY], dl_texts[T_H_W_PLAY_OUT], NULL, 0, NULL, NULL, dl_play},
};
// the game's own page, last (DlGame.welcome_*, 2.62): the page before it goes on to it
static int welcome_game = -1;
static void welcome_game_open(void) { dl_open_page(welcome_game); }
static const DlRow welcome_next_row = {DL_ACTION, dl_texts[T_W_NEXT], dl_texts[T_H_W_GAME], NULL, 0, NULL, NULL, welcome_game_open};
static const DlRow welcome_play_row = {DL_ACTION, dl_texts[T_W_PLAY], dl_texts[T_H_W_PLAY], NULL, 0, NULL, NULL, dl_play};
static const DlRow welcome_play_out_row = {DL_ACTION, dl_texts[T_W_PLAY], dl_texts[T_H_W_PLAY_OUT], NULL, 0, NULL, NULL, dl_play};

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
static const DlRow net_updates_row = {DL_SWITCH, dl_texts[T_UPDATES], dl_texts[T_H_UPDATES], &dl_cfg.updates, 1, NULL, NULL, dl_online_start, 0, updates_more},
                   net_updates_games_row = {DL_SWITCH, dl_texts[T_UPDATES], dl_texts[T_H_UPDATES_GAMES], &dl_cfg.updates, 1, NULL, NULL, dl_online_start, 0, updates_more}; // (the list of the other games read too, 2.93)
static const DlRow net_open_row = {DL_ACTION, dl_texts[T_UPD_OPEN], dl_texts[T_H_UPD_OPEN], NULL, 0, NULL, NULL, dl_update_open};
static const DlRow net_install_row = {DL_ACTION, dl_texts[T_UPD_INSTALL], dl_texts[T_H_UPD_INSTALL], NULL, 0, NULL, dl_update_install_show, dl_update_install};
static DlRow net_row = {.kind = DL_PAGE, .label = dl_texts[T_NET], .help = dl_texts[T_H_NET], .page = PG_NET}; // not const: its help says what is there

// ---- helps that name only what is offered (the game's hide, the system, the project), made at start:
// the words of each language joined as it joins them ("A, B et C")
typedef const wchar_t *Words[DL_LANGS];
static const wchar_t *const words_sep[DL_LANGS] = {L"、", L", ", L", ", L", ", L", ", L", ", L", ", L"、", L", "};
static const wchar_t *const words_last[DL_LANGS] = {L"、", L" et ", L" and ", L" y ", L" e ", L" und ", L" e ", L"和", L" 및 "};
static void words_join(wchar_t *out, size_t cap, const Words *const *items, int n, int lang, bool last_word) {
  out[0] = 0;
  for (int k = 0; k < n; k++) {
    if (k) wcsncat(out, k == n - 1 && last_word ? words_last[lang] : words_sep[lang], cap - wcslen(out) - 1);
    wcsncat(out, (*items[k])[lang], cap - wcslen(out) - 1);
  }
}
// "Picture" in Settings: the rows of the Picture page (without the CRT filter, the scanlines or HDR when
// they are not there)
static const Words image_words[] = {
    {L"フルスクリーン", L"Plein écran", L"Fullscreen", L"Pantalla completa", L"Tela cheia", L"Vollbild", L"Schermo intero", L"全屏", L"전체 화면"},
    {L"整数倍表示", L"échelle entière", L"integer scaling", L"escala entera", L"escala inteira", L"Ganzzahl-Skalierung", L"scala intera",
     L"整数缩放", L"정수 배율"},
    {L"描画方式", L"affichage", L"display", L"visualización", L"exibição", L"Darstellung", L"visualizzazione", L"显示方式", L"표시 방식"},
    {L"フィルター", L"filtre", L"filter", L"filtro", L"filtro", L"Filter", L"filtro", L"滤镜", L"필터"},
    {L"スキャンライン", L"scanlines", L"scanlines", L"scanlines", L"scanlines", L"Scanlines", L"scanline", L"扫描线", L"스캔라인"},
    {L"ブラウン管フィルター", L"filtre CRT", L"CRT filter", L"filtro CRT", L"filtro CRT", L"Röhren-Filter", L"filtro CRT", L"CRT 滤镜", L"CRT 필터"},
    {L"HDR", L"HDR", L"HDR", L"HDR", L"HDR", L"HDR", L"HDR", L"HDR", L"HDR"},
    {L"垂直同期", L"synchro verticale", L"vertical sync", L"sincronización vertical", L"sincronização vertical", L"vertikale Synchronisation", L"sincronia verticale",
     L"垂直同步", L"수직 동기화"},
    {L"FPS表示", L"FPS", L"the FPS counter", L"contador de FPS", L"contador de FPS", L"FPS-Anzeige", L"contatore FPS", L"FPS 显示", L"FPS 표시"},
};
enum { IW_INT = 1, IW_FILTER = 3, IW_SCAN = 4, IW_CRT = 5, IW_HDR = 6 };
static wchar_t image_help_text[DL_LANGS][320];
static const wchar_t *image_help[DL_LANGS];
// The entry Settings: its pages (and Internet when there), then its actions (and Restart when there)
static const Words settings_words[] = {
    {L"言語", L"Langues", L"Languages", L"Idiomas", L"Idiomas", L"Sprachen", L"Lingue", L"语言", L"언어"},
    {L"画面", L"image", L"picture", L"imagen", L"imagem", L"Bild", L"immagine", L"画面", L"화면"},
    {L"サウンド", L"son", L"sound", L"sonido", L"som", L"Ton", L"suono", L"声音", L"사운드"},
    {L"操作", L"commandes", L"controls", L"controles", L"controles", L"Steuerung", L"comandi", L"操作", L"조작"},
    {L"インターネット", L"Internet", L"Internet", L"Internet", L"Internet", L"Internet", L"Internet", L"网络", L"인터넷"},
    {L"アクセシビリティ", L"accessibilité", L"accessibility", L"accesibilidad", L"acessibilidade", L"Barrierefreiheit", L"accessibilità",
     L"辅助功能", L"접근성"}, // outside DarkLauncher only
}, settings_more_words[] = {
    {L"ゲームのフォルダー", L"le dossier du jeu", L"the game folder", L"la carpeta del juego", L"a pasta do jogo", L"der Spielordner", L"la cartella del gioco",
     L"游戏文件夹", L"게임 폴더"},
    {L"初期設定", L"les valeurs par défaut", L"the default values", L"los valores por defecto", L"os valores padrão", L"die Standardwerte", L"i valori predefiniti",
     L"默认值", L"기본값"},
    {L"最初から始める", L"le redémarrage", L"restarting", L"el reinicio", L"o reinício", L"der Neustart", L"il riavvio", L"重新开始", L"다시 시작"},
};
static const wchar_t *const settings_mid[DL_LANGS] = {L"。", L" ; et aussi ", L"; and ", L"; y también ", L"; e também ", L"; dazu ", L"; e anche ",
                                                     L"；以及", L"; 그리고 "};
static const wchar_t *const settings_end[DL_LANGS] = {L"、もここにあります。", L".", L".", L".", L".", L".", L".", L"。", L"."};
static wchar_t settings_help_text[DL_LANGS][320];
static const wchar_t *settings_help[DL_LANGS];
static void helps_make(bool net) {
#ifdef _WIN32
  bool hdr = true;
#else
  bool hdr = false; // no HDR row on Linux
#endif
  const Words *pick[16];
  for (int lang = 0; lang < DL_LANGS; lang++) {
    int n = 0;
    for (int k = 0; k < (int)(sizeof image_words / sizeof image_words[0]); k++) {
      if ((k == IW_SCAN && (dl_hides() & DL_HIDE_SCANLINES)) || (k == IW_CRT && (dl_hides() & DL_HIDE_CRT)) || (k == IW_HDR && !hdr)) continue;
      if ((k == IW_INT || k == IW_FILTER) && dl_game->look == DL_LOOK_MODERN) continue; // a high-definition picture: neither
      pick[n++] = &image_words[k];
    }
    words_join(image_help_text[lang], 316, pick, n, lang, true);
    wcsncat(image_help_text[lang], lang == DL_LANG_JP || lang == DL_LANG_ZH ? L"。" : L".", 2);
    image_help[lang] = image_help_text[lang];
    wchar_t more[200];
    n = 0;
    for (int k = 0; k < 6; k++) // outside DarkLauncher: Languages, Internet and Accessibility only
      if (DL_NOFRAME ? k == 0 || (k == 4 && net) || k == 5 : k != 5 && (k != 4 || net)) pick[n++] = &settings_words[k];
    words_join(settings_help_text[lang], 120, pick, n, lang, false);
    n = 0;
    for (int k = 0; k < 3; k++)
      if (k != 2 || (dl_game->reset && !DL_OUTSIDE)) pick[n++] = &settings_more_words[k];
    words_join(more, 200, pick, n, lang, true);
    _snwprintf(settings_help_text[lang] + wcslen(settings_help_text[lang]), 319 - wcslen(settings_help_text[lang]), L"%ls%ls%ls", settings_mid[lang],
               more, settings_end[lang]);
    settings_help_text[lang][319] = 0;
    settings_help[lang] = settings_help_text[lang];
  }
}

// ---- Settings: one entry of the bar for the kit's pages and its rarer actions
static void settings_folder(void) { ShellExecuteA(NULL, "open", dl_base_dir, NULL, NULL, SW_SHOWNORMAL); }
static void settings_defaults(void) { dl_defaults(false); }
static DlRow settings_rows[] = {
    {.kind = DL_PAGE, .label = dl_texts[T_LANGS], .help = dl_texts[T_H_LANGS], .page = DL_PAGE_LANGS},
    {.kind = DL_PAGE, .label = dl_texts[T_IMAGE], .help = dl_texts[T_H_IMAGE], .page = DL_PAGE_IMAGE},
    {.kind = DL_PAGE, .label = dl_texts[T_SOUND], .help = dl_texts[T_H_SOUND], .page = DL_PAGE_SOUND},
    {.kind = DL_PAGE, .label = dl_texts[T_CTRL], .help = dl_texts[T_H_CTRL], .page = PG_CTRL},
    {DL_ACTION, dl_texts[T_FOLDER], dl_texts[T_H_FOLDER], NULL, 0, NULL, NULL, settings_folder},
    {DL_ACTION, dl_texts[T_DEFAULTS], dl_texts[T_H_DEFAULTS], NULL, 0, NULL, NULL, settings_defaults},
};
static const DlRow settings_reset_row = {DL_ACTION, dl_texts[T_RESET], dl_texts[T_H_RESET], NULL, 0, NULL, NULL, dl_restart};
// outside DarkLauncher (DlGame.play), in the place of Picture: the menus' own accessibility
static const DlRow settings_a11y_row = {.kind = DL_PAGE, .label = dl_texts[T_A11Y], .help = dl_texts[T_H_A11Y], .page = PG_A11Y};

// Takes out of a page the row that sets this value (a row the game does not offer: DlGame.hide).
static void page_drop(int page, const int *value) {
  if (page < 0 || page >= dl_page_n) return;
  DlPage *p = &dl_pages[page];
  for (int k = p->n - 1; k >= 0; k--)
    if (p->rows[k]->value == value) {
      memmove(&p->rows[k], &p->rows[k + 1], sizeof p->rows[0] * (size_t)(p->n - k - 1));
      p->n--;
    }
}

// A newer version found (dl_app.c): its page and "Update" join the Internet page, once (even while it is shown)
void dl_net_newer(void) {
  static bool done;
  if (done || !DL_UPDATE_ON) return;
  done = true;
  dl_page_insert(PG_NET, dl_pages[PG_NET].n, &net_open_row);
  if (UPDATE_GITHUB[0] != 0 && UPDATE_ASSET[0] != 0) dl_page_insert(PG_NET, dl_pages[PG_NET].n, &net_install_row); // the release's file, installed
  // "Update" in the bar too, under Play (2.99, asked for Cristalis: the note at the start was missed, and
  // the row on the Internet page was not found): it installs the release's file, or opens its page
  int at = 0;
  while (at < dl_sec_top && dl_secs[at].kind != SEC_PLAY) at++;
  at = at < dl_sec_top ? at + 1 : 0;
  if (dl_sec_n < DL_MAX_SECTIONS && dl_sec_top > 0) {
    memmove(&dl_secs[at + 1], &dl_secs[at], sizeof dl_secs[0] * (size_t)(dl_sec_n - at));
    dl_secs[at] = (DlSection){SEC_UPDATE, -1, 0xE896, dl_texts[T_UPD_INSTALL], dl_texts[dl_update_installable() ? T_H_UPD_INSTALL : T_H_UPD_OPEN]};
    dl_sec_n++;
    dl_sec_top++;
    if (dl_side >= at) dl_side++; // (the entry chosen stays the same)
  }
  dl_dirty = true;
}

void dl_menu_init(void) {
  bool saves = dl_game->save_state && dl_game->load_state && !DL_OUTSIDE; // outside DarkLauncher: no state of the game here
  if (dl_game->pad_layout || dl_game->pad_menu) pad_rows[sizeof pad_rows / sizeof pad_rows[0] - 1].help = dl_texts[T_PH_DEFAULTS_GAME];
  if (dl_game->pad_menu == DL_MENU_HOLD) padfn_rows[4].help = dl_texts[T_PH_MENU_HOLD]; // FN_MENU's row
  dl_page(dl_texts[T_TAB_LANGS], langs_rows, 1);
  // DarkLauncher's fast forward hidden (DlGame.hide): the helps that name it say the rest only
  if (dl_hides() & DL_HIDE_TURBO) {
    ctrl_rows[0].help = welcome_ctrl_rows[0].help = dl_texts[T_H_KEYS_NT];
    keys_rows[sizeof keys_rows / sizeof keys_rows[0] - 1].help = dl_texts[T_KH_DEFAULTS_NT];
    pad_rows[sizeof pad_rows / sizeof pad_rows[0] - 2].help = dl_texts[T_PH_FN_NT];
    settings_rows[3].help = dl_texts[T_H_CTRL_NT];
  }
  // the rows the game does not offer (DlGame.hide, DlGame.look), and the "CRT" preset with the CRT filter
  if (dl_hides() & DL_HIDE_CRT) {
    preset_n = 0;
    for (int k = 0; k < 4; k++)
      if (k != 2) preset_ids[preset_n++] = k;
  }
  image_rows[0].max = welcome_screen_rows[1].max = preset_n - 1;
  if (dl_hides() & DL_HIDE_CRT) image_rows[0].help = welcome_screen_rows[1].help = dl_texts[T_H_PRESET_NOCRT];
  if (dl_game->look == DL_LOOK_RETRO3D) { // an old console's 3D: Sharp or Smooth, the presets said so
    image_rows[0].help = welcome_screen_rows[1].help = dl_texts[dl_hides() & DL_HIDE_CRT ? T_H_PRESET_3D_NOCRT : T_H_PRESET_3D];
    for (size_t k = 0; k < sizeof image_rows / sizeof image_rows[0]; k++)
      if (image_rows[k].value == &dl_cfg.smooth) image_rows[k].max = 1, image_rows[k].help = dl_texts[T_H_FILTER_3D];
  }
  dl_page(dl_texts[T_TAB_IMAGE], image_rows, sizeof image_rows / sizeof image_rows[0]);
  if (dl_hides() & DL_HIDE_CRT) page_drop(DL_PAGE_IMAGE, &dl_cfg.crt);
  if (dl_hides() & DL_HIDE_SCANLINES) page_drop(DL_PAGE_IMAGE, &dl_cfg.scanlines);
  if (dl_game->look == DL_LOOK_MODERN) { // a high-definition picture: smoothed at any size, nothing to preset
    page_drop(DL_PAGE_IMAGE, &preset_pick);
    page_drop(DL_PAGE_IMAGE, &dl_cfg.integer_scale);
    page_drop(DL_PAGE_IMAGE, &dl_cfg.smooth);
  }
  dl_page(dl_texts[T_TAB_SOUND], sound_rows, sizeof sound_rows / sizeof sound_rows[0]);
  dl_page(dl_texts[T_TAB_CTRL], ctrl_rows, sizeof ctrl_rows / sizeof ctrl_rows[0]);
  int at = 3; // after the three pages of assignments: player 2's controller, the vibration, run-ahead
  if (dl_game->players >= 2) dl_page_insert(PG_CTRL, at++, &p2_row);
  if (dl_game->rumble) dl_page_insert(PG_CTRL, at++, &rumble_row);
  if (dl_ahead_ok()) dl_page_insert(PG_CTRL, at, &ahead_row);
  dl_page(dl_texts[T_TAB_KEYS], keys_rows, sizeof keys_rows / sizeof keys_rows[0]);
  dl_page(dl_texts[T_TAB_PAD], pad_rows, sizeof pad_rows / sizeof pad_rows[0]);
  if (dl_game->analog) // the sticks go to the game: the help no longer says the left one is the directional pad
    for (int k = 0; k < DL_BTNS; k++) pad_rows[k].help = dl_texts[T_PH_BTN_ANALOG];
  for (int k = DL_BTNS_CLASSIC; k < DL_BTNS; k++) // L2 R2 L3 R3, only for a game that reads them
    if (!(dl_game->extra_buttons & (1u << k))) {
      page_drop(PG_KEYS, &dl_key_vk[k]);
      page_drop(PG_PAD, &dl_pad_code[k]);
    }
  DlPage *pf = &dl_pages[dl_page(dl_texts[T_TAB_PADFN], padfn_rows, 1)];
  for (int k = 1; k < (int)(sizeof padfn_rows / sizeof padfn_rows[0]); k++)
    if (saves || (k != 1 && k != 2)) pf->rows[pf->n++] = &padfn_rows[k];
  if (dl_hides() & DL_HIDE_TURBO) { // its speed, its key and its controller button
    page_drop(PG_CTRL, &dl_cfg.turbo);
    page_drop(PG_KEYS, &dl_key_vk[DL_BTNS]);
    page_drop(PG_PADFN, &dl_pad_code[FN_TURBO]);
  }
  DlPage *ph = &dl_pages[dl_page(dl_texts[T_TAB_HOT], NULL, 0)];
  for (int k = 0; k < (int)(sizeof hot_rows / sizeof hot_rows[0]); k++)
    if ((saves || k > HK_NEXT) && (k != HK_REWIND || dl_rewind_ok())) ph->rows[ph->n++] = &hot_rows[k];
  dl_list_page(dl_texts[T_TAB_SAVES], &dl_saves_list);
  dl_list_page(dl_texts[T_TAB_SHOTS], &dl_shots_list);
  dl_page(dl_texts[T_TAB_A11Y], a11y_rows, sizeof a11y_rows / sizeof a11y_rows[0]);
  dl_list_page(dl_texts[T_TAB_BACKUPS], &dl_backups_list);
  dl_list_page(dl_texts[T_TAB_NEWS], &news_list);
  if (dl_over) welcome_out_rows[1].help = dl_texts[T_H_W_PLAY]; // over the game: Esc comes back to the menus, as inside
  if (DL_NOFRAME) dl_page(dl_texts[T_TAB_WELCOME], welcome_out_rows, sizeof welcome_out_rows / sizeof welcome_out_rows[0]);
  else dl_page(dl_texts[T_TAB_WELCOME], welcome_rows, sizeof welcome_rows / sizeof welcome_rows[0]);
  dl_page(dl_texts[T_TAB_CTRL], welcome_ctrl_rows, sizeof welcome_ctrl_rows / sizeof welcome_ctrl_rows[0]);
  dl_page(dl_texts[T_TAB_IMAGE], welcome_screen_rows, sizeof welcome_screen_rows / sizeof welcome_screen_rows[0]);
  if (dl_game->look == DL_LOOK_MODERN) page_drop(PG_WELCOME_SCREEN, &preset_pick); // nothing to preset
  DlPage *pn = &dl_pages[dl_page(dl_texts[T_TAB_NET], NULL, 0)];
  if (DISCORD_APP_ID[0]) pn->rows[pn->n++] = &net_discord_row;
  if (DL_UPDATE_ON) {
    pn->rows[pn->n++] = DL_GAMES_URL[0] && !(dl_game->hide & DL_HIDE_GAMES) ? &net_updates_games_row : &net_updates_row; // the new version's page and "Update" come once one is found (dl_net_newer)
  }
  bool net = pn->n > 0; // a row of the Settings page (made below), after Controls
  bool discord = DISCORD_APP_ID[0] != 0;
  net_row.help = dl_texts[discord && DL_UPDATE_ON ? (UPDATE_DEFAULT_ON ? T_H_NET_ON : T_H_NET)
                          : discord ? T_H_NET_DISCORD : UPDATE_DEFAULT_ON ? T_H_NET_UPD_ON : T_H_NET_UPD_OFF];
  helps_make(net);
  settings_rows[1].help = image_help; // "Picture": the rows offered
  DlPage *ps = &dl_pages[dl_page(dl_texts[T_TAB_SETTINGS], settings_rows, sizeof settings_rows / sizeof settings_rows[0])];
  if (DL_NOFRAME) { // Languages, Internet, Accessibility, Folder, Defaults: Picture, Sound and Controls are the game's
    memmove(&ps->rows[1], &ps->rows[4], sizeof ps->rows[0] * (size_t)(ps->n - 4));
    ps->n -= 3;
    dl_page_insert(PG_SETTINGS, 1, &settings_a11y_row);
    if (net) dl_page_insert(PG_SETTINGS, 1, &net_row);
  } else {
    if (net) dl_page_insert(PG_SETTINGS, 4, &net_row); // Internet, after Controls
    if (dl_game->reset) dl_page_insert(PG_SETTINGS, dl_pages[PG_SETTINGS].n, &settings_reset_row); // a game that cannot restart has no such row
  }
  add_section(SEC_PLAY, -1, 0xE768, dl_texts[T_PLAY], dl_texts[DL_OUTSIDE ? T_H_PLAY_OUT : T_H_PLAY]);
  if (saves) add_section(SEC_PAGE, PG_SAVES, 0xE74E, dl_texts[T_SAVES], dl_texts[T_H_SAVES]);
  if (!DL_NOFRAME) add_section(SEC_PAGE, PG_SHOTS, 0xE722, dl_texts[T_SHOTS], dl_texts[T_H_SHOTS]); // the game outside takes its own
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
static DlRow safe_rows[] = { // not const: its help names only what is offered (dl_menu_init)
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

// ---- the author's other games (dl_games.h, 2.71): an installed one is started (this one closes, its
// session kept), another one's page is opened. The entry, before Settings, only when there is one.
#include <wctype.h>
#ifndef _WIN32
#include <unistd.h>
#endif
bool dl_all_games; // --all-games: the games not released yet too
typedef struct {
  char id[64], stem[64], url[200], picture[64]; // picture: its file beside the list on line
  wchar_t name[64], text[DL_LANGS][400];
  bool released;
  HBITMAP pic; // the picture, once read (the list on line)
  int pic_w, pic_h;
#ifdef _WIN32
  wchar_t exe[MAX_PATH], dir[MAX_PATH]; // an installed copy (exe[0] 0: none)
#else
  char exe[1024], dir[1024];
#endif
} OtherGame;
#define OTHER_MAX 12
static OtherGame others[OTHER_MAX];
static int other_n, shown[OTHER_MAX], shown_n;

static char *trim(char *s) {
  while (*s == ' ' || *s == '\t') s++;
  char *e = s + strlen(s);
  while (e > s && (e[-1] == ' ' || e[-1] == '\t' || e[-1] == '\r')) *--e = 0;
  return s;
}

// The list's text (dl_games.h's, or the one on line in the same format) into others[].
static void others_read(const char *s) {
  static const char *const langs[DL_LANGS] = {"jp", "fr", "en", "es", "br", "de", "it", "zh", "ko"};
  other_n = 0;
  OtherGame *g = NULL;
  while (*s) {
    size_t len = strcspn(s, "\n"), n = len < 1023 ? len : 1023;
    char buf[1024];
    memcpy(buf, s, n);
    buf[n] = 0;
    s += len + (s[len] ? 1 : 0);
    char *line = trim(buf), *eq = strchr(line, '=');
    if (!*line || *line == '#') continue;
    if (*line == '[') {
      char *close = strchr(line, ']');
      g = close && other_n < OTHER_MAX ? &others[other_n++] : NULL;
      if (g) {
        memset(g, 0, sizeof *g);
        *close = 0;
        snprintf(g->id, sizeof g->id, "%s", line + 1);
      }
      continue;
    }
    if (!g || !eq) continue;
    *eq = 0;
    char *key = trim(line), *val = trim(eq + 1);
    if (!strcmp(key, "name")) MultiByteToWideChar(CP_UTF8, 0, val, -1, g->name, 64);
    else if (!strcmp(key, "stem")) snprintf(g->stem, sizeof g->stem, "%s", val);
    else if (!strcmp(key, "url")) snprintf(g->url, sizeof g->url, "%s", val);
    else if (!strcmp(key, "released")) g->released = atoi(val) != 0;
    else if (!strcmp(key, "picture")) snprintf(g->picture, sizeof g->picture, "%s", val);
    else if (!strcmp(key, "exe") && getenv("DL_TEST_GAMES")) { // tests only: an installed copy, here
#ifdef _WIN32
      MultiByteToWideChar(CP_UTF8, 0, val, -1, g->exe, MAX_PATH);
#else
      snprintf(g->exe, sizeof g->exe, "%s", val);
#endif
    }
    else if (!strncmp(key, "text.", 5))
      for (int l = 0; l < DL_LANGS; l++)
        if (!strcmp(key + 5, langs[l])) MultiByteToWideChar(CP_UTF8, 0, val, -1, g->text[l], 400);
  }
}

// An installed copy: on Windows its entry in the list of programs (the kit's installer writes it, under
// UNINSTALL_ID: DisplayIcon the program, InstallLocation its folder), read through advapi32.dll loaded
// now; on Linux the folder the archive's install.sh fills.
static void other_find(OtherGame *g) {
  g->exe[0] = g->dir[0] = 0;
#ifdef _WIN32
  typedef LONG(WINAPI * GetValue)(HKEY, LPCWSTR, LPCWSTR, DWORD, LPDWORD, PVOID, LPDWORD);
  static GetValue get;
  static bool tried;
  if (!tried) {
    tried = true;
    HMODULE m = LoadLibraryA("advapi32.dll");
    if (m) get = (GetValue)(void *)GetProcAddress(m, "RegGetValueW");
  }
  if (!get || !g->id[0]) return;
  wchar_t key[200], icon[MAX_PATH], dir[MAX_PATH];
  _snwprintf(key, 199, L"Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\%hs", g->id);
  key[199] = 0;
  HKEY roots[2] = {HKEY_CURRENT_USER, HKEY_LOCAL_MACHINE};
  for (int r = 0; r < 2 && !g->exe[0]; r++) {
    DWORD n = sizeof icon, nd = sizeof dir;
    if (get(roots[r], key, L"DisplayIcon", 0x00000002 /* RRF_RT_REG_SZ */, NULL, icon, &n) != ERROR_SUCCESS) continue;
    if (get(roots[r], key, L"InstallLocation", 0x00000002, NULL, dir, &nd) != ERROR_SUCCESS) dir[0] = 0;
    wchar_t *p = icon;
    if (*p == L'"') {
      p++;
      wchar_t *q = wcschr(p, L'"');
      if (q) *q = 0;
    }
    for (wchar_t *e = p; *e; e++) // "...\game.exe,0": the program alone
      if (e[0] == L'.' && towlower(e[1]) == L'e' && towlower(e[2]) == L'x' && towlower(e[3]) == L'e' && (!e[4] || e[4] == L',')) { e[4] = 0; break; }
    if (GetFileAttributesW(p) == INVALID_FILE_ATTRIBUTES) continue;
    lstrcpynW(g->exe, p, MAX_PATH);
    lstrcpynW(g->dir, dir, MAX_PATH);
    if (!g->dir[0]) { // its folder: the program's
      lstrcpynW(g->dir, p, MAX_PATH);
      wchar_t *slash = wcsrchr(g->dir, L'\\');
      if (slash) *slash = 0;
    }
  }
#else
  if (!g->stem[0]) return;
  const char *data = getenv("XDG_DATA_HOME"), *home = getenv("HOME");
  char base[1024];
  if (data && *data) snprintf(base, sizeof base, "%s", data);
  else if (home) snprintf(base, sizeof base, "%s/.local/share", home);
  else return;
  snprintf(g->dir, sizeof g->dir, "%s/%s", base, g->stem);
  snprintf(g->exe, sizeof g->exe, "%s/%s", g->dir, g->stem);
  if (access(g->exe, X_OK)) g->exe[0] = g->dir[0] = 0;
#endif
}

static bool other_start(const OtherGame *g) {
#ifdef _WIN32
  wchar_t cmd[MAX_PATH + 3];
  _snwprintf(cmd, MAX_PATH + 2, L"\"%ls\"", g->exe);
  cmd[MAX_PATH + 2] = 0;
  STARTUPINFOW si = {sizeof si};
  PROCESS_INFORMATION pi;
  if (!CreateProcessW(g->exe, cmd, NULL, NULL, FALSE, 0, NULL, g->dir[0] ? g->dir : NULL, &si, &pi)) return false;
  CloseHandle(pi.hThread);
  CloseHandle(pi.hProcess);
  return true;
#else
  pid_t pid = fork();
  if (pid < 0) return false;
  if (!pid) {
    setsid();
    if (g->dir[0] && chdir(g->dir)) _exit(127);
    execl(g->exe, g->exe, (char *)NULL);
    _exit(127);
  }
  return true;
#endif
}

static void others_open(void) {}
static int others_count(void) { return shown_n; }
static void others_row(int k, wchar_t *label, wchar_t *value) {
  const OtherGame *g = &others[shown[k]];
  lstrcpynW(label, g->name, 64);
  lstrcpynW(value, g->exe[0] ? TX(T_GAME_INSTALLED) : L"", 64);
}
static const wchar_t *others_help(int k) {
  static wchar_t out[900];
  const OtherGame *g = &others[shown[k]];
  const wchar_t *t = g->text[dl_cfg.ui_lang];
  if (!t[0]) {
    t = g->text[DL_LANG_EN];
    ui_audit_note("no translation", g->name); // the list's own texts count too
  }
  _snwprintf(out, 899, L"%ls\n\n%ls", t, TX(g->exe[0] ? T_H_GAME_PLAY : T_H_GAME_PAGE));
  out[899] = 0;
  return out;
}
// On Windows the other game is started on a thread of its own (2.83): CreateProcess waits while
// Windows reads and checks the program, which on a sleeping external disk took long enough for Kahna's
// window to be "not responding" and closed by the player before the game came. The window keeps
// answering, says "Starting...", and closes once the game has started (dl_others_follow).
#ifdef _WIN32
static volatile LONG other_state; // 0 nothing, 1 starting, 2 started, 3 failed
static const OtherGame *other_going;
static DWORD WINAPI other_thread(LPVOID unused) {
  (void)unused;
  const char *slow = getenv("DL_TEST_GAMES_DELAY"); // tests only (with DL_TEST_GAMES): a start as slow as from a sleeping disk, in ms
  if (slow && getenv("DL_TEST_GAMES")) Sleep((DWORD)atoi(slow));
  InterlockedExchange(&other_state, other_start(other_going) ? 2 : 3);
  return 0;
}
#endif

static void others_enter(int k) {
  const OtherGame *g = &others[shown[k]];
  if (dl_audit_dir) return;
  if (g->exe[0]) {
#ifdef _WIN32
    if (other_state == 1) return; // one is starting already
    other_going = g;
    other_state = 1;
    HANDLE t = CreateThread(NULL, 0, other_thread, NULL, 0, NULL);
    if (t) {
      CloseHandle(t);
      dl_note(TX(T_M_GAME_STARTING));
    } else other_state = other_start(g) ? 2 : 3;
#else
    if (other_start(g)) dl_running = false; // a normal close: the session and the settings kept
    else dl_note(TX(T_M_GAME_FAIL));
#endif
  } else if (g->url[0]) ShellExecuteA(NULL, "open", g->url, NULL, NULL, SW_SHOWNORMAL);
}

// The game's picture (the list on line) in the place of the preview.
static bool others_preview(void *dc, int k, double x0, double y0, double x1, double y1) {
  const OtherGame *g = &others[shown[k]];
  if (g->pic && g->pic_w > 0 && g->pic_h > 0) dl_draw_picture(dc, g->pic, g->pic_w, g->pic_h, x0, y0, x1, y1);
  return true;
}
// not const: .preview is given once a list on line brings pictures (without it, the page is as before)
static DlList others_list = {others_open, others_count, others_row, others_help, others_enter};
static int others_page_id = -1;

// ---- the list on line (2.93, DL_GAMES_URL in dl_games.h): read on a thread with its pictures, taken by
// dl_others_follow. Nothing is kept on disk.
static volatile LONG online_state; // 0 not asked, 1 reading, 2 read, 3 nothing (or taken)
static char *online_text;
static struct { char file[64]; char *data; int n; } online_pics[OTHER_MAX];
static int online_pic_n;

static bool picture_name_ok(const char *s) { // a file beside the list: letters, digits, - _ . only
  if (!*s || strstr(s, "..")) return false;
  for (; *s; s++)
    if (!((*s >= 'a' && *s <= 'z') || (*s >= 'A' && *s <= 'Z') || (*s >= '0' && *s <= '9') || *s == '-' || *s == '_' || *s == '.')) return false;
  return true;
}

static DWORD WINAPI online_thread(void *arg) {
  (void)arg;
  int cap = 1 << 16;
  char *t = malloc((size_t)cap + 1);
  int n = t ? dl_http_get(DL_GAMES_URL, t, cap) : -1;
  if (n <= 0 || !strchr(t, '[')) { // nothing, or an error's page
    free(t);
    InterlockedExchange(&online_state, 3);
    return 0;
  }
  t[n] = 0;
  char base[300];
  snprintf(base, sizeof base, "%s", DL_GAMES_URL);
  char *slash = strrchr(base, '/');
  if (slash) slash[1] = 0;
  for (const char *s = t; *s && online_pic_n < OTHER_MAX;) { // the pictures it names
    size_t len = strcspn(s, "\n");
    char line[256];
    snprintf(line, sizeof line, "%.*s", (int)(len < 255 ? len : 255), s);
    s += len + (s[len] ? 1 : 0);
    char *eq = strchr(line, '=');
    if (!eq) continue;
    *eq = 0;
    char *val = trim(eq + 1);
    if (strcmp(trim(line), "picture") || !picture_name_ok(val) || strlen(val) >= 64) continue;
    char url[400];
    snprintf(url, sizeof url, "%s%s", base, val);
    int pcap = 1 << 21; // 2 MB at most
    char *p = malloc((size_t)pcap);
    int pn = p ? dl_http_get(url, p, pcap) : -1;
    if (pn <= 0 || pn >= pcap) { free(p); continue; }
    snprintf(online_pics[online_pic_n].file, 64, "%s", val);
    online_pics[online_pic_n].data = p;
    online_pics[online_pic_n++].n = pn;
  }
  online_text = t;
  InterlockedExchange(&online_state, 2);
  return 0;
}

void dl_others_online(void) {
  if (!DL_GAMES_URL[0] || online_state || dl_audit_dir || (dl_game && (dl_game->hide & DL_HIDE_GAMES))) return;
  online_state = 1;
  HANDLE t = CreateThread(NULL, 0, online_thread, NULL, 0, NULL);
  if (t) CloseHandle(t);
  else online_state = 3;
}

// A picture of the list on line, read through a file of a moment beside the settings (the program's
// reader of pictures takes files).
static HBITMAP online_picture(const char *file, int *w, int *h) {
  *w = *h = 0;
  for (int k = 0; k < online_pic_n; k++) {
    if (strcmp(online_pics[k].file, file)) continue;
    const char *dot = strrchr(file, '.');
    char path[MAX_PATH];
    snprintf(path, sizeof path, "%s%s.game_picture%s", dl_base_dir, FILE_STEM, dot ? dot : "");
    FILE *f = fopen(path, "wb");
    if (!f) return NULL;
    bool ok = fwrite(online_pics[k].data, 1, (size_t)online_pics[k].n, f) == (size_t)online_pics[k].n;
    fclose(f);
    HBITMAP b = ok ? dl_picture_load(path, w, h) : NULL;
    DeleteFileA(path);
    return b;
  }
  return NULL;
}

static void others_pick(bool from_file);
static void others_page(void);

void dl_others_follow(void) {
#ifdef _WIN32
  LONG s = other_state;
  if (s == 2) { // the other game runs: this one closes as usual, its session and settings kept
    other_state = 0;
    dl_running = false;
  } else if (s == 3) {
    other_state = 0;
    dl_note(TX(T_M_GAME_FAIL));
  }
  if (s == 1) return; // (others[] in use by the thread starting one)
#endif
  if (online_state != 2) return;
  online_state = 3; // taken: it replaces dl_games.h's list
  for (int k = 0; k < other_n; k++)
    if (others[k].pic) DeleteObject(others[k].pic);
  others_read(online_text);
  others_pick(false);
  bool any = false;
  for (int k = 0; k < shown_n; k++) {
    OtherGame *g = &others[shown[k]];
    if (g->picture[0]) g->pic = online_picture(g->picture, &g->pic_w, &g->pic_h);
    any |= g->pic != NULL;
  }
  if (any) others_list.preview = others_preview;
  others_page(); // the page and its entry, if there were none
  dl_dirty = true;
}

// The games shown (others[] read): not this one, released (or all with --all-games), installed or with a
// page. from_file: a test's list (DL_TEST_GAMES), whose "exe" lines stand for installed copies.
static void others_pick(bool from_file) {
  bool all = dl_all_games; // --ui-audit with --all-games draws every text, one game shown as installed
  shown_n = 0;
  for (int k = 0; k < other_n; k++) {
    OtherGame *g = &others[k];
    if (!strcmp(g->id, UNINSTALL_ID) || !g->name[0] || (!all && !g->released)) continue;
    if (dl_audit_dir) {
      if (!shown_n) g->exe[0] = '?'; // not looked for: the pictures do not depend on this machine
    } else if (!from_file) other_find(g);
    else if (g->exe[0]) { // the test's copy: its folder
#ifdef _WIN32
      lstrcpynW(g->dir, g->exe, MAX_PATH);
      wchar_t *slash = wcsrchr(g->dir, L'\\');
#else
      snprintf(g->dir, sizeof g->dir, "%s", g->exe);
      char *slash = strrchr(g->dir, '/');
#endif
      if (slash) *slash = 0;
    }
    if (!g->exe[0] && !g->url[0] && !all) continue; // nothing to do with it
    shown[shown_n++] = k;
  }
}

// The page and its entry before Settings, once there is a game to show (again: nothing). Made last, so
// that the ids of every other page stay as they were; the bar's selection follows its entry.
static void others_page(void) {
  if (!shown_n || others_page_id >= 0 || dl_sec_n >= DL_MAX_SECTIONS || dl_sec_top < 1) return;
  int page = dl_list_page(dl_texts[T_TAB_GAMES], &others_list);
  if (page < 0) return;
  others_page_id = page;
  int at = dl_sec_top - 1; // Settings
  memmove(&dl_secs[at + 1], &dl_secs[at], sizeof dl_secs[0] * (size_t)(dl_sec_n - at));
  dl_secs[at] = (DlSection){SEC_PAGE, page, 0xE71D, dl_texts[T_GAMES], dl_texts[T_H_GAMES]};
  dl_sec_n++;
  dl_sec_top++;
  if (dl_side >= at) dl_side++; // (the entry chosen stays the same)
}

// dl_games.h's list (or a test's), at the end of dl_menu_finish.
static void others_add(void) {
  if (dl_game->hide & DL_HIDE_GAMES) return;
  // DL_TEST_GAMES (tests only): a list file instead of dl_games.h's, its "exe = " lines standing for
  // installed copies (tools\games_test.ps1)
  static char test_list[32768];
  const char *test = getenv("DL_TEST_GAMES");
  FILE *f = test ? fopen(test, "rb") : NULL;
  if (f) {
    test_list[fread(test_list, 1, sizeof test_list - 1, f)] = 0;
    fclose(f);
  }
  others_read(f ? test_list : dl_games_list);
  others_pick(f != NULL);
  others_page();
}

void dl_menu_finish(void) {
  // the kit's pages and rarer actions behind one entry (Settings), then Quit alone at the bottom
  add_section(SEC_PAGE, PG_SETTINGS, 0xE713, dl_texts[T_PA_SETTINGS], settings_help);
  dl_sec_top = dl_sec_n;
  add_section(SEC_QUIT, -1, 0xE7E8, dl_texts[T_QUIT], dl_texts[T_H_QUIT]);
  // the safe mode page, reached only after a crash (made always, so that --ui-audit sees it)
  // safe mode turns off HDR and the CRT filter: its help names them only where the player is offered them
#ifdef _WIN32
  bool hdr_row = true;
#else
  bool hdr_row = false; // no HDR row on Linux
#endif
  bool crt_row = !(dl_hides() & DL_HIDE_CRT);
  if (DL_NOFRAME) hdr_row = crt_row = false; // no Picture page: only the menus' software display
  safe_rows[0].help = dl_texts[hdr_row ? (crt_row ? T_H_SAFE_KEEP : T_H_SAFE_KEEP_HDR) : (crt_row ? T_H_SAFE_KEEP_CRT : T_H_SAFE_KEEP_SOFT)];
  safe_page = dl_page(dl_texts[T_SAFE_TAB], safe_rows, sizeof safe_rows / sizeof safe_rows[0]);
  if (dl_audit_dir) dl_page_insert(safe_page, 1, &safe_load_row);
  // the game's own first-start page (DlGame.welcome_*), made last so that the ids of every other page
  // stay as they were
  if (dl_game->welcome_tab && dl_game->welcome_rows && dl_game->welcome_count > 0) {
    welcome_game = dl_page(dl_game->welcome_tab, dl_game->welcome_rows, dl_game->welcome_count);
    if (welcome_game >= 0) {
      DlPage *pw = &dl_pages[welcome_game];
      if (pw->n < DL_MAX_ROWS) pw->rows[pw->n++] = DL_OUTSIDE ? &welcome_play_out_row : &welcome_play_row;
      if (DL_NOFRAME) welcome_out_rows[1] = welcome_next_row;
      else welcome_screen_rows[2] = welcome_next_row; // "Next" instead of "Start playing"
    }
  }
  others_add();
}

// ---- rows the game takes at start only (DlRow.at_start): their values when the program started, and
// "Restart to apply" at the end of the pages that have one while one of them differs
typedef struct { const int *value; int was; } StartValue;
static StartValue starts[64];
static int start_n;
static bool start_page[DL_MAX_PAGES];
static const DlRow start_row = {DL_ACTION, dl_texts[T_PK_RESTART], dl_texts[T_H_START_RESTART], NULL, 0, NULL, NULL, dl_relaunch};

bool dl_start_pending(void) {
  for (int s = 0; s < start_n; s++)
    if (*starts[s].value != starts[s].was) return true;
  return false;
}

static void start_rows_follow(void) {
  bool pending = dl_audit_dir || dl_start_pending(); // the audit draws the row
  for (int p = 0; p < dl_page_n; p++) {
    if (!start_page[p]) continue;
    DlPage *pg = &dl_pages[p];
    bool there = pg->n && pg->rows[pg->n - 1] == &start_row;
    if (pending && !there) dl_page_insert(p, pg->n, &start_row);
    else if (!pending && there) pg->n--; // the selection on it goes to "Back"
  }
}

void dl_start_values(void) {
  start_n = 0;
  for (int p = 0; p < dl_page_n; p++)
    for (int k = 0; k < dl_pages[p].n; k++) {
      const DlRow *r = dl_pages[p].rows[k];
      if (!r->at_start || !r->value) continue;
      start_page[p] = true;
      int s = 0;
      while (s < start_n && starts[s].value != r->value) s++; // the same value on two pages: once
      if (s == start_n && start_n < 64) starts[start_n++] = (StartValue){r->value, *r->value};
    }
  start_rows_follow();
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
  start_rows_follow();
}

// dl_show_achievements: asked from any thread, done at the loop's next turn; the page opened over the
// game goes back to play, and closed() is called once the game runs again (whatever way the player took)
static volatile LONG show_ach;
static void (*show_ach_closed)(void);
static bool ach_from_game, ach_closing;

bool dl_show_achievements(void (*closed)(void)) {
  if (dl_ach_page() < 0 || show_ach) return false;
  show_ach_closed = closed;
  InterlockedExchange(&show_ach, 1);
  return true;
}

static void open_page(int page);

void dl_shows_follow(void) {
  if (ach_closing && dl_mode == M_GAME) { // play goes on: the game told
    ach_closing = false;
    if (show_ach_closed) show_ach_closed();
  }
  if (!show_ach || (dl_mode != M_GAME && dl_mode != M_PAUSE) || !dl_started) return;
  InterlockedExchange(&show_ach, 0);
  dl_menu_open(); // the setup screen over the paused game, its bar on Achievements
  for (int k = 0; k < dl_sec_n; k++)
    if (dl_secs[k].kind == SEC_PAGE && dl_secs[k].page == dl_ach_page()) dl_side = k;
  stack_n = 0;
  open_page(dl_ach_page());
  ach_from_game = ach_closing = true;
  dl_dirty = true;
}

static void page_back(void) {
  if (!stack_n && ach_from_game && dl_cur == dl_ach_page()) { // opened by the game: back to it
    ach_from_game = false;
    dl_play();
    return;
  }
  ach_from_game = false;
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
      start_rows_follow(); // Defaults, a preset... may change a row taken at start
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
  start_rows_follow();
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
    case SEC_DEFAULTS: dl_defaults(false); start_rows_follow(); break;
    case SEC_RESET: dl_restart(); break;
    case SEC_QUIT: dl_running = false; break;
    case SEC_UPDATE: if (dl_update_installable()) dl_update_install(); else dl_update_open(); break;
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
    if (k == PA_SHOT && dl_over) continue; // over the game: the picture is the game's, and so are its screenshots
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
  note = ui_wrap_text(dc, note, r.right - r.left);
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
    wchar_t update[96];
    if (dl_secs[k].kind == SEC_UPDATE) { // "Update 1.4", then its progress, "ready" or "failed"
      _snwprintf(update, 95, L"%ls %ls", label, dl_update_installable() ? dl_update_install_show(0) : L"");
      update[95] = 0;
      label = update;
    }
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
  const wchar_t *text = help;
  help = ui_wrap_text(dc, text, hr.right - hr.left);
  DrawTextW(dc, help, -1, &need, DT_WORDBREAK | DT_NOPREFIX | DT_CALCRECT);
  if (need.bottom > hr.bottom) {
    SelectObject(dc, f_help_small);
    help = ui_wrap_text(dc, text, hr.right - hr.left);
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
  // the player's own backdrop if there is one (opaque, in the layer too; a theme's may let the sky show
  // through); otherwise the sky, or, as a layer for the graphics card, black then white, without the sky
  // and the game's picture, which the graphics card draws itself
  if (dl_over && dl_layer) { // over the game (dl_over_layer): its picture through a dark veil
    RECT all = {0, 0, cw, ch};
    FillRect(dc, &all, GetStockObject(dl_layer == 2 ? WHITE_BRUSH : BLACK_BRUSH));
    ui_rect_alpha(dc, -ui_ox / ui_s, -ui_oy / ui_s, (cw - ui_ox) / ui_s + 1, (ch - ui_oy) / ui_s + 1, ui_tint(RGB(0, 6, 30)), 150);
  } else if (backdrop_draw(dc, cw, ch)) {
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
    if (s->kind == SEC_PLAY && dl_update_text()) { // a newer version: told under "Play" ("Update" has it in its label)
      _snwprintf(with_update, 1099, L"%ls\n\n%ls", help, dl_update_text());
      with_update[1099] = 0;
      help = with_update;
    }
  }

  // the game's picture: large beside the bar's actions, small beside a page; on a list with
  // pictures, the picture of the selected row instead
  // (the game outside DarkLauncher, DlGame.play: no live picture; the help takes its place, except on
  // a list with pictures of its own)
  double vx0 = home ? 164 : 436, vy0 = 60, vx1 = 630, vy1 = home ? 300 : 212;
  double hy0 = home ? 310 : 220;
  dl_prev_w = 0;
  // (the game's first-start page may draw its own picture in the preview's place: DlGame.welcome_preview)
  bool own = page >= 0 && page == welcome_game && dl_game->welcome_preview;
  if (!DL_NOFRAME || (list && list->preview) || own) {
    ui_panel(dc, vx0, vy0, vx1, vy1, RGB(0, 0, 0), TX(T_PREVIEW));
    int pw, ph, aw = PX(vx1 - vx0 - 8), ah = PX(vy1 - vy0 - 12);
    dl_fit(aw, ah, false, &pw, &ph);
    dl_prev_x = ui_ox + PX(vx0 + 4) + (aw - pw) / 2;
    dl_prev_y = ui_oy + PX(vy0 + 8) + (ah - ph) / 2;
    dl_prev_w = DL_NOFRAME ? 0 : pw;
    dl_prev_h = ph;
    if (list && list->preview && sel >= 0 && sel < list->count() && list->preview(dc, sel, vx0 + 4, vy0 + 8, vx1 - 4, vy1 - 4)) dl_prev_w = 0;
    else if (own && sel >= 0 && sel < dl_game->welcome_count && dl_game->welcome_preview(dc, sel, vx0 + 4, vy0 + 8, vx1 - 4, vy1 - 4)) dl_prev_w = 0;
    else if (dl_pic_waiting && dl_prev_w) { // no real picture yet: "Starting the game…" in its place
      SIZE sz;
      // the game's picture for its start (DlGame.starting_picture, 2.64) where its picture will be, the
      // words over it on a dark band near its bottom
      static bool asked;
      static const u32 *start_px;
      static int start_w, start_h;
      if (!asked && dl_start_done && dl_game->starting_picture) {
        asked = true;
        start_px = dl_game->starting_picture(&start_w, &start_h);
        if (start_w <= 0 || start_h <= 0 || start_w > 8192 || start_h > 8192) start_px = NULL;
      }
      double ty = (vy0 + vy1) / 2 - 4;
      if (start_px) {
        ui_picture(dc, 254, start_px, start_w, start_h, dl_prev_x, dl_prev_y, dl_prev_w, dl_prev_h);
        double px0 = (dl_prev_x - ui_ox) / ui_s, px1 = (dl_prev_x + dl_prev_w - ui_ox) / ui_s, py1 = (dl_prev_y + dl_prev_h - ui_oy) / ui_s;
        ty = py1 - 26;
        ui_rect_alpha(dc, px0, ty - 4, px1, ty + 20, RGB(0, 0, 0), 150);
      }
      ui_text(dc, f_help, (vx0 + vx1) / 2, ty, TX(T_STARTING), ui_tint(RGB(240, 242, 255)), TA_CENTER);
      GetTextExtentPoint32W(dc, TX(T_STARTING), lstrlenW(TX(T_STARTING)), &sz);
      if (sz.cx > PX(vx1 - vx0 - 16)) ui_audit_note("preview note too long", TX(T_STARTING));
      dl_prev_w = 0;
    } else if (!dl_layer && dl_prev_w) dl_blit(dc, dl_prev_x, dl_prev_y, dl_prev_w, dl_prev_h);
  } else hy0 = vy0;
  draw_help(dc, vx0, hy0, 630, PANEL_BOTTOM, help);

  if (dl_input_kind == DL_IN_KEYS) {
    ui_text(dc, f_small, 17, 382, TX(T_FOOTER), RGB(0, 0, 0), TA_LEFT);
    ui_text(dc, f_small, 16, 381, TX(T_FOOTER), RGB(255, 255, 255), TA_LEFT);
  } else pad_footer(dc);
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
void dl_menu_mouse(int mx, int my, bool click) {
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

// What the setup screen shows, for its soft change of page (dl_gpu.c, dl_gl.c): the page in hand or the
// one the bar's entry shows (opening it from the bar is no change); -1: none of its pages (the pause
// menu, "Try it full size", the game).
int dl_menu_view(void) {
  if (dl_mode == M_PAGE) return 1000 + dl_cur;
  if (dl_mode == M_SIDE) return dl_secs[dl_side].kind == SEC_PAGE ? 1000 + dl_secs[dl_side].page : 2000 + dl_side;
  return -1;
}

// Where the setup screen is, for dl_relaunch: the entry of the bar, the page, its row and the pages it
// was opened from. "" in the game or the pause menu: the program started again shows the setup screen.
void dl_menu_where(char *out, int n) {
  out[0] = 0;
  if (dl_mode != M_SIDE && dl_mode != M_PAGE) return;
  int k = snprintf(out, (size_t)n, "menu %d %d %d %d %d", dl_mode == M_PAGE, dl_side, dl_cur, dl_sel, stack_n);
  for (int s = 0; s < stack_n && k > 0 && k < n; s++) k += snprintf(out + k, (size_t)(n - k), " %d %d", stack[s], stack_sel[s]);
}

bool dl_menu_back(const char *where) {
  int page, side, cur, sel, sn, used = 0, st[8], ss[8];
  if (sscanf(where, "menu %d %d %d %d %d%n", &page, &side, &cur, &sel, &sn, &used) != 5) return false;
  if (side < 0 || side >= dl_sec_n || cur < 0 || cur >= dl_page_n || sn < 0 || sn > 8) return false;
  for (int s = 0; s < sn; s++) {
    int u = 0;
    if (sscanf(where + used, " %d %d%n", &st[s], &ss[s], &u) != 2 || st[s] < 0 || st[s] >= dl_page_n) return false;
    used += u;
  }
  dl_mode = page ? M_PAGE : M_SIDE;
  dl_side = side;
  dl_cur = cur;
  stack_n = sn;
  memcpy(stack, st, sizeof st[0] * (size_t)sn);
  memcpy(stack_sel, ss, sizeof ss[0] * (size_t)sn);
  list_read = -1;
  int rows = page_rows(cur) - 1; // without "Back"; the restart row is gone now: the last row of the page instead
  dl_sel = sel < 0 || rows <= 0 ? 0 : sel >= rows ? rows - 1 : sel;
  dl_dirty = true;
  return true;
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
    { // the preview while the game has given no real picture yet: beside the bar, then beside a page
      bool was = dl_pic_waiting;
      dl_pic_waiting = true;
      dl_side = 0;
      dl_draw_menu(wdc);
      if (dl_menu_bits) {
        GetClientRect(dl_hwnd, &rc);
        snprintf(path, sizeof path, "%s" DL_SEP "%d_starting.png", dl_audit_dir, l);
        dl_save_png(path, dl_menu_bits, rc.right, rc.bottom);
      }
      if (dl_page_n > 0) {
        dl_mode = M_PAGE;
        dl_cur = 0;
        dl_sel = 0;
        dl_draw_menu(wdc);
        dl_mode = M_SIDE;
      }
      dl_pic_waiting = was;
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
    if (dl_game->save_state && dl_game->load_state && !DL_OUTSIDE) { // the keyboard on screen (a slot named with a controller)
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
    pause_rows(); // the pause menu, each row selected (none outside DarkLauncher: no game here)
    dl_mode = M_PAUSE;
    for (int r = 0; r < pa_n && !DL_OUTSIDE; r++) {
      dl_sel = r;
      dl_draw_menu(wdc);
      if (r == 1 && dl_menu_bits) {
        GetClientRect(dl_hwnd, &rc);
        snprintf(path, sizeof path, "%s" DL_SEP "%d_pause.png", dl_audit_dir, l);
        dl_save_png(path, dl_menu_bits, rc.right, rc.bottom);
      }
    }
  }
  if (DL_UPDATE_ON) { // a newer version found (2.99): "Update" under Play, selected, in each language
    dl_update_sample();
    dl_net_newer();
    if (ROW_Y0 + dl_sec_top * ROW_H > PANEL_BOTTOM - 6 - (dl_sec_n - dl_sec_top) * ROW_H) ui_audit_note("navigation bar too full with Update", L"");
    dl_mode = M_SIDE;
    for (int l = 0; l < DL_LANGS; l++) {
      if (!dl_lang_ok(l)) continue;
      dl_cfg.ui_lang = l;
      for (dl_side = 0; dl_side < dl_sec_n && dl_secs[dl_side].kind != SEC_UPDATE; dl_side++) {}
      dl_draw_menu(wdc);
      if (dl_menu_bits) {
        GetClientRect(dl_hwnd, &rc);
        snprintf(path, sizeof path, "%s" DL_SEP "%d_update.png", dl_audit_dir, l);
        dl_save_png(path, dl_menu_bits, rc.right, rc.bottom);
      }
    }
  }
  dl_cfg.ui_lang = keep;
  ReleaseDC(dl_hwnd, wdc);
  dl_buttons_audit(dl_audit_dir);
  printf("ui audit: %d issues\n", dl_audit_issues);
  return dl_audit_issues ? 2 : 0;
}
