/* DOSBox 0.74-3: 拡大前の画面を 640x400 にして日本語を重ねる */
#include "phjp.h"
#include <string.h>

#define SRC_W 320
#define SRC_H 200

#define AT_SETSIZE     0x50f6f0
#define AT_START       0x50ea60
#define AT_END         0x50ed30
#define AT_DRAWLINE    0x1ee3640
#define AT_PALETTE     0x1ee36e8

typedef void (__cdecl *setsize_fn)(uint32_t, uint32_t, uint32_t, float, double, uint32_t, uint32_t);
typedef uint8_t (__cdecl *start_fn)(void);
typedef void (__cdecl *end_fn)(uint32_t);
typedef void (__cdecl *line_fn)(const void *);

static setsize_fn o_setsize;
static start_fn   o_start;
static end_fn     o_end;

static int g_on, g_collect, g_y;
static uint8_t g_src[SRC_W * SRC_H], g_dst[SRC_W * 2 * SRC_H * 2];

#define DRAWLINE (*(line_fn volatile *)AT_DRAWLINE)

static void __cdecl empty_line(const void *p) { (void)p; }

static void __cdecl take_line(const void *p)
{
    if (g_y < SRC_H) memcpy(g_src + g_y * SRC_W, p, SRC_W);
    g_y++;
}

static void __cdecl h_setsize(uint32_t w, uint32_t h, uint32_t bpp, float fps, double ratio, uint32_t dw, uint32_t dh)
{
    g_collect = 0;
    g_on = w == SRC_W && h == SRC_H && bpp == 8;
    LOG("画面 %ux%u bpp=%u 倍=%u,%u 比=%d on=%d", w, h, bpp, dw & 255, dh & 255, (int)(ratio * 1000), g_on);
    if (g_on) o_setsize(SRC_W * 2, SRC_H * 2, bpp, fps, ratio, 0, 0);
    else o_setsize(w, h, bpp, fps, ratio, dw, dh);
}

static uint8_t __cdecl h_start(void)
{
    if (!g_on) return o_start();
    g_collect = 1;
    g_y = 0;
    DRAWLINE = take_line;
    return 1;
}

static void __cdecl h_end(uint32_t abort)
{
    int y;
    if (!g_on) { o_end(abort); return; }
    if (!g_collect) return;
    g_collect = 0;
    DRAWLINE = empty_line;
    if ((abort & 255) || g_y < SRC_H) return;
    overlay_cga(g_src, g_dst);
#ifdef PHJP_TEST
    test_frame8(g_src, g_dst, (const uint8_t *)AT_PALETTE);
#endif
    if (!(o_start() & 255)) return;
    for (y = 0; y < SRC_H * 2; y++) DRAWLINE(g_dst + y * SRC_W * 2);
    o_end(0);
}

static void *detour(uint32_t at, const uint8_t *sig, int n, void *fn)
{
    uint8_t *p = (uint8_t *)at, *t;
    DWORD prot;
    t = VirtualAlloc(NULL, 32, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
    if (!t) return NULL;
    memcpy(t, p, n);
    t[n] = 0xe9;
    *(int32_t *)(t + n + 1) = (int32_t)(p + n - (t + n + 5));
    VirtualProtect(p, n, PAGE_EXECUTE_READWRITE, &prot);
    memset(p, 0x90, n);
    p[0] = 0xe9;
    *(int32_t *)(p + 1) = (int32_t)((uint8_t *)fn - (p + 5));
    VirtualProtect(p, n, prot, &prot);
    FlushInstructionCache(GetCurrentProcess(), p, n);
    return t;
}

int dosbox_install(void)
{
    static const uint8_t s_size[]  = { 0x83,0xec,0x3c,0xb9,0xe0,0xe8,0x50,0x00 };
    static const uint8_t s_start[] = { 0x56,0x31,0xd2,0x53,0x83,0xec,0x14 };
    static const uint8_t s_end[]   = { 0x83,0xec,0x2c,0x80,0x3d,0xf4,0x3f,0xee,0x01,0x00 };
    uint8_t *b = (uint8_t *)GetModuleHandleA(NULL);
    IMAGE_NT_HEADERS *nt = (IMAGE_NT_HEADERS *)(b + ((IMAGE_DOS_HEADER *)b)->e_lfanew);

    if ((uint32_t)b != 0x400000 || nt->OptionalHeader.SizeOfImage < AT_PALETTE - 0x400000 + 1024) return 0;
    if (memcmp((void *)AT_SETSIZE, s_size, sizeof s_size) ||
        memcmp((void *)AT_START, s_start, sizeof s_start) ||
        memcmp((void *)AT_END, s_end, sizeof s_end)) {
        LOG("DOSBox の版が違う");
        return 0;
    }
    o_start   = (start_fn)detour(AT_START, s_start, sizeof s_start, h_start);
    o_end     = (end_fn)detour(AT_END, s_end, sizeof s_end, h_end);
    o_setsize = (setsize_fn)detour(AT_SETSIZE, s_size, sizeof s_size, h_setsize);
    return o_start && o_end && o_setsize;
}
