// The part of GDI DarkLauncher's setup screen draws with (linux/win32.h), drawn here into pixels for Linux:
// brushes, pens, rounded rectangles, ellipses, polygons and lines with smooth edges, pictures copied,
// stretched or blended, gradients, clipping by rectangles, rounded rectangles and paths, and text through
// FreeType with the fonts fontconfig finds (Noto for Japanese). Not the whole of GDI: what the shared
// code calls, the way it calls it.
#include "win32.h"
#include <math.h>
#include <stdio.h>
#include <strings.h>
#include <sys/stat.h>
#include <fontconfig/fontconfig.h>
#include <ft2build.h>
#include FT_FREETYPE_H
#include FT_SYNTHESIS_H
#include FT_TRUETYPE_TABLES_H

enum { OBJ_BRUSH = 1, OBJ_PEN, OBJ_FONT, OBJ_BITMAP, OBJ_RGN };
#define FALLBACKS 3

bool dl_icon_known(unsigned code); // dl_icons.c: Segoe MDL2 Assets' icons, drawn
bool dl_icon_draw(unsigned code, int x, int y, int size, void (*plot)(int px, int py, int a, void *ctx), void *ctx);

struct GdiObj {
  int kind;
  bool stock;
  COLORREF color; // brush, pen
  bool none;      // NULL_BRUSH, NULL_PEN
  int width;      // pen
  uint32_t *bits; // bitmap: 0x00RRGGBB (or premultiplied 0xAARRGGBB), rows from the top
  int w, h;
  bool bottom_up;
  int x0, y0, x1, y1, corner; // region: a rectangle, or a rounded one (corner: its corners' size)
  FT_Face face, fall[FALLBACKS]; // font: its face, then faces for the characters it lacks
  bool fall_tried;
  const struct Palt *palt[1 + FALLBACKS]; // the proportional widths of face and fall[] (NULL: none)
  bool palt_tried[1 + FALLBACKS];
  int em, ascent, descent, avg;
  double sx;                     // horizontal scale (a width asked for), 1 otherwise
  bool bold, italic, fake_bold, fake_italic, icons;
  wchar_t name[64];
  struct Glyph *glyphs;          // font: the glyphs rendered so far (glyph_of)
};

struct GdiDc {
  HGDIOBJ bitmap, brush, pen, font;
  COLORREF text;
  int bk, align, extra, stretch;
  POINT pos;
  uint8_t *clip;    // coverage of each pixel of the bitmap (0-255), NULL: no clipping (clip_mem, or a path's)
  uint8_t *clip_mem; // the regions' coverage, kept from one region to the next (8 MB at 4K, made once)
  size_t clip_cap;
  int clip_w, cx0, cy0, cx1, cy1; // the bitmap's width it was made for, and the box the last region wrote
  uint8_t *path;    // the path being made (BeginPath ... EndPath), as coverage
  bool in_path;
  int px0, py0, px1, py1;
};

static struct GdiObj stock_white = {OBJ_BRUSH, true, RGB(255, 255, 255)}, stock_black = {OBJ_BRUSH, true, RGB(0, 0, 0)},
                     stock_null_brush = {OBJ_BRUSH, true, 0, true}, stock_null_pen = {OBJ_PEN, true, 0, true, 1},
                     stock_pen = {OBJ_PEN, true, RGB(0, 0, 0), false, 1};
static uint32_t one_px;
static struct GdiObj stock_bitmap = {OBJ_BITMAP, true, 0, false, 0, &one_px, 1, 1};

HGDIOBJ GetStockObject(int which) {
  switch (which) {
    case WHITE_BRUSH: return &stock_white;
    case BLACK_BRUSH: return &stock_black;
    case NULL_BRUSH: return &stock_null_brush;
    case NULL_PEN: return &stock_null_pen;
  }
  return NULL;
}

// ---- device contexts and objects
HDC CreateCompatibleDC(HDC dc) {
  (void)dc;
  HDC d = calloc(1, sizeof *d);
  d->bitmap = &stock_bitmap;
  d->brush = &stock_white;
  d->pen = &stock_pen;
  d->align = TA_LEFT | TA_TOP;
  d->bk = OPAQUE;
  d->stretch = COLORONCOLOR;
  return d;
}

// The clipping taken off (a path's freed; the regions' kept for the next one).
static void clip_drop(HDC dc) {
  if (dc->clip && dc->clip != dc->clip_mem) free(dc->clip);
  dc->clip = NULL;
}

BOOL DeleteDC(HDC dc) {
  if (!dc) return FALSE;
  clip_drop(dc);
  free(dc->clip_mem);
  free(dc->path);
  free(dc);
  return TRUE;
}


// A glyph as FreeType rendered it, kept by its font: the menus draw the same letters at every move, and
// rendering them again each time (with their widths measured first) was most of the text's time at 4K.
typedef struct Glyph {
  unsigned code;     // the character + 1 (0: a free place)
  bool ok;           // FreeType could render it
  int left, top, w, rows;
  double dx, adv;    // proportional adjustment of its place, and its advance
  uint8_t *bits;     // w x rows coverage
} Glyph;
#define GLYPHS 1024 // places of a font's table (it starts again when full)
static void glyphs_free(HGDIOBJ f) {
  if (!f->glyphs) return;
  for (int k = 0; k < GLYPHS; k++) free(f->glyphs[k].bits);
  free(f->glyphs);
  f->glyphs = NULL;
}

static HGDIOBJ obj_new(int kind) {
  HGDIOBJ o = calloc(1, sizeof *o);
  o->kind = kind;
  return o;
}

HGDIOBJ SelectObject(HDC dc, HGDIOBJ o) {
  if (!dc || !o) return NULL;
  HGDIOBJ old = NULL;
  switch (o->kind) {
    case OBJ_BRUSH: old = dc->brush; dc->brush = o; break;
    case OBJ_PEN: old = dc->pen; dc->pen = o; break;
    case OBJ_FONT: old = dc->font; dc->font = o; break;
    case OBJ_BITMAP:
      old = dc->bitmap;
      dc->bitmap = o;
      clip_drop(dc); // the clipping was for the other picture's size
      break;
  }
  return old;
}

BOOL DeleteObject(HGDIOBJ o) {
  if (!o || o->stock) return FALSE;
  if (o->kind == OBJ_BITMAP) free(o->bits);
  if (o->kind == OBJ_FONT) {
    if (o->face) FT_Done_Face(o->face);
    for (int k = 0; k < FALLBACKS; k++)
      if (o->fall[k]) FT_Done_Face(o->fall[k]);
    glyphs_free(o);
  }
  free(o);
  return TRUE;
}

int GetObjectA(HGDIOBJ o, int size, void *out) {
  if (!o || o->kind != OBJ_BITMAP || size < (int)sizeof(BITMAP)) return 0;
  BITMAP *b = out;
  memset(b, 0, sizeof *b);
  b->bmWidth = o->w;
  b->bmHeight = o->h;
  b->bmWidthBytes = o->w * 4;
  b->bmPlanes = 1;
  b->bmBitsPixel = 32;
  b->bmBits = o->bits;
  return (int)sizeof(BITMAP);
}

HBRUSH CreateSolidBrush(COLORREF c) {
  HGDIOBJ o = obj_new(OBJ_BRUSH);
  o->color = c;
  return o;
}

HPEN CreatePen(int style, int width, COLORREF c) {
  (void)style;
  HGDIOBJ o = obj_new(OBJ_PEN);
  o->color = c;
  o->width = width < 1 ? 1 : width;
  return o;
}

static HBITMAP bitmap_new(int w, int h) {
  HGDIOBJ o = obj_new(OBJ_BITMAP);
  o->w = w < 1 ? 1 : w;
  o->h = h < 1 ? 1 : h;
  o->bits = calloc((size_t)o->w * o->h, 4);
  return o;
}

HBITMAP CreateDIBSection(HDC dc, const BITMAPINFO *bi, UINT usage, void **bits, HANDLE section, DWORD offset) {
  (void)dc, (void)usage, (void)section, (void)offset;
  int h = bi->bmiHeader.biHeight;
  HBITMAP b = bitmap_new(bi->bmiHeader.biWidth, h < 0 ? -h : h);
  b->bottom_up = h > 0;
  if (bits) *bits = b->bits;
  return b;
}

HBITMAP CreateCompatibleBitmap(HDC dc, int w, int h) {
  (void)dc;
  return bitmap_new(w, h);
}

HBITMAP CreateBitmap(int w, int h, UINT planes, UINT bits, const void *data) {
  (void)planes, (void)bits, (void)data;
  return bitmap_new(w, h);
}

