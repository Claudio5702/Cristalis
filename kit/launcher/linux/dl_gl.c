// The picture through the graphics card on Linux (OpenGL 3.3), as dl_gpu.c does it with Direct3D 11 on
// Windows: the game's picture through a shader (the filters, the smoothing of pixel-art edges, the
// scanlines, the CRT look, the colour-blind filter, the game's own scene() in GLSL), the setup screen as
// a layer drawn by GDI on black then on white, over the animated sky (the theme's colour and style), the
// frame rate, the notes and "Achievement unlocked" as layers, smooth motion over several refreshes,
// pictures read back for screenshots and --gpu-check. No HDR yet. GDI does everything when OpenGL is
// missing or fails. Also the window's own picture when the graphics card is off (dl_gl_present).
#include "dl_int.h"
#include <SDL3/SDL.h>
#include <SDL3/SDL_opengl.h>
#include <math.h>

SDL_Window *dl_win32_sdl_window(void); // dl_win32.c: the window, made with an OpenGL context (NULL: none)

// ---- OpenGL's functions: those of OpenGL 1.1 from libGL itself, the later ones from the driver
#define GL_FUNCS(X)                                                                                                             \
  X(PFNGLCREATESHADERPROC, glCreateShader) X(PFNGLSHADERSOURCEPROC, glShaderSource) X(PFNGLCOMPILESHADERPROC, glCompileShader) \
  X(PFNGLGETSHADERIVPROC, glGetShaderiv) X(PFNGLGETSHADERINFOLOGPROC, glGetShaderInfoLog)                                       \
  X(PFNGLCREATEPROGRAMPROC, glCreateProgram) X(PFNGLATTACHSHADERPROC, glAttachShader) X(PFNGLLINKPROGRAMPROC, glLinkProgram)    \
  X(PFNGLGETPROGRAMIVPROC, glGetProgramiv) X(PFNGLDELETESHADERPROC, glDeleteShader) X(PFNGLUSEPROGRAMPROC, glUseProgram)        \
  X(PFNGLGETUNIFORMLOCATIONPROC, glGetUniformLocation) X(PFNGLUNIFORM1IPROC, glUniform1i) X(PFNGLUNIFORM1FPROC, glUniform1f)    \
  X(PFNGLUNIFORM2FPROC, glUniform2f) X(PFNGLUNIFORM4FVPROC, glUniform4fv) X(PFNGLGENVERTEXARRAYSPROC, glGenVertexArrays)        \
  X(PFNGLBINDVERTEXARRAYPROC, glBindVertexArray) X(PFNGLACTIVETEXTUREPROC, glActiveTexture_) X(PFNGLDELETEPROGRAMPROC, glDeleteProgram)
#define GL_DECLARE(type, name) static type p_##name;
GL_FUNCS(GL_DECLARE)
#define glCreateShader p_glCreateShader
#define glShaderSource p_glShaderSource
#define glCompileShader p_glCompileShader
#define glGetShaderiv p_glGetShaderiv
#define glGetShaderInfoLog p_glGetShaderInfoLog
#define glCreateProgram p_glCreateProgram
#define glAttachShader p_glAttachShader
#define glLinkProgram p_glLinkProgram
#define glGetProgramiv p_glGetProgramiv
#define glDeleteShader p_glDeleteShader
#define glUseProgram p_glUseProgram
#define glGetUniformLocation p_glGetUniformLocation
#define glUniform1i p_glUniform1i
#define glUniform1f p_glUniform1f
#define glUniform2f p_glUniform2f
#define glUniform4fv p_glUniform4fv
#define glGenVertexArrays p_glGenVertexArrays
#define glBindVertexArray p_glBindVertexArray
#define glActiveTexture p_glActiveTexture_
#define glDeleteProgram p_glDeleteProgram

