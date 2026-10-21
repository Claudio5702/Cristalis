// The picture through the graphics card (Direct3D 11), taken from Kahna's gpu.inc.
//
// The game's picture is sent as a texture and drawn by a pixel shader that does the scaling, the
// smoothing of pixel-art edges, the scanlines and the CRT look. The setup screen is drawn by GDI as a
// layer (once on black, once on white, which gives the opacity of each pixel) and laid over the sky,
// which the graphics card animates; the live preview is drawn through the same shader as the game.
// HDR output follows the Windows setting of the window's screen. If anything fails, the program
// goes back to GDI for the rest of the session.
#define COBJMACROS
#include "dl_int.h"
#include <d3d11_1.h>
#include <dxgi1_6.h>
#include <math.h>

// The shader text: this first part, then the game's scene() (DlGame.shader) or the plain one, then
// gpu_hlsl_tail.
static const char gpu_hlsl_head[] =
    "Texture2D tex : register(t0);\n"
    "SamplerState smp : register(s0);\n"
    "cbuffer C : register(b0) { float2 texSize; float2 outSize; float2 origin; float filt; float scan; float crt; float hdr; float2 srcSize; float cvd; float3 cvdPad; float4 game[8]; };\n" // srcSize: the console's resolution (texSize / dl_picture_scale)
    "struct V { float4 p : SV_Position; float2 uv : TEXCOORD0; };\n"
    "V vs(uint id : SV_VertexID) {\n"
    "  V o; float2 t = float2((id << 1) & 2, id & 2);\n"
    "  o.uv = t; o.p = float4(t * float2(2, -2) + float2(-1, 1), 0, 1); return o;\n"
    "}\n"
    // HDR output (hdr > 0): linear light, 1 = 80 cd/m2, so colours are turned from the usual curve
    // and the white is brought to the chosen brightness
    "float3 outc(float3 c) { return hdr > 0 ? pow(max(c, 0), 2.2) * hdr : c; }\n"
    "float3 px(int2 i) { return tex.Load(int3(clamp(i, int2(0, 0), int2(texSize) - 1), 0)).rgb; }\n"
    "bool same(float3 a, float3 b) { return dot(abs(a - b), float3(1, 1, 1)) < 0.09; }\n"
    // Pixel-art edges: where two neighbours of a pixel agree across a corner and the pixel itself
    // differs (a staircase), that corner is cut along the diagonal, so slanted lines become straight.
    "float3 edges(float2 t) {\n"
    "  int2 i = int2(floor(t)); float2 f = t - floor(t);\n"
    "  float3 E = px(i), B = px(i + int2(0, -1)), D = px(i + int2(-1, 0)), F = px(i + int2(1, 0)), H = px(i + int2(0, 1));\n"
    "  float3 r = E;\n"
    "  if (!same(B, H) && !same(D, F)) {\n"
    "    float aa = 0.75 / max(outSize.x / texSize.x, 1.0);\n"
    "    if (same(D, B) && !same(E, D)) r = lerp(D, r, smoothstep(0.5 - aa, 0.5 + aa, f.x + f.y));\n"
    "    if (same(B, F) && !same(E, F)) r = lerp(F, r, smoothstep(0.5 - aa, 0.5 + aa, 1 - f.x + f.y));\n"
    "    if (same(D, H) && !same(E, D)) r = lerp(D, r, smoothstep(0.5 - aa, 0.5 + aa, f.x + 1 - f.y));\n"
    "    if (same(H, F) && !same(E, F)) r = lerp(F, r, smoothstep(0.5 - aa, 0.5 + aa, 2 - f.x - f.y));\n"
    "  }\n"
    "  return r;\n"
    "}\n"
    // filt 0: flat pixels, only the one-screen-pixel border between two of them is blended
    // filt 1: plain interpolation;  filt 2: flat pixels with the staircases straightened
    "float3 fetch(float2 uv) {\n"
    "  if (filt > 1.5) {\n"
    "    float2 t = uv * texSize, o = 0.25 * texSize / outSize;\n" // four samples per screen pixel
    "    return (edges(t + float2(-o.x, -o.y)) + edges(t + float2(o.x, -o.y)) + edges(t + float2(-o.x, o.y)) + edges(t + o)) * 0.25;\n"
    "  }\n"
    "  if (filt > 0.5) return tex.SampleLevel(smp, uv, 0).rgb;\n"
    "  float2 t = uv * texSize - 0.5, i = floor(t), f = t - i;\n"
    "  f = saturate((f - 0.5) * max(outSize / texSize, 1.0) + 0.5);\n"
    "  return tex.SampleLevel(smp, (i + 0.5 + f) / texSize, 0).rgb;\n"
    "}\n";