uint32_t *dl_gdi_bits(HDC dc, int *w, int *h) {
  *w = dc->bitmap->w;
  *h = dc->bitmap->h;
  return dc->bitmap->bits;
}

// ---- pixels
static inline uint32_t *target(HDC dc, int *w, int *h) {
  *w = dc->bitmap->w;
  *h = dc->bitmap->h;
  return dc->bitmap->bits;
}

// The setup screen drawn as a layer for the graphics card in one pass (dl_gl.c's menu_layer): while
// dl_gdi_alpha is set, the drawing into the setup screen's bitmap (dl_menu_bits) keeps each pixel's
// opacity in its top byte, its colours premultiplied, from the stock black brush that clears it to nothing
// on. Windows draws the layer twice, on black and on white, and works the opacity out from the difference;
// here the drawing is DarkLauncher's own, so it can tell, and the second pass is not needed (at 4K each
// pass of the menus took tens of milliseconds). Elsewhere the top byte stays 0, as before.
bool dl_gdi_alpha;
extern uint32_t *dl_menu_bits __attribute__((weak)); // (dl_menu.c's; NULL in a program without it: canvas_test)
static inline bool alpha_on(HDC dc) { return dl_gdi_alpha && &dl_menu_bits && dc->bitmap->bits == dl_menu_bits; }
static inline uint32_t alpha_over(uint32_t d, int a) { // d's opacity once covered at a (0-255)
  int o = (int)(d >> 24);
  return (uint32_t)(o + (255 - o) * a / 255) << 24;
}

// Colour cr, cg, cb over pixel x, y of a w x h picture with coverage a (0-255), through the clipping;
// alpha: its opacity kept too (dl_gdi_alpha).
static inline void put_px(uint32_t *px, int w, int h, const uint8_t *clip, bool alpha, int x, int y, int cr, int cg, int cb, int a) {
  if (x < 0 || y < 0 || x >= w || y >= h || a <= 0) return;
  size_t i = (size_t)y * w + x;
  if (clip) a = a * clip[i] / 255;
  if (a <= 0) return;
  if (a > 255) a = 255;
  uint32_t d = px[i];
  int r = d >> 16 & 255, g = d >> 8 & 255, b = d & 255;
  r += (cr - r) * a / 255;
  g += (cg - g) * a / 255;
  b += (cb - b) * a / 255;
  px[i] = (uint32_t)r << 16 | (uint32_t)g << 8 | (uint32_t)b | (alpha ? alpha_over(d, a) : 0);
}

// Colour c over pixel x, y with coverage a (0-255), through the clipping.
static inline void put(HDC dc, int x, int y, COLORREF c, int a) {
  int w, h;
  uint32_t *px = target(dc, &w, &h);
  put_px(px, w, h, dc->clip, alpha_on(dc), x, y, GetRValue(c), GetGValue(c), GetBValue(c), a);
}

static inline int cover(double v) { return v <= 0 ? 0 : v >= 1 ? 255 : (int)(v * 255 + 0.5); }

// Colour c (0xRRGGBB) over the pixels x0..x1-1 of row y at coverage a, through the clipping: what put()
// does, a row at once (the menus at 4K are millions of pixels: put() for each was most of their time).
static void span(HDC dc, int y, int x0, int x1, uint32_t c, int a) {
  int w, h;
  uint32_t *px = target(dc, &w, &h);
  if (y < 0 || y >= h || a <= 0) return;
  if (x0 < 0) x0 = 0;
  if (x1 > w) x1 = w;
  uint32_t *row = px + (size_t)y * w;
  const uint8_t *clip = dc->clip ? dc->clip + (size_t)y * w : NULL;
  int cr = c >> 16 & 255, cg = c >> 8 & 255, cb = c & 255;
  bool alpha = alpha_on(dc);
  uint32_t opaque = alpha ? 0xFF000000u : 0;
  for (int x = x0; x < x1; x++) {
    int k = clip ? a * clip[x] / 255 : a;
    if (k <= 0) continue;
    if (k >= 255) { row[x] = c | opaque; continue; }
    uint32_t d = row[x];
    int r = d >> 16 & 255, g = d >> 8 & 255, b = d & 255;
    r += (cr - r) * k / 255;
    g += (cg - g) * k / 255;
    b += (cb - b) * k / 255;
    row[x] = (uint32_t)r << 16 | (uint32_t)g << 8 | (uint32_t)b | (alpha ? alpha_over(d, k) : 0);
  }
}
static inline uint32_t rgb_of(COLORREF c) { return (uint32_t)GetRValue(c) << 16 | (uint32_t)GetGValue(c) << 8 | GetBValue(c); }

// Distance from p to a rectangle of half sizes hx, hy with round corners r, centred on 0 (negative inside).
static double round_box(double px, double py, double hx, double hy, double r) {
  if (r > hx) r = hx;
  if (r > hy) r = hy;
  double qx = fabs(px) - hx + r, qy = fabs(py) - hy + r;
  double ox = qx > 0 ? qx : 0, oy = qy > 0 ? qy : 0;
  return sqrt(ox * ox + oy * oy) + (qx > qy ? (qx < 0 ? qx : 0) : (qy < 0 ? qy : 0)) - r;
}

// A shape given by its distance (negative inside) over the box x0,y0 - x1,y1: filled with the brush,
// then its edge drawn with the pen inside it.
typedef double (*DistFn)(double x, double y, const void *shape);
static void shape(HDC dc, int x0, int y0, int x1, int y1, DistFn dist, const void *s) {
  HGDIOBJ b = dc->brush, p = dc->pen;
  double pw = p && !p->none ? p->width : 0;
  for (int y = y0 - 1; y <= y1; y++)
    for (int x = x0 - 1; x <= x1; x++) {
      double d = dist(x + 0.5, y + 0.5, s);
      if (d > 1) continue;
      if (b && !b->none) put(dc, x, y, b->color, cover(0.5 - d));
      if (pw > 0) put(dc, x, y, p->color, cover(0.5 - d) - cover(0.5 - d - pw));
    }
}

typedef struct { double cx, cy, hx, hy, r; } BoxShape;
static double box_dist(double x, double y, const void *s) {
  const BoxShape *b = s;
  return round_box(x - b->cx, y - b->cy, b->hx, b->hy, b->r);
}
typedef struct { double cx, cy, rx, ry; } EllipseShape;
static double ellipse_dist(double x, double y, const void *s) {
  const EllipseShape *e = s;
  double nx = (x - e->cx) / e->rx, ny = (y - e->cy) / e->ry, k = sqrt(nx * nx + ny * ny);
  return (k - 1) * (e->rx < e->ry ? e->rx : e->ry);
}

BOOL RoundRect(HDC dc, int x0, int y0, int x1, int y1, int w, int h) {
  BoxShape s = {(x0 + x1) / 2.0, (y0 + y1) / 2.0, (x1 - x0) / 2.0, (y1 - y0) / 2.0, (w < h ? w : h) / 2.0};
  // as shape(), but the pixels further than the corners and the pen from every edge are known at once:
  // inside by more than the pen and a pixel, so the brush covers them wholly and the pen not at all
  HGDIOBJ b = dc->brush, p = dc->pen;
  double pw = p && !p->none ? p->width : 0;
  int m = (int)ceil((s.r < s.hx && s.r < s.hy ? s.r : (s.hx < s.hy ? s.hx : s.hy)) + pw) + 2;
  bool brush = b && !b->none;
  // Away from the corners the distance follows one edge only: in a row's middle (x0 + m to x1 - m) it is
  // the same for every pixel of the row, and in a column's middle the same for every row; where it would
  // differ, the pixel is deep inside (brush wholly, pen not at all). So each is worked out once, as box_dist
  // gives it, and only the corners pixel by pixel. The pixels are the same as shape()'s.
  int band = m + 1;
  double side[2 * 160 + 4];                 // the distance of the columns near the left and right edges, in the middle rows
  bool sides = band <= 160 && x1 - m > x0 + m && y1 - m > y0 + m;
  if (sides)
    for (int i = 0; i < band; i++) {
      side[i] = box_dist(x0 - 1 + i + 0.5, (y0 + y1) / 2 + 0.5, &s);      // x0 - 1 ... x0 + m - 1
      side[band + i] = box_dist(x1 - m + i + 0.5, (y0 + y1) / 2 + 0.5, &s); // x1 - m ... x1
    }
  for (int y = y0 - 1; y <= y1; y++) {
    bool mid = y >= y0 + m && y < y1 - m;
    for (int x = x0 - 1; x <= x1; x++) {
      if (x == x0 + m && x1 - m > x) { // the middle of this row at once
        if (mid) {
          if (brush) span(dc, y, x, x1 - m, rgb_of(b->color), 255);
        } else {
          double d = box_dist(x + 0.5, y + 0.5, &s);
          if (d <= 1) {
            if (brush) span(dc, y, x, x1 - m, rgb_of(b->color), cover(0.5 - d));
            if (pw > 0) span(dc, y, x, x1 - m, rgb_of(p->color), cover(0.5 - d) - cover(0.5 - d - pw));
          }
        }
        x = x1 - m - 1;
        continue;
      }
      double d = mid && sides ? side[x < x0 + m ? x - (x0 - 1) : band + x - (x1 - m)] : box_dist(x + 0.5, y + 0.5, &s);
      if (d > 1) continue;
      if (brush) put(dc, x, y, b->color, cover(0.5 - d));
      if (pw > 0) put(dc, x, y, p->color, cover(0.5 - d) - cover(0.5 - d - pw));
    }
  }
  return TRUE;
}

