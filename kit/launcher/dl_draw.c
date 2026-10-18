// Drawing of the setup screen on a logical 640 x 400 canvas, scaled to the window and centred:
// panels, rows, switches, bars, the project's title and the sky behind them. GDI only.
#include "dl_int.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

double ui_s = 1;
int ui_ox, ui_oy;
HFONT f_item, f_small, f_help, f_help_small;
static HFONT f_title, f_title_small;
static int font_px;
int dl_audit_issues;

int PX(double v) { return (int)(v * ui_s + 0.5); }

// --ui-audit: a text that does not fit is reported with where it was drawn
void ui_audit_note(const char *what, const wchar_t *text) {
  if (!dl_audit_dir) return;
  printf("lang=%d mode=%d page=%d row=%d %s: %ls\n", dl_cfg.ui_lang, dl_mode, dl_cur, dl_sel, what, text);
  dl_audit_issues++;
}

static const DlTheme *theme(void) { return dl_game ? dl_game->theme : NULL; }

static const wchar_t *face(void) {
  if (dl_cfg.ui_lang == 0) return L"Yu Gothic UI";
  return theme() && theme()->text_font ? theme()->text_font : L"Verdana";
}

static const wchar_t *title_face(void) { return theme() && theme()->title_font ? theme()->title_font : L"Palatino Linotype"; }

// ---- the theme's colours: DarkLauncher's blues turned by the hue between its selection blue and the
// theme's colour, their strength scaled the same way, their lightness kept
static void to_hsl(const double *rgb, double *hsl) {
  double r = rgb[0] / 255, g = rgb[1] / 255, b = rgb[2] / 255;
  double hi = r > g ? (r > b ? r : b) : (g > b ? g : b), lo = r < g ? (r < b ? r : b) : (g < b ? g : b), d = hi - lo;
  hsl[2] = (hi + lo) / 2;
  if (d <= 0) { hsl[0] = hsl[1] = 0; return; }
  hsl[1] = hsl[2] > 0.5 ? d / (2 - hi - lo) : d / (hi + lo);
  double h = hi == r ? (g - b) / d + (g < b ? 6 : 0) : hi == g ? (b - r) / d + 2 : (r - g) / d + 4;
  hsl[0] = h / 6;
}

static double hue_part(double p, double q, double t) {
  if (t < 0) t += 1;
  if (t > 1) t -= 1;
  return t < 1.0 / 6 ? p + (q - p) * 6 * t : t < 0.5 ? q : t < 2.0 / 3 ? p + (q - p) * (2.0 / 3 - t) * 6 : p;
}

static void from_hsl(const double *hsl, double *rgb) {
  double h = hsl[0], s = hsl[1], l = hsl[2];
  if (s <= 0) { rgb[0] = rgb[1] = rgb[2] = l * 255; return; }
  double q = l < 0.5 ? l * (1 + s) : l + s - l * s, p = 2 * l - q;
  rgb[0] = hue_part(p, q, h + 1.0 / 3) * 255;
  rgb[1] = hue_part(p, q, h) * 255;
  rgb[2] = hue_part(p, q, h - 1.0 / 3) * 255;
}

static double hue_turn, strength = 1;

static bool theme_colours(void) {
  static int ready = -1;
  if (ready < 0) {
    ready = theme() && theme()->colour;
    if (ready) {
      u32 c = theme()->colour;
      double ref[3] = {34, 84, 184}, mine[3] = {c >> 16 & 255, c >> 8 & 255, c & 255}, a[3], b[3]; // ref: the selection
      to_hsl(ref, a);
      to_hsl(mine, b);
      hue_turn = b[0] - a[0];
      strength = b[1] / a[1];
    }
  }
  return ready;
}

static void tint3(double *rgb) {
  double hsl[3];
  to_hsl(rgb, hsl);
  hsl[0] += hue_turn;
  hsl[0] -= floor(hsl[0]);
  hsl[1] *= strength;
  if (hsl[1] > 1) hsl[1] = 1;
  from_hsl(hsl, rgb);
}

COLORREF ui_tint(COLORREF c) {
  if (!theme_colours()) return c;
  double v[3] = {GetRValue(c), GetGValue(c), GetBValue(c)};
  tint3(v);
  int q[3];
  for (int k = 0; k < 3; k++) { q[k] = (int)(v[k] + 0.5); q[k] = q[k] < 0 ? 0 : q[k] > 255 ? 255 : q[k]; }
  return RGB(q[0], q[1], q[2]);
}

bool ui_theme_hsl(float *turn_out, float *strength_out) {
  if (!theme_colours()) return false;
  *turn_out = (float)hue_turn;
  *strength_out = (float)strength;
  return true;
}

static int frame_style(void) { return theme() ? theme()->frame : DL_FRAME_ROUND; }
static int title_style(void) { return theme() ? theme()->title : DL_TITLE_METAL; }
int ui_sky_style(void) { return theme() ? theme()->sky : DL_SKY_CLOUDS; }

// A corner of r logical pixels in the theme's frame style.
static double ui_corner(double r) {
  switch (frame_style()) {
    case DL_FRAME_SQUARE: return 0;
    case DL_FRAME_DOUBLE: return r < 1.5 ? r : 1.5;
    case DL_FRAME_SOFT: return r * 2;
    default: return r;
  }
}

COLORREF ui_value(void) {
  if (theme() && theme()->value) { u32 v = theme()->value; return RGB(v >> 16 & 255, v >> 8 & 255, v & 255); }
  return ui_tint(RGB(130, 214, 255));
}

// ---- the theme's pictures (0xAARRGGBB, premultiplied), made at the size they are drawn: averaged
// when smaller, pixel for pixel when larger (the pixels of a game's icon stay sharp)
typedef struct {
  bool tried;
  const u32 *src;
  int sw, sh;
  HDC dc;
  HBITMAP bmp;
  int dw, dh;
} UiPic;

