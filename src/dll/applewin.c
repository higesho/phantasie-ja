/* AppleWin 1.30.14.1: 画面へ写す直前の絵に日本語を重ねる

   文字は Apple II の主メモリーの文字画面から読む（絵の文字は色がにじんで読めない）。
   写し元の絵には書かない。自前の絵に写して描き、そこから窓へ写す。 */
#include "phjp.h"
#include <string.h>

#define IMG_W 600
#define IMG_H 420
#define AREA_X 20
#define AREA_Y 18
#define CW 14
#define CH 16

/* 番地は読み込み先 0x400000 での値 */
#define AT_SIG      0x4792c9
#define AT_MEM      0x5ef9e0
#define AT_MEMMAIN  0x5efe18
#define AT_SHADOW   0x5ef9f8
#define AT_VIDEO    0x571f80

#define VM_80COL 0x01
#define VM_MIXED 0x10
#define VM_PAGE2 0x20
#define VM_TEXT  0x40

typedef BOOL (WINAPI *stretch_fn)(HDC, int, int, int, int, HDC, int, int, int, int, DWORD);
static stretch_fn o_stretch;
static uint8_t *g_base;
static HDC g_dc;
static uint32_t *g_bits;

static void *at(uint32_t a) { return g_base + (a - 0x400000); }

static int make_dib(void)
{
    BITMAPINFO bi;
    HBITMAP bm;
    if (g_bits) return 1;
    memset(&bi, 0, sizeof bi);
    bi.bmiHeader.biSize = sizeof bi.bmiHeader;
    bi.bmiHeader.biWidth = IMG_W;
    bi.bmiHeader.biHeight = IMG_H; /* 元と同じ下から上 */
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    if (!g_dc && !(g_dc = CreateCompatibleDC(NULL))) return 0;
    bm = CreateDIBSection(g_dc, &bi, DIB_RGB_COLORS, (void **)&g_bits, NULL, 0);
    if (!bm) return 0;
    SelectObject(g_dc, bm);
    return 1;
}

static uint32_t *pixel(int x, int y)
{
    return g_bits + (IMG_H - 1 - (AREA_Y + y)) * IMG_W + AREA_X + x;
}

static void put_apple(void *ctx, int x, int y, uint32_t c)
{
    (void)ctx;
    if (x >= 0 && x < COLS * CW && y >= 0 && y < 24 * CH) *pixel(x, y) = c;
}

static int light(uint32_t c)
{
    return (int)((c >> 16 & 255) * 3 + (c >> 8 & 255) * 6 + (c & 255));
}

static int screen_char(int c)
{
    c &= 0x7f; /* 0x00-3f 反転、0x40-7f 点滅、0x80- 通常 */
    if (c < 0x20) c += 0x40;
    return c == 0x7f ? 0 : c;
}

static int raw_char(int c)
{
    if (c >= 0x80) return screen_char(c);
    c &= 0x3f;
    return c < 0x20 ? c + 0x40 : c;
}

static int read_screen(row_t *rows)
{
    const uint8_t *mem = *(uint8_t **)at(AT_MEM);
    const uint8_t *mainram = *(uint8_t **)at(AT_MEMMAIN);
    uint8_t **shadow = (uint8_t **)at(AT_SHADOW);
    uint32_t mode = *(uint32_t *)at(AT_VIDEO);
    int base = mode & VM_PAGE2 ? 0x800 : 0x400, first, r, i, x, y;

    if (!mem || !mainram || (mode & VM_80COL)) return 0;
    if (mode & VM_TEXT) first = 0;
    else if (mode & VM_MIXED) first = 20;
    else return 0;

    for (r = first; r < 24; r++) {
        row_t *row = &rows[r - first];
        int off = base + (r & 7) * 128 + (r >> 3) * 40;
        int page = off >> 8, top = r * CH;
        const uint8_t *ram = shadow[page] == mainram + page * 256 ? mem : mainram;
        uint32_t bright = 0, dark = 0xffffff;
        int hi = -1, lo = 1 << 20;

        for (y = top; y < top + CH; y++) {
            for (x = 0; x < COLS * CW; x++) {
                uint32_t c = *pixel(x, y) & 0xffffff;
                int l = light(c);
                if (l > hi) { hi = l; bright = c; }
                if (l < lo) { lo = l; dark = c; }
            }
        }
        row->y = top;
        for (i = 0; i < COLS; i++) {
            /* 字の左端の 2 列は地。反転と点滅は絵のほうを正とする */
            int n = 0, inverse;
            for (y = 0; y < CH; y++)
                for (x = 0; x < 2; x++)
                    if (light(*pixel(i * CW + x, top + y)) * 2 > hi + lo) n++;
            inverse = ram[off + i] < 0x80 && hi - lo > 40 && n >= 8;
            row->c[i].ch = (uint8_t)raw_char(ram[off + i]);
            row->c[i].bg = inverse ? bright : dark;
            row->c[i].fg = inverse ? dark : bright;
        }
    }
    return 24 - first;
}