BOOL Rectangle(HDC dc, int x0, int y0, int x1, int y1) { return RoundRect(dc, x0, y0, x1, y1, 0, 0); }

BOOL Ellipse(HDC dc, int x0, int y0, int x1, int y1) {
  EllipseShape s = {(x0 + x1) / 2.0, (y0 + y1) / 2.0, (x1 - x0) / 2.0, (y1 - y0) / 2.0};
  if (s.rx <= 0 || s.ry <= 0) return FALSE;
  shape(dc, x0, y0, x1, y1, ellipse_dist, &s);
  return TRUE;
}

int FillRect(HDC dc, const RECT *r, HBRUSH b) {
  if (!b || b->none) return 1;
  if (b == &stock_black && alpha_on(dc)) { // the layer's ground: nothing (see dl_gdi_alpha)
    int w, h;
    uint32_t *px = target(dc, &w, &h);
    int x0 = r->left < 0 ? 0 : r->left, x1 = r->right > w ? w : r->right;
    for (int y = r->top < 0 ? 0 : r->top; y < r->bottom && y < h && x1 > x0; y++) memset(px + (size_t)y * w + x0, 0, (size_t)(x1 - x0) * 4);
    return 1;
  }
  for (int y = r->top; y < r->bottom; y++) span(dc, y, r->left, r->right, rgb_of(b->color), 255);
  return 1;
}

// Distance from a point to the segment a-b.
static double seg_dist(double px, double py, double ax, double ay, double bx, double by) {
  double dx = bx - ax, dy = by - ay, l = dx * dx + dy * dy, t = l > 0 ? ((px - ax) * dx + (py - ay) * dy) / l : 0;
  t = t < 0 ? 0 : t > 1 ? 1 : t;
  double ex = ax + t * dx - px, ey = ay + t * dy - py;
  return sqrt(ex * ex + ey * ey);
}

static void stroke_segment(HDC dc, double ax, double ay, double bx, double by) {
  HGDIOBJ p = dc->pen;
  if (!p || p->none) return;
  double hw = p->width / 2.0;
  int x0 = (int)floor((ax < bx ? ax : bx) - hw - 1), x1 = (int)ceil((ax > bx ? ax : bx) + hw + 1);
  int y0 = (int)floor((ay < by ? ay : by) - hw - 1), y1 = (int)ceil((ay > by ? ay : by) + hw + 1);
  for (int y = y0; y <= y1; y++)
    for (int x = x0; x <= x1; x++) put(dc, x, y, p->color, cover(hw + 0.5 - seg_dist(x + 0.5, y + 0.5, ax, ay, bx, by)));
}

BOOL MoveToEx(HDC dc, int x, int y, POINT *old) {
  if (old) *old = dc->pos;
  dc->pos.x = x;
  dc->pos.y = y;
  return TRUE;
}

BOOL LineTo(HDC dc, int x, int y) {
  stroke_segment(dc, dc->pos.x + 0.5, dc->pos.y + 0.5, x + 0.5, y + 0.5);
  dc->pos.x = x;
  dc->pos.y = y;
  return TRUE;
}

// Filled with the brush (each pixel sampled 4 x 4 times, even-odd rule), then its edges with the pen.
BOOL Polygon(HDC dc, const POINT *p, int n) {
  if (n < 3) return FALSE;
  int x0 = p[0].x, x1 = p[0].x, y0 = p[0].y, y1 = p[0].y;
  for (int k = 1; k < n; k++) {
    if (p[k].x < x0) x0 = p[k].x;
    if (p[k].x > x1) x1 = p[k].x;
    if (p[k].y < y0) y0 = p[k].y;
    if (p[k].y > y1) y1 = p[k].y;
  }
  if (dc->brush && !dc->brush->none)
    for (int y = y0; y <= y1; y++)
      for (int x = x0; x <= x1; x++) {
        int in = 0;
        for (int sy = 0; sy < 4; sy++)
          for (int sx = 0; sx < 4; sx++) {
            double tx = x + (sx + 0.5) / 4, ty = y + (sy + 0.5) / 4;
            bool inside = false;
            for (int i = 0, j = n - 1; i < n; j = i++)
              if ((p[i].y > ty) != (p[j].y > ty) && tx < (double)(p[j].x - p[i].x) * (ty - p[i].y) / (p[j].y - p[i].y) + p[i].x) inside = !inside;
            in += inside;
          }
        put(dc, x, y, dc->brush->color, in * 255 / 16);
      }
  for (int i = 0, j = n - 1; i < n; j = i++) stroke_segment(dc, p[j].x, p[j].y, p[i].x, p[i].y);
  return TRUE;
}

// ---- clipping
HRGN CreateRectRgn(int x0, int y0, int x1, int y1) {
  HGDIOBJ o = obj_new(OBJ_RGN);
  o->x0 = x0, o->y0 = y0, o->x1 = x1, o->y1 = y1;
  return o;
}

HRGN CreateRoundRectRgn(int x0, int y0, int x1, int y1, int w, int h) {
  HRGN r = CreateRectRgn(x0, y0, x1, y1);
  r->corner = w < h ? w : h;
  return r;
}

int SelectClipRgn(HDC dc, HRGN r) {
  clip_drop(dc);
  if (!r) return 1;
  int w, h;
  target(dc, &w, &h);
  size_t need = (size_t)w * h;
  if (need > dc->clip_cap) { // made once for this size, all 0
    free(dc->clip_mem);
    dc->clip_mem = calloc(need, 1);
    dc->clip_cap = dc->clip_mem ? need : 0;
    dc->clip_w = w, dc->cx1 = dc->cx0, dc->cy1 = dc->cy0;
    if (!dc->clip_mem) return 0;
  } else if (dc->clip_w != w) { // another width: the rows are elsewhere
    memset(dc->clip_mem, 0, dc->clip_cap);
    dc->clip_w = w, dc->cx1 = dc->cx0, dc->cy1 = dc->cy0;
  } else // only what the last region wrote goes back to 0
    for (int y = dc->cy0; y < dc->cy1; y++) memset(dc->clip_mem + (size_t)y * w + dc->cx0, 0, (size_t)(dc->cx1 - dc->cx0));
  dc->clip = dc->clip_mem;
  dc->cx0 = r->x0 < 0 ? 0 : r->x0 > w ? w : r->x0, dc->cx1 = r->x1 < dc->cx0 ? dc->cx0 : r->x1 > w ? w : r->x1;
  dc->cy0 = r->y0 < 0 ? 0 : r->y0 > h ? h : r->y0, dc->cy1 = r->y1 < dc->cy0 ? dc->cy0 : r->y1 > h ? h : r->y1;
  BoxShape s = {(r->x0 + r->x1) / 2.0, (r->y0 + r->y1) / 2.0, (r->x1 - r->x0) / 2.0, (r->y1 - r->y0) / 2.0, r->corner / 2.0};
  // the distance only near the corners: elsewhere a pixel of the box is inside by half a pixel at least,
  // so wholly covered
  int m = (int)ceil(r->corner / 2.0) + 1;
  for (int y = r->y0 < 0 ? 0 : r->y0; y < r->y1 && y < h; y++) {
    bool corner_row = y < r->y0 + m || y >= r->y1 - m;
    uint8_t *row = dc->clip + (size_t)y * w;
    for (int x = r->x0 < 0 ? 0 : r->x0; x < r->x1 && x < w; x++) {
      if (!corner_row || (x >= r->x0 + m && x < r->x1 - m)) row[x] = 255;
      else row[x] = (uint8_t)cover(0.5 - box_dist(x + 0.5, y + 0.5, &s));
    }
  }
  return 1;
}