static void pic_scale(const u32 *src, int sw, int sh, u32 *out, int dw, int dh) {
  for (int y = 0; y < dh; y++)
    for (int x = 0; x < dw; x++) {
      int x0 = x * sw / dw, x1 = (x + 1) * sw / dw, y0 = y * sh / dh, y1 = (y + 1) * sh / dh;
      if (x1 <= x0) x1 = x0 + 1;
      if (y1 <= y0) y1 = y0 + 1;
      u32 sum[4] = {0}, n = 0;
      for (int v = y0; v < y1; v++)
        for (int u = x0; u < x1; u++, n++) {
          u32 p = src[(size_t)v * sw + u];
          for (int k = 0; k < 4; k++) sum[k] += p >> (k * 8) & 255;
        }
      out[(size_t)y * dw + x] = sum[0] / n | sum[1] / n << 8 | sum[2] / n << 16 | sum[3] / n << 24;
    }
}

// Draws p whole, centred in the box (window pixels), with its proportions.
static void pic_draw(HDC dc, UiPic *p, int bx, int by, int bw, int bh) {
  if (!p->src || p->sw <= 0 || p->sh <= 0 || bw <= 0 || bh <= 0) return;
  int w = bw, h = (int)((s64)bw * p->sh / p->sw);
  if (h > bh) { h = bh; w = (int)((s64)bh * p->sw / p->sh); }
  if (w < 1 || h < 1) return;
  if (w != p->dw || h != p->dh) {
    BITMAPINFO bi = {{sizeof(BITMAPINFOHEADER), w, -h, 1, 32}};
    u32 *bits = NULL;
    HBITMAP nb = CreateDIBSection(dc, &bi, DIB_RGB_COLORS, (void **)&bits, NULL, 0);
    if (!nb) return;
    pic_scale(p->src, p->sw, p->sh, bits, w, h);
    if (!p->dc) p->dc = CreateCompatibleDC(dc);
    SelectObject(p->dc, nb);
    if (p->bmp) DeleteObject(p->bmp);
    p->bmp = nb;
    p->dw = w;
    p->dh = h;
  }
  BLENDFUNCTION bf = {AC_SRC_OVER, 0, 255, AC_SRC_ALPHA};
  AlphaBlend(dc, bx + (bw - w) / 2, by + (bh - h) / 2, w, h, p->dc, 0, 0, w, h, bf);
}

bool ui_theme_icon(HDC dc, unsigned icon, double x0, double y0, double x1, double y1) {
  static struct { unsigned icon; UiPic pic; } got[DL_MAX_SECTIONS];
  static int got_n;
  if (!theme() || !theme()->icon || !dl_start_done) return false;
  int k = 0;
  while (k < got_n && got[k].icon != icon) k++;
  if (k == got_n) {
    if (got_n == DL_MAX_SECTIONS) return false;
    got[got_n++].icon = icon;
  }
  UiPic *p = &got[k].pic;
  if (!p->tried) { p->tried = true; p->src = theme()->icon(icon, &p->sw, &p->sh); }
  if (!p->src) return false;
  pic_draw(dc, p, ui_ox + PX(x0), ui_oy + PX(y0), PX(x1 - x0), PX(y1 - y0));
  return true;
}

// Icon 0 at the sizes Windows asks for (title bar, taskbar), its colours no longer premultiplied.
static HICON make_icon(const u32 *src, int sw, int sh, int size) {
  u32 *px = malloc((size_t)size * size * 4);
  if (!px) return NULL;
  int w = size, h = size * sh / sw;
  if (h > size) { h = size; w = size * sw / sh; }
  if (w < 1) w = 1;
  if (h < 1) h = 1;
  u32 *fit = malloc((size_t)w * h * 4);
  if (!fit) { free(px); return NULL; }
  pic_scale(src, sw, sh, fit, w, h);
  memset(px, 0, (size_t)size * size * 4);
  for (int y = 0; y < h; y++)
    for (int x = 0; x < w; x++) {
      u32 c = fit[(size_t)y * w + x], a = c >> 24;
      if (a) c = a << 24 | (((c >> 16 & 255) * 255 / a) << 16) | (((c >> 8 & 255) * 255 / a) << 8) | ((c & 255) * 255 / a);
      px[(size_t)(y + (size - h) / 2) * size + x + (size - w) / 2] = c;
    }
  free(fit);
  BITMAPINFO bi = {{sizeof(BITMAPINFOHEADER), size, -size, 1, 32}};
  void *bits = NULL;
  HDC sdc = GetDC(NULL);
  HBITMAP colour = CreateDIBSection(sdc, &bi, DIB_RGB_COLORS, &bits, NULL, 0);
  ReleaseDC(NULL, sdc);
  HBITMAP mask = CreateBitmap(size, size, 1, 1, NULL);
  HICON ic = NULL;
  if (colour && mask) {
    memcpy(bits, px, (size_t)size * size * 4);
    ICONINFO ii = {TRUE, 0, 0, mask, colour};
    ic = CreateIconIndirect(&ii);
  }
  if (colour) DeleteObject(colour);
  if (mask) DeleteObject(mask);
  free(px);
  return ic;
}

void ui_window_icon(void) {
  if (!theme() || !theme()->icon) return;
  int w, h;
  const u32 *src = theme()->icon(0, &w, &h);
  if (!src || w <= 0 || h <= 0) return;
  HICON big = make_icon(src, w, h, GetSystemMetrics(SM_CXICON)), small = make_icon(src, w, h, GetSystemMetrics(SM_CXSMICON));
  if (big) SendMessageW(dl_hwnd, WM_SETICON, ICON_BIG, (LPARAM)big);
  if (small) SendMessageW(dl_hwnd, WM_SETICON, ICON_SMALL, (LPARAM)small);
}