static bool gl_load(void) {
  static int state = -1;
  if (state >= 0) return state;
  state = 1;
#define DL_GL_LOAD(type, name)                                                                     \
  {                                                                                              \
    char n[64];                                                                                  \
    snprintf(n, sizeof n, "%s", #name);                                                          \
    if (n[strlen(n) - 1] == '_') n[strlen(n) - 1] = 0; /* named apart from SDL_opengl.h's own */ \
    p_##name = (type)SDL_GL_GetProcAddress(n);                                                   \
    if (!p_##name) state = 0;                                                                    \
  }
  GL_FUNCS(DL_GL_LOAD)
  return state;
}

// ---- the shaders: the parts of the game's shader, then the game's scene() (DlGame.shader_glsl), then
// the rest; the same as dl_gpu.c's HLSL
static const char gl_vs[] =
    "#version 330 core\n"
    "out vec2 uv;\n"
    "void main() {\n"
    "  vec2 t = vec2((gl_VertexID << 1) & 2, gl_VertexID & 2);\n"
    "  uv = t; gl_Position = vec4(t * vec2(2, -2) + vec2(-1, 1), 0, 1);\n"
    "}\n";

static const char gl_head[] =
    "#version 330 core\n"
    "in vec2 uv;\n"
    "out vec4 frag;\n"
    "uniform sampler2D tex, t1, t2, t3, t4, t5, t6, t7;\n"
    "uniform vec2 texSize, outSize, origin, srcSize;\n"
    "uniform float filt, scan, crt, hdr, cvd;\n"
    "uniform vec4 game[8];\n"
    "vec3 outc(vec3 c) { return hdr > 0.0 ? pow(max(c, 0.0), vec3(2.2)) * hdr : c; }\n"
    "vec3 px(ivec2 i) { return texelFetch(tex, clamp(i, ivec2(0, 0), ivec2(texSize) - 1), 0).rgb; }\n"
    "bool same(vec3 a, vec3 b) { return dot(abs(a - b), vec3(1.0)) < 0.09; }\n"
    "vec3 edges(vec2 t) {\n"
    "  ivec2 i = ivec2(floor(t)); vec2 f = t - floor(t);\n"
    "  vec3 E = px(i), B = px(i + ivec2(0, -1)), D = px(i + ivec2(-1, 0)), F = px(i + ivec2(1, 0)), H = px(i + ivec2(0, 1));\n"
    "  vec3 r = E;\n"
    "  if (!same(B, H) && !same(D, F)) {\n"
    "    float aa = 0.75 / max(outSize.x / texSize.x, 1.0);\n"
    "    if (same(D, B) && !same(E, D)) r = mix(D, r, smoothstep(0.5 - aa, 0.5 + aa, f.x + f.y));\n"
    "    if (same(B, F) && !same(E, F)) r = mix(F, r, smoothstep(0.5 - aa, 0.5 + aa, 1.0 - f.x + f.y));\n"
    "    if (same(D, H) && !same(E, D)) r = mix(D, r, smoothstep(0.5 - aa, 0.5 + aa, f.x + 1.0 - f.y));\n"
    "    if (same(H, F) && !same(E, F)) r = mix(F, r, smoothstep(0.5 - aa, 0.5 + aa, 2.0 - f.x - f.y));\n"
    "  }\n"
    "  return r;\n"
    "}\n"
    "vec3 fetch(vec2 q) {\n"
    "  if (filt > 1.5) {\n"
    "    vec2 t = q * texSize, o = 0.25 * texSize / outSize;\n"
    "    return (edges(t + vec2(-o.x, -o.y)) + edges(t + vec2(o.x, -o.y)) + edges(t + vec2(-o.x, o.y)) + edges(t + o)) * 0.25;\n"
    "  }\n"
    "  if (filt > 0.5) return textureLod(tex, q, 0.0).rgb;\n"
    "  vec2 t = q * texSize - 0.5, i = floor(t), f = t - i;\n"
    "  f = clamp((f - 0.5) * max(outSize / texSize, vec2(1.0)) + 0.5, 0.0, 1.0);\n"
    "  return textureLod(tex, (i + 0.5 + f) / texSize, 0.0).rgb;\n"
    "}\n";
static const char gl_scene[] = "vec3 scene(vec2 q) { return fetch(q); }\n";
static const char gl_tail[] =
    "vec3 cvd_fix(vec3 c) {\n"
    "  if (cvd < 0.5) return c;\n"
    "  mat3 m;\n" // rows as dl_gpu.c's float3x3, so c * m here is its mul(m, c)
    "  if (cvd < 1.5) m = mat3(0.152286, 1.052583, -0.204868, 0.114503, 0.786281, 0.099216, -0.003882, -0.048116, 1.051998);\n"
    "  else if (cvd < 2.5) m = mat3(0.367322, 0.860646, -0.227968, 0.280085, 0.672501, 0.047413, -0.011820, 0.042940, 0.968881);\n"
    "  else m = mat3(1.255528, -0.076749, -0.178779, -0.078411, 0.930809, 0.147602, 0.004733, 0.691367, 0.303900);\n"
    "  vec3 e = c - c * m;\n"
    "  if (cvd < 2.5) return clamp(c + vec3(0.0, 0.7 * e.r + e.g, 0.7 * e.r + e.b), 0.0, 1.0);\n"
    "  return clamp(c + vec3(e.r + 0.7 * e.b, e.g + 0.7 * e.b, 0.0), 0.0, 1.0);\n"
    "}\n"
    "vec3 scene_c(vec2 q) { return cvd_fix(scene(q)); }\n"
    "void main() {\n"
    "  vec2 c = uv * 2.0 - 1.0, s = c;\n"
    "  if (crt > 1.5) { s.x = c.x * (1.0 + 0.045 * c.y * c.y); s.y = c.y * (1.0 + 0.07 * c.x * c.x); }\n"
    "  if (abs(s.x) >= 1.0 || abs(s.y) >= 1.0) { frag = vec4(0, 0, 0, 1); return; }\n"
    "  vec2 q = (s + 1.0) * 0.5;\n"
    "  float line_pos = fract(q.y * srcSize.y);\n"
    "  vec3 col;\n"
    "  if (crt > 0.5) {\n"
    "    vec2 dx = vec2(1.0 / srcSize.x, 0.0), dy = vec2(0.0, 1.0 / srcSize.y);\n"
    "    vec3 lin = pow(scene_c(q), vec3(2.2)) * 0.6 + (pow(scene_c(q - dx * 0.75), vec3(2.2)) + pow(scene_c(q + dx * 0.75), vec3(2.2))) * 0.2;\n"
    "    vec3 glow = vec3(0.0);\n"
    "    const vec2 ring[8] = vec2[8](vec2(1.5, 0), vec2(-1.5, 0), vec2(0, 1.5), vec2(0, -1.5), vec2(2.5, 2.5), vec2(-2.5, 2.5), vec2(2.5, -2.5), vec2(-2.5, -2.5));\n"
    "    for (int k = 0; k < 8; k++) glow += pow(textureLod(tex, q + ring[k].x * dx + ring[k].y * dy, 0.0).rgb, vec3(2.2));\n"
    "    lin += glow * 0.02;\n"
    "    float per_line = outSize.y / srcSize.y, per_col = outSize.x / srcSize.x;\n"
    "    float depth = per_line >= 3.0 ? 1.0 : per_line >= 2.0 ? 0.55 : 0.0;\n"
    "    float lum = clamp(dot(lin, vec3(0.30, 0.59, 0.11)), 0.0, 1.0);\n"
    "    float width = mix(0.26, 0.48, sqrt(lum));\n"
    "    float dist = line_pos - 0.5;\n"
    "    float beam = exp(-dist * dist / (2.0 * width * width));\n"
    "    lin *= mix(1.0, beam * 1.55, depth);\n"
    "    if (per_col >= 3.0) {\n"
    "      float m = mod(floor(gl_FragCoord.x - origin.x), 3.0), hi = 1.22, lo = 0.80;\n"
    "      lin *= vec3(m < 0.5 ? hi : lo, abs(m - 1.0) < 0.5 ? hi : lo, m > 1.5 ? hi : lo);\n"
    "    }\n"
    "    lin *= 1.0 - (crt > 1.5 ? 0.22 : 0.08) * dot(c, c);\n"
    "    if (crt > 1.5) {\n"
    "      vec2 e = max(abs(s) - 0.94, 0.0) / 0.06;\n"
    "      lin *= 1.0 - smoothstep(0.75, 1.0, length(e));\n"
    "    }\n"
    "    col = pow(clamp(lin, 0.0, 1.0), vec3(1.0 / 2.2));\n"
    "  } else {\n"
    "    col = scene_c(q);\n"
    "    if (line_pos > 0.6667) col *= 1.0 - scan * 0.1875;\n"
    "  }\n"
    "  frag = vec4(col, 1.0);\n"
    "}\n";

// what GDI drew (the setup screen, the frame rate...): colours already multiplied by their opacity
static const char gl_ui[] =
    "#version 330 core\n"
    "in vec2 uv; out vec4 frag; uniform sampler2D tex;\n"
    "void main() { frag = textureLod(tex, uv, 0.0); }\n";

// the window's own picture (the graphics card off): as it is
static const char gl_copy[] =
    "#version 330 core\n"
    "in vec2 uv; out vec4 frag; uniform sampler2D tex;\n"
    "void main() { frag = vec4(texelFetch(tex, ivec2(uv * vec2(textureSize(tex, 0))), 0).rgb, 1.0); }\n";

// the sky behind the setup screen, as ps_sky in dl_gpu.c: the theme's tint, its other skies, animated
static const char gl_sky[] =
    "#version 330 core\n"
    "in vec2 uv; out vec4 frag;\n"
    "uniform vec2 origin; uniform vec4 game[8];\n"
    "vec3 dl_to_hsl(vec3 c) {\n"
    "  float hi = max(c.r, max(c.g, c.b)), lo = min(c.r, min(c.g, c.b)), d = hi - lo, l = (hi + lo) * 0.5;\n"
    "  if (d <= 0.0) return vec3(0.0, 0.0, l);\n"
    "  float s = l > 0.5 ? d / (2.0 - hi - lo) : d / (hi + lo);\n"
    "  float h = hi == c.r ? (c.g - c.b) / d + (c.g < c.b ? 6.0 : 0.0) : hi == c.g ? (c.b - c.r) / d + 2.0 : (c.r - c.g) / d + 4.0;\n"
    "  return vec3(h / 6.0, s, l);\n"
    "}\n"
    "float dl_hue_part(float p, float q, float t) { t = fract(t); return t < 1.0 / 6.0 ? p + (q - p) * 6.0 * t : t < 0.5 ? q : t < 2.0 / 3.0 ? p + (q - p) * (2.0 / 3.0 - t) * 6.0 : p; }\n"
    "vec3 dl_tint(vec3 c) {\n"
    "  if (game[0].w < 0.5) return c;\n"
    "  vec3 h = dl_to_hsl(clamp(c, 0.0, 1.0));\n"
    "  h.x = fract(h.x + game[0].x); h.y = min(h.y * game[0].y, 1.0);\n"
    "  if (h.y <= 0.0) return h.zzz;\n"
    "  float q = h.z < 0.5 ? h.z * (1.0 + h.y) : h.z + h.y - h.z * h.y, p = 2.0 * h.z - q;\n"
    "  return vec3(dl_hue_part(p, q, h.x + 1.0 / 3.0), dl_hue_part(p, q, h.x), dl_hue_part(p, q, h.x - 1.0 / 3.0));\n"
    "}\n"
    "float cell(ivec2 p) {\n"
    "  uint h = uint(p.x) * 374761393u + uint(p.y) * 668265263u;\n"
    "  h = (h ^ (h >> 13)) * 1274126177u;\n"
    "  return float((h ^ (h >> 16)) & 0xFFFFu) / 65535.0;\n"
    "}\n"
    "float vnoise(vec2 p) {\n"
    "  vec2 i = floor(p), f = p - i;\n"
    "  f = f * f * (3.0 - 2.0 * f);\n"
    "  ivec2 b = ivec2(i);\n"
    "  return mix(mix(cell(b), cell(b + ivec2(1, 0)), f.x), mix(cell(b + ivec2(0, 1)), cell(b + ivec2(1, 1)), f.x), f.y);\n"
    "}\n"
    "float clouds(vec2 p) {\n"
    "  float s = 0.0, amp = 0.5;\n"
    "  for (int k = 0; k < 4; k++) { s += vnoise(p) * amp; p = p * 2.03 + vec2(17.1, 5.3); amp *= 0.5; }\n"
    "  return s / 0.9375;\n"
    "}\n"
    "vec3 sky_other(float style, vec2 q, float time, float aspect) {\n"
    "  const vec3 c0 = vec3(4, 20, 92) / 255.0, c1 = vec3(12, 52, 160) / 255.0, c2 = vec3(44, 112, 216) / 255.0, c3 = vec3(160, 200, 244) / 255.0;\n"
    "  if (style < 1.5) {\n"
    "    vec2 d = vec2((q.x - 0.8) * aspect, q.y - 0.1);\n"
    "    return mix(c0, c2, pow(q.y, 1.3)) + c3 * 0.25 * exp(-dot(d, d) * 3.0);\n"
    "  }\n"
    "  float neb = smoothstep(0.45, 0.80, clouds(vec2(q.x * aspect * 3.0 + time * 0.004, q.y * 3.0))) * 0.25;\n"
    "  vec3 col = mix(mix(c0 * 0.5, c1 * 0.7, q.y), c2, neb);\n"
    "  vec2 g = q * vec2(aspect * 90.0, 90.0);\n"
    "  ivec2 i = ivec2(floor(g));\n"
    "  float r = cell(i + ivec2(7, 7));\n"
    "  if (r > 0.93) {\n"
    "    vec2 s = g - vec2(i) - 0.5 - (vec2(cell(i + ivec2(11, 11)), cell(i + ivec2(23, 23))) - 0.5) * 0.6;\n"
    "    col = mix(col, vec3(1.0), (r - 0.93) / 0.07 * (0.75 + 0.25 * sin(time * 2.0 + r * 50.0)) * smoothstep(0.18, 0.0, length(s)));\n"
    "  }\n"
    "  return col;\n"
    "}\n"
    "vec3 sky_clouds(vec2 q, float time, float aspect) {\n"
    "  float u = q.x, y = q.y;\n"
    "  const float horizon = 0.60;\n"
    "  const vec3 haze = vec3(236, 245, 255) / 255.0;\n"
    "  vec3 col;\n"
    "  if (y < horizon) {\n"
    "    float t = y / horizon;\n"
    "    const vec3 c0 = vec3(4, 20, 92) / 255.0, c1 = vec3(12, 52, 160) / 255.0, c2 = vec3(44, 112, 216) / 255.0, c3 = vec3(160, 200, 244) / 255.0;\n"
    "    col = t < 0.45 ? mix(c0, c1, t / 0.45) : t < 0.78 ? mix(c1, c2, (t - 0.45) / 0.33) : t < 0.93 ? mix(c2, c3, (t - 0.78) / 0.15) : mix(c3, haze, (t - 0.93) / 0.07);\n"
    "    float wisp = smoothstep(0.50, 0.78, clouds(vec2(u * aspect * 2.6 + time * 0.012, y * 14.0))) * (0.22 + 0.74 * t);\n"
    "    col = mix(col, vec3(246, 250, 255) / 255.0, wisp);\n"
    "  } else {\n"
    "    float p = (y - horizon) / (1.0 - horizon), z = 1.0 / (p + 0.055);\n"
    "    float n = clouds(vec2((u - 0.5) * aspect * z * 0.55 + 40.0 + time * 0.015, z * 0.9 + time * 0.22));\n"
    "    const vec3 deep = vec3(10, 52, 164) / 255.0, mid = vec3(78, 142, 228) / 255.0, top = vec3(250, 252, 255) / 255.0;\n"
    "    col = mix(mix(deep, mid, smoothstep(0.25, 0.55, n)), top, smoothstep(0.38, 0.70, n));\n"
    "    col = mix(col, haze, exp(-p * 7.5));\n"
    "    col *= 1.0 - 0.38 * p * p;\n"
    "  }\n"
    "  return col;\n"
    "}\n"
    "void main() {\n"
    "  float time = origin.x, aspect = origin.y, style = game[0].z, grain = -1.0;\n"
    "  vec2 q = uv;\n"
    "  if (style > 2.5) {\n"
    "    vec2 n = vec2(aspect * 120.0, 120.0);\n"
    "    ivec2 b = ivec2(floor(q * n));\n"
    "    q = (vec2(b) + 0.5) / n;\n"
    "    grain = (float(((b.x & 1) * 2 + (b.y & 1) * 3) % 4) + 0.5) / 4.0;\n"
    "  }\n"
    "  vec3 col;\n"
    "  if (style > 0.5 && style < 2.5) col = sky_other(style, q, time, aspect);\n"
    "  else col = sky_clouds(q, time, aspect);\n"
    "  if (grain >= 0.0) col = floor(col * 10.0 + grain) / 10.0;\n"
    "  frag = vec4(dl_tint(col), 1.0);\n"
    "}\n";

// ---- state
enum { GT_PICTURE, GT_MENU, GT_FPS, GT_NOTE, GT_TOAST, GT_OVERLAY, GT_GAME = GT_OVERLAY + DL_MAX_OVERLAYS, GT_COUNT = GT_GAME + 7 };
typedef struct { GLuint id; int w, h, format; } GlTex;
static struct {
  bool tried, ok;
  GLuint vao, ps, ui, sky, copy;
  GlTex t[GT_COUNT];
  int win_h; // the window's height, for OpenGL's viewports (from the bottom)
} gl;

int dl_layer;
char dl_shot_path[MAX_PATH];
bool dl_shot_ok, dl_shot_game;

static GLuint shader_of(GLenum kind, const char *const *parts, int n, const char *what) {
  GLuint s = glCreateShader(kind);
  glShaderSource(s, n, parts, NULL);
  glCompileShader(s);
  GLint ok = 0;
  glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
  if (!ok) { // the compiler's message, the only clue when a shader is wrong
    char log[4096], p[MAX_PATH];
    glGetShaderInfoLog(s, sizeof log, NULL, log);
    snprintf(p, sizeof p, "%sgpu_shader_error.txt", dl_base_dir);
    FILE *f = fopen(p, "w");
    if (f) { fprintf(f, "%s:\n%s", what, log); fclose(f); }
    glDeleteShader(s);
    return 0;
  }
  return s;
}

static GLuint program_of(const char *const *parts, int n, const char *what) {
  const char *vs[1] = {gl_vs};
  GLuint v = shader_of(GL_VERTEX_SHADER, vs, 1, "vertex"), f = shader_of(GL_FRAGMENT_SHADER, parts, n, what);
  if (!v || !f) return 0;
  GLuint p = glCreateProgram();
  glAttachShader(p, v);
  glAttachShader(p, f);
  glLinkProgram(p);
  glDeleteShader(v);
  glDeleteShader(f);
  GLint ok = 0;
  glGetProgramiv(p, GL_LINK_STATUS, &ok);
  if (!ok) { glDeleteProgram(p); return 0; }
  glUseProgram(p); // the textures' units: tex 0, t1-t7 1-7
  static const char *const units[8] = {"tex", "t1", "t2", "t3", "t4", "t5", "t6", "t7"};
  for (int k = 0; k < 8; k++) {
    GLint at = glGetUniformLocation(p, units[k]);
    if (at >= 0) glUniform1i(at, k);
  }
  return p;
}

static bool gl_programs(void) {
  if (!dl_win32_sdl_window() || !gl_load()) return false;
  const char *scene = dl_game->shader_glsl ? dl_game->shader_glsl : gl_scene;
  const char *main_parts[3] = {gl_head, scene, gl_tail}, *ui[1] = {gl_ui}, *sky[1] = {gl_sky}, *copy[1] = {gl_copy};
  gl.ps = program_of(main_parts, 3, "picture");
  gl.ui = program_of(ui, 1, "setup screen");
  gl.sky = program_of(sky, 1, "sky");
  gl.copy = program_of(copy, 1, "copy");
  if (!gl.vao) glGenVertexArrays(1, &gl.vao);
  return gl.ps && gl.ui && gl.sky && gl.copy;
}

static void gl_shutdown(void) {
  for (int k = 0; k < GT_COUNT; k++)
    if (gl.t[k].id) { glDeleteTextures(1, &gl.t[k].id); gl.t[k] = (GlTex){0}; }
  GLuint *progs[4] = {&gl.ps, &gl.ui, &gl.sky, &gl.copy};
  for (int k = 0; k < 4; k++)
    if (*progs[k]) { glDeleteProgram(*progs[k]); *progs[k] = 0; }
  gl.ok = false;
}

static bool gl_start(void) {
  if (gl.tried) return gl.ok;
  gl.tried = true;
  gl.ok = gl_programs();
  if (!gl.ok && gl_load()) gl_shutdown();
  return gl.ok;
}

void dl_gpu_prepare(void) {} // OpenGL compiles at once, when first used
bool dl_gpu_ready(void) { return dl_cfg.gpu && !dl_audit_dir && gl_start(); }
void dl_gpu_restart(void) {
  if (gl.tried && gl_load()) gl_shutdown();
  gl.tried = false;
  dl_dirty = true;
}
void dl_gpu_stop(void) {
  if (gl.tried && gl_load()) gl_shutdown();
}
void dl_gpu_follow(bool now) { (void)now; } // no HDR yet on Linux

// Texture 'which' at the given size and format, filled (format: DL_TEX_*).
static bool upload_as(int which, int format, const void *data, int w, int h) {
  static const GLint inner[3] = {GL_RGBA8, GL_R8, GL_RGBA32F};
  static const GLenum fmt[3] = {GL_BGRA, GL_RED, GL_RGBA}, type[3] = {GL_UNSIGNED_BYTE, GL_UNSIGNED_BYTE, GL_FLOAT};
  GlTex *t = &gl.t[which];
  glActiveTexture(GL_TEXTURE0);
  if (!t->id) glGenTextures(1, &t->id);
  glBindTexture(GL_TEXTURE_2D, t->id);
  glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
  if (t->w != w || t->h != h || t->format != format) {
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexImage2D(GL_TEXTURE_2D, 0, inner[format], w, h, 0, fmt[format], type[format], data);
    t->w = w, t->h = h, t->format = format;
  } else glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, w, h, fmt[format], type[format], data);
  return true;
}

static bool upload(int which, const u32 *data, int w, int h) { return upload_as(which, DL_TEX_BGRA8, data, w, h); }

bool dl_gpu_texture(int slot, int format, const void *data, int w, int h) {
  if (!gl.ok || slot < 1 || slot > 7 || format < 0 || format > 2 || !data || w <= 0 || h <= 0) return false;
  return upload_as(GT_GAME + slot - 1, format, data, w, h);
}

// A quad over the rectangle x,y,w,h of the window (from the top) drawn by a program from texture
// 'which' (-1: none), blended "over" for colours already multiplied by their opacity.
static void quad(int x, int y, int w, int h, GLuint prog, int which, bool blend) {
  glViewport(x, gl.win_h - y - h, w, h);
  glUseProgram(prog);
  if (which >= 0) {
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, gl.t[which].id);
  }
  if (blend) {
    glEnable(GL_BLEND);
    glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
  } else glDisable(GL_BLEND);
  glBindVertexArray(gl.vao);
  glDrawArrays(GL_TRIANGLES, 0, 3);
}

