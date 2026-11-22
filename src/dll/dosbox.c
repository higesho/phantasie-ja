/* DOSBox（0.72〜0.74 系の公式版）: 拡大前の画面を 640x400 にして日本語を重ねる
   描画の関数と変数の番地は、起動時に命令の形で探す（dbxfind.c） */
#include "phjp.h"
#include "dbxfind.h"
#include <string.h>

#define SRC_W 320
#define SRC_H 200

typedef void (__cdecl *setsize_fn)(uint32_t, uint32_t, uint32_t, float, double, uint32_t, uint32_t);
typedef uint8_t (__cdecl *start_fn)(void);
typedef void (__cdecl *end_fn)(uint32_t);
typedef void (__cdecl *line_fn)(const void *);

static setsize_fn o_setsize;
static start_fn   o_start;
static end_fn     o_end;
static line_fn volatile *g_drawline;
#ifdef PHJP_TEST
static const uint8_t *g_palette;
#endif

static int g_on, g_collect, g_y, g_end_arg;
static uint8_t g_src[SRC_W * SRC_H], g_dst[SRC_W * 2 * SRC_H * 2];

#define DRAWLINE (*g_drawline)

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
    if (!g_end_arg) abort = 0;  /* 0.72 は引数なし。そこにあるのは呼んだ側の値 */
    if (!g_collect) return;
    g_collect = 0;
    DRAWLINE = empty_line;
    if ((abort & 255) || g_y < SRC_H) return;
    overlay_cga(g_src, g_dst);
#ifdef PHJP_TEST
    if (g_palette) test_frame8(g_src, g_dst, g_palette);
#endif
    if (!(o_start() & 255)) return;
    for (y = 0; y < SRC_H * 2; y++) DRAWLINE(g_dst + y * SRC_W * 2);
    o_end(0);
}

/* 頭の n バイト（位置によらない命令だけ。dbxfind.c が確かめる）を写して飛び先を作る */
static void *detour(uint32_t at, int n, void *fn)
{
    uint8_t *p = (uint8_t *)at, *t;
    DWORD prot;
    if (n < 5 || n > 24) return NULL;
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
    uint8_t *b = (uint8_t *)GetModuleHandleA(NULL);
    IMAGE_NT_HEADERS *nt = (IMAGE_NT_HEADERS *)(b + ((IMAGE_DOS_HEADER *)b)->e_lfanew);
    IMAGE_SECTION_HEADER *s = IMAGE_FIRST_SECTION(nt);
    uint32_t base = (uint32_t)b, lo = 0, hi = 0;
    dbx_t f;
    int i;

    /* 命令の中の番地は読み込み先 0x400000 が前提 */
    if (base != nt->OptionalHeader.ImageBase) return 0;
    for (i = 0; i < nt->FileHeader.NumberOfSections; i++, s++) {
        if (!(s->Characteristics & IMAGE_SCN_MEM_EXECUTE)) continue;
        lo = base + s->VirtualAddress;
        hi = lo + s->Misc.VirtualSize;
        break;
    }
    if (!lo || !dbx_find(b, base, nt->OptionalHeader.SizeOfImage, lo, hi, &f)) {
        LOG("DOSBox の描画の関数が見つからない");
        return 0;
    }
    LOG("DOSBox SetSize=%x Start=%x End=%x DrawLine=%x 印=%x パレット=%x 頭=%d,%d,%d 中断の引数=%d",
        f.setsize, f.start, f.end, f.drawline, f.flag, f.palette, f.n_setsize, f.n_start, f.n_end, f.end_arg);
    g_drawline = (line_fn volatile *)f.drawline;
    g_end_arg = f.end_arg;
#ifdef PHJP_TEST
    g_palette = (const uint8_t *)f.palette;
#endif
    o_start   = (start_fn)detour(f.start, f.n_start, h_start);
    o_end     = (end_fn)detour(f.end, f.n_end, h_end);
    o_setsize = (setsize_fn)detour(f.setsize, f.n_setsize, h_setsize);
    return o_start && o_end && o_setsize;
}