const void *ui_theme_sound(int kind, int volume) {
  static u8 *wav[2];
  static int made_for[2] = {-1, -1};
  static bool tried[2];
  static const int16_t *samples[2];
  static int count[2];
  if (!theme() || !theme()->sound || !dl_start_done || kind < 0 || kind > 1) return NULL;
  if (!tried[kind]) {
    tried[kind] = true;
    int rate = 0;
    samples[kind] = theme()->sound(kind, &count[kind], &rate);
    if (samples[kind] && count[kind] > 0 && rate > 0 && (wav[kind] = malloc(44 + (size_t)count[kind] * 2))) {
      u32 n = (u32)count[kind], hd[11] = {0x46464952, 36 + n * 2, 0x45564157, 0x20746D66, 16, 0x00010001, (u32)rate, (u32)rate * 2, 0x00100002, 0x61746164, n * 2};
      memcpy(wav[kind], hd, 44);
    }
  }
  if (!wav[kind]) return NULL;
  if (made_for[kind] != volume) { // the samples at that volume, in place (as dl_beep does)
    made_for[kind] = volume;
    for (int k = 0; k < count[kind]; k++) {
      s16 v = (s16)(samples[kind][k] * volume / 10);
      memcpy(wav[kind] + 44 + k * 2, &v, 2);
    }
  }
  return wav[kind];
}

void ui_fonts(void) {
  int px = PX(100);
  static int font_lang = -1;
  if (px == font_px && font_lang == dl_cfg.ui_lang) return;
  font_px = px;
  font_lang = dl_cfg.ui_lang;
  HFONT *all[] = {&f_title, &f_title_small, &f_item, &f_small, &f_help, &f_help_small};
  for (int k = 0; k < 6; k++)
    if (*all[k]) DeleteObject(*all[k]);
  f_help_small = CreateFontW(-PX(10.5), 0, 0, 0, FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET, 0, 0, ANTIALIASED_QUALITY, 0, face());
  // title: a serif face drawn slightly narrow, with a larger initial (see ui_title)
  bool slant = title_style() != DL_TITLE_FLAT;
  f_title = CreateFontW(-PX(43), PX(14.5), 0, 0, FW_BOLD, slant, 0, 0, DEFAULT_CHARSET, 0, 0, ANTIALIASED_QUALITY, 0, title_face());
  f_title_small = CreateFontW(-PX(33), PX(11.5), 0, 0, FW_BOLD, slant, 0, 0, DEFAULT_CHARSET, 0, 0, ANTIALIASED_QUALITY, 0, title_face());
  f_item = CreateFontW(-PX(15), 0, 0, 0, FW_BOLD, 0, 0, 0, DEFAULT_CHARSET, 0, 0, ANTIALIASED_QUALITY, 0, face());
  f_small = CreateFontW(-PX(11), 0, 0, 0, FW_BOLD, 0, 0, 0, DEFAULT_CHARSET, 0, 0, ANTIALIASED_QUALITY, 0, face());
  f_help = CreateFontW(-PX(13), 0, 0, 0, FW_NORMAL, dl_cfg.ui_lang != 0, 0, 0, DEFAULT_CHARSET, 0, 0, ANTIALIASED_QUALITY, 0, face());
}

void ui_rect(HDC dc, double x0, double y0, double x1, double y1, COLORREF c) {
  RECT r = {ui_ox + PX(x0), ui_oy + PX(y0), ui_ox + PX(x1), ui_oy + PX(y1)};
  HBRUSH b = CreateSolidBrush(c);
  FillRect(dc, &r, b);
  DeleteObject(b);
}

void ui_rect_alpha(HDC dc, double x0, double y0, double x1, double y1, COLORREF c, int alpha) {
  static HDC src;
  static u32 *px;
  if (!src) {
    BITMAPINFO bi = {0};
    bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth = 1;
    bi.bmiHeader.biHeight = 1;
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    src = CreateCompatibleDC(dc);
    SelectObject(src, CreateDIBSection(src, &bi, DIB_RGB_COLORS, (void **)&px, NULL, 0));
  }
  *px = ((u32)GetRValue(c) << 16) | ((u32)GetGValue(c) << 8) | GetBValue(c);
  BLENDFUNCTION bf = {AC_SRC_OVER, 0, (BYTE)alpha, 0};
  int X0 = ui_ox + PX(x0), Y0 = ui_oy + PX(y0);
  AlphaBlend(dc, X0, Y0, ui_ox + PX(x1) - X0, ui_oy + PX(y1) - Y0, src, 0, 0, 1, 1, bf);
}

// Filled shape with rounded corners (r in logical units).
void ui_round(HDC dc, double x0, double y0, double x1, double y1, COLORREF c, double r) {
  HBRUSH b = CreateSolidBrush(c);
  HGDIOBJ ob = SelectObject(dc, b), op = SelectObject(dc, GetStockObject(NULL_PEN));
  int d = PX(r * 2);
  RoundRect(dc, ui_ox + PX(x0), ui_oy + PX(y0), ui_ox + PX(x1) + 1, ui_oy + PX(y1) + 1, d, d);
  SelectObject(dc, ob);
  SelectObject(dc, op);
  DeleteObject(b);
}

void ui_box(HDC dc, double x0, double y0, double x1, double y1, COLORREF c, double r) { ui_round(dc, x0, y0, x1, y1, c, ui_corner(r)); }

