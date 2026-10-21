// PlayStation controllers read directly: DualShock 4 and DualSense, by USB or Bluetooth, through the HID
// functions of Windows (hid.dll and setupapi.dll, loaded at run time: nothing more to link). Their
// buttons come out as XInput codes (cross A, circle B, square X, triangle Y, L1 LB, R1 RB, L2 LT, R2 RT,
// Options Start, Share or Create Back, L3 and R3 the stick clicks, PS code 10, the touchpad's click code
// 11), so that they work as Xbox pads do; their rumble is sent as their output reports. The layouts of
// the reports are the ones SDL's HIDAPI drivers use. A pad that another program holds for itself
// (DS4Windows) cannot be opened: the Xbox pad that program shows is read instead.
#include "dl_int.h"
#include <setupapi.h>

typedef struct { ULONG Size; USHORT VendorID, ProductID, VersionNumber; } HidAttr;
typedef struct { USHORT Usage, UsagePage, InputReportByteLength, OutputReportByteLength, FeatureReportByteLength, rest[27]; } HidCaps;
typedef void(WINAPI *GetHidGuidFn)(GUID *);
typedef BOOLEAN(WINAPI *GetAttributesFn)(HANDLE, HidAttr *);
typedef BOOLEAN(WINAPI *GetFeatureFn)(HANDLE, void *, ULONG);
typedef BOOLEAN(WINAPI *GetPreparsedFn)(HANDLE, void **);
typedef BOOLEAN(WINAPI *FreePreparsedFn)(void *);
typedef LONG(WINAPI *GetCapsFn)(void *, HidCaps *);
typedef HDEVINFO(WINAPI *ClassDevsFn)(const GUID *, PCWSTR, HWND, DWORD);
typedef BOOL(WINAPI *EnumInterfacesFn)(HDEVINFO, PSP_DEVINFO_DATA, const GUID *, DWORD, PSP_DEVICE_INTERFACE_DATA);
typedef BOOL(WINAPI *InterfaceDetailFn)(HDEVINFO, PSP_DEVICE_INTERFACE_DATA, PSP_DEVICE_INTERFACE_DETAIL_DATA_W, DWORD, PDWORD, PSP_DEVINFO_DATA);
typedef BOOL(WINAPI *DestroyListFn)(HDEVINFO);
static struct {
  GetHidGuidFn guid;
  GetAttributesFn attributes;
  GetFeatureFn feature;
  GetPreparsedFn preparsed;
  FreePreparsedFn free_preparsed;
  GetCapsFn caps;
  ClassDevsFn class_devs;
  EnumInterfacesFn enum_interfaces;
  InterfaceDetailFn interface_detail;
  DestroyListFn destroy_list;
} hid;

#define SONY_MAX 4
typedef struct {
  HANDLE h;
  bool dualsense, bt;
  int out_len;
  OVERLAPPED rd, wr;
  bool reading, writing;
  u8 in[640], out[640];
  u32 raw;           // its XInput codes held
  s16 lx, ly, rx, ry; // its sticks, as XInput's
  int want_l, want_r; // rumble asked for, and sent (-1: nothing yet)
  int sent_l, sent_r;
  wchar_t path[MAX_PATH];
} Sony;
static Sony pads[SONY_MAX];
static int pad_n;
static bool rescan = true; // at start, then when Windows says devices came or went

void dl_sony_rescan(void) { rescan = true; }