// the game's picture as it is when the game adds nothing
static const char gpu_hlsl_scene[] = "float3 scene(float2 q) { return fetch(q); }\n";
static const char gpu_hlsl_tail[] =
    // the colour-blind filter (cvd 1 protan, 2 deutan, 3 tritan), as cvd_apply in dl_app.c
    "float3 cvd_fix(float3 c) {\n"
    "  if (cvd < 0.5) return c;\n"
    "  float3x3 m;\n"
    "  if (cvd < 1.5) m = float3x3(0.152286, 1.052583, -0.204868, 0.114503, 0.786281, 0.099216, -0.003882, -0.048116, 1.051998);\n"
    "  else if (cvd < 2.5) m = float3x3(0.367322, 0.860646, -0.227968, 0.280085, 0.672501, 0.047413, -0.011820, 0.042940, 0.968881);\n"
    "  else m = float3x3(1.255528, -0.076749, -0.178779, -0.078411, 0.930809, 0.147602, 0.004733, 0.691367, 0.303900);\n"
    "  float3 e = c - mul(m, c);\n"
    "  if (cvd < 2.5) return saturate(c + float3(0, 0.7 * e.r + e.g, 0.7 * e.r + e.b));\n"
    "  return saturate(c + float3(e.r + 0.7 * e.b, e.g + 0.7 * e.b, 0));\n"
    "}\n"
    "float3 scene_c(float2 q) { return cvd_fix(scene(q)); }\n"
    "float4 ps(V v) : SV_Target {\n"
    "  float2 c = v.uv * 2 - 1, s = c;\n"
    "  if (crt > 1.5) { s.x = c.x * (1 + 0.045 * c.y * c.y); s.y = c.y * (1 + 0.07 * c.x * c.x); }\n" // bulging tube
    "  if (abs(s.x) >= 1 || abs(s.y) >= 1) return float4(0, 0, 0, 1);\n"
    "  float2 q = (s + 1) * 0.5;\n"
    "  float line_pos = frac(q.y * srcSize.y);\n"
    "  float3 col;\n"
    "  if (crt > 0.5) {\n"
    // A tube, worked out in linear light: the beam spreads a little sideways, bright areas glow into
    // their surroundings, each scanline is a beam that gets wider where the picture is brighter, and
    // the screen is a grille of red, green and blue stripes.
    "    float2 dx = float2(1.0 / srcSize.x, 0), dy = float2(0, 1.0 / srcSize.y);\n"
    "    float3 lin = pow(scene_c(q), 2.2) * 0.6 + (pow(scene_c(q - dx * 0.75), 2.2) + pow(scene_c(q + dx * 0.75), 2.2)) * 0.2;\n"
    "    float3 glow = float3(0, 0, 0);\n"
    "    static const float2 ring[8] = {float2(1.5, 0), float2(-1.5, 0), float2(0, 1.5), float2(0, -1.5), float2(2.5, 2.5), float2(-2.5, 2.5), float2(2.5, -2.5), float2(-2.5, -2.5)};\n"
    "    for (int k = 0; k < 8; k++) glow += pow(tex.SampleLevel(smp, q + ring[k].x * dx + ring[k].y * dy, 0).rgb, 2.2);\n"
    "    lin += glow * 0.02;\n"
    "    float per_line = outSize.y / srcSize.y, per_col = outSize.x / srcSize.x;\n"
    "    float depth = per_line >= 3 ? 1.0 : per_line >= 2 ? 0.55 : 0;\n" // scanlines need room to be drawn
    "    float lum = saturate(dot(lin, float3(0.30, 0.59, 0.11)));\n"
    "    float width = lerp(0.26, 0.48, sqrt(lum));\n"                     // brighter beam, wider line
    "    float dist = line_pos - 0.5;\n"
    "    float beam = exp(-dist * dist / (2 * width * width));\n"
    "    lin *= lerp(1.0, beam * 1.55, depth);\n"
    "    if (per_col >= 3) {\n"
    // in HDR the grille can be as marked as on a real tube: the brightness it takes away is given back
    // by the screen's headroom instead of being softened
    "      float m = fmod(floor(v.p.x - origin.x), 3), hi = hdr > 0 ? 2.4 : 1.22, lo = hdr > 0 ? 0.3 : 0.80;\n"
    "      lin *= float3(m < 0.5 ? hi : lo, abs(m - 1) < 0.5 ? hi : lo, m > 1.5 ? hi : lo);\n"
    "    }\n"
    "    lin *= 1 - (crt > 1.5 ? 0.22 : 0.08) * dot(c, c);\n"            // darker towards the corners
    "    if (crt > 1.5) {\n"                                             // rounded corners of the glass
    "      float2 e = max(abs(s) - 0.94, 0) / 0.06;\n"
    "      lin *= 1 - smoothstep(0.75, 1.0, length(e));\n"
    "    }\n"
    "    if (hdr > 0) return float4(min(lin * hdr, 12.5), 1);\n"
    "    col = pow(saturate(lin), 1.0 / 2.2);\n"
    "  } else {\n"
    "    col = scene_c(q);\n"
    "    if (line_pos > 0.6667) col *= 1 - scan * 0.1875;\n" // last third of each line darkened
    "  }\n"
    // HDR without the tube: the picture at the chosen white, the brightest colours raised a little
    "  if (hdr > 0) {\n"
    "    float3 lin = pow(max(col, 0), 2.2);\n"
    "    float lum = dot(lin, float3(0.2126, 0.7152, 0.0722));\n"
    "    return float4(lin * hdr * (1 + 0.8 * smoothstep(0.55, 1.0, lum)), 1);\n"
    "  }\n"
    "  return float4(col, 1);\n"
    "}\n"
    // what GDI drew (the setup screen, the frame rate): colours already multiplied by their opacity
    "float4 ps_ui(V v) : SV_Target {\n"
    "  float4 u = tex.SampleLevel(smp, v.uv, 0);\n"
    "  return float4(outc(u.a > 0 ? u.rgb / u.a : float3(0, 0, 0)) * u.a, u.a);\n"
    "}\n"
    // ---- backdrop of the setup screen: the same sky as sky_pixel in dl_draw.c, animated: the wisps
    // drift and the sea of clouds comes towards the viewer (origin = time, aspect)
    "float cell(int2 p) {\n"
    "  uint h = uint(p.x) * 374761393u + uint(p.y) * 668265263u;\n"
    "  h = (h ^ (h >> 13)) * 1274126177u;\n"
    "  return float((h ^ (h >> 16)) & 0xFFFFu) / 65535.0;\n"
    "}\n"
    "float vnoise(float2 p) {\n"
    "  float2 i = floor(p), f = p - i;\n"
    "  f = f * f * (3 - 2 * f);\n"
    "  int2 b = int2(i);\n"
    "  return lerp(lerp(cell(b), cell(b + int2(1, 0)), f.x), lerp(cell(b + int2(0, 1)), cell(b + int2(1, 1)), f.x), f.y);\n"
    "}\n"
    "float clouds(float2 p) {\n"
    "  float s = 0, amp = 0.5;\n"
    "  for (int k = 0; k < 4; k++) { s += vnoise(p) * amp; p = p * 2.03 + float2(17.1, 5.3); amp *= 0.5; }\n"
    "  return s / 0.9375;\n"
    "}\n"
    // the theme's colour (DlTheme): hue turned by game[0].x, strength scaled by game[0].y, lightness kept,
    // when game[0].w is 1 (dl_draw.c does the same for GDI)
    "float3 dl_to_hsl(float3 c) {\n"
    "  float hi = max(c.r, max(c.g, c.b)), lo = min(c.r, min(c.g, c.b)), d = hi - lo, l = (hi + lo) * 0.5;\n"
    "  if (d <= 0) return float3(0, 0, l);\n"
    "  float s = l > 0.5 ? d / (2 - hi - lo) : d / (hi + lo);\n"
    "  float h = hi == c.r ? (c.g - c.b) / d + (c.g < c.b ? 6 : 0) : hi == c.g ? (c.b - c.r) / d + 2 : (c.r - c.g) / d + 4;\n"
    "  return float3(h / 6, s, l);\n"
    "}\n"
    "float dl_hue_part(float p, float q, float t) { t = frac(t); return t < 1.0 / 6 ? p + (q - p) * 6 * t : t < 0.5 ? q : t < 2.0 / 3 ? p + (q - p) * (2.0 / 3 - t) * 6 : p; }\n"
    "float3 dl_tint(float3 c) {\n"
    "  if (game[0].w < 0.5) return c;\n"
    "  float3 h = dl_to_hsl(saturate(c));\n"
    "  h.x = frac(h.x + game[0].x); h.y = min(h.y * game[0].y, 1);\n"
    "  if (h.y <= 0) return h.zzz;\n"
    "  float q = h.z < 0.5 ? h.z * (1 + h.y) : h.z + h.y - h.z * h.y, p = 2 * h.z - q;\n"
    "  return float3(dl_hue_part(p, q, h.x + 1.0 / 3), dl_hue_part(p, q, h.x), dl_hue_part(p, q, h.x - 1.0 / 3));\n"
    "}\n"
    // the other skies of the theme (game[0].z: DL_SKY_*), as sky_other in dl_draw.c, animated
    "float3 sky_other(float style, float2 uv, float time, float aspect) {\n"
    "  const float3 c0 = float3(4, 20, 92) / 255.0, c1 = float3(12, 52, 160) / 255.0, c2 = float3(44, 112, 216) / 255.0, c3 = float3(160, 200, 244) / 255.0;\n"
    "  if (style < 1.5) {\n"
    "    float2 d = float2((uv.x - 0.8) * aspect, uv.y - 0.1);\n"
    "    return lerp(c0, c2, pow(uv.y, 1.3)) + c3 * 0.25 * exp(-dot(d, d) * 3);\n"
    "  }\n"
    "  float neb = smoothstep(0.45, 0.80, clouds(float2(uv.x * aspect * 3 + time * 0.004, uv.y * 3))) * 0.25;\n"
    "  float3 col = lerp(lerp(c0 * 0.5, c1 * 0.7, uv.y), c2, neb);\n"
    "  float2 g = uv * float2(aspect * 90, 90);\n"
    "  int2 i = int2(floor(g));\n"
    "  float r = cell(i + int2(7, 7));\n"
    "  if (r > 0.93) {\n"
    "    float2 s = g - float2(i) - 0.5 - (float2(cell(i + int2(11, 11)), cell(i + int2(23, 23))) - 0.5) * 0.6;\n"
    "    col = lerp(col, float3(1, 1, 1), (r - 0.93) / 0.07 * (0.75 + 0.25 * sin(time * 2 + r * 50)) * smoothstep(0.18, 0.0, length(s)));\n"
    "  }\n"
    "  return col;\n"
    "}\n"
    "float3 sky_clouds(float2 uv, float time, float aspect) {\n"
    "  float u = uv.x, y = uv.y;\n"
    "  const float horizon = 0.60;\n"
    "  const float3 haze = float3(236, 245, 255) / 255.0;\n"
    "  float3 col;\n"
    "  if (y < horizon) {\n"
    "    float t = y / horizon;\n"
    "    const float3 c0 = float3(4, 20, 92) / 255.0, c1 = float3(12, 52, 160) / 255.0, c2 = float3(44, 112, 216) / 255.0, c3 = float3(160, 200, 244) / 255.0;\n"
    "    col = t < 0.45 ? lerp(c0, c1, t / 0.45) : t < 0.78 ? lerp(c1, c2, (t - 0.45) / 0.33) : t < 0.93 ? lerp(c2, c3, (t - 0.78) / 0.15) : lerp(c3, haze, (t - 0.93) / 0.07);\n"
    "    float wisp = smoothstep(0.50, 0.78, clouds(float2(u * aspect * 2.6 + time * 0.012, y * 14.0))) * (0.22 + 0.74 * t);\n"
    "    col = lerp(col, float3(246, 250, 255) / 255.0, wisp);\n"
    "  } else {\n"
    "    float p = (y - horizon) / (1 - horizon), z = 1 / (p + 0.055);\n"
    "    float n = clouds(float2((u - 0.5) * aspect * z * 0.55 + 40 + time * 0.015, z * 0.9 + time * 0.22));\n"
    "    const float3 deep = float3(10, 52, 164) / 255.0, mid = float3(78, 142, 228) / 255.0, top = float3(250, 252, 255) / 255.0;\n"
    "    col = lerp(lerp(deep, mid, smoothstep(0.25, 0.55, n)), top, smoothstep(0.38, 0.70, n));\n"
    "    col = lerp(col, haze, exp(-p * 7.5));\n"
    "    col *= 1 - 0.38 * p * p;\n"
    "  }\n"
    "  return col;\n"
    "}\n"
    "float4 ps_sky(V v) : SV_Target {\n"
    "  float time = origin.x, aspect = origin.y, style = game[0].z, grain = -1;\n"
    "  float2 uv = v.uv;\n"
    "  if (style > 2.5) {\n" // big pixels, few colours with an ordered dither
    "    float2 n = float2(aspect * 120, 120);\n"
    "    int2 b = int2(floor(uv * n));\n"
    "    uv = (float2(b) + 0.5) / n;\n"
    "    grain = (((b.x & 1) * 2 + (b.y & 1) * 3) % 4 + 0.5) / 4;\n"
    "  }\n"
    "  float3 col;\n"
    "  if (style > 0.5 && style < 2.5) col = sky_other(style, uv, time, aspect);\n"
    "  else col = sky_clouds(uv, time, aspect);\n"
    "  if (grain >= 0) col = floor(col * 10 + grain) / 10;\n"
    "  return float4(outc(dl_tint(col)), 1);\n"
    "}\n";