// The selected row, in the theme's cursor style.
void ui_cursor(HDC dc, double x0, double y0, double x1, double y1) {
  int st = theme() ? theme()->cursor : DL_CURSOR_BAR;
  if (st == DL_CURSOR_ARROW) { // a faint bar and a pointer at its start
    ui_box(dc, x0, y0, x1, y1, ui_tint(RGB(22, 50, 118)), 4);
    double cy = (y0 + y1) / 2;
    POINT p[3] = {{ui_ox + PX(x0 + 1.5), ui_oy + PX(cy - 4.5)}, {ui_ox + PX(x0 + 1.5), ui_oy + PX(cy + 4.5)}, {ui_ox + PX(x0 + 7), ui_oy + PX(cy)}};
    HBRUSH b = CreateSolidBrush(ui_value());
    HGDIOBJ ob = SelectObject(dc, b), op = SelectObject(dc, GetStockObject(NULL_PEN));
    Polygon(dc, p, 3);
    SelectObject(dc, ob);
    SelectObject(dc, op);
    DeleteObject(b);
  } else if (st == DL_CURSOR_OUTLINE) {
    HPEN pen = CreatePen(PS_SOLID, PX(1.5) < 1 ? 1 : PX(1.5), ui_value());
    HGDIOBJ op = SelectObject(dc, pen), ob = SelectObject(dc, GetStockObject(NULL_BRUSH));
    int d = PX(ui_corner(4) * 2);
    RoundRect(dc, ui_ox + PX(x0), ui_oy + PX(y0), ui_ox + PX(x1) + 1, ui_oy + PX(y1) + 1, d, d);
    SelectObject(dc, op);
    SelectObject(dc, ob);
    DeleteObject(pen);
  } else ui_box(dc, x0, y0, x1, y1, ui_tint(RGB(34, 84, 184)), 4);
}

// A panel letting the sky show through a little, in the theme's frame style (rounded with a line by
// default), with an optional title on its top edge.
void ui_panel(HDC dc, double x0, double y0, double x1, double y1, COLORREF fill, const wchar_t *tab) {
  int st = frame_style();
  double rad = st == DL_FRAME_SQUARE ? 0 : st == DL_FRAME_DOUBLE ? 3 : st == DL_FRAME_SOFT ? 16 : 6;
  int X0 = ui_ox + PX(x0), Y0 = ui_oy + PX(y0), X1 = ui_ox + PX(x1), Y1 = ui_oy + PX(y1), d = PX(rad * 2);
  if (fill != RGB(0, 0, 0)) {
    HRGN rg = CreateRoundRectRgn(X0, Y0, X1 + 1, Y1 + 1, d, d);
    SelectClipRgn(dc, rg);
    ui_rect_alpha(dc, x0, y0, x1, y1, fill, st == DL_FRAME_DOUBLE ? 235 : st == DL_FRAME_SOFT ? 180 : 205);
    SelectClipRgn(dc, NULL);
    DeleteObject(rg);
  } else ui_round(dc, x0, y0, x1, y1, fill, rad);
  for (int k = 0; k < (st == DL_FRAME_SOFT ? 0 : st == DL_FRAME_DOUBLE ? 2 : 1); k++) { // the edge; a second line inside it
    HPEN pen = CreatePen(PS_SOLID, PX(1) < 1 ? 1 : PX(1), k ? ui_tint(RGB(176, 206, 250)) : COL_EDGE);
    HGDIOBJ op = SelectObject(dc, pen), ob = SelectObject(dc, GetStockObject(NULL_BRUSH));
    int in = k * PX(3);
    RoundRect(dc, X0 + in, Y0 + in, X1 + 1 - in, Y1 + 1 - in, d, d);
    SelectObject(dc, op);
    SelectObject(dc, ob);
    DeleteObject(pen);
  }
  if (tab) {
    SIZE sz;
    SelectObject(dc, f_small);
    GetTextExtentPoint32W(dc, tab, lstrlenW(tab), &sz);
    double w = sz.cx / ui_s + 12;
    ui_box(dc, x0 + 12, y0 - 8, x0 + 12 + w, y0 + 7, ui_tint(RGB(176, 206, 250)), 4);
    SetBkMode(dc, TRANSPARENT);
    SetTextAlign(dc, TA_LEFT | TA_TOP);
    SetTextColor(dc, ui_tint(RGB(8, 26, 84)));
    TextOutW(dc, ui_ox + PX(x0 + 18), ui_oy + PX(y0 - 7), tab, lstrlenW(tab));
  }
}

void ui_text(HDC dc, HFONT f, double x, double y, const wchar_t *s, COLORREF c, UINT align) {
  SelectObject(dc, f);
  SetBkMode(dc, TRANSPARENT);
  SetTextAlign(dc, align | TA_TOP);
  SetTextColor(dc, c);
  TextOutW(dc, ui_ox + PX(x), ui_oy + PX(y), s, lstrlenW(s));
}

// Like ui_text, but the text must fit in max_w logical units: the letters are drawn narrower first
// (shorter too only below 3/4 of the width), and what still does not fit is clipped.
void ui_text_fit(HDC dc, double x, double y, const wchar_t *s, COLORREF c, UINT align, double max_w) {
  SIZE sz;
  HFONT f = f_item;
  double dy = 0;
  SelectObject(dc, f);
  GetTextExtentPoint32W(dc, s, lstrlenW(s), &sz);
  HFONT made = NULL;
  if (sz.cx > PX(max_w) && sz.cx > 0) {
    TEXTMETRICW tm;
    GetTextMetricsW(dc, &tm);
    double ratio = (double)PX(max_w) / sz.cx * 0.98, tall = ratio < 0.75 ? ratio / 0.75 : 1.0;
    if (ratio < 0.62) ui_audit_note("label squeezed", s);
    int w = (int)(tm.tmAveCharWidth * (ratio < 0.75 ? 0.75 : ratio) * tall);
    made = CreateFontW(-(int)(PX(15) * tall), w < 1 ? 1 : w, 0, 0, FW_BOLD, 0, 0, 0, DEFAULT_CHARSET, 0, 0, ANTIALIASED_QUALITY, 0, face());
    if (made) { f = made; dy = 7.5 * (1 - tall); }
    else { f = f_small; dy = 2.5; }
  }
  double left = align == TA_CENTER ? x - max_w / 2 : align == TA_RIGHT ? x - max_w : x;
  HRGN clip = CreateRectRgn(ui_ox + PX(left), ui_oy + PX(y - 2), ui_ox + PX(left + max_w), ui_oy + PX(y + 20));
  SelectClipRgn(dc, clip);
  ui_text(dc, f, x, y + dy, s, c, align);
  SelectClipRgn(dc, NULL);
  DeleteObject(clip);
  if (made) { SelectObject(dc, f_item); DeleteObject(made); }
}