static bool hid_load(void) {
  static int state = -1;
  if (state >= 0) return state;
  HMODULE h = LoadLibraryA("hid.dll"), s = LoadLibraryA("setupapi.dll");
  state = 0;
  if (!h || !s) return false;
  hid.guid = (GetHidGuidFn)(void *)GetProcAddress(h, "HidD_GetHidGuid");
  hid.attributes = (GetAttributesFn)(void *)GetProcAddress(h, "HidD_GetAttributes");
  hid.feature = (GetFeatureFn)(void *)GetProcAddress(h, "HidD_GetFeature");
  hid.preparsed = (GetPreparsedFn)(void *)GetProcAddress(h, "HidD_GetPreparsedData");
  hid.free_preparsed = (FreePreparsedFn)(void *)GetProcAddress(h, "HidD_FreePreparsedData");
  hid.caps = (GetCapsFn)(void *)GetProcAddress(h, "HidP_GetCaps");
  hid.class_devs = (ClassDevsFn)(void *)GetProcAddress(s, "SetupDiGetClassDevsW");
  hid.enum_interfaces = (EnumInterfacesFn)(void *)GetProcAddress(s, "SetupDiEnumDeviceInterfaces");
  hid.interface_detail = (InterfaceDetailFn)(void *)GetProcAddress(s, "SetupDiGetDeviceInterfaceDetailW");
  hid.destroy_list = (DestroyListFn)(void *)GetProcAddress(s, "SetupDiDestroyDeviceInfoList");
  state = hid.guid && hid.attributes && hid.feature && hid.preparsed && hid.free_preparsed && hid.caps && hid.class_devs &&
          hid.enum_interfaces && hid.interface_detail && hid.destroy_list;
  return state;
}

static void pad_close(int k) {
  Sony *s = &pads[k];
  if (s->reading || s->writing) CancelIo(s->h);
  CloseHandle(s->h);
  if (s->rd.hEvent) CloseHandle(s->rd.hEvent);
  if (s->wr.hEvent) CloseHandle(s->wr.hEvent);
  memset(s, 0, sizeof *s); // the slot is free again (slots never move: their reads may be under way)
  pad_n--;
}

// Sony's DualShock 4 (both models, the USB dongle): 1; DualSense and DualSense Edge: 2.
static int sony_kind(USHORT vid, USHORT pid) {
  if (vid != 0x054C) return 0;
  if (pid == 0x05C4 || pid == 0x09CC || pid == 0x0BA0) return 1;
  if (pid == 0x0CE6 || pid == 0x0DF2) return 2;
  return 0;
}

// The pads connected and not open yet. Only Sony's devices are opened (their path names the vendor).
static void scan(void) {
  rescan = false;
  if (pad_n >= SONY_MAX || !hid_load()) return;
  GUID g;
  hid.guid(&g);
  HDEVINFO list = hid.class_devs(&g, NULL, NULL, DIGCF_PRESENT | DIGCF_DEVICEINTERFACE);
  if (list == INVALID_HANDLE_VALUE) return;
  SP_DEVICE_INTERFACE_DATA ifd = {sizeof ifd};
  for (DWORD i = 0; pad_n < SONY_MAX && hid.enum_interfaces(list, NULL, &g, i, &ifd); i++) {
    union { SP_DEVICE_INTERFACE_DETAIL_DATA_W d; u8 room[sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA_W) + MAX_PATH * 2]; } det;
    det.d.cbSize = sizeof det.d;
    if (!hid.interface_detail(list, &ifd, &det.d, sizeof det, NULL, NULL)) continue;
    wchar_t low[MAX_PATH];
    lstrcpynW(low, det.d.DevicePath, MAX_PATH);
    CharLowerW(low);
    if (!wcsstr(low, L"054c")) continue; // by USB "vid_054c", by Bluetooth "..._vid&0002054c..."
    bool open = false;
    for (int k = 0; k < SONY_MAX; k++) open |= pads[k].h && !lstrcmpiW(pads[k].path, det.d.DevicePath);
    if (open) continue;
    HANDLE h = CreateFileW(det.d.DevicePath, GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, FILE_FLAG_OVERLAPPED, NULL);
    if (h == INVALID_HANDLE_VALUE) continue;
    HidAttr a = {sizeof a};
    int kind = hid.attributes(h, &a) ? sony_kind(a.VendorID, a.ProductID) : 0;
    void *pp = NULL;
    HidCaps c = {0};
    if (!kind || !hid.preparsed(h, &pp) || hid.caps(pp, &c) != 0x00110000) { // HIDP_STATUS_SUCCESS
      if (pp) hid.free_preparsed(pp);
      CloseHandle(h);
      continue;
    }
    hid.free_preparsed(pp);
    Sony *s = pads;
    while (s->h) s++; // a free slot (pad_n < SONY_MAX)
    pad_n++;
    memset(s, 0, sizeof *s);
    s->h = h;
    s->dualsense = kind == 2;
    s->out_len = c.OutputReportByteLength;
    s->bt = s->out_len != 32 && s->out_len != 48; // by USB a DualShock 4 takes 32 bytes, a DualSense 48
    s->rd.hEvent = CreateEventW(NULL, TRUE, FALSE, NULL);
    s->wr.hEvent = CreateEventW(NULL, TRUE, FALSE, NULL);
    s->sent_l = s->sent_r = -1;
    lstrcpynW(s->path, det.d.DevicePath, MAX_PATH);
    if (s->bt) { // by Bluetooth, reading this feature report turns on the full input reports
      u8 f[128] = {s->dualsense ? 0x05 : 0x02};
      int n = c.FeatureReportByteLength > 0 && c.FeatureReportByteLength <= 128 ? c.FeatureReportByteLength : (s->dualsense ? 41 : 37);
      hid.feature(h, f, (ULONG)n);
    }
  }
  hid.destroy_list(list);
}

