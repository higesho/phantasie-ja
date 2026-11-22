/* 試験版だけに入る部品。固有のタイトルの窓を相手に撮影する。配布版には入れない */
#include "phjp.h"
#include <stdio.h>
#include <stdarg.h>
#include <string.h>

#define MSG_SHOT (WM_APP + 0x3a7)

static char g_dir[MAX_PATH], g_title[80];
static HWND g_wnd;
static WNDPROC o_proc;
static CRITICAL_SECTION g_cs;
static uint8_t f_src[320 * 200], f_pal[1024], f_rgb[800 * 600 * 4];
static int f_w, f_h, f_bpp, f_up;

void test_log(const char *fmt, ...)
{
    char p[MAX_PATH + 32];
    va_list a;
    FILE *f;
    sprintf(p, "%sphjp_test.log", g_dir);
    if (!(f = fopen(p, "a"))) return;
    va_start(a, fmt); vfprintf(f, fmt, a); va_end(a);
    fputc('\n', f); fclose(f);
}

void test_frame8(const uint8_t *src, const uint8_t *dst, const uint8_t *pal)
{
    int i;
    EnterCriticalSection(&g_cs);
    memcpy(f_src, src, sizeof f_src);
    memcpy(f_pal, pal, sizeof f_pal);
    for (i = 0; i < 640 * 400; i++) {
        const uint8_t *c = pal + dst[i] * 4;
        f_rgb[i * 4] = c[2]; f_rgb[i * 4 + 1] = c[1]; f_rgb[i * 4 + 2] = c[0];
    }
    f_w = 640; f_h = 400; f_bpp = 32; f_up = 0;
    LeaveCriticalSection(&g_cs);
}

void test_frame32(const void *bits, int w, int h, int bpp)
{
    if (bpp != 32 || w * h * 4 > (int)sizeof f_rgb) return;
    EnterCriticalSection(&g_cs);
    memcpy(f_rgb, bits, w * h * 4);
    f_w = w; f_h = h; f_bpp = bpp; f_up = 1;
    LeaveCriticalSection(&g_cs);
}

static void save(void)
{
    char p[MAX_PATH + 32];
    BITMAPFILEHEADER fh = {0};
    BITMAPINFOHEADER ih = {0};
    FILE *f;
    if (!f_w) return;
    EnterCriticalSection(&g_cs);
    fh.bfType = 0x4d42; fh.bfOffBits = sizeof fh + sizeof ih;
    fh.bfSize = fh.bfOffBits + f_w * f_h * 4;
    ih.biSize = sizeof ih; ih.biWidth = f_w; ih.biHeight = f_up ? f_h : -f_h;
    ih.biPlanes = 1; ih.biBitCount = 32;
    sprintf(p, "%sphjp_shot.bmp", g_dir);
    if ((f = fopen(p, "wb")) != NULL) {
        fwrite(&fh, sizeof fh, 1, f); fwrite(&ih, sizeof ih, 1, f);
        fwrite(f_rgb, f_w * 4, f_h, f); fclose(f);
    }
    if (g_game != GAME_PH2) {
        sprintf(p, "%sphjp_src.bin", g_dir);
        if ((f = fopen(p, "wb")) != NULL) { fwrite(f_src, sizeof f_src, 1, f); fwrite(f_pal, 1024, 1, f); fclose(f); }
    }
    LeaveCriticalSection(&g_cs);
}

static LRESULT CALLBACK proc(HWND w, UINT m, WPARAM wp, LPARAM lp)
{
    if (m == MSG_SHOT) { save(); return 0x5048; }
    if (m == WM_SETTEXT) lp = (LPARAM)g_title;
    return CallWindowProcA(o_proc, w, m, wp, lp);
}

