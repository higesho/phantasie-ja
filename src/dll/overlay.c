/* 東雲の字を描く。第1作・第3作は 320x200 の絵から文字を読む */
#include "phjp.h"
#include <string.h>

#define W 320
#define H 200
#define KU 94

typedef struct {
    const uint8_t *kanji, *ascii;
    int w, h;
} font_t;

static font_t g_f16, g_f14;
static uint32_t g_hlo[1024], g_hhi[1024];
static uint8_t g_hch[1024];

static int slot(uint32_t lo, uint32_t hi)
{
    return (int)(((lo * 2654435761u) ^ (hi * 1597334677u) ^ (hi >> 13)) >> 22);
}

static void build_glyphs(const uint8_t *g)
{
    int c, i;
    uint32_t lo, hi;
    for (c = 33; c < 127; c++) {
        memcpy(&lo, g + c * 8, 4);
        memcpy(&hi, g + c * 8 + 4, 4);
        if (!lo && !hi) continue;
        for (i = slot(lo, hi); g_hch[i]; i = (i + 1) & 1023)
            if (g_hlo[i] == lo && g_hhi[i] == hi) break;
        if (g_hch[i]) continue; /* 同じ形は若い番号 */
        g_hlo[i] = lo; g_hhi[i] = hi; g_hch[i] = (uint8_t)c;
    }
}

static int lookup(uint32_t lo, uint32_t hi)
{
    int i;
    for (i = slot(lo, hi); g_hch[i]; i = (i + 1) & 1023)
        if (g_hlo[i] == lo && g_hhi[i] == hi) return g_hch[i];
    return 0;
}

int overlay_init(const char *exedir)
{
    g_f16.kanji = res_data(RES_KANJI16); g_f16.ascii = res_data(RES_ASCII16); g_f16.w = 16; g_f16.h = 16;
    g_f14.kanji = res_data(RES_KANJI14); g_f14.ascii = res_data(RES_ASCII14); g_f14.w = 14; g_f14.h = 14;
    if (!g_f16.kanji || !g_f16.ascii || !g_f14.kanji || !g_f14.ascii) return 0;
    if (g_game != GAME_PH2) {
        uint8_t g[2048];
        if (!glyphs_load(g_game, exedir, g)) {
            LOG("ゲームの字形を読めない");
            return 0;
        }
        build_glyphs(g);
    }
    return 1;
}

void overlay_op(const op_t *op, int cw, int ch, int top, put_fn put, void *ctx)
{
    const font_t *f = cw == 14 ? &g_f14 : &g_f16;
    const uint8_t *s = op->text;
    int x, y, left = op->a * cw, right = op->b * cw, dy = top + (ch - f->h) / 2;

    for (y = top; y < top + ch; y++)
        for (x = left; x < right; x++) put(ctx, x, y, op->bg);
    x = left;
    while (*s) {
        const uint8_t *g = NULL;
        int w = cw / 2, r, i, wide = 0;
        if (*s >= 0x80 && s[1]) {
            int ku = s[0] - 0xa1, ten = s[1] - 0xa1;
            w = cw; wide = 1;
            if (ku >= 0 && ku < KU && ten >= 0 && ten < KU) g = f->kanji + (ku * KU + ten) * f->h * 2;
            s += 2;
        } else {
            g = f->ascii + *s * f->h;
            s++;
        }
        if (x + w > right) break;
        if (g) {
            for (r = 0; r < f->h; r++) {
                unsigned bits = wide ? (unsigned)(g[r * 2] << 8 | g[r * 2 + 1]) : (unsigned)g[r] << 8;
                for (i = 0; i < w; i++)
                    if (bits & (0x8000u >> i)) put(ctx, x + i, dy + r, op->fg);
            }
        }
        x += w;
    }
}

/* ---- 320x200 ---- */

typedef struct {
    uint8_t c0, c1, n, mask; /* mask は c1 の点 */
} line_t;

static line_t g_line[H][COLS];

static void read_lines(const uint8_t *src)
{
    int y, col, x;
    for (y = 0; y < H; y++) {
        for (col = 0; col < COLS; col++) {
            const uint8_t *p = src + y * W + col * 8;
            line_t *l = &g_line[y][col];
            l->c0 = p[0]; l->c1 = p[0]; l->n = 1; l->mask = 0;
            for (x = 1; x < 8; x++) {
                if (p[x] == l->c0) continue;
                if (l->n == 1) { l->n = 2; l->c1 = p[x]; }
                else if (p[x] != l->c1) { l->n = 3; break; }
                l->mask |= 0x80 >> x;
            }
        }
    }
}

/* 升を読む: 文字なら番号、一様なら空白、ほかは 0 */
static int read_cell(int y, int col, cell_t *out)
{
    uint8_t a = g_line[y][col].c0, b = a, m[8];
    uint32_t lo, hi;
    int r, two = 0, c;

    out->ch = 0; out->bg = out->fg = a;
    for (r = 0; r < 8; r++) {
        const line_t *l = &g_line[y + r][col];
        if (l->n > 2) return 0;
        if (l->c0 != a) {
            if (!two) { two = 1; b = l->c0; }
            else if (l->c0 != b) return 0;
        }
        if (l->n == 2 && l->c1 != a) {
            if (!two) { two = 1; b = l->c1; }
            else if (l->c1 != b) return 0;
        }
        /* m は色 b の点 */
        m[r] = l->c0 != a ? (uint8_t)~(l->n == 2 ? l->mask : 0) : (l->n == 2 ? l->mask : 0);
    }
    if (!two) {
        out->ch = ' ';
        return ' ';
    }
    memcpy(&lo, m, 4);
    memcpy(&hi, m + 4, 4);
    if ((c = lookup(lo, hi)) != 0) {
        out->fg = b;
    } else if ((c = lookup(~lo, ~hi)) != 0) {
        out->bg = b;
    } else {
        return 0;
    }
    out->ch = (uint8_t)c;
    return c;
}

typedef struct {
    uint8_t *dst;
} cga_ctx;

static void put_cga(void *ctx, int x, int y, uint32_t c)
{
    if (x >= 0 && x < W * 2 && y >= 0 && y < H * 2) ((cga_ctx *)ctx)->dst[y * W * 2 + x] = (uint8_t)c;
}

void overlay_cga(const uint8_t *src, uint8_t *dst)
{
    static uint8_t prev[W * H], cache[W * H * 4];
    static row_t rows[MAXROWS];
    static op_t ops[MAXOPS];
    static int have;
    cga_ctx ctx;
    int x, y, n = 0, nops, i;

    if (have && !memcmp(prev, src, sizeof prev)) {
        memcpy(dst, cache, sizeof cache);
        return;
    }
    read_lines(src);
    for (y = 0; y <= H - 8 && n < MAXROWS; y++) {
        row_t *r = &rows[n];
        int found = 0;
        for (i = 0; i < COLS; i++) {
            int c = read_cell(y, i, &r->c[i]);
            if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9')) found = 1;
        }
        if (found) {
            r->y = y;
            n++;
        }
    }
    nops = text_translate(rows, n, ops);

    for (y = 0; y < H; y++) {
        uint8_t *d = cache + y * 2 * W * 2;
        for (x = 0; x < W; x++) d[x * 2] = d[x * 2 + 1] = src[y * W + x];
        memcpy(d + W * 2, d, W * 2);
    }
    ctx.dst = cache;
    for (i = 0; i < nops; i++) overlay_op(&ops[i], 16, 16, rows[ops[i].row].y * 2, put_cga, &ctx);

    memcpy(prev, src, sizeof prev);
    memcpy(dst, cache, sizeof cache);
    have = 1;
}