// ---- pictures
int SetStretchBltMode(HDC dc, int mode) {
  int old = dc->stretch;
  dc->stretch = mode;
  return old;
}
BOOL SetBrushOrgEx(HDC dc, int x, int y, POINT *old) { (void)dc, (void)x, (void)y, (void)old; return TRUE; }
BOOL GdiFlush(void) { return TRUE; }

// Source pixel (x, y) of a picture given by its pixels, clamped to its edges.
typedef struct { const uint32_t *px; int w, h; bool bottom_up; } Src;
static inline uint32_t src_at(const Src *s, int x, int y) {
  x = x < 0 ? 0 : x >= s->w ? s->w - 1 : x;
  y = y < 0 ? 0 : y >= s->h ? s->h - 1 : y;
  if (s->bottom_up) y = s->h - 1 - y;
  return s->px[(size_t)y * s->w + x];
}

// The area sx,sy sw x sh of the source into x,y w x h: pixel for pixel, or (HALFTONE) averaged when
// smaller and blended between neighbours when larger. alpha: per-pixel premultiplied opacity
// (AlphaBlend); konst: the opacity of the whole (255: opaque).
static void blit(HDC dc, int x, int y, int w, int h, const Src *s, int sx, int sy, int sw, int sh, bool smooth, bool alpha, int konst) {
  if (w <= 0 || h <= 0 || sw <= 0 || sh <= 0) return;
  int tw, th;
  uint32_t *px = target(dc, &tw, &th);
  bool layer = alpha_on(dc); // (the opacity kept in the top byte: see dl_gdi_alpha)
  if (!alpha && konst == 255 && !dc->clip && (!smooth || (sw == w && sh == h))) { // opaque, pixel for pixel: rows copied
    static int *cols, cols_n;
    if (w > cols_n) { free(cols); cols = malloc(sizeof *cols * (size_t)w); cols_n = w; }
    for (int i = 0; i < w; i++) {
      int c = sx + i * sw / w;
      cols[i] = c < 0 ? 0 : c >= s->w ? s->w - 1 : c;
    }
    int i0 = x < 0 ? -x : 0, i1 = x + w > tw ? tw - x : w;
    for (int j = 0; j < h; j++) {
      int ty = y + j, r = sy + j * sh / h;
      if (ty < 0 || ty >= th) continue;
      r = r < 0 ? 0 : r >= s->h ? s->h - 1 : r;
      const uint32_t *row = s->px + (size_t)(s->bottom_up ? s->h - 1 - r : r) * s->w;
      uint32_t *out = px + (size_t)ty * tw + x;
      uint32_t opaque = layer ? 0xFF000000u : 0;
      for (int i = i0; i < i1; i++) out[i] = (row[cols[i]] & 0xFFFFFF) | opaque;
    }
    return;
  }
  if (s->w == 1 && s->h == 1 && !alpha) { // one colour over the area (a panel letting the sky show through): the same
    uint32_t c = s->px[0];                // sums as below, without reading the source again for each pixel
    int sv[3] = {c & 255, c >> 8 & 255, c >> 16 & 255};
    uint8_t lut[3][256]; // each channel's result for each value under it (the same sums: one colour, one opacity)
    for (int k = 0; k < 3; k++)
      for (int dv = 0; dv < 256; dv++) lut[k][dv] = (uint8_t)(dv + (sv[k] - dv) * konst / 255);
    // over nothing yet (a layer's ground, see dl_gdi_alpha), wholly within the clipping: always the same
    uint32_t fresh = (uint32_t)lut[0][0] | (uint32_t)lut[1][0] << 8 | (uint32_t)lut[2][0] << 16 | (layer ? (uint32_t)(255 * konst / 255) << 24 : 0);
    for (int j = 0; j < h; j++) {
      int ty = y + j;
      if (ty < 0 || ty >= th) continue;
      int i0 = x < 0 ? -x : 0, i1 = x + w > tw ? tw - x : w;
      uint32_t *row = px + (size_t)ty * tw + x;
      const uint8_t *cl = dc->clip ? dc->clip + (size_t)ty * tw + x : NULL;
      for (int i = i0; i < i1; i++) {
        int clip = cl ? cl[i] : 255;
        if (!clip) continue;
        uint32_t o = row[i], out = 0;
        if (o == 0 && clip == 255) { row[i] = fresh; continue; }
        if (clip == 255) out = (uint32_t)lut[0][o & 255] | (uint32_t)lut[1][o >> 8 & 255] << 8 | (uint32_t)lut[2][o >> 16 & 255] << 16;
        else
          for (int k = 0; k < 3; k++) {
            int dv = o >> (k * 8) & 255, v = lut[k][dv];
            v = dv + (v - dv) * clip / 255;
            out |= (uint32_t)v << (k * 8);
          }
        if (layer) {
          int o_a = (int)(o >> 24), a1 = o_a + (255 - o_a) * konst / 255;
          out |= (uint32_t)(clip == 255 ? a1 : o_a + (a1 - o_a) * clip / 255) << 24;
        }
        row[i] = out;
      }
    }
    return;
  }
  for (int j = 0; j < h; j++) {
    int ty = y + j;
    if (ty < 0 || ty >= th) continue;
    for (int i = 0; i < w; i++) {
      int tx = x + i;
      if (tx < 0 || tx >= tw) continue;
      uint32_t c;
      if (!smooth || (sw == w && sh == h)) c = src_at(s, sx + i * sw / w, sy + j * sh / h);
      else if (sw > w || sh > h) { // smaller: the average of the source pixels it covers
        int ax = sx + i * sw / w, bx = sx + (i + 1) * sw / w, ay = sy + j * sh / h, by = sy + (j + 1) * sh / h;
        if (bx <= ax) bx = ax + 1;
        if (by <= ay) by = ay + 1;
        unsigned sum[4] = {0}, n = 0;
        for (int v = ay; v < by; v++)
          for (int u = ax; u < bx; u++, n++) {
            uint32_t p = src_at(s, u, v);
            for (int k = 0; k < 4; k++) sum[k] += p >> (k * 8) & 255;
          }
        c = sum[0] / n | sum[1] / n << 8 | sum[2] / n << 16 | sum[3] / n << 24;
      } else { // larger: between the four nearest
        double fx = sx + (i + 0.5) * sw / w - 0.5, fy = sy + (j + 0.5) * sh / h - 0.5;
        int ix = (int)floor(fx), iy = (int)floor(fy);
        double ux = fx - ix, uy = fy - iy;
        uint32_t a = src_at(s, ix, iy), b = src_at(s, ix + 1, iy), cc = src_at(s, ix, iy + 1), d = src_at(s, ix + 1, iy + 1);
        c = 0;
        for (int k = 0; k < 32; k += 8) {
          double v = (a >> k & 255) * (1 - ux) * (1 - uy) + (b >> k & 255) * ux * (1 - uy) + (cc >> k & 255) * (1 - ux) * uy + (d >> k & 255) * ux * uy;
          c |= (uint32_t)(v + 0.5) << k;
        }
      }
      size_t at = (size_t)ty * tw + tx;
      int clip = dc->clip ? dc->clip[at] : 255;
      if (!clip) continue;
      uint32_t o = px[at];
      int sa = alpha ? (int)(c >> 24) * konst / 255 : konst; // the source's opacity here
      uint32_t out = 0;
      for (int k = 0; k < 24; k += 8) {
        int sv = c >> k & 255, dv = o >> k & 255, v;
        if (alpha) v = sv * konst / 255 + dv * (255 - sa) / 255; // premultiplied
        else v = dv + (sv - dv) * sa / 255;
        v = dv + (v - dv) * clip / 255;
        out |= (uint32_t)(v < 0 ? 0 : v > 255 ? 255 : v) << k;
      }
      if (layer) { // the opacity, as the colours: the source's over what is there, then through the clipping
        int o_a = (int)(o >> 24), a1 = o_a + (255 - o_a) * sa / 255;
        out |= (uint32_t)(o_a + (a1 - o_a) * clip / 255) << 24;
      }
      px[at] = out;
    }
  }
}

static Src src_of(HDC dc) {
  HGDIOBJ b = dc->bitmap;
  return (Src){b->bits, b->w, b->h, false};
}