typedef struct { float tex_size[2], out_size[2], origin[2], filt, scan, crt, hdr, src_size[2], cvd, cvd_pad[3], game[32]; } GpuConsts;

typedef struct { ID3D11Texture2D *tex; ID3D11ShaderResourceView *srv; int w, h, format; } GpuTex;
// the game's textures t1-t7 (dl_gpu_texture) come after the kit's own
enum { GT_PICTURE, GT_MENU, GT_FPS, GT_NOTE, GT_TOAST, GT_OVERLAY, GT_GAME = GT_OVERLAY + DL_MAX_OVERLAYS, GT_COUNT = GT_GAME + 7 };

static struct {
  bool tried, ok;
  ID3D11Device *dev;
  ID3D11DeviceContext *ctx;
  IDXGISwapChain *swap;
  ID3D11RenderTargetView *rtv;
  ID3D11VertexShader *vs;
  ID3D11PixelShader *ps, *ps_ui, *ps_sky;
  ID3D11BlendState *blend;
  ID3D11SamplerState *linear;
  ID3D11Buffer *consts;
  GpuTex t[GT_COUNT];
  int bb_w, bb_h;
} gpu;

// HDR output: hdr_want is set by dl_gpu_follow; the swap chain is made for it when the graphics card
// path starts (16-bit floating point, linear light, flip model), and hdr_white is the brightness of
// the white in the units of that format (80 cd/m2).
static const int hdr_nits[5] = {0, 150, 200, 300, 400}; // white of the HDR output, by setting
static bool hdr_want, hdr_chain;
static float hdr_white = 2.5f;

int dl_layer;
char dl_shot_path[MAX_PATH];
bool dl_shot_ok, dl_shot_game;

#define GPU_RELEASE(p) do { if (p) { (p)->lpVtbl->Release(p); (p) = NULL; } } while (0)

// The game's picture in a texture of its own device that it shares (dl_picture_shared), opened on
// DarkLauncher's device; read between AcquireSync(0) and ReleaseSync(0), as the game writes it.
static struct {
  HANDLE handle;                 // as the game gave it last
  bool failed;                   // that handle cannot be opened on this device (not tried again)
  ID3D11Texture2D *tex;          // NULL: not opened yet on this device
  ID3D11ShaderResourceView *srv;
  IDXGIKeyedMutex *mutex;
  int w, h;
  DXGI_FORMAT format;            // of the view: B8G8R8A8_UNORM or R8G8B8A8_UNORM
} shared;

static void shared_close(void) {
  GPU_RELEASE(shared.mutex);
  GPU_RELEASE(shared.srv);
  GPU_RELEASE(shared.tex);
  shared.failed = false;
}

static void gpu_shutdown(void) {
  shared_close(); // opened again from its handle by the next device
  if (gpu.ctx) ID3D11DeviceContext_ClearState(gpu.ctx);
  for (int k = 0; k < GT_COUNT; k++) { GPU_RELEASE(gpu.t[k].srv); GPU_RELEASE(gpu.t[k].tex); gpu.t[k].w = 0; }
  GPU_RELEASE(gpu.consts); GPU_RELEASE(gpu.linear); GPU_RELEASE(gpu.ps); GPU_RELEASE(gpu.ps_ui); GPU_RELEASE(gpu.ps_sky);
  GPU_RELEASE(gpu.blend); GPU_RELEASE(gpu.vs);
  GPU_RELEASE(gpu.rtv);
  GPU_RELEASE(gpu.swap);
  // a flip-model (HDR) swap chain is destroyed only once the context has been flushed; until then a
  // new swap chain on the same window fails, and the window keeps showing the last HDR picture
  if (gpu.ctx) ID3D11DeviceContext_Flush(gpu.ctx);
  GPU_RELEASE(gpu.ctx); GPU_RELEASE(gpu.dev);
  gpu.ok = false;
  gpu.bb_w = 0;
}

static bool gpu_target(void) {
  ID3D11Texture2D *back = NULL;
  if (FAILED(IDXGISwapChain_GetBuffer(gpu.swap, 0, &IID_ID3D11Texture2D, (void **)&back))) return false;
  HRESULT hr = ID3D11Device_CreateRenderTargetView(gpu.dev, (ID3D11Resource *)back, NULL, &gpu.rtv);
  ID3D11Texture2D_Release(back);
  return SUCCEEDED(hr);
}

