// The icons DarkLauncher (and its games) draw with Windows' Segoe MDL2 Assets font, which Linux does not
// have and which may not be given away: each one drawn here instead, in smooth strokes on a square of the
// font's size. Known: the kit's entries of the bar and its stars, and those its games use so far.
#include <math.h>
#include <stdbool.h>
#include <stddef.h>

typedef struct {
  char kind;      // 'L' a line, 'A' an arc, 'R' a rounded rectangle's edge, 'O' a circle's edge, 'C' a disc,
                  // 'E' an ellipse's edge, 'P' a closed outline, 'F' a filled outline (points in pts)
  double a, b, c, d, e;
  const double *pts;
  int n;
  double width; // its stroke, as a part of the icon's size (0: the usual)
} Shape;

#define ICON_MAX 32
static Shape shapes[ICON_MAX];
static int shape_n;
static void add(Shape s) { if (shape_n < ICON_MAX) shapes[shape_n++] = s; }
static void line(double x0, double y0, double x1, double y1) { add((Shape){'L', x0, y0, x1, y1}); }
static void arc(double cx, double cy, double r, double from, double to) { add((Shape){'A', cx, cy, r, from, to}); } // degrees, clockwise from the right
static void rrect(double x0, double y0, double x1, double y1, double r) { add((Shape){'R', x0, y0, x1, y1, r}); }
static void ring(double cx, double cy, double r) { add((Shape){'O', cx, cy, r}); }
static void disc(double cx, double cy, double r) { add((Shape){'C', cx, cy, r}); }
static void ellipse(double cx, double cy, double rx, double ry) { add((Shape){'E', cx, cy, rx, ry}); }
static void outline(const double *p, int n) { add((Shape){'P', 0, 0, 0, 0, 0, p, n}); }
static void filled(const double *p, int n) { add((Shape){'F', 0, 0, 0, 0, 0, p, n}); }

static double star_pts[20];
static void star(bool fill) {
  for (int k = 0; k < 10; k++) {
    double r = k & 1 ? 0.19 : 0.45, t = (-90 + k * 36) * M_PI / 180;
    star_pts[k * 2] = 0.5 + r * cos(t);
    star_pts[k * 2 + 1] = 0.53 + r * sin(t);
  }
  if (fill) filled(star_pts, 10);
  else outline(star_pts, 10);
}