static void set2(GLuint p, const char *name, float a, float b) {
  GLint at = glGetUniformLocation(p, name);
  if (at >= 0) glUniform2f(at, a, b);
}
static void set1(GLuint p, const char *name, float a) {
  GLint at = glGetUniformLocation(p, name);
  if (at >= 0) glUniform1f(at, a);
}

// The game's picture in x,y,w,h with the Picture page's filters; the game's constants and textures first.
static void game_quad(int x, int y, int w, int h, int step, int steps) {
  float g[32] = {0};
  if (dl_game->gpu_frame) dl_game->gpu_frame(g, step, steps);
  glUseProgram(gl.ps);
  set2(gl.ps, "texSize", (float)dl_pic_w, (float)dl_pic_h);
  set2(gl.ps, "outSize", (float)w, (float)h);
  set2(gl.ps, "origin", (float)x, (float)y);
  set2(gl.ps, "srcSize", (float)dl_pic_w / dl_pic_scale, (float)dl_pic_h / dl_pic_scale);
  set1(gl.ps, "filt", (float)dl_cfg.smooth);
  set1(gl.ps, "scan", dl_crt() ? 0.0f : (float)dl_scanlines());
  set1(gl.ps, "crt", (float)dl_crt());
  set1(gl.ps, "hdr", 0);
  set1(gl.ps, "cvd", (float)dl_cfg.cvd);
  GLint at = glGetUniformLocation(gl.ps, "game");
  if (at >= 0) glUniform4fv(at, 8, g);
  for (int k = 7; k >= 0; k--) { // the game's textures t1-t7, then the picture on unit 0
    glActiveTexture(GL_TEXTURE0 + k);
    glBindTexture(GL_TEXTURE_2D, k ? gl.t[GT_GAME + k - 1].id : gl.t[GT_PICTURE].id);
  }
  quad(x, y, w, h, gl.ps, -1, false);
}

