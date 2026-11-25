// x86_boot <game_dir> [max_steps] : run PM3WIN.EXE from its entry point inside the x86 interpreter with Win32/DirectDraw shims (boot probe).
// Virtual machine: registry Software\NINELIVES\Princess Maker3JP\1.0 "InstPath" = C:\PM3, drives C: (fixed) and D: (CD-ROM, D:\PM3W\ID_PM3JP.DAT),
// C:\PM3, D:\PM3W and D:\ all map to <game_dir>; files created by the game go to <game_dir>/_save. 16bpp 640x480 display, IDirectDraw/Surface/Clipper/Palette HLE.
// Debug: BP=<hex eip> dumps registers/locals once; with WATCH=<hex addr> it single-steps from the breakpoint until that word changes.
#include "pm3/midi.h"
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <sys/stat.h>
#include <strings.h>
#include <dirent.h>
#include <fstream>
#include <iterator>
#include <map>
#include <algorithm>
#include <deque>

#include "pm3/pe.h"
#include "pm3/x86.h"
#include "pm3/vfs.h"
#include "pm3/x86host.h"

using namespace pm3;

struct ApiDef { const char* name; int argc; bool cdecl_; };
static const ApiDef kApis[] = {
 // KERNEL32
 {"GetLogicalDriveStringsA",2,0},{"lstrlenA",1,0},{"GetExitCodeThread",2,0},{"GetCurrentDirectoryA",2,0},{"SetCurrentDirectoryA",1,0},{"FindFirstFileA",2,0},
 {"TerminateThread",2,0},{"FileTimeToSystemTime",2,0},{"GetFileTime",4,0},{"FindNextFileA",2,0},{"FindClose",1,0},{"GetDriveTypeA",1,0},{"FileTimeToLocalFileTime",2,0},
 {"CreateThread",6,0},{"WaitForSingleObject",2,0},{"CloseHandle",1,0},{"GetProcAddress",2,0},{"GetOEMCP",0,0},{"GetACP",0,0},{"GetCPInfo",2,0},{"FlushFileBuffers",1,0},
 {"SetStdHandle",2,0},{"RtlUnwind",4,0},{"GetFileType",1,0},{"GetStdHandle",1,0},{"SetHandleCount",1,0},{"GetEnvironmentStringsW",0,0},{"GetEnvironmentStrings",0,0},
 {"WideCharToMultiByte",8,0},{"FreeEnvironmentStringsW",1,0},{"FreeEnvironmentStringsA",1,0},{"GetModuleFileNameA",3,0},{"UnhandledExceptionFilter",1,0},{"IsBadWritePtr",2,0},
 {"HeapReAlloc",4,0},{"VirtualAlloc",4,0},{"VirtualFree",3,0},{"HeapCreate",3,0},{"HeapDestroy",1,0},{"GetLastError",0,0},{"GetCurrentProcess",0,0},{"TerminateProcess",2,0},
 {"ExitProcess",1,0},{"GetVersion",0,0},{"GetCommandLineA",0,0},{"GetStartupInfoA",1,0},{"GetModuleHandleA",1,0},{"GetLocalTime",1,0},{"GetSystemTime",1,0},
 {"GetTimeZoneInformation",1,0},{"HeapFree",3,0},{"HeapAlloc",3,0},{"LocalAlloc",2,0},{"LocalFree",1,0},{"Sleep",1,0},{"LocalSize",1,0},{"GetStringTypeW",5,0},{"LoadLibraryA",1,0},
 {"SetEndOfFile",1,0},{"MultiByteToWideChar",6,0},{"LCMapStringA",6,0},{"LCMapStringW",6,0},{"OutputDebugStringA",1,0},{"GetStringTypeA",5,0},{"WriteFile",5,0},{"CompareStringA",6,0},
 {"CompareStringW",6,0},{"GetDiskFreeSpaceA",5,0},{"SetEnvironmentVariableA",2,0},{"WriteConsoleA",5,0},{"SetFilePointer",4,0},{"ReadFile",5,0},{"CreateFileA",7,0},
 // USER32
 {"MoveWindow",6,0},{"CopyRect",2,0},{"GetSystemMetrics",1,0},{"GetSubMenu",2,0},{"GetWindowRect",2,0},{"SetWindowPos",7,0},{"AdjustWindowRectEx",4,0},{"GetMenu",1,0},{"SetFocus",1,0},
 {"SetRect",5,0},{"ShowCursor",1,0},{"GetClientRect",2,0},{"EnumDisplaySettingsA",3,0},{"CheckMenuItem",3,0},{"EnableMenuItem",3,0},{"AppendMenuA",4,0},{"wsprintfA",0,1},{"PostMessageA",4,0},
 {"GetMenuItemCount",1,0},{"CallWindowProcA",5,0},{"DefWindowProcA",4,0},{"ShowWindow",2,0},{"ReleaseDC",2,0},{"FillRect",3,0},{"GetDC",1,0},{"IntersectRect",3,0},{"GetForegroundWindow",0,0},
 {"ChangeDisplaySettingsA",2,0},{"DeleteMenu",3,0},{"FindWindowA",2,0},{"SetForegroundWindow",1,0},{"IsDlgButtonChecked",2,0},{"GetDlgItem",2,0},{"EnableWindow",2,0},{"CheckDlgButton",3,0},
 {"EndDialog",2,0},{"DialogBoxParamA",5,0},{"GetCursorPos",1,0},{"ScreenToClient",2,0},{"DrawMenuBar",1,0},{"RedrawWindow",4,0},{"DestroyMenu",1,0},{"IsIconic",1,0},{"ClientToScreen",2,0},
 {"PostQuitMessage",1,0},{"SetCursor",1,0},{"MessageBoxA",4,0},{"SystemParametersInfoA",4,0},{"LoadIconA",2,0},{"LoadCursorA",2,0},{"RegisterClassA",1,0},{"LoadMenuA",2,0},{"SetMenu",2,0},
 {"PeekMessageA",5,0},{"GetMessageA",4,0},{"TranslateMessage",1,0},{"DispatchMessageA",1,0},{"WaitMessage",0,0},{"UnionRect",3,0},{"BeginPaint",2,0},{"EndPaint",2,0},{"GetDlgItemTextA",4,0},
 {"GetParent",1,0},{"DestroyWindow",1,0},{"CreateWindowExA",12,0},{"SendMessageA",4,0},{"UpdateWindow",1,0},{"SetWindowLongA",3,0},{"GetWindowLongA",2,0},{"InvalidateRect",3,0},
 // GDI32
 {"SetPixel",4,0},{"RealizePalette",1,0},{"SetSystemPaletteUse",2,0},{"GetStockObject",1,0},{"GetSystemPaletteEntries",4,0},{"GetPixel",3,0},{"SetDIBitsToDevice",12,0},{"GetDeviceCaps",2,0},
 {"DeleteObject",1,0},{"CreateDIBSection",6,0},{"SetStretchBltMode",2,0},{"DeleteDC",1,0},{"BitBlt",9,0},{"SelectPalette",3,0},{"CreateCompatibleDC",1,0},{"SetDIBColorTable",4,0},
 {"SelectObject",2,0},{"SetPaletteEntries",4,0},{"CreateFontA",14,0},{"GetDIBColorTable",4,0},{"GetNearestPaletteIndex",2,0},{"SetTextColor",2,0},{"CreatePalette",1,0},{"ResizePalette",2,0},{"SetBkColor",2,0},
 // ADVAPI32 / WINMM / IMM32 / DSOUND / DDRAW
 {"RegSetValueExA",6,0},{"RegOpenKeyExA",5,0},{"RegCloseKey",1,0},{"RegQueryValueExA",6,0},
 {"waveOutPrepareHeader",3,0},{"waveOutOpen",6,0},{"waveOutUnprepareHeader",3,0},{"waveOutClose",1,0},{"waveOutReset",1,0},{"waveOutWrite",3,0},{"mciGetErrorStringA",3,0},{"timeGetTime",0,0},
 {"mciGetDeviceIDA",1,0},{"mciSendCommandA",4,0},{"ImmAssociateContext",2,0},{"ImmGetDefaultIMEWnd",1,0},{"DirectSoundCreate",3,0},{"DirectDrawCreate",3,0},
 {"?MIDI_FileOpen@@YAIPAXPAD@Z",2,1},{"?MIDI_Play@@YAIPAXHJ@Z",3,1},{"?MIDI_Stop@@YAIPAX@Z",1,1},{"?MIDI_EndDestroy@@YAXPAX@Z",1,1},{"?MIDI_InitCreate@@YAPAXPAX@Z",1,1},
 {"?MIDI_GetIsPlay@@YAHPAX@Z",1,1},{"?MIDI_CallBack@@YAXPAXPAUmidihdr_tag@@@Z",2,1},
};

static std::string cstr(const X86& c, uint32_t a, size_t max = 80) { std::string s; for (size_t i = 0; i < max && a + i < c.mem.size() && c.mem[a + i]; i++) s += (char)c.mem[a + i]; return s; }