// ---- compiled shaders
// Compiling the shaders takes Windows about two seconds. It is done once, on a second thread while
// the setup screen is already shown by GDI, and the result is kept in gpu_shaders.bin next to the
// program (redone whenever the shader text above changes), so later starts do not compile at all.
enum { SH_VS, SH_PS, SH_UI, SH_SKY, SH_COUNT };
static const char *const shader_entry[SH_COUNT] = {"vs", "ps", "ps_ui", "ps_sky"};
static struct { u8 *code; u32 size; } shader[SH_COUNT];
static volatile LONG shaders_state; // 0 not asked, 1 being prepared, 2 ready, 3 failed
#define SHADER_CACHE_MAGIC 0x314C4447u // "GDL1"

static char *shader_text; // the three parts, put together once

static u32 shader_text_hash(void) {
  u32 h = 2166136261u;
  for (const char *c = shader_text; *c; c++) h = (h ^ (u8)*c) * 16777619u;
  return h;
}

static bool shaders_from_cache(const char *path) {
  FILE *f = fopen(path, "rb");
  if (!f) return false;
  u32 head[2];
  bool ok = fread(head, 4, 2, f) == 2 && head[0] == SHADER_CACHE_MAGIC && head[1] == shader_text_hash();
  for (int k = 0; ok && k < SH_COUNT; k++) {
    u32 n = 0;
    ok = fread(&n, 4, 1, f) == 1 && n > 0 && n < (1u << 20);
    if (ok) { shader[k].code = malloc(n); shader[k].size = n; ok = shader[k].code && fread(shader[k].code, 1, n, f) == n; }
  }
  fclose(f);
  if (!ok) for (int k = 0; k < SH_COUNT; k++) { free(shader[k].code); shader[k].code = NULL; }
  return ok;
}

static bool shaders_compile(void) {
  typedef HRESULT(WINAPI * CompileFn)(LPCVOID, SIZE_T, LPCSTR, const void *, void *, LPCSTR, LPCSTR, UINT, UINT, ID3DBlob **, ID3DBlob **);
  HMODULE comp = LoadLibraryA("d3dcompiler_47.dll");
  CompileFn compile = comp ? (CompileFn)(void *)GetProcAddress(comp, "D3DCompile") : NULL;
  if (!compile) return false;
  for (int k = 0; k < SH_COUNT; k++) {
    ID3DBlob *out = NULL, *err = NULL;
    HRESULT hr = compile(shader_text, strlen(shader_text), NULL, NULL, NULL, shader_entry[k], k == SH_VS ? "vs_4_0" : "ps_4_0", 0, 0, &out, &err);
    if (err) {
      if (FAILED(hr)) { // keep the compiler's message: it is the only clue when a shader is wrong
        char p[MAX_PATH];
        snprintf(p, sizeof p, "%sgpu_shader_error.txt", dl_base_dir);
        FILE *f = fopen(p, "w");
        if (f) { fwrite(err->lpVtbl->GetBufferPointer(err), 1, err->lpVtbl->GetBufferSize(err), f); fclose(f); }
      }
      err->lpVtbl->Release(err);
    }
    if (FAILED(hr) || !out) return false;
    shader[k].size = (u32)out->lpVtbl->GetBufferSize(out);
    shader[k].code = malloc(shader[k].size);
    if (shader[k].code) memcpy(shader[k].code, out->lpVtbl->GetBufferPointer(out), shader[k].size);
    out->lpVtbl->Release(out);
    if (!shader[k].code) return false;
  }
  return true;
}

static DWORD WINAPI shaders_thread(LPVOID unused) {
  (void)unused;
  const char *scene = dl_game->shader ? dl_game->shader : gpu_hlsl_scene;
  shader_text = malloc(sizeof gpu_hlsl_head + strlen(scene) + sizeof gpu_hlsl_tail);
  if (!shader_text) { InterlockedExchange(&shaders_state, 3); return 0; }
  strcpy(shader_text, gpu_hlsl_head);
  strcat(shader_text, scene);
  strcat(shader_text, gpu_hlsl_tail);
  char path[MAX_PATH];
  snprintf(path, sizeof path, "%sgpu_shaders.bin", dl_base_dir);
  bool ok = shaders_from_cache(path);
  if (!ok) {
    ok = shaders_compile();
    if (ok) { // a folder that cannot be written to only means compiling again next time
      FILE *f = fopen(path, "wb");
      if (f) {
        u32 head[2] = {SHADER_CACHE_MAGIC, shader_text_hash()};
        fwrite(head, 4, 2, f);
        for (int k = 0; k < SH_COUNT; k++) { fwrite(&shader[k].size, 4, 1, f); fwrite(shader[k].code, 1, shader[k].size, f); }
        fclose(f);
      }
    }
  }
  InterlockedExchange(&shaders_state, ok ? 2 : 3);
  return 0;
}

void dl_gpu_prepare(void) {
  if (InterlockedCompareExchange(&shaders_state, 1, 0) != 0) return;
  HANDLE th = CreateThread(NULL, 0, shaders_thread, NULL, 0, NULL);
  if (th) CloseHandle(th);
  else InterlockedExchange(&shaders_state, 3);
}

// Started on first use; while the shaders are still being prepared the answer is "not yet".
static bool gpu_start(void) {
  if (gpu.tried) return gpu.ok;
  dl_gpu_prepare();
  if (shaders_state == 1) return false;
  gpu.tried = true;
  if (shaders_state != 2) return false;
  RECT rc;
  GetClientRect(dl_hwnd, &rc);
  DXGI_SWAP_CHAIN_DESC sd = {0};
  hdr_chain = hdr_want;
  // once a window has shown a flip-model swap chain, Windows keeps showing its last picture instead
  // of what a copying swap chain puts there: after HDR, the flip model is kept for the ordinary output
  static bool flipped;
  bool flip = hdr_chain || flipped;
  flipped = flip;
  sd.BufferCount = flip ? 2 : 1;
  sd.BufferDesc.Width = (UINT)(rc.right > 0 ? rc.right : 1);
  sd.BufferDesc.Height = (UINT)(rc.bottom > 0 ? rc.bottom : 1);
  sd.BufferDesc.Format = hdr_chain ? DXGI_FORMAT_R16G16B16A16_FLOAT : DXGI_FORMAT_B8G8R8A8_UNORM;
  sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
  sd.OutputWindow = dl_hwnd;
  sd.SampleDesc.Count = 1;
  sd.Windowed = TRUE;
  // copies into the window, so that GDI can still draw there when the graphics card is turned off;
  // HDR needs the flip model
  sd.SwapEffect = flip ? DXGI_SWAP_EFFECT_FLIP_DISCARD : DXGI_SWAP_EFFECT_DISCARD;
  D3D_FEATURE_LEVEL got;
  // hardware first; the software rasteriser of Windows second (remote sessions, missing driver)
  static const D3D_DRIVER_TYPE kinds[2] = {D3D_DRIVER_TYPE_HARDWARE, D3D_DRIVER_TYPE_WARP};
  HRESULT hr = E_FAIL;
  for (int k = 0; k < 2 && FAILED(hr); k++)
    hr = D3D11CreateDeviceAndSwapChain(NULL, kinds[k], NULL, 0, NULL, 0, D3D11_SDK_VERSION, &sd, &gpu.swap, &gpu.dev, &got, &gpu.ctx);
  if (FAILED(hr)) return false;
  // Alt+Enter is handled by the program itself (borderless window), not by DXGI
  IDXGIFactory *factory = NULL;
  if (SUCCEEDED(IDXGISwapChain_GetParent(gpu.swap, &IID_IDXGIFactory, (void **)&factory))) {
    IDXGIFactory_MakeWindowAssociation(factory, dl_hwnd, DXGI_MWA_NO_ALT_ENTER | DXGI_MWA_NO_WINDOW_CHANGES);
    IDXGIFactory_Release(factory);
  }
  gpu.bb_w = (int)sd.BufferDesc.Width;
  gpu.bb_h = (int)sd.BufferDesc.Height;
  bool good = gpu_target() &&
              SUCCEEDED(ID3D11Device_CreateVertexShader(gpu.dev, shader[SH_VS].code, shader[SH_VS].size, NULL, &gpu.vs)) &&
              SUCCEEDED(ID3D11Device_CreatePixelShader(gpu.dev, shader[SH_PS].code, shader[SH_PS].size, NULL, &gpu.ps)) &&
              SUCCEEDED(ID3D11Device_CreatePixelShader(gpu.dev, shader[SH_UI].code, shader[SH_UI].size, NULL, &gpu.ps_ui)) &&
              SUCCEEDED(ID3D11Device_CreatePixelShader(gpu.dev, shader[SH_SKY].code, shader[SH_SKY].size, NULL, &gpu.ps_sky));
  if (good) {
    D3D11_SAMPLER_DESC sm = {0};
    sm.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
    sm.AddressU = sm.AddressV = sm.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
    sm.MaxLOD = D3D11_FLOAT32_MAX;
    D3D11_BUFFER_DESC bd = {0};
    bd.ByteWidth = sizeof(GpuConsts);
    bd.Usage = D3D11_USAGE_DEFAULT;
    bd.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    D3D11_BLEND_DESC bl = {0}; // "over", for colours already multiplied by their opacity
    bl.RenderTarget[0].BlendEnable = TRUE;
    bl.RenderTarget[0].SrcBlend = bl.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_ONE;
    bl.RenderTarget[0].DestBlend = bl.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_INV_SRC_ALPHA;
    bl.RenderTarget[0].BlendOp = bl.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP_ADD;
    bl.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
    good = SUCCEEDED(ID3D11Device_CreateSamplerState(gpu.dev, &sm, &gpu.linear)) && SUCCEEDED(ID3D11Device_CreateBuffer(gpu.dev, &bd, NULL, &gpu.consts)) &&
           SUCCEEDED(ID3D11Device_CreateBlendState(gpu.dev, &bl, &gpu.blend));
  }
  if (!good) { gpu_shutdown(); return false; }
  gpu.ok = true;
  return true;
}