int dl_gpu_steps(void) {
  int n = dl_mode == M_GAME && dl_game->gpu_steps && gl.ok ? dl_game->gpu_steps() : 1;
  return n < 1 ? 1 : n > 8 ? 8 : n;
}

// The back buffer as drawn so far (x,y,w,h of it from the top; w 0: all of it) into a PNG file.
static bool save(const char *path, int x, int y, int w, int h, int cw, int ch) {
  if (w <= 0) { x = y = 0; w = cw; h = ch; }
  if (x < 0) { w += x; x = 0; }
  if (y < 0) { h += y; y = 0; }
  if (x + w > cw) w = cw - x;
  if (y + h > ch) h = ch - y;
  if (w <= 0 || h <= 0) return false;
  u32 *px = malloc((size_t)w * h * 4), *row = malloc((size_t)w * 4);
  if (!px || !row) { free(px); free(row); return false; }
  glPixelStorei(GL_PACK_ALIGNMENT, 1);
  glReadBuffer(GL_BACK);
  glReadPixels(x, ch - y - h, w, h, GL_BGRA, GL_UNSIGNED_BYTE, px);
  for (int k = 0; k < h / 2; k++) { // OpenGL's rows from the bottom
    memcpy(row, px + (size_t)k * w, (size_t)w * 4);
    memcpy(px + (size_t)k * w, px + (size_t)(h - 1 - k) * w, (size_t)w * 4);
    memcpy(px + (size_t)(h - 1 - k) * w, row, (size_t)w * 4);
  }
  for (size_t k = 0; k < (size_t)w * h; k++) px[k] &= 0xFFFFFF;
  bool ok = dl_save_png(path, px, w, h);
  free(px);
  free(row);
  return ok;
}