// The shapes of one icon; false: not known here.
static bool icon_shapes(unsigned code) {
  static const double play[] = {0.28, 0.16, 0.84, 0.5, 0.28, 0.84};
  static const double speaker[] = {0.12, 0.38, 0.3, 0.38, 0.52, 0.2, 0.52, 0.8, 0.3, 0.62, 0.12, 0.62};
  static const double folder[] = {0.08, 0.24, 0.38, 0.24, 0.46, 0.34, 0.92, 0.34, 0.92, 0.8, 0.08, 0.8};
  static const double page[] = {0.24, 0.1, 0.6, 0.1, 0.78, 0.28, 0.78, 0.9, 0.24, 0.9};
  static const double box[] = {0.12, 0.3, 0.5, 0.14, 0.88, 0.3, 0.88, 0.72, 0.5, 0.88, 0.12, 0.72};
  shape_n = 0;
  switch (code) {
    case 0xE768: outline(play, 3); break; // Play
    case 0xE74E: // Saves: a floppy disk
      rrect(0.14, 0.14, 0.86, 0.86, 0.06);
      rrect(0.3, 0.14, 0.66, 0.4, 0);
      rrect(0.3, 0.58, 0.7, 0.86, 0);
      break;
    case 0xE722: // Screenshots: a camera
      rrect(0.08, 0.3, 0.92, 0.82, 0.08);
      line(0.34, 0.3, 0.42, 0.18), line(0.42, 0.18, 0.62, 0.18), line(0.62, 0.18, 0.7, 0.3);
      ring(0.5, 0.56, 0.16);
      break;
    case 0xE734: star(false); break; // Achievements
    case 0xE735: star(true); break;
    case 0xE774: // Languages: a globe
      ring(0.5, 0.5, 0.4);
      ellipse(0.5, 0.5, 0.17, 0.4);
      line(0.1, 0.5, 0.9, 0.5);
      break;
    case 0xE7F4: // Picture: a screen
      rrect(0.08, 0.16, 0.92, 0.68, 0.04);
      line(0.5, 0.68, 0.5, 0.84), line(0.3, 0.84, 0.7, 0.84);
      break;
    case 0xE767: // Sound: a loudspeaker
      outline(speaker, 6);
      arc(0.52, 0.5, 0.18, -45, 45), arc(0.52, 0.5, 0.32, -45, 45);
      break;
    case 0xE765: // Controls: a keyboard
      rrect(0.06, 0.26, 0.94, 0.74, 0.06);
      for (int k = 0; k < 5; k++) line(0.18 + k * 0.16, 0.42, 0.22 + k * 0.16, 0.42), line(0.18 + k * 0.16, 0.54, 0.22 + k * 0.16, 0.54);
      line(0.32, 0.64, 0.68, 0.64);
      break;
    case 0xE7FC: // a game: a controller
      rrect(0.06, 0.3, 0.94, 0.74, 0.2);
      line(0.22, 0.52, 0.38, 0.52), line(0.3, 0.44, 0.3, 0.6);
      disc(0.66, 0.46, 0.05), disc(0.77, 0.57, 0.05);
      break;
    case 0xE706: // a sun
      ring(0.5, 0.5, 0.17);
      for (int k = 0; k < 8; k++) {
        double t = k * M_PI / 4;
        line(0.5 + 0.27 * cos(t), 0.5 + 0.27 * sin(t), 0.5 + 0.41 * cos(t), 0.5 + 0.41 * sin(t));
      }
      break;
    case 0xE8B7: outline(folder, 6); break; // the game's folder
    case 0xE7A7: // Defaults: back
      arc(0.55, 0.58, 0.3, 200, 400);
      line(0.27, 0.48, 0.12, 0.54), line(0.27, 0.48, 0.32, 0.66);
      break;
    case 0xE72C: // Restart: round again
      arc(0.5, 0.52, 0.32, -40, 260);
      line(0.745, 0.314, 0.76, 0.12), line(0.745, 0.314, 0.94, 0.3);
      break;
    case 0xE7E8: // Quit: power
      arc(0.5, 0.56, 0.32, -55, 235);
      line(0.5, 0.1, 0.5, 0.48);
      break;
    case 0xE7F1: // a chip (a cartridge's memory)
      rrect(0.26, 0.26, 0.74, 0.74, 0.04);
      for (int k = 0; k < 3; k++) {
        double t = 0.36 + k * 0.14;
        line(0.12, t, 0.26, t), line(0.74, t, 0.88, t), line(t, 0.12, t, 0.26), line(t, 0.74, t, 0.88);
      }
      break;
    case 0xE713: // settings: a gear
      ring(0.5, 0.5, 0.14), ring(0.5, 0.5, 0.3);
      for (int k = 0; k < 8; k++) {
        double t = k * M_PI / 4;
        add((Shape){'L', 0.5 + 0.3 * cos(t), 0.5 + 0.3 * sin(t), 0.5 + 0.43 * cos(t), 0.5 + 0.43 * sin(t), 0, NULL, 0, 0.13});
      }
      break;
    case 0xE7C3: // a file
      outline(page, 5);
      line(0.6, 0.1, 0.6, 0.28), line(0.6, 0.28, 0.78, 0.28);
      break;
    case 0xE7B8: // a package
      outline(box, 6);
      line(0.12, 0.3, 0.5, 0.46), line(0.5, 0.46, 0.88, 0.3), line(0.5, 0.46, 0.5, 0.88);
      break;
    default: return false;
  }
  return true;
}

bool dl_icon_known(unsigned code) { return icon_shapes(code); }