bool dl_gpu_ready(void) { return dl_cfg.gpu && !dl_audit_dir && gpu_start(); }

void dl_gpu_restart(void) {
  if (gpu.tried) gpu_shutdown();
  gpu.tried = false;
  dl_dirty = true;
  if (dl_cfg.gpu) dl_gpu_prepare();
}

void dl_gpu_stop(void) { gpu_shutdown(); }

// Makes sure texture 'which' exists at the given size and format, then fills it (bpp bytes per texel).
static bool gpu_upload_as(int which, DXGI_FORMAT format, int bpp, const void *data, int w, int h) {
  GpuTex *t = &gpu.t[which];
  if (!t->tex || t->w != w || t->h != h || t->format != (int)format) {
    GPU_RELEASE(t->srv);
    GPU_RELEASE(t->tex);
    D3D11_TEXTURE2D_DESC td = {0};
    td.Width = (UINT)w;
    td.Height = (UINT)h;
    td.MipLevels = td.ArraySize = 1;
    td.Format = format;
    td.SampleDesc.Count = 1;
    td.Usage = D3D11_USAGE_DYNAMIC;
    td.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    td.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    t->w = 0;
    if (FAILED(ID3D11Device_CreateTexture2D(gpu.dev, &td, NULL, &t->tex))) return false;
    if (FAILED(ID3D11Device_CreateShaderResourceView(gpu.dev, (ID3D11Resource *)t->tex, NULL, &t->srv))) return false;
    t->w = w;
    t->h = h;
    t->format = (int)format;
  }
  D3D11_MAPPED_SUBRESOURCE m;
  if (FAILED(ID3D11DeviceContext_Map(gpu.ctx, (ID3D11Resource *)t->tex, 0, D3D11_MAP_WRITE_DISCARD, 0, &m))) return false;
  for (int y = 0; y < h; y++) memcpy((u8 *)m.pData + (size_t)y * m.RowPitch, (const u8 *)data + (size_t)y * (size_t)w * (size_t)bpp, (size_t)w * (size_t)bpp);
  ID3D11DeviceContext_Unmap(gpu.ctx, (ID3D11Resource *)t->tex, 0);
  return true;
}

static bool gpu_upload(int which, const u32 *data, int w, int h) { return gpu_upload_as(which, DXGI_FORMAT_B8G8R8A8_UNORM, 4, data, w, h); }

// Texture t<slot> of the game's shader (from DlGame.gpu_frame).
bool dl_gpu_texture(int slot, int format, const void *data, int w, int h) {
  static const DXGI_FORMAT formats[3] = {DXGI_FORMAT_B8G8R8A8_UNORM, DXGI_FORMAT_R8_UNORM, DXGI_FORMAT_R32G32B32A32_FLOAT};
  static const int bpp[3] = {4, 1, 16};
  if (!gpu.ok || slot < 1 || slot > 7 || format < 0 || format > 2 || !data || w <= 0 || h <= 0) return false;
  return gpu_upload_as(GT_GAME + slot - 1, formats[format], bpp[format], data, w, h);
}

// The game's shared texture opened on this device (once for each handle and device).
static bool shared_open(HANDLE handle) {
  if (shared.handle == handle && (shared.tex || shared.failed)) return shared.tex != NULL;
  shared_close();
  shared.handle = handle;
  shared.failed = true; // until it works
  ID3D11Device1 *dev1 = NULL;
  if (!gpu.ok || !handle || FAILED(ID3D11Device_QueryInterface(gpu.dev, &IID_ID3D11Device1, (void **)&dev1))) return false;
  HRESULT hr = ID3D11Device1_OpenSharedResource1(dev1, handle, &IID_ID3D11Texture2D, (void **)&shared.tex);
  ID3D11Device1_Release(dev1);
  if (FAILED(hr)) { shared.tex = NULL; return false; }
  D3D11_TEXTURE2D_DESC td;
  ID3D11Texture2D_GetDesc(shared.tex, &td);
  DXGI_FORMAT view = td.Format == DXGI_FORMAT_B8G8R8A8_UNORM || td.Format == DXGI_FORMAT_B8G8R8A8_TYPELESS   ? DXGI_FORMAT_B8G8R8A8_UNORM
                     : td.Format == DXGI_FORMAT_R8G8B8A8_UNORM || td.Format == DXGI_FORMAT_R8G8B8A8_TYPELESS ? DXGI_FORMAT_R8G8B8A8_UNORM
                                                                                                             : DXGI_FORMAT_UNKNOWN;
  D3D11_SHADER_RESOURCE_VIEW_DESC vd = {0};
  vd.Format = view;
  vd.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
  vd.Texture2D.MipLevels = 1;
  bool good = view != DXGI_FORMAT_UNKNOWN && (td.BindFlags & D3D11_BIND_SHADER_RESOURCE) &&
              SUCCEEDED(ID3D11Texture2D_QueryInterface(shared.tex, &IID_IDXGIKeyedMutex, (void **)&shared.mutex)) &&
              SUCCEEDED(ID3D11Device_CreateShaderResourceView(gpu.dev, (ID3D11Resource *)shared.tex, &vd, &shared.srv));
  if (!good) { shared_close(); shared.failed = true; return false; }
  shared.failed = false;
  shared.w = (int)td.Width;
  shared.h = (int)td.Height;
  shared.format = view;
  return true;
}

bool dl_picture_shared(void *handle, double aspect) {
  if (!gpu.ok || !shared_open(handle) || shared.w > dl_game->width || shared.h > dl_game->height) return false;
  dl_picture_size(shared.w, shared.h, aspect);
  dl_pic_shared = true;
  return true;
}