// ---- layers drawn by GDI, once on black, once on white (as dl_gpu.c)
static void layer_merge(const u32 *on_black, const u32 *on_white, u32 *out, size_t n) {
  for (size_t k = 0; k < n; k++) {
    u32 b = on_black[k], w = on_white[k];
    int d = (int)((w >> 16) & 255) - (int)((b >> 16) & 255) + (int)((w >> 8) & 255) - (int)((b >> 8) & 255) + (int)(w & 255) - (int)(b & 255);
    int a = 255 - d / 3;
    out[k] = ((u32)(a < 0 ? 0 : a > 255 ? 255 : a) << 24) | (b & 0xFFFFFF);
  }
}

static u32 *menu_layer(HDC wdc, int cw, int ch) {
  static u32 *layer, *on_black;
  static size_t cap;
  size_t n = (size_t)cw * (size_t)ch;
  if (n > cap) {
    u32 *a = realloc(layer, n * 4), *b = a ? realloc(on_black, n * 4) : NULL;
    if (a) layer = a;
    if (b) on_black = b;
    if (!a || !b) return NULL;
    cap = n;
  }
  dl_layer = 1;
  dl_draw_menu(wdc);
  if (dl_menu_bits) memcpy(on_black, dl_menu_bits, n * 4);
  dl_layer = 2;
  dl_draw_menu(wdc);
  dl_layer = 0;
  if (!dl_menu_bits) return NULL;
  layer_merge(on_black, dl_menu_bits, layer, n);
  return layer;
}