BOOL BitBlt(HDC dst, int x, int y, int w, int h, HDC src, int sx, int sy, DWORD rop) {
  (void)rop;
  Src s = src_of(src);
  blit(dst, x, y, w, h, &s, sx, sy, w, h, false, false, 255);
  return TRUE;
}

BOOL StretchBlt(HDC dst, int x, int y, int w, int h, HDC src, int sx, int sy, int sw, int sh, DWORD rop) {
  (void)rop;
  Src s = src_of(src);
  blit(dst, x, y, w, h, &s, sx, sy, sw, sh, dst->stretch == HALFTONE, false, 255);
  return TRUE;
}

int StretchDIBits(HDC dst, int x, int y, int w, int h, int sx, int sy, int sw, int sh, const void *bits, const BITMAPINFO *bi, UINT usage, DWORD rop) {
  (void)usage, (void)rop;
  int bh = bi->bmiHeader.biHeight;
  Src s = {bits, bi->bmiHeader.biWidth, bh < 0 ? -bh : bh, bh > 0};
  blit(dst, x, y, w, h, &s, sx, sy, sw, sh, dst->stretch == HALFTONE, false, 255);
  return h;
}

BOOL AlphaBlend(HDC dst, int x, int y, int w, int h, HDC src, int sx, int sy, int sw, int sh, BLENDFUNCTION bf) {
  Src s = src_of(src);
  blit(dst, x, y, w, h, &s, sx, sy, sw, sh, false, bf.AlphaFormat == AC_SRC_ALPHA, bf.SourceConstantAlpha);
  return TRUE;
}

// Rectangles each from one colour to another, from the top down (V) or left to right (H).
BOOL GradientFill(HDC dc, TRIVERTEX *v, ULONG nv, void *mesh, ULONG nm, ULONG mode) {
  const GRADIENT_RECT *m = mesh;
  for (ULONG k = 0; k < nm; k++) {
    if (m[k].UpperLeft >= nv || m[k].LowerRight >= nv) continue;
    const TRIVERTEX *a = &v[m[k].UpperLeft], *b = &v[m[k].LowerRight];
    int x0 = a->x < b->x ? a->x : b->x, x1 = a->x < b->x ? b->x : a->x, y0 = a->y < b->y ? a->y : b->y, y1 = a->y < b->y ? b->y : a->y;
    for (int y = y0; y < y1; y++)
      for (int x = x0; x < x1; x++) {
        double t = mode == GRADIENT_FILL_RECT_V ? (y - y0 + 0.5) / (y1 - y0) : (x - x0 + 0.5) / (x1 - x0);
        int r = (int)((a->Red + (b->Red - a->Red) * t) / 256), g = (int)((a->Green + (b->Green - a->Green) * t) / 256),
            bl = (int)((a->Blue + (b->Blue - a->Blue) * t) / 256);
        put(dc, x, y, RGB(r, g, bl), 255);
      }
  }
  return TRUE;
}

// ---- fonts: GDI's names turned into what fontconfig knows
static FT_Library ft;

static bool ft_ready(void) {
  static int state = -1;
  if (state < 0) state = !FT_Init_FreeType(&ft) && FcInit();
  return state;
}

static FT_Face face_for(const char *family, bool bold, bool italic, bool *got_bold, bool *got_italic) {
  FcPattern *p = FcPatternCreate();
  FcPatternAddString(p, FC_FAMILY, (const FcChar8 *)family);
  FcPatternAddInteger(p, FC_WEIGHT, bold ? FC_WEIGHT_BOLD : FC_WEIGHT_REGULAR);
  FcPatternAddInteger(p, FC_SLANT, italic ? FC_SLANT_ITALIC : FC_SLANT_ROMAN);
  FcConfigSubstitute(NULL, p, FcMatchPattern);
  FcDefaultSubstitute(p);
  FcResult res;
  FcPattern *m = FcFontMatch(NULL, p, &res);
  FcPatternDestroy(p);
  FT_Face f = NULL;
  FcChar8 *file;
  int index = 0, weight = FC_WEIGHT_REGULAR, slant = FC_SLANT_ROMAN;
  if (m && FcPatternGetString(m, FC_FILE, 0, &file) == FcResultMatch) {
    FcPatternGetInteger(m, FC_INDEX, 0, &index);
    FcPatternGetInteger(m, FC_WEIGHT, 0, &weight);
    FcPatternGetInteger(m, FC_SLANT, 0, &slant);
    if (FT_New_Face(ft, (const char *)file, index, &f)) f = NULL;
  }
  if (m) FcPatternDestroy(m);
  if (got_bold) *got_bold = weight >= FC_WEIGHT_DEMIBOLD;
  if (got_italic) *got_italic = slant != FC_SLANT_ROMAN;
  return f;
}

// Whether fontconfig has this very family (installed with the game, or Microsoft's fonts).
static bool has_family(const char *family) {
  FcPattern *p = FcPatternCreate();
  FcPatternAddString(p, FC_FAMILY, (const FcChar8 *)family);
  FcConfigSubstitute(NULL, p, FcMatchPattern);
  FcDefaultSubstitute(p);
  FcResult res;
  FcPattern *m = FcFontMatch(NULL, p, &res);
  FcPatternDestroy(p);
  FcChar8 *got;
  bool same = m && FcPatternGetString(m, FC_FAMILY, 0, &got) == FcResultMatch && !strcasecmp((const char *)got, family);
  if (m) FcPatternDestroy(m);
  return same;
}

// What stands in for Windows' faces Linux lacks: one of the same look, as wide as the Windows face, so
// that the texts fitted on Windows fit here too. The widths are a text in the seven languages in the
// Windows face over the same in the stand-in (Noto Sans, Noto Serif, Noto Sans Mono), regular and bold,
// measured with GDI and with this file. A wider face keeps the stand-in's width (the texts have room).
static const struct { const wchar_t *face; const char *family; float wide, wide_bold; } stand_ins[] = {
  {L"Verdana", "sans-serif", 1, 1}, {L"Lucida Sans Unicode", "sans-serif", 1, 1},
  {L"Tahoma", "sans-serif", 0.945f, 1}, {L"Segoe UI", "sans-serif", 0.931f, 0.942f},
  {L"Segoe UI Semibold", "sans-serif", 0.957f, 0.957f}, {L"Segoe UI Semilight", "sans-serif", 0.904f, 0.904f},
  {L"Arial", "sans-serif", 0.964f, 0.968f}, {L"Microsoft Sans Serif", "sans-serif", 0.964f, 0.930f},
  {L"Arial Narrow", "sans-serif", 0.79f, 0.79f}, {L"Calibri", "sans-serif", 0.867f, 0.834f},
  {L"Candara", "sans-serif", 0.896f, 0.856f}, {L"Corbel", "sans-serif", 0.882f, 0.871f},
  {L"Trebuchet MS", "sans-serif", 0.962f, 0.945f}, {L"Franklin Gothic Medium", "sans-serif", 0.917f, 0.885f},
  {L"Bahnschrift", "sans-serif", 0.956f, 0.902f}, {L"Bahnschrift SemiCondensed", "sans-serif", 0.822f, 0.776f},
  {L"Bahnschrift Condensed", "sans-serif", 0.703f, 0.663f},
  {L"Palatino Linotype", "serif", 0.937f, 0.901f}, {L"Georgia", "serif", 0.936f, 1},
  {L"Times New Roman", "serif", 0.856f, 0.853f}, {L"Constantia", "serif", 0.912f, 0.935f},
  {L"Cambria", "serif", 0.901f, 0.909f}, {L"Sylfaen", "serif", 0.884f, 0.848f},
  {L"Consolas", "monospace", 0.917f, 0.917f}, {L"Courier New", "monospace", 1, 1},
};

// Windows' faces turned into fontconfig's: the face itself when Linux has it, otherwise its stand-in
// (*wide: how much narrower it is drawn), guessed from the name for a face not in the list.
static const char *family_of(const wchar_t *face, bool bold, char own[128], bool *japanese, double *wide) {
  *japanese = false;
  *wide = 1;
  if (!face || !*face) return "sans-serif";
  if (!wcscmp(face, L"Yu Gothic UI")) { *japanese = true; return "Noto Sans CJK JP"; }
  if (!wcscmp(face, L"Yu Mincho")) { *japanese = true; return "Noto Serif CJK JP"; }
  if (snprintf(own, 128, "%ls", face) > 0 && has_family(own)) return own;
  for (size_t i = 0; i < sizeof stand_ins / sizeof *stand_ins; i++)
    if (!wcscmp(face, stand_ins[i].face)) {
      *wide = bold ? stand_ins[i].wide_bold : stand_ins[i].wide;
      return stand_ins[i].family;
    }
  if (wcsstr(face, L"Condensed")) *wide = wcsstr(face, L"Semi") ? 0.82 : 0.70;
  else if (wcsstr(face, L"Narrow")) *wide = 0.79;
  if (wcsstr(face, L"Mono") || wcsstr(face, L"Courier") || wcsstr(face, L"Console")) return "monospace";
  if (wcsstr(face, L"Serif") && !wcsstr(face, L"Sans")) return "serif";
  if (wcsstr(face, L"Roman") || wcsstr(face, L"Garamond") || wcsstr(face, L"Antiqua") || wcsstr(face, L"Book")) return "serif";
  return "sans-serif";
}