// The shared picture for one drawing: true once the game has let go of it, then shared_unlock after
// the drawing. False: the picture is not there, or the game holds it too long (not drawn this time).
static bool shared_lock(void) {
  return dl_pic_shared && shared_open(shared.handle) && IDXGIKeyedMutex_AcquireSync(shared.mutex, 0, 500) == S_OK;
}
static void shared_unlock(void) {
  ID3D11ShaderResourceView *none = NULL; // nothing of the game's texture stays bound
  ID3D11DeviceContext_PSSetShaderResources(gpu.ctx, 0, 1, &none);
  IDXGIKeyedMutex_ReleaseSync(shared.mutex, 0);
}

// The shared picture read back into dl_pic, which holds it from then on (a save state's picture, a
// screenshot by GDI...): rare moments, so a copy through the processor is fine there.
void dl_pic_fetch(void) {
  if (!dl_pic_shared) return;
  bool locked = shared_lock();
  dl_pic_shared = false;
  if (!locked) return;
  D3D11_TEXTURE2D_DESC td;
  ID3D11Texture2D_GetDesc(shared.tex, &td);
  td.Usage = D3D11_USAGE_STAGING;
  td.BindFlags = 0;
  td.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
  td.MiscFlags = 0;
  ID3D11Texture2D *copy = NULL;
  if (SUCCEEDED(ID3D11Device_CreateTexture2D(gpu.dev, &td, NULL, &copy)))
    ID3D11DeviceContext_CopyResource(gpu.ctx, (ID3D11Resource *)copy, (ID3D11Resource *)shared.tex);
  shared_unlock();
  if (!copy) return;
  D3D11_MAPPED_SUBRESOURCE m;
  if (SUCCEEDED(ID3D11DeviceContext_Map(gpu.ctx, (ID3D11Resource *)copy, 0, D3D11_MAP_READ, 0, &m))) {
    bool rgba = shared.format == DXGI_FORMAT_R8G8B8A8_UNORM;
    for (int y = 0; y < shared.h; y++) {
      const u32 *src = (const u32 *)((const u8 *)m.pData + (size_t)y * m.RowPitch);
      u32 *out = dl_pic + (size_t)y * (size_t)shared.w;
      for (int x = 0; x < shared.w; x++) {
        u32 c = src[x];
        out[x] = rgba ? (c & 0xFF) << 16 | (c & 0xFF00) | (c >> 16 & 0xFF) : c & 0xFFFFFF;
      }
    }
    ID3D11DeviceContext_Unmap(gpu.ctx, (ID3D11Resource *)copy, 0);
  }
  ID3D11Texture2D_Release(copy);
}

static bool gpu_resize(int cw, int ch) {
  if (cw == gpu.bb_w && ch == gpu.bb_h) return true;
  ID3D11DeviceContext_OMSetRenderTargets(gpu.ctx, 0, NULL, NULL);
  GPU_RELEASE(gpu.rtv);
  gpu.bb_w = cw;
  gpu.bb_h = ch;
  return SUCCEEDED(IDXGISwapChain_ResizeBuffers(gpu.swap, 0, (UINT)cw, (UINT)ch, DXGI_FORMAT_UNKNOWN, 0)) && gpu_target();
}

static void gpu_consts(const GpuConsts *c) { ID3D11DeviceContext_UpdateSubresource(gpu.ctx, (ID3D11Resource *)gpu.consts, 0, NULL, c, 0, 0); }

// A quad over the rectangle x,y,w,h drawn by a pixel shader from texture 'which' (-1: none).
static void gpu_quad(int x, int y, int w, int h, ID3D11PixelShader *ps, int which, bool blend) {
  D3D11_VIEWPORT vp = {(FLOAT)x, (FLOAT)y, (FLOAT)w, (FLOAT)h, 0, 1};
  ID3D11DeviceContext_RSSetViewports(gpu.ctx, 1, &vp);
  if (which >= 0) ID3D11DeviceContext_PSSetShaderResources(gpu.ctx, 0, 1, &gpu.t[which].srv);
  ID3D11DeviceContext_OMSetBlendState(gpu.ctx, blend ? gpu.blend : NULL, NULL, 0xFFFFFFFF);
  ID3D11DeviceContext_PSSetShader(gpu.ctx, ps, NULL, 0);
  ID3D11DeviceContext_Draw(gpu.ctx, 3, 0);
}

// The game's picture in the rectangle x,y,w,h with the filters of the Picture page; the game gives
// its own constants and textures first (step of steps: smooth motion).
static void gpu_game_quad(int x, int y, int w, int h, int step, int steps) {
  GpuConsts c = {{(float)dl_pic_w, (float)dl_pic_h}, {(float)w, (float)h}, {(float)x, (float)y},
                 (float)dl_cfg.smooth, dl_crt() ? 0.0f : (float)dl_scanlines(), (float)dl_crt(), hdr_chain ? hdr_white : 0,
                 {(float)dl_pic_w / dl_pic_scale, (float)dl_pic_h / dl_pic_scale}};
  c.cvd = (float)dl_cfg.cvd;
  if (dl_game->gpu_frame) dl_game->gpu_frame(c.game, step, steps);
  gpu_consts(&c);
  bool locked = shared_lock(); // the picture in the game's own texture
  ID3D11ShaderResourceView *views[8] = {locked ? shared.srv : gpu.t[GT_PICTURE].srv};
  for (int k = 0; k < 7; k++) views[k + 1] = gpu.t[GT_GAME + k].srv;
  ID3D11DeviceContext_PSSetShaderResources(gpu.ctx, 0, 8, views);
  gpu_quad(x, y, w, h, gpu.ps, -1, false);
  if (locked) shared_unlock();
}