/* 元の字のにじみは升の上へ 1 点、右へ 3 点出る。その薄い点だけを消す */
static void wipe_smear(const op_t *op, int top)
{
    int half = (light(op->fg) + light(op->bg)) / 2, right = op->b * CW, x, y;

    if (light(op->fg) <= light(op->bg)) return; /* 反転 */
    for (y = top - 1; y < top + CH; y++) { /* 画の縁があるので -1 も読める */
        for (x = op->a * CW; x < right + 3 && x < COLS * CW; x++) {
            uint32_t *p;
            if (y >= top && x < right) continue; /* 升の中は描くときに塗る */
            p = pixel(x, y);
            if (light(*p) < half) *p = op->bg;
        }
    }
}

static int draw(const DIBSECTION *ds)
{
    static row_t rows[24];
    static op_t ops[MAXOPS];
    int nrows, nops, i;

    if (!make_dib()) return 0;
    GdiFlush();
    memcpy(g_bits, ds->dsBm.bmBits, IMG_W * IMG_H * 4);
    nrows = read_screen(rows);
    if (!nrows) return 0;
    nops = text_translate(rows, nrows, ops);
    /* 地の色は行全体の最暗。色を読み終えてから描く */
    for (i = 0; i < nops; i++) wipe_smear(&ops[i], rows[ops[i].row].y);
    for (i = 0; i < nops; i++) overlay_op(&ops[i], CW, CH, rows[ops[i].row].y, put_apple, NULL);
    return nops;
}

static BOOL WINAPI h_stretch(HDC d, int dx, int dy, int dw, int dh, HDC s, int sx, int sy, int sw, int sh, DWORD rop)
{
    DIBSECTION ds;
    if (rop == SRCCOPY && GetObjectA(GetCurrentObject(s, OBJ_BITMAP), sizeof ds, &ds) == sizeof ds
        && ds.dsBm.bmBits && ds.dsBm.bmWidth == IMG_W && ds.dsBm.bmHeight == IMG_H
        && ds.dsBm.bmBitsPixel == 32 && ds.dsBmih.biHeight > 0) {
        int n = draw(&ds);
#ifdef PHJP_TEST
        test_frame32(g_bits ? (void *)g_bits : ds.dsBm.bmBits, IMG_W, IMG_H, 32);
#endif
        if (n) s = g_dc;
    }
    return o_stretch(d, dx, dy, dw, dh, s, sx, sy, sw, sh, rop);
}

int applewin_install(void)
{
    const uint8_t *p;
    IMAGE_NT_HEADERS *nt;

    g_base = (uint8_t *)GetModuleHandleA(NULL);
    nt = (IMAGE_NT_HEADERS *)(g_base + ((IMAGE_DOS_HEADER *)g_base)->e_lfanew);
    if (nt->OptionalHeader.SizeOfImage < AT_MEMMAIN + 4 - 0x400000) return 0;
    /* 8b 35 [番地] 25 00 f0 00 00 57 8b 3d [番地] */
    p = (const uint8_t *)at(AT_SIG);
    if (memcmp(p, "\x8b\x35", 2) || memcmp(p + 6, "\x25\x00\xf0\x00\x00\x57\x8b\x3d", 8)) {
        LOG("AppleWin の版が違う");
        return 0;
    }
    o_stretch = (stretch_fn)patch_iat((HMODULE)g_base, "GDI32.dll", "StretchBlt", h_stretch);
    return o_stretch != NULL;
}