// One input report as XInput codes; another kind of report changes nothing.
static u32 hat_bits(int hat) {
  static const u32 dirs[8] = {1, 1 | 8, 8, 2 | 8, 2, 2 | 4, 4, 1 | 4}; // up, right, down, left are XInput's 1, 8, 2, 4
  return hat >= 0 && hat < 8 ? dirs[hat] : 0;
}

static s16 axis(int v, bool up) { // 0-255 (128 at rest) as XInput's -32767..32767, up positive
  int a = (up ? 128 - v : v - 128) * 258;
  return (s16)(a > 32767 ? 32767 : a < -32767 ? -32767 : a);
}

static u32 parse(Sony *s, const u8 *r, int n) {
  int lx, ly, rx, ry, l2, r2, b0, b1, b2;
  if (s->dualsense && ((r[0] == 0x01 && n > 20) || (r[0] == 0x31 && n > 20))) { // DualSense, full report
    const u8 *d = r[0] == 0x31 ? r + 2 : r + 1;
    lx = d[0], ly = d[1], rx = d[2], ry = d[3], l2 = d[4], r2 = d[5], b0 = d[7], b1 = d[8], b2 = d[9];
  } else if (r[0] == 0x01 && n >= 10) { // DualShock 4 by USB, or the short Bluetooth report of either
    lx = r[1], ly = r[2], rx = r[3], ry = r[4], b0 = r[5], b1 = r[6], b2 = r[7], l2 = r[8], r2 = r[9];
  } else if (r[0] == 0x11 && n >= 12) { // DualShock 4 by Bluetooth
    lx = r[3], ly = r[4], rx = r[5], ry = r[6], b0 = r[7], b1 = r[8], b2 = r[9], l2 = r[10], r2 = r[11];
  } else return s->raw;
  s->lx = axis(lx, false);
  s->ly = axis(ly, true);
  s->rx = axis(rx, false);
  s->ry = axis(ry, true);
  u32 raw = hat_bits(b0 & 15);
  if (b0 & 0x20) raw |= 1u << 12; // cross: A
  if (b0 & 0x40) raw |= 1u << 13; // circle: B
  if (b0 & 0x10) raw |= 1u << 14; // square: X
  if (b0 & 0x80) raw |= 1u << 15; // triangle: Y
  if (b1 & 0x01) raw |= 1u << 8;  // L1
  if (b1 & 0x02) raw |= 1u << 9;  // R1
  if (b1 & 0x10) raw |= 1u << 5;  // Share / Create: Back
  if (b1 & 0x20) raw |= 1u << 4;  // Options: Start
  if (b1 & 0x40) raw |= 1u << 6;  // L3
  if (b1 & 0x80) raw |= 1u << 7;  // R3
  if (b2 & 0x01) raw |= 1u << 10; // PS
  if (b2 & 0x02) raw |= 1u << 11; // the touchpad's click
  if (l2 > 100) raw |= 1u << 16;
  if (r2 > 100) raw |= 1u << 17;
  if (ly < 66) raw |= 1u << 18; // the left stick, past the same point as XInput's
  if (ly > 190) raw |= 1u << 19;
  if (lx < 66) raw |= 1u << 20;
  if (lx > 190) raw |= 1u << 21;
  return raw;
}