// ---- the project's name, tall serif capitals with a larger initial, in white with a night-blue edge
static double title_path(HDC dc, double x, double base) {
  static const struct { const wchar_t *text; bool big; } part[] = {{L"" PROJECT_LOGO_FIRST, true}, {L"" PROJECT_LOGO_REST, false}};
  SetBkMode(dc, TRANSPARENT);
  SetTextAlign(dc, TA_LEFT | TA_BASELINE);
  SetTextCharacterExtra(dc, PX(2.2));
  int px = ui_ox + PX(x);
  BeginPath(dc);
  for (int k = 0; k < 2; k++) {
    SIZE sz;
    int n = lstrlenW(part[k].text);
    SelectObject(dc, part[k].big ? f_title : f_title_small);
    TextOutW(dc, px, ui_oy + PX(base), part[k].text, n);
    GetTextExtentPoint32W(dc, part[k].text, n, &sz);
    px += sz.cx;
  }
  EndPath(dc);
  SetTextCharacterExtra(dc, 0);
  return (px - ui_ox) / ui_s;
}

static void title_extra(HDC dc, double end, double base);

void ui_title(HDC dc, double x, double base) {
  static UiPic logo;
  if (!logo.tried && dl_start_done && theme() && theme()->logo) { logo.tried = true; logo.src = theme()->logo(&logo.sw, &logo.sh); }
  if (logo.src && logo.sw > 0 && logo.sh > 0) { // the game's logo, as tall as the name, and the subtitle beside it
    int bh = PX(54), bw = PX(320), w = (int)((s64)bh * logo.sw / logo.sh);
    if (w > bw) w = bw;
    pic_draw(dc, &logo, ui_ox + PX(x), ui_oy + PX(base - 38), w, bh);
    double end = x + w / ui_s - 25;
    title_extra(dc, end, base);
    return;
  }
  if (title_style() == DL_TITLE_FLAT) { // a shadow, the letters in the value colour, a rule of that colour
    HBRUSH shadow = CreateSolidBrush(ui_tint(RGB(4, 14, 60))), face = CreateSolidBrush(ui_value());
    HGDIOBJ ob = SelectObject(dc, shadow), op = SelectObject(dc, GetStockObject(NULL_PEN));
    title_path(dc, x + 2, base + 2);
    FillPath(dc);
    SelectObject(dc, face);
    double end = title_path(dc, x, base);
    FillPath(dc);
    SelectObject(dc, ob);
    SelectObject(dc, op);
    DeleteObject(shadow);
    DeleteObject(face);
    COLORREF v = ui_value(), fade = ui_tint(RGB(0x10, 0x38, 0xA0));
    TRIVERTEX r[2] = {{ui_ox + PX(x), ui_oy + PX(base + 6.5), GetRValue(v) << 8, GetGValue(v) << 8, GetBValue(v) << 8, 0},
                      {ui_ox + PX(end + 40), ui_oy + PX(base + 6.5) + (PX(1.5) < 1 ? 1 : PX(1.5)), GetRValue(fade) << 8, GetGValue(fade) << 8, GetBValue(fade) << 8, 0}};
    GRADIENT_RECT rr = {0, 1};
    GradientFill(dc, r, 2, &rr, 1, GRADIENT_FILL_RECT_H);
    title_extra(dc, end, base);
    return;
  }
  // soft shadow, then a dark edge, then the metal
  HBRUSH shade = CreateSolidBrush(ui_tint(RGB(4, 14, 60))), edge_fill = CreateSolidBrush(ui_tint(RGB(10, 30, 96)));
  HPEN edge = CreatePen(PS_SOLID, PX(3.2) < 2 ? 2 : PX(3.2), ui_tint(RGB(6, 22, 84)));
  HGDIOBJ old_brush = SelectObject(dc, shade), old_pen = SelectObject(dc, GetStockObject(NULL_PEN));
  title_path(dc, x + 2.2, base + 2.6);
  FillPath(dc);
  SelectObject(dc, edge);
  SelectObject(dc, edge_fill);
  title_path(dc, x, base);
  StrokeAndFillPath(dc);
  double end = title_path(dc, x, base);
  SelectClipPath(dc, RGN_COPY);
  // white, with a faint blue towards the foot of the letters
  int x0 = ui_ox + PX(x - 4), x1 = ui_ox + PX(end + 4), top = ui_oy + PX(base - 34), mid = ui_oy + PX(base - 13), bottom = ui_oy + PX(base + 4);
  COLORREF foot = ui_tint(RGB(0xC4, 0xDC, 0xFF));
  TRIVERTEX v[4] = {{x0, top, 0xFF00, 0xFF00, 0xFF00, 0}, {x1, mid, 0xFF00, 0xFF00, 0xFF00, 0},
                    {x0, mid, 0xFF00, 0xFF00, 0xFF00, 0}, {x1, bottom, GetRValue(foot) << 8, GetGValue(foot) << 8, GetBValue(foot) << 8, 0}};
  GRADIENT_RECT gr[2] = {{0, 1}, {2, 3}};
  GradientFill(dc, v, 4, gr, 2, GRADIENT_FILL_RECT_V);
  SelectClipRgn(dc, NULL);
  SelectObject(dc, old_brush);
  SelectObject(dc, old_pen);
  DeleteObject(shade);
  DeleteObject(edge_fill);
  DeleteObject(edge);
  // thin rule under the name, fading out to the right
  COLORREF fade = ui_tint(RGB(0x10, 0x38, 0xA0));
  TRIVERTEX r[2] = {{ui_ox + PX(x), ui_oy + PX(base + 6.5), 0xFF00, 0xFF00, 0xFF00, 0},
                    {ui_ox + PX(end + 40), ui_oy + PX(base + 6.5) + (PX(1.2) < 1 ? 1 : PX(1.2)), GetRValue(fade) << 8, GetGValue(fade) << 8, GetBValue(fade) << 8, 0}};
  GRADIENT_RECT rr = {0, 1};
  GradientFill(dc, r, 2, &rr, 1, GRADIENT_FILL_RECT_H);
  title_extra(dc, end, base);
}