static double seg(double px, double py, double ax, double ay, double bx, double by) {
  double dx = bx - ax, dy = by - ay, l = dx * dx + dy * dy, t = l > 0 ? ((px - ax) * dx + (py - ay) * dy) / l : 0;
  t = t < 0 ? 0 : t > 1 ? 1 : t;
  double ex = ax + t * dx - px, ey = ay + t * dy - py;
  return sqrt(ex * ex + ey * ey);
}

static double rbox(double px, double py, double hx, double hy, double r) {
  double qx = fabs(px) - hx + r, qy = fabs(py) - hy + r, ox = qx > 0 ? qx : 0, oy = qy > 0 ? qy : 0;
  return sqrt(ox * ox + oy * oy) + (qx > qy ? (qx < 0 ? qx : 0) : (qy < 0 ? qy : 0)) - r;
}

// Distance (in the icon's units) from u, v to a shape's stroke, or (fill) signed to its inside.
static double dist(const Shape *s, double u, double v, bool *fill) {
  *fill = false;
  switch (s->kind) {
    case 'L': return seg(u, v, s->a, s->b, s->c, s->d);
    case 'O': return fabs(hypot(u - s->a, v - s->b) - s->c);
    case 'C': *fill = true; return hypot(u - s->a, v - s->b) - s->c;
    case 'E': {
      double nx = (u - s->a) / s->c, ny = (v - s->b) / s->d;
      return fabs(sqrt(nx * nx + ny * ny) - 1) * (s->c < s->d ? s->c : s->d);
    }
    case 'R': return fabs(rbox(u - (s->a + s->c) / 2, v - (s->b + s->d) / 2, (s->c - s->a) / 2, (s->d - s->b) / 2, s->e));
    case 'A': {
      double t = atan2(v - s->b, u - s->a) * 180 / M_PI;
      while (t < s->d) t += 360;
      if (t <= s->e) return fabs(hypot(u - s->a, v - s->b) - s->c);
      double ax = s->a + s->c * cos(s->d * M_PI / 180), ay = s->b + s->c * sin(s->d * M_PI / 180);
      double bx = s->a + s->c * cos(s->e * M_PI / 180), by = s->b + s->c * sin(s->e * M_PI / 180);
      double da = hypot(u - ax, v - ay), db = hypot(u - bx, v - by);
      return da < db ? da : db;
    }
    case 'P':
    case 'F': {
      double d = 1e9;
      bool in = false;
      for (int i = 0, j = s->n - 1; i < s->n; j = i++) {
        double xi = s->pts[i * 2], yi = s->pts[i * 2 + 1], xj = s->pts[j * 2], yj = s->pts[j * 2 + 1], e = seg(u, v, xj, yj, xi, yi);
        if (e < d) d = e;
        if ((yi > v) != (yj > v) && u < (xj - xi) * (v - yi) / (yj - yi) + xi) in = !in;
      }
      if (s->kind == 'F') { *fill = true; return in ? -d : d; }
      return d;
    }
  }
  return 1e9;
}

// The icon on the size x size square at x, y: plot(px, py, coverage 0-255) for each pixel it touches.
bool dl_icon_draw(unsigned code, int x, int y, int size, void (*plot)(int px, int py, int a, void *ctx), void *ctx) {
  if (!icon_shapes(code) || size <= 0) return false;
  double stroke = 0.085;
  for (int j = 0; j < size; j++)
    for (int i = 0; i < size; i++) {
      double u = (i + 0.5) / size, v = (j + 0.5) / size, best = 0;
      for (int k = 0; k < shape_n; k++) {
        bool fill;
        double d = dist(&shapes[k], u, v, &fill) * size, c;
        if (fill) c = 0.5 - d;
        else c = (shapes[k].width > 0 ? shapes[k].width : stroke) * size / 2 + 0.5 - d;
        if (c > best) best = c;
      }
      if (best > 0) plot(x + i, y + j, best >= 1 ? 255 : (int)(best * 255 + 0.5), ctx);
    }
  return true;
}