static bool small_layer(HDC wdc, int which, int w, int bh, int h, void (*draw)(HDC dc, int h)) {
  static HDC mem;
  BITMAPINFO bi = {{sizeof(BITMAPINFOHEADER), w, -bh, 1, 32}};
  u32 *bits = NULL, *on_black = malloc((size_t)w * bh * 4), *layer = malloc((size_t)w * bh * 4);
  if (!mem) mem = CreateCompatibleDC(wdc);
  HBITMAP bmp = CreateDIBSection(wdc, &bi, DIB_RGB_COLORS, (void **)&bits, NULL, 0);
  bool ok = bmp && on_black && layer;
  if (ok) {
    HGDIOBJ old = SelectObject(mem, bmp);
    RECT r = {0, 0, w, bh};
    for (int pass = 0; pass < 2; pass++) {
      FillRect(mem, &r, GetStockObject(pass ? WHITE_BRUSH : BLACK_BRUSH));
      draw(mem, h);
      if (!pass) memcpy(on_black, bits, (size_t)w * bh * 4);
    }
    SelectObject(mem, old);
    layer_merge(on_black, bits, layer, (size_t)w * bh);
    ok = upload(which, layer, w, bh);
  }
  if (bmp) DeleteObject(bmp);
  free(on_black);
  free(layer);
  return ok;
}