static BOOL CALLBACK pick(HWND w, LPARAM l)
{
    DWORD pid;
    char c[64];
    GetWindowThreadProcessId(w, &pid);
    if (pid != GetCurrentProcessId() || !IsWindowVisible(w)) return TRUE;
    GetClassNameA(w, c, sizeof c);
    if (strcmp(c, "SDL_app") && strcmp(c, "APPLE2FRAME")) return TRUE;
    *(HWND *)l = w;
    return FALSE;
}

static DWORD WINAPI watch(LPVOID a)
{
    for (;;) {
        HWND w = NULL;
        EnumWindows(pick, (LPARAM)&w);
        if (w && w != g_wnd) {
            g_wnd = w;
            o_proc = (WNDPROC)SetWindowLongA(w, GWL_WNDPROC, (LONG)proc);
            SetWindowTextA(w, g_title);
            SetWindowPos(w, HWND_BOTTOM, 0, 0, 0, 0, SWP_NOACTIVATE | SWP_NOSIZE | SWP_NOMOVE);
            LOG("窓 %s", g_title);
        }
        Sleep(200);
    }
}

/* ほかの作業の窓やキー・マウスを奪わないようにする */
static BOOL WINAPI no_fg(HWND w) { return TRUE; }
static HWND WINAPI no_focus(HWND w) { return NULL; }
static BOOL WINAPI no_clip(const RECT *r) { return TRUE; }
static BOOL WINAPI no_setcur(int x, int y) { return TRUE; }
static LONG WINAPI no_mode(void *m, DWORD f) { return DISP_CHANGE_SUCCESSFUL; }
static BOOL WINAPI no_hot(HWND w, int i, UINT a, UINT b) { return TRUE; }
static HHOOK WINAPI no_hook(int i, HOOKPROC p, HINSTANCE h, DWORD t) { return (HHOOK)1; }
static BOOL WINAPI no_unhook(HHOOK h) { return TRUE; }
static BOOL WINAPI show(HWND w, int n)
{
    return ShowWindow(w, n == SW_HIDE ? n : SW_SHOWNOACTIVATE);
}
static BOOL WINAPI place(HWND w, HWND a, int x, int y, int cx, int cy, UINT f)
{
    return SetWindowPos(w, a, x, y, cx, cy, f | SWP_NOACTIVATE | SWP_NOZORDER);
}
static LONG WINAPI no_regset(HKEY k, LPCSTR n, DWORD r, DWORD t, const BYTE *d, DWORD c) { return ERROR_SUCCESS; }

void test_init(void)
{
    char *s;
    int i;
    HMODULE m[2];
    GetModuleFileNameA(NULL, g_dir, MAX_PATH);
    if ((s = strrchr(g_dir, '\\')) != NULL) s[1] = 0;
    sprintf(g_title, "PHJP-TEST-PH%d-%lu", g_game, GetCurrentProcessId());
    InitializeCriticalSection(&g_cs);
    m[0] = GetModuleHandleA(NULL);
    m[1] = GetModuleHandleA("SDL.dll");
    for (i = 0; i < 2; i++) {
        if (!m[i]) continue;
        patch_iat(m[i], "USER32.dll", "SetForegroundWindow", no_fg);
        patch_iat(m[i], "USER32.dll", "SetFocus", no_focus);
        patch_iat(m[i], "USER32.dll", "ClipCursor", no_clip);
        patch_iat(m[i], "USER32.dll", "SetCursorPos", no_setcur);
        patch_iat(m[i], "USER32.dll", "ChangeDisplaySettingsA", no_mode);
        patch_iat(m[i], "USER32.dll", "RegisterHotKey", no_hot);
        patch_iat(m[i], "USER32.dll", "SetWindowsHookExA", no_hook);
        patch_iat(m[i], "USER32.dll", "UnhookWindowsHookEx", no_unhook);
        patch_iat(m[i], "USER32.dll", "ShowWindow", show);
        patch_iat(m[i], "USER32.dll", "SetWindowPos", place);
        patch_iat(m[i], "ADVAPI32.dll", "RegSetValueExA", no_regset);
    }
    CreateThread(NULL, 0, watch, NULL, 0, NULL);
}
