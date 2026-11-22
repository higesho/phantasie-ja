/* DOSBox の描画の関数と変数を、番地ではなく命令の形で探す（0.72〜0.74 系の公式版）

   手がかり（src/gui/render.cpp）
     RENDER_SetSize:     描画を止めて（印を 0 に）、幅 0x500・高さ 0x400 の上限と比べる
     RENDER_StartUpdate: 印と 0 を比べ、最後に印を 1 にする。描画の変数にも書く
     RENDER_EndUpdate:   印と 0 を比べ、描画の変数に「何もしない関数（ret だけ）」を入れる
   どれかが一つに決まらなければ 0 を返し、何も書き換えない */
#include "dbxfind.h"
#include <string.h>

typedef struct {
    const uint8_t *img;
    uint32_t base, size, lo, hi;
} view_t;

static const uint8_t *at(const view_t *v, uint32_t va, uint32_t n)
{
    if (va < v->base || va - v->base > v->size || n > v->size - (va - v->base)) return NULL;
    return v->img + (va - v->base);
}

static int byte_at(const view_t *v, uint32_t va)
{
    const uint8_t *p = at(v, va, 1);
    return p ? *p : -1;
}

static uint32_t u32_at(const view_t *v, uint32_t va)
{
    const uint8_t *p = at(v, va, 4);
    uint32_t x = 0;
    if (p) memcpy(&x, p, 4);
    return x;
}

static int same(const view_t *v, uint32_t va, const char *s, int n)
{
    const uint8_t *p = at(v, va, n);
    return p && !memcmp(p, s, n);
}

/* 頭の命令を 5 バイト以上読む。知らない命令なら 0 */
static int prologue(const view_t *v, uint32_t a)
{
    int n = 0;
    while (n < 5) {
        int c = byte_at(v, a + n), d = byte_at(v, a + n + 1);
        if (c < 0 || d < 0) return 0;
        if (c >= 0x50 && c <= 0x57) n += 1;                  /* push r32 */
        else if (c == 0x83 && d == 0xec) n += 3;             /* sub esp, imm8 */
        else if ((c == 0x31 || c == 0x33) && d >= 0xc0) n += 2; /* xor r32, r32 */
        else if (c >= 0xb8 && c <= 0xbf) n += 5;             /* mov r32, imm32 */
        else if (c == 0x80 && d == 0x3d) n += 7;             /* cmp byte [abs32], imm8 */
        else return 0;
    }
    return n;
}

/* 直前が詰め物か ret で終わっている */
static int padded_before(const view_t *v, uint32_t a)
{
    int b = byte_at(v, a - 1);
    return b == 0x90 || b == 0xc3 || b == 0xcc
        || same(v, a - 4, "\x8d\x74\x26\x00", 4)
        || same(v, a - 7, "\x8d\xb4\x26\x00\x00\x00\x00", 7)
        || same(v, a - 3, "\x8d\x76\x00", 3)
        || same(v, a - 2, "\x89\xf6", 2);
}

/* va を含む関数の頭（16 にそろう） */
static uint32_t func_start(const view_t *v, uint32_t va, uint32_t back)
{
    uint32_t a = va & ~15u;
    for (; a + back > va && a >= v->lo; a -= 16)
        if (padded_before(v, a) && prologue(v, a)) return a;
    return 0;
}

/* cmp r32, imm32（81 F8+r imm32）。r を返す、違えば -1 */
static int cmp_imm(const view_t *v, uint32_t va, uint32_t imm)
{
    int b = byte_at(v, va + 1);
    if (byte_at(v, va) != 0x81 || b < 0xf8 || u32_at(v, va + 2) != imm) return -1;
    return b - 0xf8;
}

/* [var] への 4 バイトの代入（A3 か 89 /r disp32）。見つけた数 */
static int stores_to(const view_t *v, uint32_t lo, uint32_t hi, uint32_t var)
{
    uint32_t k;
    int n = 0;
    for (k = lo; k < hi; k++) {
        int c = byte_at(v, k), m = byte_at(v, k + 1);
        if (c == 0xa3 && u32_at(v, k + 1) == var) n++;
        else if (c == 0x89 && (m & 0xc7) == 0x05 && u32_at(v, k + 2) == var) n++;
    }
    return n;
}

/* 関数の頭の push と sub esp から、最初の引数の [esp+x] の x を出す */
static int first_arg(const view_t *v, uint32_t a)
{
    int off = 4, k;
    for (k = 0; k < 8; ) {
        int c = byte_at(v, a + k), d = byte_at(v, a + k + 1);
        if (c >= 0x50 && c <= 0x57) { off += 4; k += 1; }
        else if (c == 0x83 && d == 0xec) { off += byte_at(v, a + k + 2); k += 3; }
        else if ((c == 0x31 || c == 0x33) && d >= 0xc0) k += 2;
        else break;
    }
    return off;
}

/* 頭の近くで [esp+off] の 1 バイトを読む（movzx / mov r8 / cmp byte ,0） */
static int reads_arg(const view_t *v, uint32_t a, int off)
{
    uint32_t k;
    for (k = a; k < a + 0x30; k++) {
        int c = byte_at(v, k), m = byte_at(v, k + 1);
        if (c == 0x0f && m == 0xb6 && (byte_at(v, k + 2) & 0xc7) == 0x44 && byte_at(v, k + 3) == 0x24
            && byte_at(v, k + 4) == off) return 1;
        if (c == 0x8a && (m & 0xc7) == 0x44 && byte_at(v, k + 2) == 0x24 && byte_at(v, k + 3) == off) return 1;
        if (c == 0x80 && m == 0x7c && byte_at(v, k + 2) == 0x24 && byte_at(v, k + 3) == off && byte_at(v, k + 4) == 0)
            return 1;
    }
    return 0;
}