// Beside the name: the subtitle in spaced capitals, and what the program is.
static void title_extra(HDC dc, double end, double base) {
  static HFONT f_sub1, f_sub2;
  static int sub_px = -1, sub_lang = -1;
  if (sub_px != font_px || sub_lang != dl_cfg.ui_lang) {
    if (f_sub1) DeleteObject(f_sub1);
    if (f_sub2) DeleteObject(f_sub2);
    bool jp = dl_cfg.ui_lang == 0;
    f_sub1 = CreateFontW(-PX(16), 0, 0, 0, FW_BOLD, 0, 0, 0, DEFAULT_CHARSET, 0, 0, ANTIALIASED_QUALITY, 0, title_face());
    f_sub2 = CreateFontW(-PX(13), 0, 0, 0, FW_NORMAL, jp ? 0 : 1, 0, 0, DEFAULT_CHARSET, 0, 0, ANTIALIASED_QUALITY, 0, jp ? L"Yu Mincho" : title_face());
    sub_px = font_px;
    sub_lang = dl_cfg.ui_lang;
  }
  static wchar_t caps[64];
  if (!caps[0]) { lstrcpynW(caps, L"" PROJECT_SUBTITLE, 64); CharUpperW(caps); }
  SetTextCharacterExtra(dc, PX(2.0));
  ui_text(dc, f_sub1, end + 46, base - 30, caps, ui_tint(RGB(4, 14, 60)), TA_LEFT);
  ui_text(dc, f_sub1, end + 45, base - 31, caps, RGB(255, 255, 255), TA_LEFT);
  SetTextCharacterExtra(dc, 0);
  if (dl_game && dl_game->subtitle) {
    const wchar_t *s = dl_tr(dl_game->subtitle);
    ui_text(dc, f_sub2, end + 46, base - 9, s, ui_tint(RGB(4, 14, 60)), TA_LEFT);
    ui_text(dc, f_sub2, end + 45, base - 10, s, ui_tint(RGB(214, 228, 255)), TA_LEFT);
  }
}

void ui_bar(HDC dc, double x0, double y, double x1, int value, int max) {
  double w = (x1 - x0 - 8) / max;
  for (int k = 0; k < max; k++)
    ui_rect(dc, x0 + 4 + k * w, y + 5, x0 + 4 + (k + 1) * w - 1.5, y + 13, k < value ? COL_VALUE : ui_tint(RGB(30, 56, 118)));
}

// A row of a page at height y (logical), h high. sw >= 0: a switch (0 off, 1 on); plain: a value that is
// only shown (no < > arrows); bar_max > 0: a bar.
void ui_row(HDC dc, double y, double h, bool sel, const wchar_t *label, const wchar_t *value, int sw, bool plain, int bar, int bar_max) {
  if (y + h > PANEL_BOTTOM + 1) ui_audit_note("row under the panel", label);
  if (sel) ui_cursor(dc, ROW_X0, y - 1, ROW_X1, y + h - 2);
  bool has_value = value || bar_max || sw >= 0;
  ui_text_fit(dc, ROW_X0 + 9, y, label, sel ? COL_SEL : ui_tint(RGB(240, 242, 255)), TA_LEFT,
              !has_value ? ROW_X1 - ROW_X0 - 16 : sw >= 0 ? ROW_X1 - ROW_X0 - 56 : VAL_X0 - ROW_X0 - 13); // a switch leaves more room
  if (!has_value) return;
  if (sw >= 0) { // a pill with a knob on the side of its state
    double x1 = VAL_X1 - 2, x0 = x1 - 30, cy = y + 8;
    ui_round(dc, x0, cy - 7, x1, cy + 7, sw ? ui_tint(RGB(240, 246, 255)) : ui_tint(RGB(44, 72, 140)), 7);
    double kx = sw ? x1 - 7 : x0 + 7;
    ui_round(dc, kx - 5, cy - 5, kx + 5, cy + 5, sw ? ui_tint(RGB(12, 40, 110)) : ui_tint(RGB(214, 228, 252)), 5);
    return;
  }
  ui_box(dc, VAL_X0, y, VAL_X1, y + h - 4, ui_tint(RGB(6, 18, 58)), 4);
  if (bar_max) ui_bar(dc, VAL_X0, y, VAL_X1, bar, bar_max);
  else ui_text_fit(dc, (VAL_X0 + VAL_X1) / 2, y, value, COL_VALUE, TA_CENTER, sel && !plain ? VAL_W - 30 : VAL_W - 4); // selected: room for < >
  if (sel && value && !bar_max && !plain) {
    ui_text(dc, f_item, VAL_X0 + 5, y, L"<", RGB(255, 255, 255), TA_LEFT);
    ui_text(dc, f_item, VAL_X1 - 5, y, L">", RGB(255, 255, 255), TA_RIGHT);
  }
}

// For the game's previews (dl.h).
void dl_ui_text(void *dc, double x, double y, const wchar_t *s, uint32_t rgb, int align, bool small) {
  ui_text(dc, small ? f_small : f_item, x, y, s, RGB(rgb >> 16, (rgb >> 8) & 255, rgb & 255), align == 1 ? TA_CENTER : align == 2 ? TA_RIGHT : TA_LEFT);
}
void dl_ui_box(void *dc, double x0, double y0, double x1, double y1, uint32_t rgb, double radius) {
  COLORREF c = RGB(rgb >> 16, (rgb >> 8) & 255, rgb & 255);
  if (radius > 0) ui_round(dc, x0, y0, x1, y1, c, radius);
  else ui_rect(dc, x0, y0, x1, y1, c);
}