// The states DarkLauncher's quads rely on: at the start of each picture, and again after the game's
// own drawing (DlGame.gpu_draw), which may have changed any of them.
static void gpu_states(void) {
  ID3D11DeviceContext_OMSetRenderTargets(gpu.ctx, 1, &gpu.rtv, NULL);
  ID3D11DeviceContext_OMSetDepthStencilState(gpu.ctx, NULL, 0);
  ID3D11DeviceContext_OMSetBlendState(gpu.ctx, NULL, NULL, 0xFFFFFFFF);
  ID3D11DeviceContext_RSSetState(gpu.ctx, NULL);
  ID3D11DeviceContext_IASetInputLayout(gpu.ctx, NULL);
  ID3D11DeviceContext_IASetPrimitiveTopology(gpu.ctx, D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
  ID3D11DeviceContext_VSSetShader(gpu.ctx, gpu.vs, NULL, 0);
  ID3D11DeviceContext_GSSetShader(gpu.ctx, NULL, NULL, 0);
  ID3D11DeviceContext_HSSetShader(gpu.ctx, NULL, NULL, 0);
  ID3D11DeviceContext_DSSetShader(gpu.ctx, NULL, NULL, 0);
  ID3D11DeviceContext_PSSetConstantBuffers(gpu.ctx, 0, 1, &gpu.consts);
  ID3D11DeviceContext_PSSetSamplers(gpu.ctx, 0, 1, &gpu.linear);
}

// Screen refreshes for each picture of the game played (DlGame.gpu_steps; smooth motion).
int dl_gpu_steps(void) {
  int n = dl_mode == M_GAME && dl_game->gpu_steps && gpu.ok ? dl_game->gpu_steps() : 1;
  return n < 1 ? 1 : n > 8 ? 8 : n;
}

static double half_to_float(uint16_t h) {
  int e = (h >> 10) & 31, m = h & 1023;
  double v = e == 0 ? m / 1024.0 * pow(2, -14) : e == 31 ? 65504.0 : (1 + m / 1024.0) * pow(2, e - 15);
  return (h & 0x8000) ? -v : v;
}

// The back buffer as drawn so far (x,y,w,h of it; w 0: all of it) into a PNG file; HDR comes back to
// the usual colours with the chosen white as white.
static bool gpu_save(const char *path, int x, int y, int w, int h) {
  ID3D11Texture2D *back = NULL, *copy = NULL;
  bool ok = false;
  if (FAILED(IDXGISwapChain_GetBuffer(gpu.swap, 0, &IID_ID3D11Texture2D, (void **)&back))) return false;
  D3D11_TEXTURE2D_DESC td;
  ID3D11Texture2D_GetDesc(back, &td);
  td.Usage = D3D11_USAGE_STAGING;
  td.BindFlags = 0;
  td.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
  td.MiscFlags = 0;
  if (w <= 0) { x = y = 0; w = (int)td.Width; h = (int)td.Height; }
  if (x < 0) { w += x; x = 0; }
  if (y < 0) { h += y; y = 0; }
  if (x + w > (int)td.Width) w = (int)td.Width - x;
  if (y + h > (int)td.Height) h = (int)td.Height - y;
  if (w > 0 && h > 0 && SUCCEEDED(ID3D11Device_CreateTexture2D(gpu.dev, &td, NULL, &copy))) {
    ID3D11DeviceContext_CopyResource(gpu.ctx, (ID3D11Resource *)copy, (ID3D11Resource *)back);
    D3D11_MAPPED_SUBRESOURCE m;
    if (SUCCEEDED(ID3D11DeviceContext_Map(gpu.ctx, (ID3D11Resource *)copy, 0, D3D11_MAP_READ, 0, &m))) {
      u32 *px = malloc((size_t)w * (size_t)h * 4);
      for (int row = 0; px && row < h; row++) {
        const u8 *src = (const u8 *)m.pData + (size_t)(y + row) * m.RowPitch;
        if (td.Format == DXGI_FORMAT_R16G16B16A16_FLOAT) {
          src += (size_t)x * 8;
          for (int col = 0; col < w; col++) {
            u32 out = 0;
            for (int c = 0; c < 3; c++) {
              double v = pow(fmin(fmax(half_to_float(((const uint16_t *)src)[col * 4 + c]) / hdr_white, 0.0), 1.0), 1 / 2.2);
              out |= (u32)(v * 255 + 0.5) << (16 - c * 8);
            }
            px[(size_t)row * (size_t)w + (size_t)col] = out;
          }
        } else memcpy(px + (size_t)row * (size_t)w, src + (size_t)x * 4, (size_t)w * 4);
      }
      if (px) { ok = dl_save_png(path, px, w, h); free(px); }
      ID3D11DeviceContext_Unmap(gpu.ctx, (ID3D11Resource *)copy, 0);
    }
    ID3D11Texture2D_Release(copy);
  }
  ID3D11Texture2D_Release(back);
  return ok;
}

// ---- layers drawn by GDI: once on black, once on white; a pixel drawn with opacity a shows c*a over
// black and c*a + (1-a) over white, so the difference gives a, and the picture over black is c*a
static void layer_merge(const u32 *on_black, const u32 *on_white, u32 *out, size_t n) {
  for (size_t k = 0; k < n; k++) {
    u32 b = on_black[k], w = on_white[k];
    int d = (int)((w >> 16) & 255) - (int)((b >> 16) & 255) + (int)((w >> 8) & 255) - (int)((b >> 8) & 255) + (int)(w & 255) - (int)(b & 255);
    int a = 255 - d / 3;
    out[k] = ((u32)(a < 0 ? 0 : a > 255 ? 255 : a) << 24) | (b & 0xFFFFFF);
  }
}

// The setup screen (or the note of "Try it full size") as a layer of the whole window.
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

// Something small drawn by GDI (draw(dc, h) inside a w x bh box) made into a layer in texture 'which'.
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
      GdiFlush();
      if (!pass) memcpy(on_black, bits, (size_t)w * bh * 4);
    }
    SelectObject(mem, old);
    layer_merge(on_black, bits, layer, (size_t)w * bh);
    ok = gpu_upload(which, layer, w, bh);
  }
  if (bmp) DeleteObject(bmp);
  free(on_black);
  free(layer);
  return ok;
}

static void fps_draw(HDC dc, int h) { dl_draw_fps(dc, 0, 0, h); }
static void note_draw(HDC dc, int h) { dl_draw_note(dc, 0, 0, h); }

// The frame rate over a picture h pixels high: made again when the number changes.
static bool fps_layer(HDC wdc, int h, int *lw, int *lh) {
  static int made_fps = -1, made_h;
  int fh = h / 24 < 10 ? 10 : h / 24;
  *lw = fh * 6;
  *lh = fh * 2;
  if (made_fps == dl_fps_shown && made_h == h && gpu.t[GT_FPS].tex) return true;
  made_fps = dl_fps_shown;
  made_h = h;
  return small_layer(wdc, GT_FPS, *lw, *lh, h, fps_draw);
}

// The note over the game ("state 3 saved"): made again when it changes.
static bool note_layer(HDC wdc, int h, int *lw, int *lh) {
  static wchar_t made[160];
  static int made_h;
  const wchar_t *s = dl_note_now();
  dl_note_size(wdc, h, lw, lh);
  if (made_h == h && !lstrcmpW(made, s) && gpu.t[GT_NOTE].tex) return true;
  lstrcpynW(made, s, 160);
  made_h = h;
  return small_layer(wdc, GT_NOTE, *lw, *lh, h, note_draw);
}

static void toast_draw(HDC dc, int h) { dl_draw_toast(dc, 0, 0, h); }

// "Achievement unlocked" over the game: made again when it changes.
static bool toast_layer(HDC wdc, int h, int *lw, int *lh) {
  static wchar_t made[96];
  static int made_h;
  const wchar_t *s = dl_toast_now();
  dl_toast_size(wdc, h, lw, lh);
  if (made_h == h && !lstrcmpW(made, s) && gpu.t[GT_TOAST].tex) return true;
  lstrcpynW(made, s, 96);
  made_h = h;
  return small_layer(wdc, GT_TOAST, *lw, *lh, h, toast_draw);
}