namespace pm3 {
static bool g_quiet = false;   // set per run: shell mode without PM3_VERBOSE
static void hostlog(const char* fmt, ...) { if (g_quiet) return; va_list ap; va_start(ap, fmt); vprintf(fmt, ap); va_end(ap); }

int x86host_main(int argc, char** argv, const X86Hooks* hooks) {
    g_quiet = hooks && !hooks->verbose && !getenv("PM3_VERBOSE");
    if (argc < 2) { fprintf(stderr, "usage: x86_boot <game_dir> [max_steps]\n"); return 2; }
    std::string gd = argv[1]; uint64_t maxs = argc > 2 ? strtoull(argv[2], 0, 10) : (hooks ? 0ull : 20000000ull);
    std::ifstream ef(gd + "/PM3WIN.EXE", std::ios::binary); std::vector<uint8_t> exe((std::istreambuf_iterator<char>(ef)), {});
    X86 cpu(0x4000000); PeImage img; std::string err;
    if (!PeImage::load(exe, cpu, img, &err)) { fprintf(stderr, "PE: %s\n", err.c_str()); return 1; }
    cpu.r[4] = 0x3F00000;
    std::map<std::string, ApiDef> defs; for (auto& d : kApis) defs[d.name] = d;
    uint32_t heap = 0x800000, tmp = 0x3E00000;   // bump allocator, scratch area for strings
    std::map<uint32_t, uint32_t> asz;   // allocation sizes for LocalSize/HeapSize
    std::map<uint32_t, std::vector<uint32_t>> freel;   // rounded size -> freed blocks (the game loads/frees large assets constantly; a pure bump heap ran into the stack)
    auto alloc = [&](uint32_t n) { uint32_t rn = (n + 15) & ~15u; auto& fl = freel[rn]; uint32_t p;
        if (!fl.empty()) { p = fl.back(); fl.pop_back(); std::memset(&cpu.mem[p], 0, rn); } else { p = heap; heap += rn; if (heap > 0x3D00000) hostlog("[heap exhausted: %u]\n", heap); }
        asz[p] = n; return p; };
    auto release = [&](uint32_t p) { auto it = asz.find(p); if (it == asz.end() || p < 0x800000 || p >= 0x3E00000) return; freel[(it->second + 15) & ~15u].push_back(p); asz.erase(it); };
    uint32_t cmdline = tmp; const char* cl = "PM3WIN.EXE"; cpu.write_bytes(cmdline, (const uint8_t*)cl, strlen(cl) + 1);
    uint32_t envblk = tmp + 0x100; // "\0\0"
    std::vector<std::string> ring; int calls = 0; bool stop = false; std::map<std::string, int> seen;


    // ---- virtual file system: C:\PM3\ , D:\PM3W\ , D:\ and relative paths all map to the game folder; created files go to <game>/_save ----
    struct VFile { FILE* f; std::string path; };
    std::map<uint32_t, VFile> files; uint32_t nextH = 0x900;
    pm3::Vfs vfs; vfs.set_root(gd); vfs.set_save_dir(hooks && !hooks->save_dir.empty() ? hooks->save_dir : gd + "/_save");   // case-insensitive lookup, drive/PM3 aliases, writes -> <game>/_save (iOS: Documents), reads prefer saves
    auto host_path = [&](const std::string& p, bool for_write) -> std::string { std::string r = for_write ? vfs.make_writable(p) : std::string(); return r.empty() ? vfs.find(p) : r; };
    std::function<bool()> pump;   // hands the primary surface to the shell between slices of a long nested guest call (set once the surfaces exist); false = the shell wants to stop
    int msgboxes = 0; uint64_t cg_limit = 20000000ull;   // step cap of a nested guest call (loader threads decompress whole screens: raised for CreateThread)
    // nested call into guest code from inside a host function (stdcall callbacks); saves/restores all CPU state
    auto call_guest = [&](X86& c, uint32_t fn, std::initializer_list<uint32_t> args) -> uint32_t {
        uint32_t sr[8]; std::memcpy(sr, c.r, sizeof sr); uint32_t eip0 = c.eip; bool fl[7] = {c.CF, c.PF, c.AF, c.ZF, c.SF, c.DF, c.OF};
        std::vector<uint32_t> a(args); for (size_t i = a.size(); i-- > 0;) c.push32(a[i]);
        c.push32(X86::RET_STOP); c.eip = fn; bool ok; uint64_t used = 0, sl = (pump && hooks && hooks->slice_steps) ? hooks->slice_steps : cg_limit;
        for (;;) {   // a loader thread can run for seconds: slice it so the shell can still redraw, pause and quit
            uint64_t n = std::min<uint64_t>(sl, cg_limit - used), b0 = c.steps; ok = c.run(X86::RET_STOP, n); used += c.steps - b0;
            if (ok || c.err != "step limit" || used >= cg_limit) break;
            c.err.clear(); if (pump && !pump()) { ok = false; c.err = "stopped by the shell"; break; }
        }
        uint32_t ret = c.r[0];
        std::memcpy(c.r, sr, sizeof sr); c.eip = eip0; c.CF = fl[0]; c.PF = fl[1]; c.AF = fl[2]; c.ZF = fl[3]; c.SF = fl[4]; c.DF = fl[5]; c.OF = fl[6];
        if (!ok) { hostlog("  [callback failed: %s]\n", c.err.c_str()); stop = true; }
        return ret;
    };
    // ---- cooperative guest threads (shell mode): the game starts a loader thread and polls WaitForSingleObject(h, 0) every frame, so the thread
    // is simply run in slices between the main thread's slices; the main thread keeps drawing and answering input while a screen loads ----
    struct GThread { uint32_t r[8]; uint32_t eip; bool fl[7]; bool done; uint32_t exit; uint32_t handle; };
    std::vector<GThread> gthreads; bool async_threads = hooks && hooks->on_frame;
    auto find_thread = [&](uint32_t h) -> GThread* { for (auto& t : gthreads) if (t.handle == h) return &t; return nullptr; };
    auto run_thread_slice = [&](X86& c, GThread& t, uint64_t n) {
        uint32_t sr[8]; std::memcpy(sr, c.r, sizeof sr); uint32_t e0 = c.eip; bool fl[7] = {c.CF, c.PF, c.AF, c.ZF, c.SF, c.DF, c.OF};
        std::memcpy(c.r, t.r, sizeof sr); c.eip = t.eip; c.CF = t.fl[0]; c.PF = t.fl[1]; c.AF = t.fl[2]; c.ZF = t.fl[3]; c.SF = t.fl[4]; c.DF = t.fl[5]; c.OF = t.fl[6];
        bool ok = c.run(X86::RET_STOP, n);
        if (ok) { t.done = true; t.exit = c.r[0]; }
        else if (c.err == "step limit") { c.err.clear(); std::memcpy(t.r, c.r, sizeof sr); t.eip = c.eip; t.fl[0] = c.CF; t.fl[1] = c.PF; t.fl[2] = c.AF; t.fl[3] = c.ZF; t.fl[4] = c.SF; t.fl[5] = c.DF; t.fl[6] = c.OF; }
        else { hostlog("  [thread %x failed: %s @%08x]\n", t.handle, c.err.c_str(), c.eip); c.err.clear(); t.done = true; t.exit = 0xDEAD; }
        std::memcpy(c.r, sr, sizeof sr); c.eip = e0; c.CF = fl[0]; c.PF = fl[1]; c.AF = fl[2]; c.ZF = fl[3]; c.SF = fl[4]; c.DF = fl[5]; c.OF = fl[6];
    };
    auto run_threads = [&](X86& c, uint64_t n) { for (auto& t : gthreads) if (!t.done) run_thread_slice(c, t, n); };
    // ---- DirectDraw HLE (IDirectDraw v1/v2 + IDirectDrawSurface v1 vtable layouts) ----
    static const char* DDN[] = {"QueryInterface","AddRef","Release","Compact","CreateClipper","CreatePalette","CreateSurface","DuplicateSurface","EnumDisplayModes","EnumSurfaces","FlipToGDISurface","GetCaps","GetDisplayMode","GetFourCCCodes","GetGDISurface","GetMonitorFrequency","GetScanLine","GetVerticalBlankStatus","Initialize","RestoreDisplayMode","SetCooperativeLevel","SetDisplayMode","WaitForVerticalBlank","GetAvailableVidMem"};
    static const int DDA[] = {3,1,1,1,4,5,4,3,5,5,1,3,2,3,2,2,2,2,2,1,3,4,3,4};
    static const char* SFN[] = {"QueryInterface","AddRef","Release","AddAttachedSurface","AddOverlayDirtyRect","Blt","BltBatch","BltFast","DeleteAttachedSurface","EnumAttachedSurfaces","EnumOverlayZOrders","Flip","GetAttachedSurface","GetBltStatus","GetCaps","GetClipper","GetColorKey","GetDC","GetFlipStatus","GetOverlayPosition","GetPalette","GetPixelFormat","GetSurfaceDesc","Initialize","IsLost","Lock","ReleaseDC","Restore","SetClipper","SetColorKey","SetOverlayPosition","SetPalette","Unlock","UpdateOverlay","UpdateOverlayDisplay","UpdateOverlayZOrder"};
    static const int SFA[] = {3,1,1,2,2,6,4,6,3,4,4,3,3,2,2,2,3,2,2,3,2,2,2,3,1,5,2,1,2,3,3,2,2,6,2,3};
    static const char* CLN[] = {"QueryInterface","AddRef","Release","GetClipList","GetHWnd","Initialize","IsClipListChanged","SetClipList","SetHWnd"};
    static const int CLA[] = {3,1,1,4,2,3,2,3,3};
    static const char* PAN[] = {"QueryInterface","AddRef","Release","GetCaps","GetEntries","Initialize","SetEntries"};
    static const int PAA[] = {3,1,1,2,5,4,5};
    struct Surf { uint32_t obj, mem; int w, h, bpp, pitch; bool primary; uint32_t back; uint32_t ck_lo = 0, ck_hi = 0; bool has_key = false; };   // ck_*: source colour key (DDCKEY_SRCBLT)
    std::map<uint32_t, Surf> surfs; int modeW = 640, modeH = 480, modeBpp = 16; std::map<std::string, int> ddseen; std::vector<std::string> ddorder;
    auto ddlog = [&](const std::string& n) { if (!ddseen[n]++) ddorder.push_back(n); };
    uint32_t ddobj = 0; std::function<uint32_t(uint32_t, int, int, bool)> new_surf;
    auto fill_pf = [&](X86& c, uint32_t pf, int bpp) {
        c.wr32(pf, 32); c.wr32(pf + 4, 0x40); c.wr32(pf + 8, 0); c.wr32(pf + 12, bpp);
        c.wr32(pf + 16, bpp == 16 ? 0xF800 : 0xFF0000); c.wr32(pf + 20, bpp == 16 ? 0x07E0 : 0xFF00); c.wr32(pf + 24, bpp == 16 ? 0x001F : 0xFF); c.wr32(pf + 28, 0);
    };
    auto make_plain = [&](const char* const* names, const int* argc, int n, const char* prefix) -> uint32_t {   // objects whose methods all succeed (clipper, palette)
        uint32_t vt = alloc(4 * n), obj = alloc(16); cpu.wr32(obj, vt);
        for (int i = 0; i < n; i++) { std::string nm = std::string(prefix) + names[i]; int ac = argc[i];
            uint32_t h = cpu.add_host([&, nm, ac](X86& c) { ddlog(nm); if (calls < 4000) hostlog("  [com] %s -> 0\n", nm.c_str()); calls++; c.ret_stdcall(ac, 0); });
            cpu.wr32(vt + 4 * i, h); }
        return obj;
    };
    auto make_com = [&](const char* const* names, const int* argc, int n, bool surface) -> uint32_t {
        uint32_t vt = alloc(4 * n), obj = alloc(16); cpu.wr32(obj, vt);
        for (int i = 0; i < n; i++) {
            std::string nm = std::string(surface ? "Surface::" : "DDraw::") + names[i]; int ac = argc[i];
            uint32_t h = cpu.add_host([&, nm, i, ac, surface](X86& c) {
                uint32_t ret = 0; ddlog(nm); uint32_t self = c.arg(0);
                if (!surface) {
                    if (i == 4) { c.wr32(c.arg(2), make_plain(CLN, CLA, 9, "Clipper::")); }
                    else if (i == 5) { c.wr32(c.arg(3), make_plain(PAN, PAA, 7, "Palette::")); }
                    else if (i == 21) { modeW = c.arg(1); modeH = c.arg(2); modeBpp = c.arg(3); }
                    else if (i == 6) { uint32_t d = c.arg(1), flags = c.rd32(d + 4), caps = c.rd32(d + 104);
                        bool prim = caps & 0x200; int w = prim ? modeW : (int)c.rd32(d + 12), h = prim ? modeH : (int)c.rd32(d + 8);
                        uint32_t o = new_surf(0, w, h, prim);
                        if (prim && (flags & 0x20) && c.rd32(d + 20) > 0) { uint32_t b = new_surf(0, w, h, false); surfs[o].back = b; }
                        c.wr32(c.arg(2), o); }
                    else if (i == 12) { uint32_t d = c.arg(1); for (uint32_t q = 4; q < 108; q += 4) c.wr32(d + q, 0); c.wr32(d, 108); c.wr32(d + 4, 0x100e); c.wr32(d + 8, modeH); c.wr32(d + 12, modeW); c.wr32(d + 16, modeW * modeBpp / 8); fill_pf(c, d + 72, modeBpp); }
                    else if (i == 8) {   // EnumDisplayModes(flags, desc, ctx, cb): offer 640x480 at 8/16/32 bpp
                        uint32_t cb = c.arg(4), ctx = c.arg(3), d = 0x1E00800; static const int bpps[] = {8, 16, 32};
                        for (int b : bpps) { for (uint32_t q = 4; q < 108; q += 4) c.wr32(d + q, 0); c.wr32(d, 108); c.wr32(d + 4, 0x100e);
                            c.wr32(d + 8, 480); c.wr32(d + 12, 640); c.wr32(d + 16, 640 * b / 8); fill_pf(c, d + 72, b); if (b == 8) c.wr32(d + 76, 0x20);
                            hostlog("  [enum] mode 640x480x%d\n", b); if (!call_guest(c, cb, {d, ctx}) || stop) break; } }
                    else if (i == 11) { uint32_t d = c.arg(1), sz = c.rd32(d); if (sz == 0 || sz > 0x200) sz = 0x180; for (uint32_t q = 4; q < sz; q += 4) c.wr32(d + q, 0);
                        c.wr32(d + 4, 0x1 | 0x40 | 0x400000 | 0x4000000 | 0x8000000); c.wr32(d + 0x30, 4u << 20); c.wr32(d + 0x34, 4u << 20); }
                    else if (i == 0) { c.wr32(c.arg(2), self); }
                } else {
                    Surf& sf = surfs[self];
                    if (getenv("TRACE_API") && (i == 5 || i == 7 || i == 25)) { static int nt = 0; if (nt++ < 40) hostlog("  [trace] Surface::%s ret@%x self=%x dst/src=%x %x\n", SFN[i], c.rd32(c.r[4]), self, c.arg(1), c.arg(2)); }
                    static std::map<uint32_t, int> srefs;   // COM refcount; the pixel memory of a fully released surface goes back to the heap
                    if (i == 1) { int& r = srefs.emplace(self, 1).first->second; ret = (uint32_t)++r; }
                    else if (i == 2) { int& r = srefs.emplace(self, 1).first->second; if (r > 0) r--; ret = (uint32_t)r; if (r == 0 && sf.mem && !sf.primary) { release(sf.mem); sf.mem = 0; sf.w = sf.h = 0; } }
                    else if (i == 25 && getenv("LOCKLOG") && (hostlog("  [lock] surf=%x %dx%d mem=%x ret@%x\n", self, sf.w, sf.h, sf.mem, c.rd32(c.r[4])), false)) {}
                    else if (i == 25) { uint32_t d = c.arg(2); for (uint32_t q = 4; q < 108; q += 4) c.wr32(d + q, 0); c.wr32(d, 108); c.wr32(d + 4, 0x100f | 0x800 | 1);
                        c.wr32(d + 8, sf.h); c.wr32(d + 12, sf.w); c.wr32(d + 16, sf.pitch); c.wr32(d + 36, sf.mem); fill_pf(c, d + 72, sf.bpp); }
                    else if (i == 22) { uint32_t d = c.arg(1); c.wr32(d + 4, 0x100f); c.wr32(d + 8, sf.h); c.wr32(d + 12, sf.w); c.wr32(d + 16, sf.pitch); fill_pf(c, d + 72, sf.bpp); }
                    else if (i == 21) fill_pf(c, c.arg(1), sf.bpp);
                    else if (i == 12) { if (sf.back) c.wr32(c.arg(2), sf.back); else ret = 0x887602E7; }
                    else if (i == 17) c.wr32(c.arg(1), 0x6000 + self);
                    else if (i == 29) { if ((c.arg(1) & 8) && c.arg(2)) { sf.ck_lo = c.rd32(c.arg(2)); sf.ck_hi = c.rd32(c.arg(2) + 4); sf.has_key = true; } else if ((c.arg(1) & 8) && !c.arg(2)) sf.has_key = false; }
                    else if (i == 5 && (c.arg(4) & 0x400)) {   // Blt with DDBLT_COLORFILL: fill the dest rect (NULL = whole surface)
                        uint32_t dr = c.arg(1), col = c.arg(5) ? c.rd32(c.arg(5) + 80) : 0; int x0 = 0, y0 = 0, x1 = sf.w, y1 = sf.h;
                        if (dr) { x0 = (int)c.rd32(dr); y0 = (int)c.rd32(dr + 4); x1 = (int)c.rd32(dr + 8); y1 = (int)c.rd32(dr + 12); }
                        x0 = std::max(x0, 0); y0 = std::max(y0, 0); x1 = std::min(x1, sf.w); y1 = std::min(y1, sf.h); int bp = sf.bpp / 8;
                        for (int y = y0; y < y1; y++) for (int x = x0; x < x1; x++) { if (bp == 2) c.wr(sf.mem + y * sf.pitch + x * 2, 2, col); else c.wr(sf.mem + y * sf.pitch + x * bp, 4, col); }
                    }
                    else if (i == 5 || i == 7) {   // Blt / BltFast: rect copy when both surfaces are ours and same depth
                        if (getenv("BLTLOG") && i == 7) hostlog("  [bltfast] dst=%x%s at=(%d,%d) src=%x trans=%x ret@%x srcKey=%d\n", self, sf.primary ? "(primary)" : "", (int)c.arg(1), (int)c.arg(2), c.arg(3), c.arg(5), c.rd32(c.r[4]), surfs.count(c.arg(3)) ? 1 : 0);
                        if (getenv("BLTLOG") && i == 5) { uint32_t dr = c.arg(1), sr = c.arg(3); hostlog("  [blt] dst=%x%s rect=(%d,%d,%d,%d) src=%x%s rect=(%d,%d,%d,%d) flags=%x fx_fill=%x ret@%x\n", self, sf.primary ? "(primary)" : "",
                            dr ? (int)c.rd32(dr) : -1, dr ? (int)c.rd32(dr + 4) : -1, dr ? (int)c.rd32(dr + 8) : -1, dr ? (int)c.rd32(dr + 12) : -1, c.arg(2), surfs.count(c.arg(2)) ? "" : "(none)",
                            sr ? (int)c.rd32(sr) : -1, sr ? (int)c.rd32(sr + 4) : -1, sr ? (int)c.rd32(sr + 8) : -1, sr ? (int)c.rd32(sr + 12) : -1, c.arg(4), c.arg(5) ? c.rd32(c.arg(5) + 80) : 0, c.rd32(c.r[4])); }
                        uint32_t src = i == 5 ? c.arg(2) : c.arg(3); uint32_t srect = i == 5 ? c.arg(3) : c.arg(4);
                        uint32_t flags = i == 5 ? c.arg(4) : c.arg(5), fx = i == 5 ? c.arg(5) : 0;
                        if (surfs.count(src) && sf.mem) { Surf& ss = surfs[src]; if (ss.mem) {
                            // destination rect (Blt: lpDestRect, NULL = whole surface; BltFast: top-left point + source size)
                            int dl = 0, dt = 0, dr_ = sf.w, db = sf.h, sl = 0, st = 0, sr_ = ss.w, sb = ss.h;
                            if (srect) { sl = (int)c.rd32(srect); st = (int)c.rd32(srect + 4); sr_ = (int)c.rd32(srect + 8); sb = (int)c.rd32(srect + 12); }
                            if (i == 5) { if (c.arg(1)) { dl = (int)c.rd32(c.arg(1)); dt = (int)c.rd32(c.arg(1) + 4); dr_ = (int)c.rd32(c.arg(1) + 8); db = (int)c.rd32(c.arg(1) + 12); }
                                          else if (!srect) { /* both NULL: whole surfaces, stretched to fit */ } }
                            else { dl = (int)c.arg(1); dt = (int)c.arg(2); dr_ = dl + (sr_ - sl); db = dt + (sb - st); }
                            int dw = dr_ - dl, dh = db - dt, sw = sr_ - sl, sh = sb - st;
                            if (dw > 0 && dh > 0 && sw > 0 && sh > 0) {
                                int bp = sf.bpp / 8, sbp = ss.bpp / 8; uint32_t szr = bp == 2 ? 2 : 4;
                                // colour keys: surface key (DDBLT_KEYSRC / DDBLTFAST_SRCCOLORKEY) or the one in the DDBLTFX (DDBLT_KEYSRCOVERRIDE, fx+92)
                                bool skey = false, dkey = false; uint32_t sk_lo = 0, sk_hi = 0, dk_lo = 0, dk_hi = 0;
                                if (i == 5) {
                                    if ((flags & 0x10000) && fx) { skey = true; sk_lo = c.rd32(fx + 92); sk_hi = c.rd32(fx + 96); }
                                    else if ((flags & 0x8000) && ss.has_key) { skey = true; sk_lo = ss.ck_lo; sk_hi = ss.ck_hi; }
                                    if ((flags & 0x4000) && fx) { dkey = true; dk_lo = c.rd32(fx + 84); dk_hi = c.rd32(fx + 88); }
                                    else if ((flags & 0x2000) && sf.has_key) { dkey = true; dk_lo = sf.ck_lo; dk_hi = sf.ck_hi; }
                                } else if ((flags & 1) && ss.has_key) { skey = true; sk_lo = ss.ck_lo; sk_hi = ss.ck_hi; }
                                else if ((flags & 2) && sf.has_key) { dkey = true; dk_lo = sf.ck_lo; dk_hi = sf.ck_hi; }
                                bool mirx = i == 5 && (flags & 0x800) && (fx ? (c.rd32(fx + 4) & 0x2) : 0), miry = i == 5 && (flags & 0x800) && (fx ? (c.rd32(fx + 4) & 0x4) : 0);   // DDBLT_DDFX + DDBLTFX_MIRRORLEFTRIGHT / UPDOWN
                                const bool same = (dw == sw && dh == sh && !mirx && !miry);
                                for (int y = 0; y < dh; y++) { int ty = dt + y; if (ty < 0 || ty >= sf.h) continue;
                                    int syy = same ? st + y : st + (int)((int64_t)(miry ? dh - 1 - y : y) * sh / dh); if (syy < 0 || syy >= ss.h) continue;
                                    if (same && !skey && !dkey && sbp == bp) {   // fast path: clipped row copy
                                        int x0 = std::max(0, std::max(-dl, -sl)), x1 = std::min(dw, std::min(sf.w - dl, ss.w - sl)); if (x1 > x0)
                                            std::memmove(&c.mem[sf.mem + ty * sf.pitch + (dl + x0) * bp], &c.mem[ss.mem + syy * ss.pitch + (sl + x0) * bp], (size_t)(x1 - x0) * bp);
                                        continue; }
                                    for (int x = 0; x < dw; x++) { int tx = dl + x; if (tx < 0 || tx >= sf.w) continue;
                                        int sxx = same ? sl + x : sl + (int)((int64_t)(mirx ? dw - 1 - x : x) * sw / dw); if (sxx < 0 || sxx >= ss.w) continue;
                                        uint32_t v = c.rd(ss.mem + syy * ss.pitch + sxx * sbp, sbp == 2 ? 2 : 4);
                                        if (skey && v >= sk_lo && v <= sk_hi) continue;
                                        if (dkey) { uint32_t o = c.rd(sf.mem + ty * sf.pitch + tx * bp, szr); if (!(o >= dk_lo && o <= dk_hi)) continue; }
                                        c.wr(sf.mem + ty * sf.pitch + tx * bp, szr, v); } }
                            } } }
                    }
                }
                if (calls < 4000) hostlog("  [com] %s -> %x\n", nm.c_str(), ret);
                calls++; c.ret_stdcall(ac, ret);
            });
            cpu.wr32(vt + 4 * i, h);
        }
        return obj;
    };
    new_surf = [&](uint32_t, int w, int h, bool prim) {
        uint32_t o = make_com(SFN, SFA, 36, true); int bpp = modeBpp; int pitch = w * (bpp / 8);
        surfs[o] = Surf{o, alloc(pitch * h), w, h, bpp, pitch, prim, 0}; return o;
    };


    // ---- DirectSound HLE (IDirectSound / IDirectSoundBuffer vtable layouts, DX3-5) ----
    static const char* DSN[] = {"QueryInterface","AddRef","Release","CreateSoundBuffer","GetCaps","DuplicateSoundBuffer","SetCooperativeLevel","Compact","GetSpeakerConfig","SetSpeakerConfig","Initialize"};
    static const int DSA[] = {3,1,1,4,2,3,3,1,2,2,2};
    static const char* DBN[] = {"QueryInterface","AddRef","Release","GetCaps","GetCurrentPosition","GetFormat","GetVolume","GetPan","GetFrequency","GetStatus","Initialize","Lock","Play","SetCurrentPosition","SetFormat","SetVolume","SetPan","SetFrequency","Stop","Unlock","Restore"};
    static const int DBA[] = {3,1,1,2,3,4,2,2,2,2,3,8,4,2,2,2,2,2,1,5,1};
    struct DsBuf { uint32_t obj, mem, size, fmt; bool primary, playing, looping; uint32_t pos; };
    std::map<uint32_t, DsBuf> dsbufs; uint32_t dsobj = 0; std::function<uint32_t(uint32_t, uint32_t, bool)> new_dsbuf;
    auto make_ds_buf = [&]() -> uint32_t {
        uint32_t vt = alloc(4 * 21), obj = alloc(16); cpu.wr32(obj, vt);
        for (int i = 0; i < 21; i++) {
            std::string nm = std::string("DSBuf::") + DBN[i]; int ac = DBA[i];
            uint32_t h = cpu.add_host([&, nm, i, ac](X86& c) {
                uint32_t ret = 0; ddlog(nm); uint32_t self = c.arg(0); DsBuf& b = dsbufs[self];
                if (i == 0) c.wr32(c.arg(2), self);
                else if (i == 3) { uint32_t d = c.arg(1); for (uint32_t q = 4; q < 20; q += 4) c.wr32(d + q, 0); c.wr32(d, 20); c.wr32(d + 4, 0x4C | 0x100); c.wr32(d + 8, b.size); }
                else if (i == 4) { if (c.arg(1)) c.wr32(c.arg(1), b.pos); if (c.arg(2)) c.wr32(c.arg(2), b.pos); }
                else if (i == 5) { if (c.arg(1) && b.fmt) { uint32_t n = std::min<uint32_t>(c.arg(2), 18); std::memcpy(&c.mem[c.arg(1)], &c.mem[b.fmt], n); if (c.arg(3)) c.wr32(c.arg(3), 18); } }
                else if (i == 6 || i == 7) c.wr32(c.arg(1), 0);
                else if (i == 8) c.wr32(c.arg(1), 22050);
                else if (i == 9) c.wr32(c.arg(1), (b.playing ? 1u : 0u) | (b.looping ? 4u : 0u));
                else if (i == 11) {   // Lock(offset, bytes, &p1, &b1, &p2, &b2, flags)
                    uint32_t off = c.arg(1), n = c.arg(2); if (b.size == 0) { ret = 0x88780032; }
                    else { if (off >= b.size) off = 0; uint32_t n1 = std::min(n, b.size - off), n2 = n - n1;
                        c.wr32(c.arg(3), b.mem + off); c.wr32(c.arg(4), n1);
                        { if (c.arg(5)) c.wr32(c.arg(5), n2 ? b.mem : 0); if (c.arg(6)) c.wr32(c.arg(6), n2); } } }
                else if (i == 12) { b.playing = true; b.looping = (c.arg(4) & 1) != 0; }
                else if (i == 13) b.pos = c.arg(1);
                else if (i == 14) b.fmt = c.arg(1) ? c.arg(1) : b.fmt;
                else if (i == 18) b.playing = false;
                if (calls < 4000) hostlog("  [com] %s -> %x\n", nm.c_str(), ret);
                calls++; c.ret_stdcall(ac, ret);
            });
            cpu.wr32(vt + 4 * i, h);
        }
        return obj;
    };
    new_dsbuf = [&](uint32_t size, uint32_t fmt, bool prim) {
        uint32_t o = make_ds_buf(); dsbufs[o] = DsBuf{o, size ? alloc(size) : 0, size, fmt, prim, false, false, 0}; return o;
    };
    auto make_ds = [&]() -> uint32_t {
        uint32_t vt = alloc(4 * 11), obj = alloc(16); cpu.wr32(obj, vt);
        for (int i = 0; i < 11; i++) {
            std::string nm = std::string("DSound::") + DSN[i]; int ac = DSA[i];
            uint32_t h = cpu.add_host([&, nm, i, ac](X86& c) {
                uint32_t ret = 0; ddlog(nm); uint32_t self = c.arg(0);
                if (i == 0) c.wr32(c.arg(2), self);
                else if (i == 3) { uint32_t d = c.arg(1), flags = c.rd32(d + 4), size = c.rd32(d + 8), fmt = c.rd32(d + 16);
                    c.wr32(c.arg(2), new_dsbuf(size, fmt, (flags & 1) != 0)); }
                else if (i == 4) { uint32_t d = c.arg(1), sz = c.rd32(d); if (sz == 0 || sz > 0x100) sz = 96; for (uint32_t q = 4; q < sz; q += 4) c.wr32(d + q, 0);
                    c.wr32(d + 4, 0x1 | 0x2 | 0x4 | 0x8 | 0x10 | 0x20 | 0x40 | 0x80); c.wr32(d + 8, 8000); c.wr32(d + 12, 48000); }
                else if (i == 5) { uint32_t src = c.arg(1); if (dsbufs.count(src)) { DsBuf s0 = dsbufs[src]; uint32_t o = new_dsbuf(s0.size, s0.fmt, false);
                    { if (s0.size) std::memcpy(&c.mem[dsbufs[o].mem], &c.mem[s0.mem], s0.size); c.wr32(c.arg(2), o); } } }
                if (calls < 4000) hostlog("  [com] %s -> %x\n", nm.c_str(), ret);
                calls++; c.ret_stdcall(ac, ret);
            });
            cpu.wr32(vt + 4 * i, h);
        }
        return obj;
    };


    // ---- window / message HLE: one thread, a message queue, WndProc called through call_guest ----
    struct Msg { uint32_t hwnd, msg, wp, lp; };
    std::deque<Msg> mq; std::map<std::string, uint32_t> wclass; std::map<uint32_t, uint32_t> wproc; uint32_t nexthwnd = 0x1234;
    bool activated = false, paint_pending = false; std::map<uint32_t, uint32_t> thr;
    auto send_msg = [&](X86& c, uint32_t h, uint32_t m, uint32_t wp, uint32_t lp) -> uint32_t {
        auto it = wproc.find(h); if (it == wproc.end() || !it->second) return 0; return call_guest(c, it->second, {h, m, wp, lp});
    };
    auto post_paint = [&](uint32_t h) { if (!paint_pending) { paint_pending = true; mq.push_back({h, 0x000F, 0, 0}); } };
    auto fill_msg = [&](X86& c, uint32_t p, const Msg& m) { c.wr32(p, m.hwnd); c.wr32(p + 4, m.msg); c.wr32(p + 8, m.wp); c.wr32(p + 12, m.lp); c.wr32(p + 16, (uint32_t)(c.steps / 1000)); c.wr32(p + 20, 0); c.wr32(p + 24, 0); };


    // ---- GDI on DirectDraw surface DCs (Surface::GetDC returns 0x6000+surface) : SetPixel / GetPixel / SetDIBitsToDevice ----
    auto sdc = [&](uint32_t hdc) -> Surf* { uint32_t o = hdc - 0x6000; auto it = surfs.find(o); return it == surfs.end() ? nullptr : &it->second; };
    auto pack_px = [&](const Surf& sf, uint32_t r, uint32_t g, uint32_t b) -> uint32_t { return sf.bpp == 16 ? (((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3)) : ((r << 16) | (g << 8) | b); };
    auto unpack_px = [&](const Surf& sf, uint32_t v) -> uint32_t {   // -> COLORREF 0x00BBGGRR
        uint32_t r, g, b; if (sf.bpp == 16) { r = (v >> 11) & 31; g = (v >> 5) & 63; b = v & 31; r = (r << 3) | (r >> 2); g = (g << 2) | (g >> 4); b = (b << 3) | (b >> 2); } else { r = (v >> 16) & 255; g = (v >> 8) & 255; b = v & 255; }
        return r | (g << 8) | (b << 16); };

    struct MEv { int t, x, y; }; std::deque<MEv> mpend; uint32_t mlast = 0;   // shell mouse events waiting to be delivered, time of the last delivery
    int mouse_x = 320, mouse_y = 240; uint64_t idle_polls = 0;
    if (hooks && hooks->on_frame) pump = [&]() -> bool {
        for (auto& kv : surfs) if (kv.second.primary) { const Surf& sf = kv.second; X86Frame fr; fr.pixels = &cpu.mem[sf.mem]; fr.width = sf.w; fr.height = sf.h; fr.pitch = sf.pitch; fr.bpp = sf.bpp; fr.idle = false; fr.steps = cpu.steps; return hooks->on_frame(fr); }
        return true; };   // last cursor position given to the guest; PeekMessage polls with an empty queue in this slice

    // ---- waveOut HLE: the game's sound effects / voices. PCM goes to hooks->on_pcm; buffers complete after their playing time (guest clock) ----
    struct WaveOut { int ch = 1, rate = 22050, bits = 16; uint32_t flags = 0, cb = 0, inst = 0; };
    struct WavePend { uint32_t hdr, done_ms, hwo; };
    std::map<uint32_t, WaveOut> waves; std::deque<WavePend> wpend; uint32_t next_wo = 0x7001;
    auto host_now = [&](X86& c) -> uint32_t { return (hooks && hooks->now_ms) ? hooks->now_ms() : (uint32_t)(c.steps / 1000); };
    auto wave_done = [&](X86& c, const WavePend& p) {   // WHDR_DONE + MM_WOM_DONE (window or function callback)
        c.wr32(p.hdr + 16, (c.rd32(p.hdr + 16) & ~0x10u) | 1u); WaveOut& w = waves[p.hwo];
        if ((w.flags & 0x70000) == 0x10000 && w.cb) mq.push_back({w.cb, 0x3BD, p.hwo, p.hdr});
        else if ((w.flags & 0x70000) == 0x30000 && w.cb) call_guest(c, w.cb, {p.hwo, 0x3BD, w.inst, p.hdr, 0}); };
    auto wave_service = [&](X86& c) { uint32_t now = host_now(c); while (!wpend.empty() && (int32_t)(now - wpend.front().done_ms) >= 0) { WavePend p = wpend.front(); wpend.pop_front(); wave_done(c, p); } };

    for (auto& im : img.imports) {
        auto it = defs.find(im.name); if (it == defs.end()) { fprintf(stderr, "no def for %s\n", im.name.c_str()); return 1; }
        ApiDef d = it->second; std::string nm = im.name;
        uint32_t haddr = cpu.add_host([&, d, nm](X86& c) {
            uint32_t ret = 0; std::string note; seen[nm]++;
            auto a = [&](int i) { return c.arg(i); };
            if (nm == "GetVersion") ret = 0x0A280105;
            else if (nm == "GetCommandLineA") ret = cmdline;
            else if (nm == "GetModuleHandleA") ret = a(0) ? 0 : img.base;
            else if (nm == "GetStartupInfoA") { for (int i = 0; i < 68; i += 4) c.wr32(a(0) + i, 0); c.wr32(a(0), 68); }
            else if (nm == "GetEnvironmentStrings" || nm == "GetEnvironmentStringsW") ret = envblk;
            else if (nm == "HeapCreate") ret = 0x10000;
            else if (nm == "HeapAlloc") { ret = alloc(a(2)); }
            else if (nm == "LocalAlloc") { ret = alloc(a(1)); }
            else if (nm == "VirtualAlloc") { ret = a(0) ? a(0) : alloc(a(1)); }
            else if (nm == "HeapReAlloc" || nm == "LocalReAlloc") { bool lo = nm == "LocalReAlloc"; uint32_t old = lo ? a(0) : a(2), nsz = lo ? a(1) : a(3); uint32_t n = alloc(nsz); uint32_t osz = asz.count(old) ? asz[old] : 0; std::memcpy(&c.mem[n], &c.mem[old], std::min(osz, nsz)); release(old); ret = n; }
            else if (nm == "LocalSize") ret = asz.count(a(0)) ? asz[a(0)] : 0;
            else if (nm == "HeapSize") ret = asz.count(a(2)) ? asz[a(2)] : 0;
            else if (nm == "LocalLock" || nm == "GlobalLock") ret = a(0);
            else if (nm == "LocalUnlock" || nm == "GlobalUnlock") ret = 1;
            else if (nm == "HeapFree" || nm == "LocalFree" || nm == "VirtualFree" || nm == "HeapDestroy") { if (nm == "HeapFree") release(a(2)); else if (nm == "LocalFree") release(a(0)); ret = 1; }
            else if (nm == "GetFileType") ret = 2;
            else if (nm == "GetStdHandle") ret = 0x100 + a(0);
            else if (nm == "SetHandleCount") ret = a(0);
            else if (nm == "GetACP" || nm == "GetOEMCP") ret = 949;
            else if (nm == "GetCPInfo") { c.wr32(a(1), 1); c.wr32(a(1) + 4, 0x3f); ret = 1; }
            else if (nm == "GetModuleFileNameA") { const char* p = "C:\\PM3\\PM3WIN.EXE"; c.write_bytes(a(1), (const uint8_t*)p, strlen(p) + 1); ret = (uint32_t)strlen(p); }
            else if (nm == "timeGetTime") ret = (hooks && hooks->now_ms) ? hooks->now_ms() : (uint32_t)(c.steps / 1000);
            else if (nm == "lstrlenA") ret = (uint32_t)cstr(c, a(0), 4096).size();
            else if (nm == "ExitProcess") { hostlog("[ExitProcess(%u)]\n", a(0)); stop = true; }
            else if (nm == "wsprintfA") {   // cdecl varargs: wsprintfA(buf, fmt, ...)
                std::string fmt = cstr(c, c.arg(1), 512), out; int ai = 2;
                for (size_t k = 0; k < fmt.size(); k++) {
                    if (fmt[k] != '%') { out += fmt[k]; continue; }
                    k++; if (k >= fmt.size()) break; if (fmt[k] == '%') { out += '%'; continue; }
                    bool left = false, zero = false; while (k < fmt.size() && (fmt[k] == '-' || fmt[k] == '0')) { if (fmt[k] == '-') left = true; else zero = true; k++; }
                    int width = 0; while (k < fmt.size() && isdigit((unsigned char)fmt[k])) width = width * 10 + (fmt[k++] - '0');
                    while (k < fmt.size() && (fmt[k] == 'l' || fmt[k] == 'h')) k++;
                    char sp = k < fmt.size() ? fmt[k] : 0; std::string piece; uint32_t v = c.arg(ai++);
                    if (sp == 's') piece = cstr(c, v, 1024); else if (sp == 'd' || sp == 'i') piece = std::to_string((int32_t)v); else if (sp == 'u') piece = std::to_string(v);
                    else if (sp == 'x' || sp == 'X') { char tb[16]; snprintf(tb, sizeof tb, sp == 'x' ? "%x" : "%X", v); piece = tb; } else if (sp == 'c') piece = std::string(1, (char)v);
                    while ((int)piece.size() < width) piece = left ? piece + ' ' : (zero ? "0" : " ") + piece;
                    out += piece;
                }
                c.write_bytes(c.arg(0), (const uint8_t*)out.c_str(), out.size() + 1); ret = (uint32_t)out.size(); note = "-> " + out.substr(0, 90);
            }
            else if (nm == "CreateWindowExA") {
                std::string cn = a(1) > 0xFFFF ? cstr(c, a(1), 40) : std::string("?"); note = "class=" + cn + " title=" + cstr(c, a(2), 30);
                uint32_t pr = wclass.count(cn) ? wclass[cn] : 0, h = nexthwnd++; wproc[h] = pr; ret = h;
                uint32_t cs = tmp + 0x400, v[12] = {a(11), a(10), a(9), a(8), a(7), a(6), a(5), a(4), a(3), a(2), a(1), a(0)}; for (int q = 0; q < 12; q++) c.wr32(cs + 4 * q, v[q]);
                if (pr) { call_guest(c, pr, {h, 0x81, 0, cs}); uint32_t r = call_guest(c, pr, {h, 0x0001, 0, cs}); if (r == 0xFFFFFFFFu) ret = 0; }
            }
            else if (nm == "MessageBoxA") { note = cstr(c, a(1), 120) + " | " + cstr(c, a(2), 40); ret = 1; if (++msgboxes >= 3) { hostlog("[stopping: repeated MessageBoxA]\n"); stop = true; } }
            else if (nm == "GetDC") ret = 0x5000;
            else if (nm == "ReleaseDC" || nm == "DeleteDC") ret = 1;
            else if (nm == "GetDeviceCaps") {   // 16bpp, 1 plane, 640x480 (a mode the EXE accepts without the colour warning)
                uint32_t idx = a(1); note = "index=" + std::to_string(idx);
                switch (idx) { case 12: ret = 16; break; case 14: ret = 1; break; case 8: ret = 640; break; case 10: ret = 480; break;
                               case 88: case 90: ret = 96; break; case 24: ret = 0; break; case 104: ret = 0; break; case 38: ret = 0x7e99; break; default: ret = 0; }
            }
            else if (nm == "GetCurrentDirectoryA") { const char* p = "C:\\PM3"; c.write_bytes(a(1), (const uint8_t*)p, strlen(p) + 1); ret = (uint32_t)strlen(p); }
            else if (nm == "GetLogicalDriveStringsA") { const char dr[] = "C:\\\0D:\\\0"; c.write_bytes(a(1), (const uint8_t*)dr, sizeof dr); ret = 6; }   // C:\ and D:\ (+ final NUL)
            else if (nm == "GetDiskFreeSpaceA") { c.wr32(a(1), 8); c.wr32(a(2), 512); c.wr32(a(3), 400000); c.wr32(a(4), 1000000); ret = 1; note = "8 sec/cluster, 512 B/sec, 1.6 GB free"; }   // the game refuses to save "system data" when this fails
            else if (nm == "GetDriveTypeA") { std::string r = a(0) ? cstr(c, a(0), 8) : ""; note = r; ret = (!r.empty() && (r[0] == 'D' || r[0] == 'd')) ? 5 : 3; }   // D: = CD-ROM, others fixed
            else if (nm == "FindClose" || nm == "SetCurrentDirectoryA" || nm == "SystemParametersInfoA" || nm == "IsWindow") ret = 1;
            else if (nm == "GetSystemMetrics") ret = a(0) == 0 ? 640 : (a(0) == 1 ? 480 : 0);   // SM_CXSCREEN / SM_CYSCREEN
            else if (nm == "GetWindowRect" || nm == "GetClientRect") { c.wr32(a(1), 0); c.wr32(a(1) + 4, 0); c.wr32(a(1) + 8, 640); c.wr32(a(1) + 12, 480); ret = 1; }
            else if (nm == "RegOpenKeyExA") {   // virtual registry: Software\NINELIVES\Princess Maker3JP\1.0 exists (written by the installer)
                std::string sub = cstr(c, a(1), 120); note = sub; std::string low = sub; for (auto& ch : low) ch = (char)tolower((unsigned char)ch);
                if (low.find("ninelives") != std::string::npos) { c.wr32(a(4), 0x7000); ret = 0; } else ret = 2;
            }
            else if (nm == "RegQueryValueExA") {   // only InstPath is known: the game folder
                std::string val = a(1) ? cstr(c, a(1), 60) : ""; note = val;
                if (val == "InstPath") { const char* v = "C:\\PM3"; uint32_t n = (uint32_t)strlen(v) + 1;
                    if (a(3)) c.wr32(a(3), 1);   // REG_SZ
                    if (a(4) && a(5) && c.rd32(a(5)) >= n) c.write_bytes(a(4), (const uint8_t*)v, n);
                    if (a(5)) { c.wr32(a(5), n); }
                    ret = 0; }
                else { if (a(5)) c.wr32(a(5), 0); ret = 2; }   // not found: report 0 bytes (the EXE NUL-terminates at buf[*cb])
            }
            else if (nm == "RegCloseKey") ret = 0;
            else if (nm == "SetPixel") { Surf* sf = sdc(a(0)); ret = a(3);
                if (sf) { int x = (int)a(1), y = (int)a(2); uint32_t col = a(3); if (x >= 0 && y >= 0 && x < sf->w && y < sf->h) {
                    uint32_t v = pack_px(*sf, col & 255, (col >> 8) & 255, (col >> 16) & 255); c.wr(sf->mem + y * sf->pitch + x * (sf->bpp / 8), sf->bpp == 16 ? 2 : 4, v); ret = unpack_px(*sf, v); } } }
            else if (nm == "GetPixel") { Surf* sf = sdc(a(0)); ret = 0xFFFFFFFFu;
                if (sf) { int x = (int)a(1), y = (int)a(2); if (x >= 0 && y >= 0 && x < sf->w && y < sf->h) ret = unpack_px(*sf, c.rd(sf->mem + y * sf->pitch + x * (sf->bpp / 8), sf->bpp == 16 ? 2 : 4)); } }
            else if (nm == "SetDIBitsToDevice") {   // hdc, xD, yD, w, h, xS, yS, startScan, cLines, bits, bmi, usage
                Surf* sf = sdc(a(0)); ret = 0;
                if (sf) { int xD = (int)a(1), yD = (int)a(2), w = (int)a(3), h = (int)a(4), xS = (int)a(5), yS = (int)a(6), st = (int)a(7), cl = (int)a(8); uint32_t bits = a(9), bmi = a(10);
                    uint32_t hs = c.rd32(bmi); int bw = (int32_t)c.rd32(bmi + 4), bh = (int32_t)c.rd32(bmi + 8), bc = c.rd(bmi + 14, 2); uint32_t used = c.rd32(bmi + 32);
                    if (getenv("DIBLOG")) hostlog("  [dib] surf=%x dst=(%d,%d) %dx%d src=(%d,%d) startScan=%d lines=%d bmp=%dx%d bpp=%d comp=%u used=%u hs=%u\n", (unsigned)sf->obj, xD, yD, w, h, xS, yS, st, cl, bw, bh, bc, c.rd32(bmi + 16), used, hs);
                    bool topdown = bh < 0; uint32_t stride = ((uint32_t)bw * bc + 31) / 32 * 4; uint32_t pal = bmi + hs; if (bc <= 8 && used == 0) used = 1u << bc;
                    int bp = sf->bpp / 8;
                    for (int r = 0; r < h; r++) { int sr = topdown ? (yS + r) : (yS + (h - 1 - r)); int line = sr - st; if (line < 0 || line >= cl) continue; int dy = yD + r; if (dy < 0 || dy >= sf->h) continue;
                        uint32_t row = bits + (uint32_t)line * stride;
                        for (int q = 0; q < w; q++) { int sx = xS + q, dx = xD + q; if (dx < 0 || dx >= sf->w || sx < 0 || sx >= bw) continue; uint32_t rr, gg, bb;
                            if (bc == 4) { uint32_t i = c.rd8(row + (sx >> 1)); i = (sx & 1) ? (i & 15) : (i >> 4); if (i >= used) i = 0; bb = c.rd8(pal + 4 * i); gg = c.rd8(pal + 4 * i + 1); rr = c.rd8(pal + 4 * i + 2); }   // 4 bpp: high nibble = left pixel (the title-menu button plates)
                            else if (bc == 1) { uint32_t i = (c.rd8(row + (sx >> 3)) >> (7 - (sx & 7))) & 1; if (i >= used) i = 0; bb = c.rd8(pal + 4 * i); gg = c.rd8(pal + 4 * i + 1); rr = c.rd8(pal + 4 * i + 2); }
                            else if (bc == 8) { uint32_t i = c.rd8(row + sx); if (i >= used) i = 0; bb = c.rd8(pal + 4 * i); gg = c.rd8(pal + 4 * i + 1); rr = c.rd8(pal + 4 * i + 2); }
                            else if (bc == 24) { bb = c.rd8(row + 3 * sx); gg = c.rd8(row + 3 * sx + 1); rr = c.rd8(row + 3 * sx + 2); }
                            else if (bc == 32) { bb = c.rd8(row + 4 * sx); gg = c.rd8(row + 4 * sx + 1); rr = c.rd8(row + 4 * sx + 2); }
                            else if (bc == 16) { uint32_t v = c.rd(row + 2 * sx, 2); rr = ((v >> 10) & 31) << 3; gg = ((v >> 5) & 31) << 3; bb = (v & 31) << 3; }
                            else continue;
                            c.wr(sf->mem + dy * sf->pitch + dx * bp, bp == 2 ? 2 : 4, pack_px(*sf, rr, gg, bb)); } }
                    ret = (uint32_t)cl; } }
            else if (nm == "SetRect") { c.wr32(a(0), a(1)); c.wr32(a(0) + 4, a(2)); c.wr32(a(0) + 8, a(3)); c.wr32(a(0) + 12, a(4)); ret = 1; }
            else if (nm == "SetRectEmpty") { for (int q = 0; q < 16; q += 4) c.wr32(a(0) + q, 0); ret = 1; }
            else if (nm == "CopyRect") { for (int q = 0; q < 16; q += 4) c.wr32(a(0) + q, c.rd32(a(1) + q)); ret = 1; }
            else if (nm == "IntersectRect") { int32_t l = std::max((int32_t)c.rd32(a(1)), (int32_t)c.rd32(a(2))), t = std::max((int32_t)c.rd32(a(1) + 4), (int32_t)c.rd32(a(2) + 4)), r = std::min((int32_t)c.rd32(a(1) + 8), (int32_t)c.rd32(a(2) + 8)), b = std::min((int32_t)c.rd32(a(1) + 12), (int32_t)c.rd32(a(2) + 12));
                if (l < r && t < b) { c.wr32(a(0), l); c.wr32(a(0) + 4, t); c.wr32(a(0) + 8, r); c.wr32(a(0) + 12, b); ret = 1; } else { for (int q = 0; q < 16; q += 4) c.wr32(a(0) + q, 0); ret = 0; } }
            else if (nm == "UnionRect") { int32_t l0 = c.rd32(a(1)), t0 = c.rd32(a(1) + 4), r0 = c.rd32(a(1) + 8), b0 = c.rd32(a(1) + 12), l1 = c.rd32(a(2)), t1 = c.rd32(a(2) + 4), r1 = c.rd32(a(2) + 8), b1 = c.rd32(a(2) + 12);
                bool e0 = l0 >= r0 || t0 >= b0, e1 = l1 >= r1 || t1 >= b1; int32_t l, t, r, b;
                if (e0 && e1) { l = t = r = b = 0; ret = 0; } else { l = e0 ? l1 : e1 ? l0 : std::min(l0, l1); t = e0 ? t1 : e1 ? t0 : std::min(t0, t1); r = e0 ? r1 : e1 ? r0 : std::max(r0, r1); b = e0 ? b1 : e1 ? b0 : std::max(b0, b1); ret = 1; }
                c.wr32(a(0), l); c.wr32(a(0) + 4, t); c.wr32(a(0) + 8, r); c.wr32(a(0) + 12, b); }
            else if (nm == "OffsetRect") { c.wr32(a(0), c.rd32(a(0)) + a(1)); c.wr32(a(0) + 4, c.rd32(a(0) + 4) + a(2)); c.wr32(a(0) + 8, c.rd32(a(0) + 8) + a(1)); c.wr32(a(0) + 12, c.rd32(a(0) + 12) + a(2)); ret = 1; }
            else if (nm == "InflateRect") { c.wr32(a(0), c.rd32(a(0)) - a(1)); c.wr32(a(0) + 4, c.rd32(a(0) + 4) - a(2)); c.wr32(a(0) + 8, c.rd32(a(0) + 8) + a(1)); c.wr32(a(0) + 12, c.rd32(a(0) + 12) + a(2)); ret = 1; }
            else if (nm == "PtInRect") { int32_t x = a(1), y = a(2); ret = (x >= (int32_t)c.rd32(a(0)) && x < (int32_t)c.rd32(a(0) + 8) && y >= (int32_t)c.rd32(a(0) + 4) && y < (int32_t)c.rd32(a(0) + 12)) ? 1 : 0; }
            else if (nm == "IsRectEmpty") { ret = ((int32_t)c.rd32(a(0)) >= (int32_t)c.rd32(a(0) + 8) || (int32_t)c.rd32(a(0) + 4) >= (int32_t)c.rd32(a(0) + 12)) ? 1 : 0; }
            else if (nm == "EqualRect") { ret = 1; for (int q = 0; q < 16; q += 4) if (c.rd32(a(0) + q) != c.rd32(a(1) + q)) ret = 0; }
            else if (nm == "waveOutOpen") { uint32_t fl = a(5); ret = 0;
                if (!(fl & 1) && a(2)) { WaveOut w; w.ch = c.rd(a(2) + 2, 2); w.rate = (int)c.rd32(a(2) + 4); w.bits = c.rd(a(2) + 14, 2); w.flags = fl; w.cb = a(3); w.inst = a(4);
                    if (w.ch < 1) { w.ch = 1; }
                    if (w.bits != 8 && w.bits != 16) { w.bits = 16; }
                    uint32_t h = next_wo++; waves[h] = w; if (a(0)) c.wr32(a(0), h); char b[96]; snprintf(b, sizeof b, "%dHz %dch %dbit cbflags=%x", w.rate, w.ch, w.bits, fl); note = b; } }
            else if (nm == "waveOutPrepareHeader") { c.wr32(a(1) + 16, c.rd32(a(1) + 16) | 2u); ret = 0; }
            else if (nm == "waveOutUnprepareHeader") { c.wr32(a(1) + 16, c.rd32(a(1) + 16) & ~2u); ret = 0; }
            else if (nm == "waveOutWrite") { uint32_t hwo = a(0), hdr = a(1), data = c.rd32(hdr), len = c.rd32(hdr + 4); ret = 0; auto wi = waves.find(hwo);
                if (wi != waves.end() && len && data && (uint64_t)data + len <= c.mem.size()) { WaveOut& w = wi->second; uint32_t bps = (uint32_t)w.rate * w.ch * (w.bits / 8), dur = bps ? (uint32_t)((uint64_t)len * 1000 / bps) : 0;
                    uint32_t now = host_now(c), start = (!wpend.empty() && (int32_t)(wpend.back().done_ms - now) > 0) ? wpend.back().done_ms : now;
                    c.wr32(hdr + 16, (c.rd32(hdr + 16) & ~1u) | 0x10u); wpend.push_back({hdr, start + dur, hwo});
                    if (hooks && hooks->on_pcm) hooks->on_pcm(&c.mem[data], len, w.ch, w.rate, w.bits);
                    char b[80]; snprintf(b, sizeof b, "%u bytes = %u ms", len, dur); note = b; }
                else if (wi != waves.end()) c.wr32(hdr + 16, c.rd32(hdr + 16) | 1u); }
            else if (nm == "waveOutReset") { while (!wpend.empty()) { WavePend p = wpend.front(); wpend.pop_front(); wave_done(c, p); } ret = 0; }
            else if (nm == "waveOutClose") { waves.erase(a(0)); ret = 0; }
            else if (nm == "RegisterClassA") { std::string cn = cstr(c, c.rd32(a(0) + 36), 40); wclass[cn] = c.rd32(a(0) + 4); note = cn; ret = 0xC001 + (uint32_t)wclass.size(); }
            else if (nm == "ShowWindow") { uint32_t h = a(0), cmd = a(1); ret = 1;
                if (cmd != 0 && !activated) { activated = true;
                    send_msg(c, h, 0x18, 1, 0); send_msg(c, h, 0x1C, 1, 0); send_msg(c, h, 0x86, 1, 0); send_msg(c, h, 0x06, 1, 0); send_msg(c, h, 0x07, 0, 0); send_msg(c, h, 0x05, 0, 640 | (480 << 16)); post_paint(h); } }
            else if (nm == "InvalidateRect" || nm == "RedrawWindow") { post_paint(a(0) ? a(0) : 0x1234); ret = 1; }
            else if (nm == "UpdateWindow") { ret = 1; if (paint_pending) { paint_pending = false; for (size_t i = 0; i < mq.size(); i++) if (mq[i].msg == 0xF) { mq.erase(mq.begin() + i); break; } send_msg(c, a(0), 0x000F, 0, 0); } }
            else if (nm == "BeginPaint") { for (int q = 0; q < 64; q += 4) c.wr32(a(1) + q, 0); c.wr32(a(1), 0x6001); c.wr32(a(1) + 16, 640); c.wr32(a(1) + 20, 480); ret = 0x6001; }
            else if (nm == "EndPaint") ret = 1;
            else if (nm == "GetCursorPos") { c.wr32(a(0), mouse_x); c.wr32(a(0) + 4, mouse_y); ret = 1; }
            else if (nm == "ScreenToClient") ret = 1;
            else if (nm == "PeekMessageA") {
                wave_service(c);
                if (hooks && hooks->poll_mouse) {   // shell events -> WM_MOUSE*; button changes are spaced >= 50 ms apart so a per-frame state sampler sees hover, press and release
                    int t, x, y; while (hooks->poll_mouse(&t, &x, &y)) mpend.push_back({t, x, y});
                    uint32_t now = hooks->now_ms ? hooks->now_ms() : (uint32_t)(c.steps / 1000);
                    while (!mpend.empty()) { MEv e = mpend.front(); if (e.t != 0 && now - mlast < 50) break; mpend.pop_front(); mouse_x = e.x; mouse_y = e.y; mlast = now;
                        uint32_t pos = (uint32_t)(e.x & 0xFFFF) | ((uint32_t)(e.y & 0xFFFF) << 16);
                        if (e.t == 0) mq.push_back({0x1234, 0x0200, 0, pos}); else if (e.t == 1) mq.push_back({0x1234, 0x0201, 1, pos}); else if (e.t == 2) mq.push_back({0x1234, 0x0202, 0, pos}); else if (e.t == 3) mq.push_back({0x1234, 0x0204, 2, pos}); else if (e.t == 4) mq.push_back({0x1234, 0x0205, 0, pos}); }   // WM_RBUTTONDOWN (MK_RBUTTON) / WM_RBUTTONUP
                }
                if (mq.empty()) idle_polls++;
                if (getenv("INJECT") && mq.empty() && seen[nm] % 300000 == 0) {   // INJECT=1 : periodic mouse move/click + Enter key, to see whether the game waits for input
                    uint32_t h = 0x1234, pos = 320 | (240u << 16); mq.push_back({h, 0x200, 0, pos}); mq.push_back({h, 0x201, 1, pos}); mq.push_back({h, 0x202, 0, pos}); mq.push_back({h, 0x100, 13, 0x1C0001}); mq.push_back({h, 0x101, 13, 0xC01C0001u}); hostlog("  [inject] input messages at peek #%d\n", seen[nm]); }
                 if (mq.empty()) ret = 0; else { Msg m = mq.front(); if (getenv("MSGLOG") && m.msg != 0xF) hostlog("  [msg] Peek -> hwnd=%x msg=%x wp=%x lp=%x remove=%x\n", m.hwnd, m.msg, m.wp, m.lp, a(4)); fill_msg(c, a(0), m); if (a(4) & 1) { if (m.msg == 0xF) paint_pending = false; mq.pop_front(); } ret = 1; } }
            else if (nm == "GetMessageA") { if (mq.empty()) { fill_msg(c, a(0), Msg{0, 0, 0, 0}); ret = 1; } else { Msg m = mq.front(); mq.pop_front(); if (m.msg == 0xF) paint_pending = false; fill_msg(c, a(0), m); ret = m.msg == 0x12 ? 0 : 1; } }
            else if (nm == "TranslateMessage") ret = 0;
            else if (nm == "DispatchMessageA") { uint32_t m = a(0), h = c.rd32(m), msg = c.rd32(m + 4), wp = c.rd32(m + 8), lp = c.rd32(m + 12); if (getenv("MSGLOG")) hostlog("  [msg] Dispatch hwnd=%x msg=%x wp=%x lp=%x\n", h, msg, wp, lp); ret = send_msg(c, h, msg, wp, lp); }
            else if (nm == "SendMessageA") { uint32_t h = a(0), msg = a(1), wp = a(2), lp = a(3); if (msg == 0x112 || msg == 0x10 || msg == 0x2) hostlog("[SendMessage msg=%x wp=%x] ret@%08x step %llu\n", msg, wp, c.rd32(c.r[4]), (unsigned long long)c.steps); ret = send_msg(c, h, msg, wp, lp); }
            else if (nm == "CallWindowProcA") { uint32_t pr = a(0), h = a(1), msg = a(2), wp = a(3), lp = a(4); ret = pr > 0x10000 ? call_guest(c, pr, {h, msg, wp, lp}) : 0; }
            else if (nm == "PostMessageA") { mq.push_back({a(0), a(1), a(2), a(3)}); ret = 1; }
            else if (nm == "PostQuitMessage") { hostlog("[PostQuitMessage(%u)] ret@%08x step %llu\n", a(0), c.rd32(c.r[4]), (unsigned long long)c.steps); mq.push_back({0, 0x12, a(0), 0}); }
            else if (nm == "DefWindowProcA") { ret = a(1) == 0x81 ? 1 : 0; }
            else if (nm == "CreateThread") { uint32_t fn = a(2), pa = a(3), tidp = a(5); uint32_t h = 0xA000 + (uint32_t)thr.size() + (uint32_t)gthreads.size(); if (tidp) c.wr32(tidp, 0x2000 + (uint32_t)thr.size() + (uint32_t)gthreads.size()); char b[64];
                if (async_threads) { GThread t; std::memset(&t, 0, sizeof t); uint32_t stk = alloc(0x40000); t.r[4] = stk + 0x40000 - 16; c.wr32(t.r[4], X86::RET_STOP); c.wr32(t.r[4] + 4, pa); t.eip = fn; t.handle = h; gthreads.push_back(t); snprintf(b, sizeof b, "start=%x (cooperative)", fn); note = b; }
                else { snprintf(b, sizeof b, "start=%x (run synchronously)", fn); note = b; cg_limit = 2000000000ull; uint32_t rc = call_guest(c, fn, {pa}); cg_limit = 20000000ull; thr[h] = rc; }
                ret = h; }
            else if (nm == "GetExitCodeThread") { uint32_t h = a(0); GThread* t = find_thread(h); if (a(1)) c.wr32(a(1), t ? (t->done ? t->exit : 259u) : (thr.count(h) ? thr[h] : 0)); ret = 1; }
            else if (nm == "WaitForSingleObject") { GThread* t = find_thread(a(0)); ret = 0;
                if (t && !t->done) { if (a(1) == 0) ret = 0x102;   // WAIT_TIMEOUT: still loading (the game polls this every frame)
                    else { uint64_t guard = 0; while (!t->done && guard < 4000000000ull && !stop) { run_thread_slice(c, *t, 200000); guard += 200000; } } } }
            else if (nm == "TerminateThread") ret = 1;

            else if (nm == "LoadCursorA" || nm == "LoadIconA" || nm == "LoadMenuA") ret = 0x77;
            else if (nm == "FindFirstFileA") {   // WIN32_FIND_DATA: attrs @0, size @32, name @44
                std::string path = cstr(c, a(0), 100); note = path; std::string hp = host_path(path, false); struct stat st;
                if (!hp.empty() && stat(hp.c_str(), &st) == 0) {
                    for (int q = 0; q < 0x140; q += 4) c.wr32(a(1) + q, 0);
                    c.wr32(a(1), S_ISDIR(st.st_mode) ? 0x10 : 0x20); c.wr32(a(1) + 32, (uint32_t)st.st_size);
                    std::string base = hp.substr(hp.find_last_of('/') + 1); c.write_bytes(a(1) + 44, (const uint8_t*)base.c_str(), base.size() + 1); ret = 0x500;
                } else ret = 0xFFFFFFFF;
            }
            else if (nm == "FindNextFileA") ret = 0;
            else if (nm == "CreateFileA") {   // a: name, access, share, sec, disposition, flags, template
                std::string path = cstr(c, a(0), 200); uint32_t acc = a(1), disp = a(4); bool wr = (acc & 0x40000000u) != 0 || disp == 1 || disp == 2 || disp == 4;
                bool have = vfs.exists(path); note = path + ((!wr && !have) ? " [missing]" : "");
                FILE* f = nullptr;
                if (wr) { f = vfs.open(path, disp == 2 ? "w+b" : "r+b"); if (!f) f = vfs.open(path, "w+b"); } else if (have) f = vfs.open(path, "rb");
                if (f) { uint32_t h = nextH++; files[h] = {f, path}; ret = h; } else ret = 0xFFFFFFFF;
            }
            else if (nm == "ReadFile") { auto it = files.find(a(0)); if (it == files.end()) ret = 0; else { std::vector<uint8_t> buf(a(2)); size_t n = fread(buf.data(), 1, a(2), it->second.f); c.write_bytes(a(1), buf.data(), n); if (a(3)) c.wr32(a(3), (uint32_t)n); ret = 1; } }
            else if (nm == "WriteFile") { auto it = files.find(a(0)); if (it == files.end()) ret = 0; else { size_t n = fwrite(&c.mem[a(1)], 1, a(2), it->second.f); if (a(3)) c.wr32(a(3), (uint32_t)n); ret = 1; } }
            else if (nm == "SetFilePointer") { auto it = files.find(a(0)); if (it == files.end()) ret = 0xFFFFFFFF; else { fseek(it->second.f, (long)(int32_t)a(1), a(3) == 0 ? SEEK_SET : (a(3) == 1 ? SEEK_CUR : SEEK_END)); ret = (uint32_t)ftell(it->second.f); } }
            else if (nm == "GetFileSize") { auto it = files.find(a(0)); if (it == files.end()) ret = 0xFFFFFFFF; else { long cur = ftell(it->second.f); fseek(it->second.f, 0, SEEK_END); long sz = ftell(it->second.f); fseek(it->second.f, cur, SEEK_SET); ret = (uint32_t)sz; if (a(1)) c.wr32(a(1), 0); } }
            else if (nm == "CloseHandle") { auto it = files.find(a(0)); if (it != files.end()) { fclose(it->second.f); files.erase(it); } ret = 1; }
            else if (nm == "FlushFileBuffers" || nm == "SetEndOfFile") ret = 1;
            else if (nm == "MultiByteToWideChar") { uint32_t n = (int32_t)a(3) < 0 ? 0 : a(3); if (a(4) && a(5)) { for (uint32_t i = 0; i < n && i < a(5); i++) c.wr(a(4) + 2 * i, 2, c.mem[a(2) + i]); } ret = a(3) == 0xFFFFFFFFu ? (uint32_t)cstr(c, a(2), 4096).size() + 1 : n; }
            else if (hooks && hooks->midi && nm == "?MIDI_FileOpen@@YAIPAXPAD@Z") {   // BGM file (MIDI\\Mxxx.MID) -> built-in synth
                std::string path = cstr(c, a(1), 260); ret = 1; FILE* f = vfs.open(path, "rb");
                if (f) { std::vector<uint8_t> d; uint8_t tmpb[4096]; size_t n; while ((n = fread(tmpb, 1, sizeof tmpb, f)) > 0) d.insert(d.end(), tmpb, tmpb + n); fclose(f); if (hooks->midi->load(d.data(), d.size())) ret = 0; }
                note = path + (ret ? " [failed]" : ""); }
            else if (hooks && hooks->midi && nm == "?MIDI_Play@@YAIPAXHJ@Z") { hooks->midi->play(a(2) != 0); ret = 0; }
            else if (hooks && hooks->midi && nm == "?MIDI_Stop@@YAIPAX@Z") { hooks->midi->stop(); ret = 0; }
            else if (hooks && hooks->midi && nm == "?MIDI_GetIsPlay@@YAHPAX@Z") ret = hooks->midi->playing() ? 1 : 0;
            else if (nm == "DirectDrawCreate") { if (!ddobj) ddobj = make_com(DDN, DDA, 24, false); c.wr32(a(1), ddobj); note = "HLE"; }
            else if (nm == "DirectSoundCreate") { if (!dsobj) dsobj = make_ds(); c.wr32(a(1), dsobj); note = "HLE"; }
            if (getenv("TRACE_API") && (nm == "mciSendCommandA" || nm == "mciGetDeviceIDA" || (nm == "PeekMessageA" && seen[nm] <= 2) || (nm == "timeGetTime" && seen[nm] <= 2) || nm == "?MIDI_FileOpen@@YAIPAXPAD@Z" || nm == "?MIDI_Play@@YAIPAXHJ@Z" || nm == "waveOutWrite" || nm == "waveOutOpen"))
                hostlog("  [trace] %s ret@%x args %x %x %x %x\n", nm.c_str(), c.rd32(c.r[4]), a(0), a(1), a(2), a(3));
            ring.push_back(nm); if (ring.size() > 12) ring.erase(ring.begin());
            if ((calls < 4000 && seen[nm] <= 12) || stop) hostlog("%s%s(%s) -> %u\n", nm.c_str(), note.empty() ? "" : "", note.c_str(), ret);
            calls++;
            if (d.cdecl_) { c.r[0] = ret; c.eip = c.pop32(); } else c.ret_stdcall(d.argc, ret);
            if (stop) c.eip = X86::RET_STOP;
        });
        cpu.wr32(im.iat_addr, haddr);
    }
    cpu.eip = img.base + img.entry; cpu.push32(X86::RET_STOP);
    bool ok;
    if (getenv("BP")) {   // BP=0xADDR: run to that address once, dump state, then continue
        uint32_t bp = (uint32_t)strtoul(getenv("BP"), 0, 16); ok = cpu.run(bp, maxs);
        hostlog("[BP %08x] eip=%08x eax=%08x ecx=%08x edx=%08x ebx=%08x esp=%08x ebp=%08x esi=%08x edi=%08x ZF=%d\n", bp, cpu.eip, cpu.r[0], cpu.r[1], cpu.r[2], cpu.r[3], cpu.r[4], cpu.r[5], cpu.r[6], cpu.r[7], cpu.ZF);
        hostlog("  [ebp+0]=%08x [ebp+4]=%08x\n", cpu.rd32(cpu.r[5]), cpu.rd32(cpu.r[5] + 4));
        for (int k = 0; k < 12; k++) hostlog("  [ebp-%02x] = %08x\n", 4 * (k + 1), cpu.rd32(cpu.r[5] - 4 * (k + 1)));
        if (ok && getenv("WATCH")) {   // single-step until the 32-bit word at WATCH changes
            uint32_t wa = (uint32_t)strtoul(getenv("WATCH"), 0, 16), w0 = cpu.rd32(wa), prev = cpu.eip; uint64_t n = 0; bool hit = false;
            while (n++ < 50000000ull) { prev = cpu.eip; bool r = cpu.run(X86::RET_STOP, 1); if (cpu.rd32(wa) != w0) { hit = true; break; } if (r) break; if (cpu.err != "step limit") { hostlog("[WATCH] stopped: %s\n", cpu.err.c_str()); break; } }
            if (hit) { hostlog("[WATCH] %08x changed %08x -> %08x by instruction at %08x (now eip=%08x) after %llu steps\n", wa, w0, cpu.rd32(wa), prev, cpu.eip, (unsigned long long)n);
                       hostlog("  eax=%08x ecx=%08x edx=%08x ebx=%08x esp=%08x ebp=%08x esi=%08x edi=%08x\n", cpu.r[0], cpu.r[1], cpu.r[2], cpu.r[3], cpu.r[4], cpu.r[5], cpu.r[6], cpu.r[7]); }
            else hostlog("[WATCH] no change\n");
            ok = false; cpu.err.clear(); ok = true;
        } else if (ok) { cpu.steps++; ok = cpu.run(X86::RET_STOP, maxs); }
    } else if (const char* pf = getenv("PROFILE")) {   // PROFILE=<warmup_steps>,<sample_steps> : EIP histogram (64-byte buckets) over the sample window
        uint64_t warm = strtoull(pf, 0, 10), cnt = 0; if (const char* cm = strchr(pf, ',')) cnt = strtoull(cm + 1, 0, 10);
        ok = cpu.run(X86::RET_STOP, warm); if (!ok && cpu.err == "step limit") { ok = true; cpu.err.clear(); }
        std::map<uint32_t, uint64_t> hist;
        for (uint64_t i = 0; ok && i < cnt; i++) { uint32_t e = cpu.eip; if (e < 0x600000) hist[e >> 6]++; bool r = cpu.run(X86::RET_STOP, 1); if (!r && cpu.err != "step limit") { ok = false; break; } cpu.err.clear(); }
        std::vector<std::pair<uint64_t, uint32_t>> v; for (auto& kv : hist) v.push_back({kv.second, kv.first << 6}); std::sort(v.rbegin(), v.rend());
        hostlog("PROFILE: %zu buckets over %llu steps\n", v.size(), (unsigned long long)cnt); for (size_t i = 0; i < v.size() && i < 25; i++) hostlog("  %08x  %llu\n", v[i].second, (unsigned long long)v[i].first);
        ok = true;
    } else if (hooks && hooks->on_frame) {   // shell-driven: run in slices, hand the primary surface to the shell after each one
        uint64_t slice = hooks->slice_steps ? hooks->slice_steps : 200000, done = 0; ok = true;
        while (ok && (maxs == 0 || done < maxs)) {
            uint64_t n = slice; if (maxs && maxs - done < n) n = maxs - done; uint64_t polls0 = idle_polls; uint64_t before = cpu.steps;
            ok = cpu.run(X86::RET_STOP, n); done += cpu.steps - before;
            if (!ok && cpu.err == "step limit") { ok = true; cpu.err.clear(); } else if (!ok || stop) break;
            run_threads(cpu, slice * 2);   // loader threads get their share between the main thread's slices
            if (stop) break;
            for (auto& kv : surfs) if (kv.second.primary) { const Surf& sf = kv.second; X86Frame fr; fr.pixels = &cpu.mem[sf.mem]; fr.width = sf.w; fr.height = sf.h; fr.pitch = sf.pitch; fr.bpp = sf.bpp;
                fr.idle = idle_polls - polls0 > 1000; fr.steps = cpu.steps; if (!hooks->on_frame(fr)) { ok = true; maxs = 1; done = 1; } break; }
            if (maxs == 1 && done == 1) break;
        }
    } else ok = cpu.run(X86::RET_STOP, maxs);
    if (!ok) { hostlog("regs: eax=%08x ecx=%08x edx=%08x ebx=%08x esp=%08x ebp=%08x esi=%08x edi=%08x\n", cpu.r[0], cpu.r[1], cpu.r[2], cpu.r[3], cpu.r[4], cpu.r[5], cpu.r[6], cpu.r[7]);
        hostlog("stack:"); for (int k = 0; k < 16 && cpu.r[4] > 0x1000; k++) hostlog(" %08x", cpu.rd32(cpu.r[4] + 4 * k)); hostlog("\n"); }
    hostlog("--- %s after %llu steps, %d API calls (eip %08x)  %s\n", ok ? "stopped" : "ERROR", (unsigned long long)cpu.steps, calls, cpu.eip, ok ? "" : cpu.err.c_str());
    if (!ring.empty()) { hostlog("last host calls:"); for (auto& n : ring) hostlog(" %s", n.c_str()); hostlog("\n"); }
    if (getenv("STATS")) { std::vector<std::pair<int, std::string>> v; for (auto& kv : seen) v.push_back({kv.second, kv.first}); std::sort(v.rbegin(), v.rend());
        hostlog("COM call counts:"); for (auto& kv : ddseen) hostlog(" %s=%d", kv.first.c_str(), kv.second); hostlog("\n");
        hostlog("API call counts:"); for (auto& e : v) hostlog(" %s=%d", e.second.c_str(), e.first); hostlog("\n"); }
    if (const char* dm = getenv("DUMPMEM")) {   // DUMPMEM=hexaddr,hexaddr,... : print guest dwords at exit
        std::string l = dm; size_t i = 0; while (i < l.size()) { size_t j = l.find(',', i); if (j == std::string::npos) j = l.size(); uint32_t ad = (uint32_t)strtoul(l.substr(i, j - i).c_str(), 0, 16); hostlog("mem[%08x] = %08x\n", ad, cpu.rd32(ad)); i = j + 1; } }
    if (const char* pa = getenv("DUMP_ALL")) {   // DUMP_ALL=<prefix> : write every surface as <prefix>_<n>.ppm and print its size and whether it holds any non-zero pixel
        int n = 0; for (auto& kv : surfs) { const Surf& sf = kv.second; std::string fn = std::string(pa) + "_" + std::to_string(n++) + (sf.primary ? "_primary" : "") + ".ppm";
            bool any = false; FILE* f = fopen(fn.c_str(), "wb"); if (!f) continue; fprintf(f, "P6\n%d %d\n255\n", sf.w, sf.h);
            for (int y = 0; y < sf.h; y++) for (int x = 0; x < sf.w; x++) { uint8_t rgb[3]; uint32_t v = cpu.rd(sf.mem + y * sf.pitch + x * (sf.bpp / 8), sf.bpp == 16 ? 2 : 4); if (v) any = true;
                if (sf.bpp == 16) { rgb[0] = ((v >> 11) & 31) * 255 / 31; rgb[1] = ((v >> 5) & 63) * 255 / 63; rgb[2] = (v & 31) * 255 / 31; } else { rgb[0] = (v >> 16) & 255; rgb[1] = (v >> 8) & 255; rgb[2] = v & 255; }
                fwrite(rgb, 1, 3, f); }
            fclose(f); hostlog("surface %d @%x: %dx%d %dbpp %s%s -> %s\n", n - 1, (unsigned)sf.obj, sf.w, sf.h, sf.bpp, sf.primary ? "PRIMARY " : "", any ? "has pixels" : "all zero", fn.c_str()); }
    }
    if (const char* po = getenv("DUMP_PRIMARY")) {   // DUMP_PRIMARY=<file.ppm> : write the primary surface (RGB565/XRGB8888) at exit
        for (auto& kv : surfs) if (kv.second.primary) { const Surf& sf = kv.second; FILE* f = fopen(po, "wb"); if (!f) break;
            fprintf(f, "P6\n%d %d\n255\n", sf.w, sf.h);
            for (int y = 0; y < sf.h; y++) for (int x = 0; x < sf.w; x++) { uint8_t rgb[3];
                if (sf.bpp == 16) { uint32_t v = cpu.rd(sf.mem + y * sf.pitch + x * 2, 2); rgb[0] = ((v >> 11) & 31) * 255 / 31; rgb[1] = ((v >> 5) & 63) * 255 / 63; rgb[2] = (v & 31) * 255 / 31; }
                else { uint32_t v = cpu.rd(sf.mem + y * sf.pitch + x * (sf.bpp / 8), 4); rgb[0] = (v >> 16) & 255; rgb[1] = (v >> 8) & 255; rgb[2] = v & 255; }
                fwrite(rgb, 1, 3, f); }
            fclose(f); hostlog("primary surface dumped to %s (%dx%d, %dbpp)\n", po, sf.w, sf.h, sf.bpp); break; }
    }
    hostlog("COM methods used (first-use order):"); for (auto& n : ddorder) hostlog(" %s", n.c_str()); hostlog("\n");
    return ok ? 0 : 1;
}
}  // namespace pm3