HFONT CreateFontW(int height, int width, int escapement, int orientation, int weight, DWORD italic, DWORD underline, DWORD strike,
                  DWORD charset, DWORD out, DWORD clip, DWORD quality, DWORD pitch, const wchar_t *face) {
  (void)escapement, (void)orientation, (void)underline, (void)strike, (void)charset, (void)out, (void)clip, (void)quality, (void)pitch;
  HGDIOBJ o = obj_new(OBJ_FONT);
  lstrcpynW(o->name, face ? face : L"", 64);
  o->sx = 1;
  o->em = height < 0 ? -height : height ? height : 12;
  if (!ft_ready()) return o;
  // Segoe MDL2 Assets: its icons are drawn by dl_icons.c, on squares of the font's size
  o->icons = face && !wcscmp(face, L"Segoe MDL2 Assets");
  if (o->icons) {
    o->ascent = o->avg = o->em;
    return o;
  }
  bool jp = false, got_bold = false, got_italic = false, bold = weight >= 600;
  char own[128];
  double wide;
  const char *family = family_of(face, bold, own, &jp, &wide);
  o->face = face_for(family, bold, italic, &got_bold, &got_italic);
  if (!o->face) return o;
  o->bold = bold;
  o->italic = italic;
  o->fake_bold = bold && !got_bold;
  o->fake_italic = italic && !got_italic;
  FT_Set_Pixel_Sizes(o->face, 0, (FT_UInt)o->em);
  if (width > 0) { // a width asked for: GDI's average character width, from the font's own
    TT_OS2 *os2 = FT_Get_Sfnt_Table(o->face, FT_SFNT_OS2);
    double avg = os2 && os2->xAvgCharWidth > 0 ? os2->xAvgCharWidth * (double)o->em / o->face->units_per_EM : o->em * 0.5;
    o->sx = width / avg;
  } else if (jp) o->sx = 0.85; // Yu Gothic UI's characters are narrower than Noto Sans CJK's (its kana proportional too: 'palt'
                               // below): the texts were fitted to them, so that no text is wider than on Windows, bold too
  else o->sx = wide;
  o->ascent = (int)ceil(o->face->size->metrics.ascender / 64.0);
  o->descent = (int)ceil(-o->face->size->metrics.descender / 64.0);
  if (jp) { // and its lines closer, as Yu Gothic UI's (Noto Sans CJK leaves room for every script's accents)
    o->ascent = (int)ceil(o->em * 1.04);
    o->descent = (int)ceil(o->em * 0.28);
  }
  TT_OS2 *os2 = FT_Get_Sfnt_Table(o->face, FT_SFNT_OS2);
  o->avg = (int)((os2 && os2->xAvgCharWidth > 0 ? os2->xAvgCharWidth * (double)o->em / o->face->units_per_EM : o->em * 0.5) * o->sx + 0.5);
  return o;
}

// ---- proportional kana: Yu Gothic UI's kana and punctuation are narrower than its kanji; Noto Sans
// CJK draws them on full squares, and gives their proportional widths in its 'palt' feature (GPOS
// single adjustments). FreeType does not apply it, so it is read here: for each glyph, how far it moves
// and how much its advance changes (font units). Kept for every face that has one, by its names.
typedef struct Palt { char key[96]; int n; int16_t (*adj)[2]; } Palt;
static Palt palts[8];
static int npalts;

static unsigned be16(const uint8_t *t, size_t len, size_t at) { return at + 2 <= len ? (unsigned)(t[at] << 8 | t[at + 1]) : 0; }
static unsigned long be32(const uint8_t *t, size_t len, size_t at) { return at + 4 <= len ? (unsigned long)be16(t, len, at) << 16 | be16(t, len, at + 2) : 0; }

// A single adjustment subtable (format 1 or 2) into p.
static void palt_single(Palt *p, const uint8_t *t, size_t len, size_t st) {
  unsigned format = be16(t, len, st), cov = (unsigned)st + be16(t, len, st + 2), vf = be16(t, len, st + 4);
  int size = 0;
  for (int b = 0; b < 8; b++) size += (vf >> b & 1) * 2;
  if ((format != 1 && format != 2) || !(vf & 5)) return;
  // the value record of the k-th covered glyph: x placement, x advance
  #define PALT_VALUE(at, dx, dadv) do { size_t v_ = (at); int o_ = 0; dx = dadv = 0; \
    if (vf & 1) { dx = (int16_t)be16(t, len, v_); o_ += 2; } if (vf & 2) o_ += 2; \
    if (vf & 4) dadv = (int16_t)be16(t, len, v_ + o_); } while (0)
  size_t values = format == 1 ? st + 6 : st + 8;
  unsigned cf = be16(t, len, cov), count = be16(t, len, cov + 2);
  for (unsigned i = 0; i < count; i++) {
    unsigned first, last, index;
    if (cf == 1) first = last = be16(t, len, cov + 4 + 2 * i), index = i;
    else if (cf == 2) first = be16(t, len, cov + 4 + 6 * i), last = be16(t, len, cov + 6 + 6 * i), index = be16(t, len, cov + 8 + 6 * i);
    else return;
    for (unsigned g = first; g <= last && g < (unsigned)p->n; g++, index++) {
      int dx, dadv;
      PALT_VALUE(format == 1 ? values : values + (size_t)index * size, dx, dadv);
      if (!p->adj[g][0] && !p->adj[g][1]) p->adj[g][0] = (int16_t)dx, p->adj[g][1] = (int16_t)dadv;
    }
  }
  #undef PALT_VALUE
}

static const Palt *palt_for(FT_Face face) {
  char key[96];
  snprintf(key, sizeof key, "%s|%s|%ld", face->family_name ? face->family_name : "", face->style_name ? face->style_name : "", face->num_glyphs);
  for (int i = 0; i < npalts; i++)
    if (!strcmp(palts[i].key, key)) return palts[i].n ? &palts[i] : NULL;
  if (npalts == (int)(sizeof palts / sizeof *palts)) return NULL;
  Palt *p = &palts[npalts++];
  snprintf(p->key, sizeof p->key, "%s", key);
  FT_ULong len = 0;
  if (FT_Load_Sfnt_Table(face, FT_MAKE_TAG('G', 'P', 'O', 'S'), 0, NULL, &len) || len < 10) return NULL;
  uint8_t *t = malloc(len);
  if (!t || FT_Load_Sfnt_Table(face, FT_MAKE_TAG('G', 'P', 'O', 'S'), 0, t, &len)) { free(t); return NULL; }
  size_t features = be16(t, len, 6), lookups = be16(t, len, 8);
  p->n = (int)face->num_glyphs;
  p->adj = calloc((size_t)p->n, sizeof *p->adj);
  bool any = false;
  for (unsigned f = 0, nf = be16(t, len, features); p->adj && f < nf; f++) {
    size_t rec = features + 2 + 6 * f;
    if (rec + 6 > len || memcmp(t + rec, "palt", 4)) continue;
    size_t feat = features + be16(t, len, rec + 4);
    for (unsigned l = 0, nl = be16(t, len, feat + 2); l < nl; l++) {
      size_t lk = lookups + be16(t, len, lookups + 2 + 2 * be16(t, len, feat + 4 + 2 * l));
      unsigned type = be16(t, len, lk);
      for (unsigned k = 0, nk = be16(t, len, lk + 4); k < nk; k++) {
        size_t st = lk + be16(t, len, lk + 6 + 2 * k);
        if (type == 9 && be16(t, len, st + 2) == 1) palt_single(p, t, len, st + be32(t, len, st + 4)), any = true; // an extension
        else if (type == 1) palt_single(p, t, len, st), any = true;
      }
    }
  }
  free(t);
  if (!any || !p->adj) { free(p->adj); p->adj = NULL; p->n = 0; return NULL; }
  return p;
}