// Icons of the navigation bar: the symbol font of Windows 10 and 11, or none.
HFONT ui_icons(HDC dc) {
  static HFONT f;
  static int px = -1, ok = -1;
  if (px != PX(13)) {
    if (f) DeleteObject(f);
    px = PX(13);
    f = CreateFontW(-px, 0, 0, 0, FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET, 0, 0, ANTIALIASED_QUALITY, 0, L"Segoe MDL2 Assets");
    wchar_t got[64] = L"";
    HGDIOBJ of = SelectObject(dc, f);
    GetTextFaceW(dc, 64, got);
    SelectObject(dc, of);
    ok = !lstrcmpiW(got, L"Segoe MDL2 Assets");
  }
  return ok > 0 ? f : NULL;
}

// ---- the backdrop: a deep blue sky over a sea of clouds, computed (layered value noise)
static double sky_cell(int x, int y) {
  u32 h = (u32)x * 374761393u + (u32)y * 668265263u;
  h = (h ^ (h >> 13)) * 1274126177u;
  return ((h ^ (h >> 16)) & 0xFFFF) / 65535.0;
}

static double sky_noise(double x, double y) {
  int ix = (int)floor(x), iy = (int)floor(y);
  double fx = x - ix, fy = y - iy;
  fx = fx * fx * (3 - 2 * fx);
  fy = fy * fy * (3 - 2 * fy);
  double a = sky_cell(ix, iy), b = sky_cell(ix + 1, iy), c = sky_cell(ix, iy + 1), d = sky_cell(ix + 1, iy + 1);
  return a + (b - a) * fx + (c - a) * fy + (a - b - c + d) * fx * fy;
}

static double sky_clouds(double x, double y) { // 0..1, four layers
  double sum = 0, amp = 0.5;
  for (int k = 0; k < 4; k++) { sum += sky_noise(x, y) * amp; x = x * 2.03 + 17.1; y = y * 2.03 + 5.3; amp *= 0.5; }
  return sum / 0.9375;
}

static double sky_step(double lo, double hi, double v) {
  double t = (v - lo) / (hi - lo);
  t = t < 0 ? 0 : t > 1 ? 1 : t;
  return t * t * (3 - 2 * t);
}

static void sky_pixel(double u, double v, double aspect, double *rgb) {
  static const double stops[5][4] = { // height in the sky (0 = top, 1 = horizon), colour
      {0.00, 4, 20, 92}, {0.45, 12, 52, 160}, {0.78, 44, 112, 216}, {0.93, 160, 200, 244}, {1.00, 236, 245, 255}};
  const double horizon = 0.60, haze[3] = {236, 245, 255};
  if (v < horizon) {
    double t = v / horizon;
    int k = 0;
    while (k < 3 && t > stops[k + 1][0]) k++;
    double f = (t - stops[k][0]) / (stops[k + 1][0] - stops[k][0]);
    for (int c = 0; c < 3; c++) rgb[c] = stops[k][c + 1] + (stops[k + 1][c + 1] - stops[k][c + 1]) * f;
    // long thin white clouds, denser and brighter towards the horizon
    double wisp = sky_step(0.50, 0.78, sky_clouds(u * aspect * 2.6, v * 14.0)) * (0.22 + 0.74 * t);
    static const double lit[3] = {246, 250, 255};
    for (int c = 0; c < 3; c++) rgb[c] += (lit[c] - rgb[c]) * wisp;
  } else {
    // the cloud sea: the same noise laid flat and seen in perspective, fading into the haze
    double p = (v - horizon) / (1 - horizon), z = 1 / (p + 0.055);
    double n = sky_clouds((u - 0.5) * aspect * z * 0.55 + 40, z * 0.9);
    double crest = sky_step(0.38, 0.70, n);
    static const double deep[3] = {10, 52, 164}, mid[3] = {78, 142, 228}, top[3] = {250, 252, 255};
    for (int c = 0; c < 3; c++) {
      double base = deep[c] + (mid[c] - deep[c]) * sky_step(0.25, 0.55, n);
      rgb[c] = base + (top[c] - base) * crest;
    }
    double fog = exp(-p * 7.5);
    for (int c = 0; c < 3; c++) rgb[c] += (haze[c] - rgb[c]) * fog;
    double dark = 1 - 0.38 * p * p; // heavier towards the bottom edge
    for (int c = 0; c < 3; c++) rgb[c] *= dark;
  }
}

// The other skies of DlTheme.sky (the same as ps_sky in dl_gpu.c, still: the graphics card animates them).
static void sky_other(int style, double u, double v, double aspect, double *rgb) {
  static const double c0[3] = {4, 20, 92}, c1[3] = {12, 52, 160}, c2[3] = {44, 112, 216}, c3[3] = {160, 200, 244};
  if (style == DL_SKY_GRADIENT) { // deep at the top, lighter below, a glow in the upper right
    double t = pow(v, 1.3), dx = (u - 0.8) * aspect, dy = v - 0.1, glow = 0.25 * exp(-(dx * dx + dy * dy) * 3);
    for (int c = 0; c < 3; c++) rgb[c] = c0[c] + (c2[c] - c0[c]) * t + c3[c] * glow;
    return;
  }
  // a starry night: a dark gradient, faint clouds of light, stars on a grid of cells
  double neb = sky_step(0.45, 0.80, sky_clouds(u * aspect * 3, v * 3)) * 0.25;
  for (int c = 0; c < 3; c++) {
    double b = c0[c] * 0.5 + (c1[c] * 0.7 - c0[c] * 0.5) * v;
    rgb[c] = b + (c2[c] - b) * neb;
  }
  double gx = u * aspect * 90, gy = v * 90;
  int ix = (int)floor(gx), iy = (int)floor(gy);
  double r = sky_cell(ix + 7, iy + 7);
  if (r > 0.93) {
    double sx = gx - ix - 0.5 - (sky_cell(ix + 11, iy + 11) - 0.5) * 0.6, sy = gy - iy - 0.5 - (sky_cell(ix + 23, iy + 23) - 0.5) * 0.6;
    double lit = (r - 0.93) / 0.07 * (0.75 + 0.25 * sin(r * 50)) * sky_step(0.18, 0.0, sqrt(sx * sx + sy * sy));
    for (int c = 0; c < 3; c++) rgb[c] += (255 - rgb[c]) * lit;
  }
}