static void fps_draw(HDC dc, int h) { dl_draw_fps(dc, 0, 0, h); }
static void note_draw(HDC dc, int h) { dl_draw_note(dc, 0, 0, h); }
static void toast_draw(HDC dc, int h) { dl_draw_toast(dc, 0, 0, h); }

static bool fps_layer(HDC wdc, int h, int *lw, int *lh) {
  static int made_fps = -1, made_h;
  int fh = h / 24 < 10 ? 10 : h / 24;
  *lw = fh * 6;
  *lh = fh * 2;
  if (made_fps == dl_fps_shown && made_h == h && gl.t[GT_FPS].id) return true;
  made_fps = dl_fps_shown;
  made_h = h;
  return small_layer(wdc, GT_FPS, *lw, *lh, h, fps_draw);
}

static bool note_layer(HDC wdc, int h, int *lw, int *lh) {
  static wchar_t made[160];
  static int made_h;
  const wchar_t *s = dl_note_now();
  dl_note_size(wdc, h, lw, lh);
  if (made_h == h && !lstrcmpW(made, s) && gl.t[GT_NOTE].id) return true;
  lstrcpynW(made, s, 160);
  made_h = h;
  return small_layer(wdc, GT_NOTE, *lw, *lh, h, note_draw);
}

static bool toast_layer(HDC wdc, int h, int *lw, int *lh) {
  static wchar_t made[96];
  static int made_h;
  const wchar_t *s = dl_toast_now();
  dl_toast_size(wdc, h, lw, lh);
  if (made_h == h && !lstrcmpW(made, s) && gl.t[GT_TOAST].id) return true;
  lstrcpynW(made, s, 96);
  made_h = h;
  return small_layer(wdc, GT_TOAST, *lw, *lh, h, toast_draw);
}