// One picture of what dl_mode shows. rebuild: the setup screen changed (its layer is drawn again).
// False: the graphics card path is no longer usable (it has been stopped; GDI from now on).
bool dl_gpu_show(bool rebuild, bool vsync) {
  if (!gpu.ok) return false;
  RECT rc;
  GetClientRect(dl_hwnd, &rc);
  int cw = rc.right, ch = rc.bottom;
  if (cw <= 0 || ch <= 0) return true;
  static int layer_w, layer_h, layer_mode = -1;
  bool menu = dl_mode != M_GAME, good = gpu_resize(cw, ch);
  HDC wdc = GetDC(dl_hwnd);
  if (good && menu && (rebuild || !gpu.t[GT_MENU].tex || layer_w != cw || layer_h != ch || layer_mode != dl_mode)) {
    const u32 *layer = menu_layer(wdc, cw, ch);
    good = layer && gpu_upload(GT_MENU, layer, cw, ch);
    layer_w = cw;
    layer_h = ch;
    layer_mode = dl_mode;
  }
  // the picture as pixels; one in the game's shared texture is drawn from there without a copy
  if (good && !(dl_pic_shared && shared_open(shared.handle))) good = gpu_upload(GT_PICTURE, dl_pic, dl_pic_w, dl_pic_h);
  int x = 0, y = 0, w = 0, h = 0, fw = 0, fhh = 0, nw = 0, nh = 0;
  bool game = dl_mode == M_GAME || dl_mode == M_TRY || dl_mode == M_PAUSE, fps = dl_cfg.show_fps && dl_mode != M_PAUSE && !dl_shot_game;
  bool note = dl_mode == M_GAME && dl_note_now() && !dl_shot_game;
  bool toast = dl_mode == M_GAME && dl_toast_now() && !dl_shot_game;
  int tw = 0, th = 0;
  if (game) {
    dl_fit(cw, ch, dl_cfg.integer_scale, &w, &h);
    x = (cw - w) / 2;
    y = (ch - h) / 2;
    if (good && fps) good = fps_layer(wdc, h, &fw, &fhh);
    if (good && note) good = note_layer(wdc, h, &nw, &nh);
    if (good && toast) good = toast_layer(wdc, h, &tw, &th);
  }
  DlOverlay ov[DL_MAX_OVERLAYS]; // the game's pictures over its own, while it is played
  int ov_n = dl_mode == M_GAME && dl_game->overlays ? dl_game->overlays(x, y, w, h, ov, DL_MAX_OVERLAYS) : 0;
  for (int k = 0; good && k < ov_n && k < DL_MAX_OVERLAYS; k++)
    if (ov[k].pixels && ov[k].w > 0 && ov[k].h > 0) good = gpu_upload(GT_OVERLAY + k, ov[k].pixels, ov[k].w, ov[k].h);
  ReleaseDC(dl_hwnd, wdc);
  // smooth motion: the picture over several screen refreshes, each one drawn again a step further
  int steps = dl_shot_path[0] ? 1 : dl_gpu_steps();
  for (int step = 1; good && step <= steps; step++) {
    static const float black[4] = {0, 0, 0, 1};
    gpu_states();
    ID3D11DeviceContext_ClearRenderTargetView(gpu.ctx, gpu.rtv, black);
    if (game) {
      // the game's own drawing (a 3D scene...) while it is played or paused, otherwise its picture
      if ((dl_mode == M_GAME || dl_mode == M_PAUSE) && dl_game->gpu_draw &&
          dl_game->gpu_draw(gpu.dev, gpu.ctx, gpu.rtv, x, y, w, h, hdr_chain ? hdr_white : 0, step, steps))
        gpu_states();
      else gpu_game_quad(x, y, w, h, step, steps);
      if (dl_shot_path[0] && dl_shot_game) { // a screenshot: the game's picture alone, as shown
        dl_shot_ok = gpu_save(dl_shot_path, x, y, w, h);
        dl_shot_path[0] = 0;
      }
      for (int k = 0; k < ov_n && k < DL_MAX_OVERLAYS; k++)
        if (ov[k].pixels && ov[k].w > 0 && ov[k].h > 0) gpu_quad(ov[k].x, ov[k].y, ov[k].w, ov[k].h, gpu.ps_ui, GT_OVERLAY + k, true);
      if (dl_mode != M_GAME) gpu_quad(0, 0, cw, ch, gpu.ps_ui, GT_MENU, true); // the pause menu, "Press a key to go back"
      if (fps) gpu_quad(x, y, fw, fhh, gpu.ps_ui, GT_FPS, true);
      if (note) {
        int fh = h / 22 < 14 ? 14 : h / 22;
        gpu_quad(x + fh, y + h - fh * 5 / 2, nw, nh, gpu.ps_ui, GT_NOTE, true);
      }
      if (toast) gpu_quad(x + w - tw - h / 30, y + h / 30, tw, th, gpu.ps_ui, GT_TOAST, true);
    } else {
      // the sky, moving, then the setup screen, then the live preview with the game's filters
      static LARGE_INTEGER t0, fq;
      LARGE_INTEGER now;
      if (!fq.QuadPart) { QueryPerformanceFrequency(&fq); QueryPerformanceCounter(&t0); }
      QueryPerformanceCounter(&now);
      GpuConsts sky = {{1, 1}, {(float)cw, (float)ch}, {dl_cfg.still ? 0.0f : (float)((double)(now.QuadPart - t0.QuadPart) / (double)fq.QuadPart), (float)cw / (float)ch}};
      sky.hdr = hdr_chain ? hdr_white : 0;
      sky.game[2] = (float)ui_sky_style();
      if (ui_theme_hsl(&sky.game[0], &sky.game[1])) sky.game[3] = 1;
      gpu_consts(&sky);
      gpu_quad(0, 0, cw, ch, gpu.ps_sky, -1, false);
      gpu_quad(0, 0, cw, ch, gpu.ps_ui, GT_MENU, true);
      if (dl_prev_w > 0) gpu_game_quad(dl_prev_x, dl_prev_y, dl_prev_w, dl_prev_h, 1, 1);
    }
    if (dl_shot_path[0]) {
      dl_shot_ok = gpu_save(dl_shot_path, 0, 0, 0, 0);
      dl_shot_path[0] = 0;
    }
    good = SUCCEEDED(IDXGISwapChain_Present(gpu.swap, vsync || steps > 1 ? 1 : 0, 0));
  }
  if (!good) { gpu_shutdown(); dl_dirty = true; } // device lost or anything else: GDI from now on
  return good;
}

// ---- HDR
// Whether Windows shows the screen that holds the window in HDR (its "Use HDR" setting).
bool dl_windows_hdr(void) {
  static const GUID iid_output6 = {0x068346e8, 0xaaec, 0x4b84, {0xad, 0xd7, 0x13, 0x7f, 0x51, 0x3f, 0x77, 0xa1}};
  bool on = false;
  IDXGIFactory1 *f = NULL;
  if (FAILED(CreateDXGIFactory1(&IID_IDXGIFactory1, (void **)&f))) return false;
  HMONITOR mon = MonitorFromWindow(dl_hwnd, MONITOR_DEFAULTTONEAREST);
  IDXGIAdapter1 *a = NULL;
  for (UINT i = 0; !on && IDXGIFactory1_EnumAdapters1(f, i, &a) != DXGI_ERROR_NOT_FOUND; i++) {
    IDXGIOutput *o = NULL;
    for (UINT j = 0; !on && IDXGIAdapter1_EnumOutputs(a, j, &o) != DXGI_ERROR_NOT_FOUND; j++) {
      IDXGIOutput6 *o6 = NULL;
      DXGI_OUTPUT_DESC1 d;
      if (SUCCEEDED(IDXGIOutput_QueryInterface(o, &iid_output6, (void **)&o6))) {
        if (SUCCEEDED(IDXGIOutput6_GetDesc1(o6, &d)) && d.Monitor == mon) on = d.ColorSpace == DXGI_COLOR_SPACE_RGB_FULL_G2084_NONE_P2020;
        IDXGIOutput6_Release(o6);
      }
      IDXGIOutput_Release(o);
    }
    IDXGIAdapter1_Release(a);
  }
  IDXGIFactory1_Release(f);
  return on;
}

// HDR output: wanted when it is chosen and Windows shows the window's screen in HDR. Checked every
// second (the window may move to another screen, the Windows setting may change); a change starts the
// graphics card path again with the other kind of swap chain.
void dl_gpu_follow(bool now) {
  static DWORD at;
  DWORD t = GetTickCount();
  if (!now && at && t - at < 1000) return;
  at = t;
  bool want = dl_cfg.hdr && dl_cfg.gpu && dl_windows_hdr();
  hdr_white = hdr_nits[dl_cfg.hdr > 0 && dl_cfg.hdr < 5 ? dl_cfg.hdr : 2] / 80.0f;
  if (want != hdr_want) {
    hdr_want = want;
    if (gpu.tried) dl_gpu_restart();
  }
}

const wchar_t *dl_hdr_value(int v) {
  static wchar_t s[24];
  if (v <= 0 || v >= 5) return TX(T_NO);
  wsprintfW(s, L"%d cd/m\x00B2", hdr_nits[v]);
  return s;
}