// How far the glyph just loaded in face moves, and how much its advance changes, in pixels.
static void palt_adjust(HGDIOBJ f, FT_Face face, double *dx, double *dadv) {
  *dx = *dadv = 0;
  int slot = face == f->face ? 0 : -1;
  for (int k = 0; slot < 0 && k < FALLBACKS; k++)
    if (face == f->fall[k]) slot = 1 + k;
  if (slot < 0) return;
  if (!f->palt_tried[slot]) f->palt_tried[slot] = true, f->palt[slot] = palt_for(face);
  const Palt *p = f->palt[slot];
  unsigned g = face->glyph->glyph_index;
  if (!p || g >= (unsigned)p->n) return;
  double scale = (double)f->em / face->units_per_EM * f->sx;
  *dx = p->adj[g][0] * scale;
  *dadv = p->adj[g][1] * scale;
}

// The face that has character c: the font's own, then Japanese, then symbols.
static FT_Face face_with(HGDIOBJ f, unsigned c) {
  if (f->face && FT_Get_Char_Index(f->face, c)) return f->face;
  if (!f->fall_tried) {
    f->fall_tried = true;
    static const char *const fams[FALLBACKS] = {"Noto Sans CJK JP", "Noto Sans Symbols", "sans-serif"};
    for (int k = 0; k < FALLBACKS; k++) {
      f->fall[k] = face_for(fams[k], f->bold, f->italic, NULL, NULL);
      if (f->fall[k]) FT_Set_Pixel_Sizes(f->fall[k], 0, (FT_UInt)f->em);
    }
  }
  for (int k = 0; k < FALLBACKS; k++)
    if (f->fall[k] && FT_Get_Char_Index(f->fall[k], c)) return f->fall[k];
  return f->face;
}

// One glyph rendered: its coverage, where it goes from the pen, its advance (and its proportional
// adjustment: *dx, *dadv).
static FT_GlyphSlot glyph(HGDIOBJ f, wchar_t ch, double *dx, double *dadv) {
  unsigned c = (unsigned)ch;
  FT_Face face = face_with(f, c);
  if (!face) return NULL;
  FT_Matrix m = {(FT_Fixed)(f->sx * 65536), f->fake_italic ? (FT_Fixed)(0.2 * 65536) : 0, 0, 65536};
  FT_Set_Transform(face, &m, NULL);
  if (FT_Load_Char(face, c, FT_LOAD_NO_BITMAP | FT_LOAD_TARGET_NORMAL)) return NULL;
  if (f->fake_bold) FT_GlyphSlot_Embolden(face->glyph);
  palt_adjust(f, face, dx, dadv);
  if (FT_Render_Glyph(face->glyph, FT_RENDER_MODE_NORMAL)) return NULL;
  return face->glyph;
}

static double advance_loaded(HGDIOBJ f, wchar_t ch);

// The glyph of ch in f, rendered once and kept (NULL: no room, which does not happen: the table starts
// again when full).
static const Glyph *glyph_of(HGDIOBJ f, wchar_t ch) {
  if (!f->glyphs && !(f->glyphs = calloc(GLYPHS, sizeof *f->glyphs))) return NULL;
  unsigned code = (unsigned)ch + 1;
  for (int turn = 0; turn < 2; turn++) {
    for (unsigned i = code * 2654435761u % GLYPHS, n = 0; n < GLYPHS; i = (i + 1) % GLYPHS, n++) {
      Glyph *g = &f->glyphs[i];
      if (g->code == code) return g;
      if (g->code) continue;
      double dx = 0, dadv = 0;
      FT_GlyphSlot s = glyph(f, ch, &dx, &dadv);
      g->code = code;
      g->ok = s != NULL;
      if (!s) { g->adv = advance_loaded(f, ch); return g; } // (rendering failed: its advance as before)
      g->left = s->bitmap_left, g->top = s->bitmap_top, g->w = (int)s->bitmap.width, g->rows = (int)s->bitmap.rows;
      g->dx = dx, g->adv = s->advance.x / 64.0 + dadv;
      g->bits = g->w * g->rows ? malloc((size_t)g->w * g->rows) : NULL;
      for (int r = 0; g->bits && r < g->rows; r++) memcpy(g->bits + (size_t)r * g->w, s->bitmap.buffer + (ptrdiff_t)r * s->bitmap.pitch, (size_t)g->w);
      if (!g->bits && g->w * g->rows) g->ok = false;
      return g;
    }
    glyphs_free(f); // full: started again
    if (!(f->glyphs = calloc(GLYPHS, sizeof *f->glyphs))) return NULL;
  }
  return NULL;
}

static double advance(HGDIOBJ f, wchar_t ch) {
  if (f->icons) return dl_icon_known((unsigned)ch) ? f->em : 0;
  const Glyph *g = glyph_of(f, ch);
  return g ? g->adv : advance_loaded(f, ch);
}

// The advance of ch loaded from its face (without the table).
static double advance_loaded(HGDIOBJ f, wchar_t ch) {
  unsigned c = (unsigned)ch;
  FT_Face face = face_with(f, c);
  if (!face) return 0;
  FT_Matrix m = {(FT_Fixed)(f->sx * 65536), f->fake_italic ? (FT_Fixed)(0.2 * 65536) : 0, 0, 65536};
  FT_Set_Transform(face, &m, NULL);
  if (FT_Load_Char(face, c, FT_LOAD_NO_BITMAP | FT_LOAD_TARGET_NORMAL)) return 0;
  if (f->fake_bold) FT_GlyphSlot_Embolden(face->glyph);
  double dx, dadv;
  palt_adjust(f, face, &dx, &dadv);
  return face->glyph->advance.x / 64.0 + dadv;
}

// ---- text
int SetBkMode(HDC dc, int mode) {
  int old = dc->bk;
  dc->bk = mode;
  return old;
}
COLORREF SetTextColor(HDC dc, COLORREF c) {
  COLORREF old = dc->text;
  dc->text = c;
  return old;
}
UINT SetTextAlign(HDC dc, UINT align) {
  UINT old = (UINT)dc->align;
  dc->align = (int)align;
  return old;
}
int SetTextCharacterExtra(HDC dc, int extra) {
  int old = dc->extra;
  dc->extra = extra;
  return old;
}

static HGDIOBJ font_of(HDC dc) {
  if (!dc->font) dc->font = CreateFontW(-12, 0, 0, 0, FW_NORMAL, 0, 0, 0, 0, 0, 0, 0, 0, L"Verdana");
  return dc->font;
}

static double text_width(HDC dc, const wchar_t *s, int n) {
  HGDIOBJ f = font_of(dc);
  double w = 0;
  for (int k = 0; k < n; k++) w += advance(f, s[k]) + dc->extra;
  return w;
}

typedef struct { HDC dc; const RECT *box; } IconTo;
static void icon_plot(int px, int py, int a, void *ctx) {
  const IconTo *t = ctx;
  if (t->box && (px < t->box->left || px >= t->box->right || py < t->box->top || py >= t->box->bottom)) return;
  put(t->dc, px, py, t->dc->text, a);
}

// The text at x, its baseline at base, into the picture (or the path being made), within the box b (NULL: anywhere).
static void text_draw(HDC dc, double x, int base, const wchar_t *s, int n, const RECT *b) {
  HGDIOBJ f = font_of(dc);
  int tw, th;
  uint32_t *pic = target(dc, &tw, &th);
  bool alpha = alpha_on(dc); // (as put() does it, worked out once for the text)
  int cr = GetRValue(dc->text), cg = GetGValue(dc->text), cb = GetBValue(dc->text);
  for (int k = 0; k < n; k++) {
    if (f->icons) { // an icon: its square from the top of the line
      IconTo to = {dc, b};
      if (dl_icon_draw((unsigned)s[k], (int)floor(x + 0.5), base - f->em + f->em / 12, f->em, icon_plot, &to)) x += f->em + dc->extra; // a little low, as the font's own
      continue;
    }
    const Glyph *g = glyph_of(f, s[k]);
    if (!g || !g->ok) continue;
    int gx = (int)floor(x + g->dx + 0.5) + g->left, gy = base - g->top;
    for (int r = 0; r < g->rows; r++)
      for (int c = 0; c < g->w; c++) {
        int a = g->bits[(size_t)r * g->w + c], px = gx + c, py = gy + r;
        if (!a || (b && (px < b->left || px >= b->right || py < b->top || py >= b->bottom))) continue;
        if (dc->in_path) {
          if (px < 0 || py < 0 || px >= tw || py >= th) continue;
          uint8_t *p = &dc->path[(size_t)py * tw + px];
          if (a > *p) *p = (uint8_t)a;
          if (px < dc->px0) dc->px0 = px;
          if (px > dc->px1) dc->px1 = px;
          if (py < dc->py0) dc->py0 = py;
          if (py > dc->py1) dc->py1 = py;
        } else put_px(pic, tw, th, dc->clip, alpha, px, py, cr, cg, cb, a);
      }
    x += g->adv + dc->extra;
  }
}