// One picture of what dl_mode shows (as dl_gpu_show in dl_gpu.c). False: no graphics card from now on.
bool dl_gpu_show(bool rebuild, bool vsync) {
  if (!gl.ok) return false;
  RECT rc;
  GetClientRect(dl_hwnd, &rc);
  int cw = rc.right, ch = rc.bottom;
  if (cw <= 0 || ch <= 0) return true;
  gl.win_h = ch;
  static int layer_w, layer_h, layer_mode = -1;
  bool menu = dl_mode != M_GAME, good = true;
  HDC wdc = GetDC(dl_hwnd);
  if (menu && (rebuild || !gl.t[GT_MENU].id || layer_w != cw || layer_h != ch || layer_mode != dl_mode)) {
    const u32 *layer = menu_layer(wdc, cw, ch);
    good = layer && upload(GT_MENU, layer, cw, ch);
    layer_w = cw, layer_h = ch, layer_mode = dl_mode;
  }
  if (good) good = upload(GT_PICTURE, dl_pic, dl_pic_w, dl_pic_h);
  int x = 0, y = 0, w = 0, h = 0, fw = 0, fhh = 0, nw = 0, nh = 0, tw = 0, th = 0;
  bool game = dl_mode == M_GAME || dl_mode == M_TRY || dl_mode == M_PAUSE, fps = dl_cfg.show_fps && dl_mode != M_PAUSE && !dl_shot_game;
  bool note = dl_mode == M_GAME && dl_note_now() && !dl_shot_game, toast = dl_mode == M_GAME && dl_toast_now() && !dl_shot_game;
  if (game) {
    dl_fit(cw, ch, dl_cfg.integer_scale, &w, &h);
    x = (cw - w) / 2;
    y = (ch - h) / 2;
    if (good && fps) good = fps_layer(wdc, h, &fw, &fhh);
    if (good && note) good = note_layer(wdc, h, &nw, &nh);
    if (good && toast) good = toast_layer(wdc, h, &tw, &th);
  }
  DlOverlay ov[DL_MAX_OVERLAYS];
  int ov_n = dl_mode == M_GAME && dl_game->overlays ? dl_game->overlays(x, y, w, h, ov, DL_MAX_OVERLAYS) : 0;
  for (int k = 0; good && k < ov_n && k < DL_MAX_OVERLAYS; k++)
    if (ov[k].pixels && ov[k].w > 0 && ov[k].h > 0) good = upload(GT_OVERLAY + k, ov[k].pixels, ov[k].w, ov[k].h);
  ReleaseDC(dl_hwnd, wdc);
  int steps = dl_shot_path[0] ? 1 : dl_gpu_steps();
  SDL_GL_SetSwapInterval(vsync || steps > 1 ? 1 : 0);
  for (int step = 1; good && step <= steps; step++) {
    glViewport(0, 0, cw, ch);
    glClearColor(0, 0, 0, 1);
    glClear(GL_COLOR_BUFFER_BIT);
    if (game) {
      game_quad(x, y, w, h, step, steps);
      if (dl_shot_path[0] && dl_shot_game) { // a screenshot: the game's picture alone, as shown
        dl_shot_ok = save(dl_shot_path, x, y, w, h, cw, ch);
        dl_shot_path[0] = 0;
      }
      for (int k = 0; k < ov_n && k < DL_MAX_OVERLAYS; k++)
        if (ov[k].pixels && ov[k].w > 0 && ov[k].h > 0) quad(ov[k].x, ov[k].y, ov[k].w, ov[k].h, gl.ui, GT_OVERLAY + k, true);
      if (dl_mode != M_GAME) quad(0, 0, cw, ch, gl.ui, GT_MENU, true);
      if (fps) quad(x, y, fw, fhh, gl.ui, GT_FPS, true);
      if (note) {
        int fh = h / 22 < 14 ? 14 : h / 22;
        quad(x + fh, y + h - fh * 5 / 2, nw, nh, gl.ui, GT_NOTE, true);
      }
      if (toast) quad(x + w - tw - h / 30, y + h / 30, tw, th, gl.ui, GT_TOAST, true);
    } else {
      // the sky, moving, then the setup screen, then the live preview with the game's filters
      static Uint64 t0;
      if (!t0) t0 = SDL_GetTicksNS();
      float g[32] = {0};
      g[2] = (float)ui_sky_style();
      if (ui_theme_hsl(&g[0], &g[1])) g[3] = 1;
      glUseProgram(gl.sky);
      set2(gl.sky, "origin", dl_cfg.still ? 0.0f : (float)((SDL_GetTicksNS() - t0) / 1e9), (float)cw / (float)ch);
      GLint at = glGetUniformLocation(gl.sky, "game");
      if (at >= 0) glUniform4fv(at, 8, g);
      quad(0, 0, cw, ch, gl.sky, -1, false);
      quad(0, 0, cw, ch, gl.ui, GT_MENU, true);
      if (dl_prev_w > 0) game_quad(dl_prev_x, dl_prev_y, dl_prev_w, dl_prev_h, 1, 1);
    }
    if (dl_shot_path[0]) {
      dl_shot_ok = save(dl_shot_path, 0, 0, 0, 0, cw, ch);
      dl_shot_path[0] = 0;
    }
    SDL_GL_SwapWindow(dl_win32_sdl_window());
  }
  if (!good) { gl_shutdown(); dl_dirty = true; }
  return good;
}

// ---- the window's own picture, when the graphics card is off (dl_win32.c's ReleaseDC). False: no
// OpenGL here, the window shows it its own way.
bool dl_gl_present(const u32 *px, int w, int h) {
  SDL_Window *win = dl_win32_sdl_window();
  if (!win || !gl_load()) return false;
  if (!gl.copy) {
    const char *copy[1] = {gl_copy};
    gl.copy = program_of(copy, 1, "copy");
    if (!gl.vao) glGenVertexArrays(1, &gl.vao);
    if (!gl.copy) return false;
  }
  if (gl.ok && dl_cfg.gpu) return true; // the graphics card path shows the window itself
  upload(GT_MENU, px, w, h);
  gl.win_h = h;
  SDL_GL_SetSwapInterval(0);
  quad(0, 0, w, h, gl.copy, GT_MENU, false);
  SDL_GL_SwapWindow(win);
  return true;
}