// The player's own backdrop instead of the sky: launcher_bg.png (or .jpg, .bmp, .gif) next to the
// program; otherwise the theme's. Scaled to cover the window with its proportions kept (made again when
// the size changes). False when there is none.
bool backdrop_draw(HDC dc, int cw, int ch) {
  static bool tried, theme_tried;
  static HBITMAP pic;
  static int iw, ih;
  if (!tried) {
    tried = true;
    static const char *const names[] = {"launcher_bg.png", "launcher_bg.jpg", "launcher_bg.bmp", "launcher_bg.gif"};
    for (int k = 0; k < 4 && !pic; k++) {
      char p[MAX_PATH];
      snprintf(p, sizeof p, "%s%s", dl_base_dir, names[k]);
      if (GetFileAttributesA(p) != INVALID_FILE_ATTRIBUTES) pic = dl_picture_load(p, &iw, &ih);
    }
  }
  if (!pic && !theme_tried && dl_start_done && theme() && theme()->backdrop) {
    theme_tried = true;
    const u32 *src = theme()->backdrop(&iw, &ih);
    if (src && iw > 0 && ih > 0) {
      BITMAPINFO bi = {{sizeof(BITMAPINFOHEADER), iw, -ih, 1, 32}};
      void *bits = NULL;
      pic = CreateDIBSection(dc, &bi, DIB_RGB_COLORS, &bits, NULL, 0);
      if (pic) memcpy(bits, src, (size_t)iw * ih * 4);
    }
  }
  if (!pic || iw <= 0 || ih <= 0) return false;
  static HDC cache, src;
  static HBITMAP cache_bmp;
  static int cache_w, cache_h;
  if (!cache) { cache = CreateCompatibleDC(dc); src = CreateCompatibleDC(dc); SelectObject(src, pic); }
  if (cw != cache_w || ch != cache_h) {
    HBITMAP nb = CreateCompatibleBitmap(dc, cw, ch);
    SelectObject(cache, nb);
    if (cache_bmp) DeleteObject(cache_bmp);
    cache_bmp = nb;
    cache_w = cw;
    cache_h = ch;
    int sw = iw, sh = (int)((s64)iw * ch / cw); // "cover": the picture cut to the window's proportions
    if (sh > ih) { sh = ih; sw = (int)((s64)ih * cw / ch); }
    SetStretchBltMode(cache, HALFTONE);
    SetBrushOrgEx(cache, 0, 0, NULL);
    StretchBlt(cache, 0, 0, cw, ch, src, (iw - sw) / 2, (ih - sh) / 2, sw, sh, SRCCOPY);
  }
  BitBlt(dc, 0, 0, cw, ch, cache, 0, 0, SRCCOPY);
  return true;
}

// Computed at half the window's size when that size changes, then kept.
void sky_draw(HDC dc, int cw, int ch) {
  static HDC cache;
  static HBITMAP cache_bmp;
  static int cache_w, cache_h;
  if (!cache) cache = CreateCompatibleDC(dc);
  if (cw != cache_w || ch != cache_h) {
    int w = cw / 2 < 320 ? 320 : cw / 2, h = ch / 2 < 200 ? 200 : ch / 2;
    u32 *px = malloc((size_t)w * (size_t)h * 4);
    HBITMAP nb = CreateCompatibleBitmap(dc, cw, ch);
    SelectObject(cache, nb);
    if (cache_bmp) DeleteObject(cache_bmp);
    cache_bmp = nb;
    cache_w = cw;
    cache_h = ch;
    if (px) {
      double aspect = (double)w / h;
      for (int y = 0; y < h; y++)
        for (int x = 0; x < w; x++) {
          double rgb[3], u = (x + 0.5) / w, v = (y + 0.5) / h, grain = -1;
          int st = ui_sky_style();
          if (st == DL_SKY_PIXEL) { // blocks 1/120 of the height, few colours with an ordered dither
            double nx = aspect * 120;
            int bx = (int)(u * nx), by = (int)(v * 120);
            u = (bx + 0.5) / nx;
            v = (by + 0.5) / 120;
            grain = (((bx & 1) * 2 + (by & 1) * 3) % 4 + 0.5) / 4;
          }
          if (st == DL_SKY_GRADIENT || st == DL_SKY_STARS) sky_other(st, u, v, aspect, rgb);
          else sky_pixel(u, v, aspect, rgb);
          if (grain >= 0)
            for (int k = 0; k < 3; k++) rgb[k] = floor(rgb[k] / 255 * 10 + grain) / 10 * 255;
          if (theme_colours()) tint3(rgb);
          u32 c = 0;
          for (int k = 0; k < 3; k++) { int q = (int)(rgb[k] + 0.5); c = (c << 8) | (u32)(q < 0 ? 0 : q > 255 ? 255 : q); }
          px[(size_t)y * (size_t)w + (size_t)x] = c;
        }
      BITMAPINFO bi = {0};
      bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
      bi.bmiHeader.biWidth = w;
      bi.bmiHeader.biHeight = -h;
      bi.bmiHeader.biPlanes = 1;
      bi.bmiHeader.biBitCount = 32;
      SetStretchBltMode(cache, HALFTONE);
      SetBrushOrgEx(cache, 0, 0, NULL);
      StretchDIBits(cache, 0, 0, cw, ch, 0, 0, w, h, px, &bi, DIB_RGB_COLORS, SRCCOPY);
      free(px);
    }
  }
  BitBlt(dc, 0, 0, cw, ch, cache, 0, 0, SRCCOPY);
}