// ---- rumble: left the heavy motor, right the light one (0-255), sent when no other write is under way
static u32 crc32_of(u32 c, const u8 *p, size_t n) {
  c = ~c;
  for (size_t i = 0; i < n; i++) {
    c ^= p[i];
    for (int k = 0; k < 8; k++) c = c & 1 ? 0xEDB88320u ^ (c >> 1) : c >> 1;
  }
  return ~c;
}

static void rumble_send(Sony *s) {
  if (s->writing) {
    DWORD done;
    if (!GetOverlappedResult(s->h, &s->wr, &done, FALSE) && GetLastError() == ERROR_IO_INCOMPLETE) return;
    s->writing = false;
  }
  if (s->want_l == s->sent_l && s->want_r == s->sent_r) return;
  u8 *out = s->out;
  int len = s->out_len > 0 && s->out_len <= (int)sizeof s->out ? s->out_len : 78;
  memset(out, 0, sizeof s->out);
  if (s->dualsense) {
    u8 *e = s->bt ? out + 2 : out + 1;
    out[0] = s->bt ? 0x31 : 0x02;
    if (s->bt) out[1] = 0x02;
    e[0] = 0x03; // rumble as a classic controller's, without the speaker's haptics
    e[2] = (u8)s->want_r;
    e[3] = (u8)s->want_l;
  } else if (s->bt) {
    out[0] = 0x11;
    out[1] = 0xC4;
    out[3] = 0x01; // rumble only: the light bar stays as it is
    out[6] = (u8)s->want_r;
    out[7] = (u8)s->want_l;
  } else {
    out[0] = 0x05;
    out[1] = 0x01;
    out[4] = (u8)s->want_r;
    out[5] = (u8)s->want_l;
  }
  if (s->bt) { // Bluetooth reports end with a CRC-32 of the HID header 0xA2 and the report's first 74 bytes
    u8 hdr = 0xA2;
    u32 c = crc32_of(crc32_of(0, &hdr, 1), out, 74);
    memcpy(out + 74, &c, 4);
  }
  ResetEvent(s->wr.hEvent);
  DWORD done;
  if (WriteFile(s->h, out, (DWORD)len, &done, &s->wr) || GetLastError() == ERROR_IO_PENDING) {
    s->writing = !HasOverlappedIoCompleted(&s->wr);
    s->sent_l = s->want_l;
    s->sent_r = s->want_r;
  }
}

void dl_sony_rumble(int left, int right) {
  for (int k = 0; k < SONY_MAX; k++) {
    if (!pads[k].h) continue;
    pads[k].want_l = left;
    pads[k].want_r = right;
    rumble_send(&pads[k]);
  }
}

// Every report waiting; each open pad's codes and sticks in out (at most max), in the order of their
// slots. Returns how many.
int dl_sony_poll(DlPadRead *out, int max) {
  if (rescan) scan();
  int got_n = 0;
  for (int k = 0; k < SONY_MAX; k++) {
    Sony *s = &pads[k];
    if (!s->h) continue;
    bool gone = false;
    for (int turn = 0; turn < 16; turn++) { // every report already there; the last one counts
      DWORD got = 0;
      if (!s->reading) {
        ResetEvent(s->rd.hEvent);
        if (ReadFile(s->h, s->in, sizeof s->in, &got, &s->rd)) { s->raw = parse(s, s->in, (int)got); continue; }
        if (GetLastError() != ERROR_IO_PENDING) { gone = true; break; }
        s->reading = true;
      }
      if (!GetOverlappedResult(s->h, &s->rd, &got, FALSE)) {
        if (GetLastError() != ERROR_IO_INCOMPLETE) gone = true;
        break;
      }
      s->reading = false;
      s->raw = parse(s, s->in, (int)got);
    }
    if (gone) { pad_close(k); continue; }
    rumble_send(s); // a change that waited for the last write
    if (got_n < max) out[got_n++] = (DlPadRead){s->raw, s->lx, s->ly, s->rx, s->ry};
  }
  return got_n;
}