static int has(const view_t *v, uint32_t lo, uint32_t hi, const char *pat, int n)
{
    uint32_t k;
    for (k = lo; k + n <= hi; k++)
        if (same(v, k, pat, n)) return 1;
    return 0;
}

int dbx_find(const uint8_t *img, uint32_t base, uint32_t size, uint32_t text_lo, uint32_t text_hi, dbx_t *out)
{
    view_t v;
    uint32_t k, setsize = 0, cmp_at = 0, cmps[3], flag, f, end = 0, start = 0, drawline = 0, pal = 0;
    int nsize = 0, ncmp = 0, i, frame;
    char pat[8];

    memset(out, 0, sizeof *out);
    v.img = img; v.base = base; v.size = size; v.lo = text_lo; v.hi = text_hi;
    if (text_lo < base || text_hi > base + size || text_lo >= text_hi) return 0;

    /* 1. SetSize */
    for (k = text_lo; k + 6 <= text_hi; k++) {
        uint32_t q;
        if (cmp_imm(&v, k, 0x500) < 0) continue;
        for (q = k + 6; q < k + 0x30; q++) {
            if (cmp_imm(&v, q, 0x400) < 0) continue;
            if ((f = func_start(&v, k, 0x400)) != 0) {
                setsize = f;
                cmp_at = k;
                nsize++;
            }
            break;
        }
    }
    if (nsize != 1) return 0;
    for (k = setsize; k < cmp_at; k++)
        if (byte_at(&v, k) == 0xc6 && byte_at(&v, k + 1) == 0x05 && byte_at(&v, k + 6) == 0x00) break;
    if (k >= cmp_at) return 0;
    flag = u32_at(&v, k + 2);

    /* SetSize の引数の並び: sub esp,N の後、倍率（double）が esp+N+0x14 */
    for (k = setsize; k < setsize + 8 && !(byte_at(&v, k) == 0x83 && byte_at(&v, k + 1) == 0xec); k++);
    if (k >= setsize + 8) return 0;
    frame = byte_at(&v, k + 2);
    pat[0] = (char)0xdd; pat[1] = 0x44; pat[2] = 0x24; pat[3] = (char)(frame + 0x14);
    if (!has(&v, setsize, setsize + 0x30, pat, 4)) return 0;

    /* 2. 印と 0 を比べる所がちょうど二つ */
    pat[0] = (char)0x80; pat[1] = 0x3d; memcpy(pat + 2, &flag, 4); pat[6] = 0;
    for (k = text_lo; k + 7 <= text_hi && ncmp < 3; k++)
        if (same(&v, k, pat, 7)) cmps[ncmp++] = k;
    if (ncmp != 2) return 0;

    for (i = 0; i < 2; i++) {
        int found = 0;
        if (!(f = func_start(&v, cmps[i], 0x20))) return 0;
        for (k = f; k < f + 0x40 && !found; k++) {
            int op = byte_at(&v, k), r;
            uint32_t s;
            if (op < 0xb8 || op > 0xbf || byte_at(&v, u32_at(&v, k + 1)) != 0xc3) continue;
            r = op - 0xb8;
            for (s = k + 5; s < k + 20; s++) {
                if (r == 0 && byte_at(&v, s) == 0xa3) { drawline = u32_at(&v, s + 1); found = 1; break; }
                if (byte_at(&v, s) == 0x89 && byte_at(&v, s + 1) == 0x05 + r * 8) { drawline = u32_at(&v, s + 2); found = 1; break; }
            }
        }
        if (found) { if (end) return 0; end = f; }
        else { if (start) return 0; start = f; }
    }
    if (!end || !start || !drawline) return 0;
    pat[0] = (char)0xc6; pat[1] = 0x05; memcpy(pat + 2, &flag, 4); pat[6] = 1;
    if (!has(&v, start, start + 0x200, pat, 7)) return 0;
    if (!stores_to(&v, start, start + 0x200, drawline)) return 0;

    /* パレット（試験版の撮影だけで使う）: 録画の関数の 8 番目の引数 [esp+0x1c] に渡す番地 */
    for (k = end; k < end + 0x140; k++) {
        int op = byte_at(&v, k);
        uint32_t x = u32_at(&v, k + 1), s;
        if (op < 0xb8 || op > 0xbf || x <= drawline || x >= drawline + 0x1000) continue;
        for (s = k + 5; s < k + 16; s++)
            if (byte_at(&v, s) == 0x89 && byte_at(&v, s + 1) == 0x44 + (op - 0xb8) * 8 && byte_at(&v, s + 2) == 0x24
                && byte_at(&v, s + 3) == 0x1c) pal = x;
    }

    out->setsize = setsize; out->start = start; out->end = end;
    out->drawline = drawline; out->flag = flag; out->palette = pal;
    out->n_setsize = prologue(&v, setsize);
    out->n_start = prologue(&v, start);
    out->n_end = prologue(&v, end);
    out->end_arg = reads_arg(&v, end, first_arg(&v, end));
    if (!at(&v, drawline, 4)) return 0;
    return out->n_setsize && out->n_start && out->n_end;
}