BOOL TextOutW(HDC dc, int x, int y, const wchar_t *s, int n) {
  HGDIOBJ f = font_of(dc);
  double w = text_width(dc, s, n), left = x;
  if ((dc->align & TA_CENTER) == TA_CENTER) left = x - w / 2;
  else if (dc->align & TA_RIGHT) left = x - w;
  int base = (dc->align & TA_BASELINE) == TA_BASELINE ? y : y + f->ascent;
  text_draw(dc, left, base, s, n, NULL);
  return TRUE;
}

BOOL GetTextExtentPoint32W(HDC dc, const wchar_t *s, int n, SIZE *out) {
  HGDIOBJ f = font_of(dc);
  out->cx = (LONG)ceil(text_width(dc, s, n));
  out->cy = f->ascent + f->descent;
  return TRUE;
}

BOOL GetTextMetricsW(HDC dc, TEXTMETRICW *out) {
  HGDIOBJ f = font_of(dc);
  memset(out, 0, sizeof *out);
  out->tmAscent = f->ascent;
  out->tmDescent = f->descent;
  out->tmHeight = f->ascent + f->descent;
  out->tmAveCharWidth = f->avg;
  out->tmMaxCharWidth = f->em;
  out->tmWeight = f->fake_bold ? FW_BOLD : FW_NORMAL;
  return TRUE;
}

// The face's name, as GDI says it (Segoe MDL2 Assets too: its icons are drawn here).
int GetTextFaceW(HDC dc, int n, wchar_t *out) {
  HGDIOBJ f = font_of(dc);
  lstrcpynW(out, f->name, n);
  return (int)wcslen(out);
}

// Lines broken between words (and between Japanese characters) to fit the box's width, from its top.
static bool breaks_anywhere(wchar_t c) { return c >= 0x2E80; }

int DrawTextW(HDC dc, const wchar_t *s, int n, RECT *r, UINT format) {
  HGDIOBJ f = font_of(dc);
  if (n < 0) n = (int)wcslen(s);
  int lh = f->ascent + f->descent, y = r->top, widest = 0;
  double room = r->right - r->left;
  int k = 0;
  while (k <= n) {
    // the longest line from k that fits
    int end = k, last_break = -1;
    double w = 0;
    while (end < n && s[end] != L'\n') {
      double a = advance(f, s[end]) + dc->extra;
      if (w + a > room && end > k && (format & DT_WORDBREAK)) break;
      w += a;
      end++;
      if (end < n && (s[end] == L' ' || breaks_anywhere(s[end]) || breaks_anywhere(s[end - 1]))) last_break = end;
    }
    int stop = end;
    if (end < n && s[end] != L'\n' && last_break > k) stop = last_break; // back to the last place a line may end
    int len = stop - k;
    while (len > 0 && s[k + len - 1] == L' ') len--;
    double lw = text_width(dc, s + k, len);
    if (lw > widest) widest = (int)ceil(lw);
    if (!(format & DT_CALCRECT)) text_draw(dc, r->left, y + f->ascent, s + k, len, r);
    y += lh;
    k = stop;
    while (k < n && s[k] == L' ') k++;
    if (k < n && s[k] == L'\n') k++;
    else if (k >= n) break;
  }
  if (format & DT_CALCRECT) {
    r->bottom = y;
    r->right = r->left + widest;
  }
  return y - r->top;
}

// ---- paths: made of text only (the project's name), filled, outlined, or kept as the clipping
BOOL BeginPath(HDC dc) {
  int w, h;
  target(dc, &w, &h);
  free(dc->path);
  dc->path = calloc((size_t)w * h, 1);
  dc->in_path = true;
  dc->px0 = w, dc->py0 = h, dc->px1 = -1, dc->py1 = -1;
  return TRUE;
}

BOOL EndPath(HDC dc) {
  dc->in_path = false;
  return TRUE;
}

static void path_done(HDC dc) {
  free(dc->path);
  dc->path = NULL;
}

static void path_fill(HDC dc, COLORREF c, const uint8_t *mask, int grow) {
  int w, h;
  target(dc, &w, &h);
  int X0 = dc->px0 - grow, X1 = dc->px1 + grow, Y0 = dc->py0 - grow, Y1 = dc->py1 + grow;
  X0 = X0 < 0 ? 0 : X0, X1 = X1 > w - 1 ? w - 1 : X1, Y0 = Y0 < 0 ? 0 : Y0, Y1 = Y1 > h - 1 ? h - 1 : Y1;
  int cols = X1 - X0 + 1, rows = Y1 - Y0 + 1;
  uint8_t *H = grow > 0 && cols > 0 && rows > 0 ? malloc((size_t)(grow + 1) * rows * cols) : NULL;
  if (H) {
    // the outline (the path widened by a disc of radius grow) in two steps, the same values as below:
    // H[r], each row's largest value within r pixels left and right (the mask is 0 beyond the path's
    // bounds), then for each pixel the largest of the disc's rows, row v reaching r = sqrt(grow^2 - v^2)
    size_t plane = (size_t)rows * cols;
    for (int y = 0; y < rows; y++) memcpy(H + (size_t)y * cols, mask + (size_t)(Y0 + y) * w + X0, (size_t)cols);
    for (int r = 1; r <= grow; r++) {
      const uint8_t *prev = H + (r - 1) * plane;
      uint8_t *cur = H + r * plane;
      for (int y = 0; y < rows; y++)
        for (int x = 0; x < cols; x++) {
          const uint8_t *p = prev + (size_t)y * cols + x;
          uint8_t m = p[0];
          if (x > 0 && p[-1] > m) m = p[-1];
          if (x < cols - 1 && p[1] > m) m = p[1];
          cur[(size_t)y * cols + x] = m;
        }
    }
    int reach[512];
    for (int v = -grow; v <= grow && grow < 256; v++) {
      int u = 0;
      while ((u + 1) * (u + 1) + v * v <= grow * grow) u++;
      reach[v + grow] = u;
    }
    for (int y = 0; y < rows && grow < 256; y++)
      for (int x = 0; x < cols; x++) {
        int a = 0;
        for (int v = -grow; v <= grow; v++) {
          int yy = y + v;
          if (yy < 0 || yy >= rows) continue;
          int m = H[reach[v + grow] * plane + (size_t)yy * cols + x];
          if (m > a) a = m;
        }
        put(dc, X0 + x, Y0 + y, c, a);
      }
    free(H);
    if (grow < 256) return;
  }
  for (int y = dc->py0 - grow; y <= dc->py1 + grow; y++)
    for (int x = dc->px0 - grow; x <= dc->px1 + grow; x++) {
      if (x < 0 || y < 0 || x >= w || y >= h) continue;
      int a = 0;
      if (!grow) a = mask[(size_t)y * w + x];
      else // the outline: the path widened by a disc of radius grow
        for (int v = -grow; v <= grow && a < 255; v++)
          for (int u = -grow; u <= grow; u++) {
            if (u * u + v * v > grow * grow) continue;
            int xx = x + u, yy = y + v;
            if (xx >= 0 && yy >= 0 && xx < w && yy < h && mask[(size_t)yy * w + xx] > a) a = mask[(size_t)yy * w + xx];
          }
      put(dc, x, y, c, a);
    }
}

BOOL FillPath(HDC dc) {
  if (!dc->path) return FALSE;
  if (dc->brush && !dc->brush->none) path_fill(dc, dc->brush->color, dc->path, 0);
  path_done(dc);
  return TRUE;
}

BOOL StrokeAndFillPath(HDC dc) {
  if (!dc->path) return FALSE;
  if (dc->pen && !dc->pen->none) path_fill(dc, dc->pen->color, dc->path, (dc->pen->width + 1) / 2);
  if (dc->brush && !dc->brush->none) path_fill(dc, dc->brush->color, dc->path, 0);
  path_done(dc);
  return TRUE;
}

BOOL SelectClipPath(HDC dc, int mode) {
  (void)mode;
  if (!dc->path) return FALSE;
  clip_drop(dc);
  dc->clip = dc->path; // the path itself becomes the clipping
  dc->path = NULL;
  return TRUE;
}
