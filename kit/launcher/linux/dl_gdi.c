// The part of GDI DarkLauncher's setup screen draws with (linux/win32.h), drawn here into pixels for Linux:
// brushes, pens, rounded rectangles, ellipses, polygons and lines with smooth edges, pictures copied,
// stretched or blended, gradients, clipping by rectangles, rounded rectangles and paths, and text through
// FreeType with the fonts fontconfig finds (Noto for Japanese). Not the whole of GDI: what the shared
// code calls, the way it calls it.
#include "win32.h"
#include <math.h>
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
  int em, ascent, descent, avg;
  double sx;                     // horizontal scale (a width asked for), 1 otherwise
  bool bold, italic, fake_bold, fake_italic, icons;
  wchar_t name[64];
};

struct GdiDc {
  HGDIOBJ bitmap, brush, pen, font;
  COLORREF text;
  int bk, align, extra, stretch;
  POINT pos;
  uint8_t *clip;    // coverage of each pixel of the bitmap (0-255), NULL: no clipping
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

BOOL DeleteDC(HDC dc) {
  if (!dc) return FALSE;
  free(dc->clip);
  free(dc->path);
  free(dc);
  return TRUE;
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
      free(dc->clip); // the clipping was for the other picture's size
      dc->clip = NULL;
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

// Colour c over pixel x, y with coverage a (0-255), through the clipping.
static inline void put(HDC dc, int x, int y, COLORREF c, int a) {
  int w, h;
  uint32_t *px = target(dc, &w, &h);
  if (x < 0 || y < 0 || x >= w || y >= h || a <= 0) return;
  size_t i = (size_t)y * w + x;
  if (dc->clip) a = a * dc->clip[i] / 255;
  if (a <= 0) return;
  if (a > 255) a = 255;
  uint32_t d = px[i];
  int r = d >> 16 & 255, g = d >> 8 & 255, b = d & 255;
  r += (GetRValue(c) - r) * a / 255;
  g += (GetGValue(c) - g) * a / 255;
  b += (GetBValue(c) - b) * a / 255;
  px[i] = (uint32_t)r << 16 | (uint32_t)g << 8 | (uint32_t)b;
}

static inline int cover(double v) { return v <= 0 ? 0 : v >= 1 ? 255 : (int)(v * 255 + 0.5); }

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
  shape(dc, x0, y0, x1, y1, box_dist, &s);
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
  for (int y = r->top; y < r->bottom; y++)
    for (int x = r->left; x < r->right; x++) put(dc, x, y, b->color, 255);
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
  free(dc->clip);
  dc->clip = NULL;
  if (!r) return 1;
  int w, h;
  target(dc, &w, &h);
  dc->clip = calloc((size_t)w * h, 1);
  BoxShape s = {(r->x0 + r->x1) / 2.0, (r->y0 + r->y1) / 2.0, (r->x1 - r->x0) / 2.0, (r->y1 - r->y0) / 2.0, r->corner / 2.0};
  for (int y = r->y0 < 0 ? 0 : r->y0; y < r->y1 && y < h; y++)
    for (int x = r->x0 < 0 ? 0 : r->x0; x < r->x1 && x < w; x++) dc->clip[(size_t)y * w + x] = (uint8_t)cover(0.5 - box_dist(x + 0.5, y + 0.5, &s));
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
      for (int i = i0; i < i1; i++) out[i] = row[cols[i]] & 0xFFFFFF;
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
  FcPattern *p = FcNameParse((const FcChar8 *)family);
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

// Windows' faces, by what they look like.
static const char *family_of(const wchar_t *face, bool *japanese) {
  *japanese = false;
  if (!face) return "sans-serif";
  if (!wcscmp(face, L"Yu Gothic UI")) { *japanese = true; return "Noto Sans CJK JP"; }
  if (!wcscmp(face, L"Yu Mincho")) { *japanese = true; return "Noto Serif CJK JP"; }
  if (!wcscmp(face, L"Palatino Linotype") || !wcscmp(face, L"Georgia") || !wcscmp(face, L"Times New Roman")) return "serif";
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
  const char *family = o->icons ? "Noto Sans CJK JP" : family_of(face, &jp);
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
  } else if (jp) o->sx = 0.86; // Yu Gothic UI's characters are narrower than Noto Sans CJK's: the texts were fitted to them
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

// One glyph rendered: its coverage, where it goes from the pen, its advance.
static FT_GlyphSlot glyph(HGDIOBJ f, wchar_t ch) {
  unsigned c = (unsigned)ch;
  FT_Face face = face_with(f, c);
  if (!face) return NULL;
  FT_Matrix m = {(FT_Fixed)(f->sx * 65536), f->fake_italic ? (FT_Fixed)(0.2 * 65536) : 0, 0, 65536};
  FT_Set_Transform(face, &m, NULL);
  if (FT_Load_Char(face, c, FT_LOAD_NO_BITMAP | FT_LOAD_TARGET_NORMAL)) return NULL;
  if (f->fake_bold) FT_GlyphSlot_Embolden(face->glyph);
  if (FT_Render_Glyph(face->glyph, FT_RENDER_MODE_NORMAL)) return NULL;
  return face->glyph;
}

static double advance(HGDIOBJ f, wchar_t ch) {
  unsigned c = (unsigned)ch;
  if (f->icons) return dl_icon_known(c) ? f->em : 0;
  FT_Face face = face_with(f, c);
  if (!face) return 0;
  FT_Matrix m = {(FT_Fixed)(f->sx * 65536), f->fake_italic ? (FT_Fixed)(0.2 * 65536) : 0, 0, 65536};
  FT_Set_Transform(face, &m, NULL);
  if (FT_Load_Char(face, c, FT_LOAD_NO_BITMAP | FT_LOAD_TARGET_NORMAL)) return 0;
  if (f->fake_bold) FT_GlyphSlot_Embolden(face->glyph);
  return face->glyph->advance.x / 64.0;
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
  target(dc, &tw, &th);
  for (int k = 0; k < n; k++) {
    if (f->icons) { // an icon: its square from the top of the line
      IconTo to = {dc, b};
      if (dl_icon_draw((unsigned)s[k], (int)floor(x + 0.5), base - f->em + f->em / 12, f->em, icon_plot, &to)) x += f->em + dc->extra; // a little low, as the font's own
      continue;
    }
    FT_GlyphSlot g = glyph(f, s[k]);
    if (!g) continue;
    int gx = (int)floor(x + 0.5) + g->bitmap_left, gy = base - g->bitmap_top;
    for (unsigned r = 0; r < g->bitmap.rows; r++)
      for (unsigned c = 0; c < g->bitmap.width; c++) {
        int a = g->bitmap.buffer[r * g->bitmap.pitch + c], px = gx + (int)c, py = gy + (int)r;
        if (!a || (b && (px < b->left || px >= b->right || py < b->top || py >= b->bottom))) continue;
        if (dc->in_path) {
          if (px < 0 || py < 0 || px >= tw || py >= th) continue;
          uint8_t *p = &dc->path[(size_t)py * tw + px];
          if (a > *p) *p = (uint8_t)a;
          if (px < dc->px0) dc->px0 = px;
          if (px > dc->px1) dc->px1 = px;
          if (py < dc->py0) dc->py0 = py;
          if (py > dc->py1) dc->py1 = py;
        } else put(dc, px, py, dc->text, a);
      }
    x += g->advance.x / 64.0 + dc->extra;
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
  free(dc->clip);
  dc->clip = dc->path; // the path itself becomes the clipping
  dc->path = NULL;
  return TRUE;
}
