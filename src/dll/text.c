/* 画面の文字の行を辞書と照らし、描き替える場所と日本語を決める

   辞書（build.py が作る。英語は持たず、長さと照合値だけ）:
     "PHJ2" u16 規則の数、規則の並び
     規則: u16 点数 / u8 旗 / 鍵 / 前の行 / 次の行 / u8 区切りの数 /
           区切り { u8 位置 / u8 幅 / u8 左へ広げてよい升 / str 日本語 }
     鍵: u8 片の数、片の並び
     片: u8 0 / u8 長さ / u8 頭 / u64 照合値、または u8 印（1=数 2=名前など（訳さない） 3=用語（辞書で訳す） 4=0〜1 字）
     前の行・次の行: u8 長さ、長さが 0 でなければ u64 照合値
     str: u8 長さ + バイト列 + 0
     照合値は小文字に揃えた FNV-1a。空白の連続は 1 個に数える（F_EXACT を除く）

   日本語の中の印: 0x10+k = k 番目の印に当たった文字、0x0e = 説明書照合の答え */
#include "phjp.h"
#include <string.h>

#define F_EXACT   1 /* 空白の数も一致 */
#define F_ISOL    2 /* 前後が区切られているときだけ */
#define F_TERM    4 /* {t} で引ける用語。後ろに +数・ローマ数字が付いてよい */
#define F_NEEDCTX 8 /* 前か次の行が合うときだけ */

#define P_NUM  1
#define P_NAME 2
#define P_TERM 3
#define P_OPT  4
#define P_LIT  5
#define P_END  0
#define MAXCAP 8
#define MAXSEG 12
#define MAXRULES 12000
#define MAXCANDS 768
#define MAXTOKS (MAXRULES * 4)
#define FNV_BASE 14695981039346656037ull
#define FNV_PRIME 1099511628211ull
#define M_ANSWER 0x0e
#define MAXANSWER 255

typedef struct {
    uint8_t kind, n, head;
    uint64_t hash;
} tok_t;

typedef struct {
    uint8_t at, w, left;
    const uint8_t *ja;
} seg_t;

typedef struct {
    int score, flags, nseg, leads;
    const tok_t *key;
    int nprev, nnext;
    uint64_t prev, next;
    const seg_t *seg;
    int link;
} rule_t;

typedef struct {
    int rule, score, a, b, tail;
    struct { int a, b; } cap[MAXCAP];
} cand_t;

static rule_t g_rules[MAXRULES];
static seg_t g_segs[MAXRULES * 2];
static tok_t g_toks[MAXTOKS];
static int g_nrules, g_first[256], g_any = -1;
static tok_t g_item[MAXANSWER], g_spell[MAXANSWER];
static int g_nitem, g_nspell;

static int low(int c) { return c >= 'A' && c <= 'Z' ? c + 32 : c; }
static int is_digit(int c) { return c >= '0' && c <= '9'; }
static int is_word(int c) { return is_digit(c) || (low(c) >= 'a' && low(c) <= 'z'); }
static int in_set(int c, const char *set) { return c && strchr(set, c) != NULL; }

static const uint8_t *take_str(const uint8_t **p)
{
    const uint8_t *s = *p + 1;
    *p = s + **p + 1;
    return s;
}

static uint64_t take64(const uint8_t **p)
{
    uint64_t v;
    memcpy(&v, *p, 8);
    *p += 8;
    return v;
}

/* 第1作の説明書照合の表: "PHA1" u8 品の数 u8 呪文の数、{ u8 長さ / u64 照合値 } */
static int answers_init(void)
{
    const uint8_t *p = res_data(RES_ANSWER);
    int i;

    if (!p || memcmp(p, "PHA1", 4)) return 0;
    g_nitem = p[4];
    g_nspell = p[5];
    p += 6;
    for (i = 0; i < g_nitem + g_nspell; i++) {
        tok_t *o = i < g_nitem ? &g_item[i] : &g_spell[i - g_nitem];
        o->n = *p++;
        o->hash = take64(&p);
    }
    return 1;
}

int text_init(void)
{
    const uint8_t *p = res_data(RES_DICT(g_game));
    int i, k, n, nseg = 0, ntok = 0, last[256], last_any = -1;

    if (!p || memcmp(p, "PHJ2", 4)) return 0;
    for (i = 0; i < 256; i++) g_first[i] = last[i] = -1;
    n = p[4] | p[5] << 8;
    p += 6;
    if (n > MAXRULES) return 0;
    for (i = 0; i < n; i++) {
        rule_t *r = &g_rules[i];
        int c, nt;
        r->score = p[0] | p[1] << 8;
        r->flags = p[2];
        p += 3;
        nt = *p++;
        if (!nt || ntok + nt >= MAXTOKS) return 0;
        r->key = &g_toks[ntok];
        while (nt--) {
            tok_t *o = &g_toks[ntok++];
            o->kind = *p++;
            if (o->kind) continue;
            o->kind = P_LIT;
            o->n = p[0]; o->head = p[1];
            p += 2;
            o->hash = take64(&p);
        }
        g_toks[ntok++].kind = P_END;
        if ((r->nprev = *p++) != 0) r->prev = take64(&p);
        if ((r->nnext = *p++) != 0) r->next = take64(&p);
        r->nseg = *p++;
        if (r->nseg > MAXSEG || nseg + r->nseg > MAXRULES * 2) return 0;
        r->seg = &g_segs[nseg];
        for (k = 0; k < r->nseg; k++) {
            seg_t *s = &g_segs[nseg++];
            s->at = p[0]; s->w = p[1]; s->left = p[2];
            p += 3;
            s->ja = take_str(&p);
        }
        r->link = -1;
        c = r->key[0].head;
        r->leads = r->key[0].kind != P_LIT;
        if (r->leads) {
            if (last_any < 0) g_any = i; else g_rules[last_any].link = i;
            last_any = i;
        } else {
            if (last[c] < 0) g_first[c] = i; else g_rules[last[c]].link = i;
            last[c] = i;
        }
    }
    g_nrules = n;
    if (g_game == GAME_PH1 && !answers_init()) g_nitem = g_nspell = 0;
    LOG("辞書 %d 件", n);
    return 1;
}

/* ---- 区切り ---- */

static int edge_left(const uint8_t *t, int i)
{
    if (i <= 0 || !t[i - 1]) return 1;
    if (t[i - 1] == ' ') return i == 1 || !t[i - 2] || t[i - 2] == ' ' || is_digit(t[i - 2]) || in_set(t[i - 2], ":-+*>)!|=/");
    return in_set(t[i - 1], ":-+*>)(!|=/\"'[");
}

static int edge_right(const uint8_t *t, int j)
{
    if (j >= COLS || !t[j]) return 1;
    if (j && t[j - 1] == ':') return 1; /* 札の後ろの値 */
    if (t[j] == ' ') return j + 1 >= COLS || !t[j + 1] || t[j + 1] == ' ' || is_digit(t[j + 1]) || in_set(t[j + 1], ":-+*<(!|=/$");
    return in_set(t[j], ":?!.,)(*-+/<|=\"']");
}

/* ---- 照合値 ---- */

/* 各升から始まる 1〜n 字の照合値と、その終わりの升。行と空白の数え方ごとに一度だけ求める */
typedef struct {
    uint32_t stamp;
    uint8_t  max;
    uint8_t  end[COLS + 1];
    uint64_t h[COLS + 1];
} span_t;

static uint8_t g_text[MAXROWS][COLS + 1];
static span_t g_span[MAXROWS][2][COLS];
static uint32_t g_stamp;
static int g_nrows;

static const span_t *spans(const uint8_t *t, int i, int exact)
{
    span_t *s = &g_span[(t - g_text[0]) / (COLS + 1)][exact][i];
    uint64_t h = FNV_BASE;
    int j = i, n = 0;

    if (s->stamp == g_stamp) return s;
    while (j < COLS) {
        int c = t[j++];
        if (c == ' ' && !exact)
            while (j < COLS && t[j] == ' ') j++;
        h = (h ^ (uint64_t)low(c)) * FNV_PRIME;
        n++;
        s->h[n] = h;
        s->end[n] = (uint8_t)j;
    }
    s->max = (uint8_t)n;
    s->stamp = g_stamp;
    return s;
}

/* i 升から n 字の照合値が合えば終わりの升、違えば -1 */
static int lit(const uint8_t *t, int i, int exact, int n, uint64_t hash)
{
    const span_t *s;
    if (i >= COLS || !n) return -1;
    s = spans(t, i, exact);
    return n <= s->max && s->h[n] == hash ? s->end[n] : -1;
}

/* ---- 一致 ---- */

static int starts_space(const tok_t *p) { return p->kind == P_LIT && p->head == ' '; }

static int run(const rule_t *r, const tok_t *p, const uint8_t *t, int i, cand_t *c, int ncap)
{
    int j, e;

    while (p->kind == P_LIT) {
        i = lit(t, i, r->flags & F_EXACT ? 1 : 0, p->n, p->hash);
        if (i < 0) return -1;
        p++;
    }
    if (p->kind == P_END) return i;
    if (ncap >= MAXCAP) return -1;

    switch (p->kind) {
    case P_NUM:
        while (i < COLS && t[i] == ' ') i++;
        c->cap[ncap].a = i;
        j = i;
        if (j < COLS && (t[j] == '-' || t[j] == '+')) j++;
        if (j >= COLS || !is_digit(t[j])) return -1;
        while (j < COLS && (is_digit(t[j]) || (t[j] == ',' && j + 1 < COLS && is_digit(t[j + 1])))) j++;
        c->cap[ncap].b = j;
        e = run(r, p + 1, t, j, c, ncap + 1);
        if (e < 0 && p[1].kind && !starts_space(p + 1) && j < COLS && t[j] == ' ') { /* 左詰めの詰め物 */
            while (j < COLS && t[j] == ' ') j++;
            e = run(r, p + 1, t, j, c, ncap + 1);
        }
        return e;
    case P_OPT:
        for (j = i + (i < COLS && is_word(t[i])); j >= i; j--) {
            c->cap[ncap].a = i; c->cap[ncap].b = j;
            e = run(r, p + 1, t, j, c, ncap + 1);
            if (e >= 0) return e;
        }
        return -1;
    default:
        if (i >= COLS || !t[i] || t[i] == ' ') return -1;
        c->cap[ncap].a = i;
        if (p[1].kind == P_END) { /* 末尾の印は区切りまで全部 */
            for (j = i + 1; j < COLS && t[j] && j - i < 24; j++) {
                if (t[j] == ' ' && (j + 1 >= COLS || !t[j + 1] || t[j + 1] == ' ')) break;
                if (in_set(t[j], ":?!,|")) break;
            }
            c->cap[ncap].b = j;
            return j;
        }
        for (j = i + 1; j <= COLS && j - i <= 24 && t[j - 1]; j++) {
            if (t[j - 1] == ' ') continue;
            c->cap[ncap].b = j;
            e = run(r, p + 1, t, j, c, ncap + 1);
            if (e < 0 && !starts_space(p + 1) && j < COLS && t[j] == ' ') {
                int k = j;
                while (k < COLS && t[k] == ' ') k++;
                e = run(r, p + 1, t, k, c, ncap + 1);
            }
            if (e >= 0) return e;
        }
        return -1;
    }
}

/* 用語の後ろに付く +数・番号・ローマ数字 */
static int suffix(const uint8_t *t, int i, int end)
{
    int j = i, k;
    if (i < end && i > 0 && is_digit(t[i]) && is_digit(t[i - 1])) return i; /* 数の途中で切らない */
    if (j < end && t[j] == ' ') j++;
    k = j;
    if (j < end && t[j] == '+') j++;
    if (j < end && is_digit(t[j])) {
        while (j < end && is_digit(t[j])) j++;
        if (j >= end || !is_word(t[j])) return j;
        return i;
    }
    if (k > i) {
        j = k;
        while (j < end && j - k < 5 && in_set(t[j], "IVX")) j++;
        if (j > k && (j >= end || !is_word(t[j]))) return j;
    }
    return i;
}

/* {t} に当たった語の訳。無ければ NULL */
static const uint8_t *find_term(const uint8_t *t, int a, int b, int *tail)
{
    int i, n;
    for (i = g_first[low(t[a])]; i >= 0; i = g_rules[i].link) {
        const rule_t *r = &g_rules[i];
        if (!(r->flags & F_TERM) || r->nseg != 1) continue;
        if (r->key[1].kind != P_END) continue;
        n = r->key[0].n;
        if (a + n > b || lit(t, a, 1, n, r->key[0].hash) < 0) continue;
        if (a + n < b && suffix(t, a + n, b) != b) continue;
        *tail = a + n;
        return r->seg[0].ja;
    }
    return NULL;
}

static int has_text(const uint8_t *hay, int n, uint64_t hash)
{
    int i;
    if (!hay || !n) return 0;
    for (i = 0; i < COLS; i++)
        if (lit(hay, i, 0, n, hash) >= 0) return 1;
    return 0;
}

static int units(const uint8_t *s);

static int try_rule(int idx, const uint8_t *t, int i, const uint8_t *above, const uint8_t *below, cand_t *c)
{
    const rule_t *r = &g_rules[idx];
    int j, k, e, near_ = 0, named = 0;
    const tok_t *o;

    if (r->leads && !edge_left(t, i)) return 0;
    j = run(r, r->key, t, i, c, 0);
    if (j <= i) return 0;
    c->tail = j;
    /* 「MAGIC 5」の用語が、詰め物の空白と値の 5 に当たるのを避ける */
    if ((r->flags & F_TERM) && is_digit(t[j - 1]) && j - i > r->key[0].n) return 0;
    if (r->flags & F_TERM) {
        e = suffix(t, j, COLS);
        /* 空白の後ろのただの数は値。元の場所に残す。訳が札に入らないときだけ一緒に描く */
        if (e > j && (!(r->flags & F_ISOL) || edge_right(t, e))
            && (!(t[j] == ' ' && is_digit(t[j + 1]))
                || (r->nseg == 1 && (units(r->seg[0].ja) + 1) / 2 > j - i))) j = e;
    }
    if (r->flags & F_ISOL) {
        if (!edge_left(t, i) || !edge_right(t, j)) return 0;
    } else if (!(r->flags & F_EXACT)) {
        if (i > 0 && is_word(t[i - 1]) && is_word(t[i])) return 0;
        if (j < COLS && is_word(t[j]) && is_word(t[j - 1])) return 0;
    }
    near_ += has_text(above, r->nprev, r->prev);
    near_ += has_text(below, r->nnext, r->next);
    if ((r->flags & F_NEEDCTX) && !near_) return 0;
    c->rule = idx; c->a = i; c->b = j;
    /* {t} に当たった用語も、その字数ぶん点に入れる（中の用語だけの規則に負けない） */
    for (o = r->key, k = 0; o->kind; o++) {
        if (o->kind == P_LIT) continue;
        if (o->kind == P_TERM && find_term(t, c->cap[k].a, c->cap[k].b, &e)) named += c->cap[k].b - c->cap[k].a;
        k++;
    }
    c->score = (r->score + named * 4) * 4 + near_;
    return 1;
}

/* ---- 説明書照合の答え（第1作） ---- */

static int number(const uint8_t *t, int a, int b)
{
    int v = 0;
    if (a >= b) return -1;
    for (; a < b; a++) {
        if (!is_digit(t[a]) || v > 999) return -1;
        v = v * 10 + t[a] - '0';
    }
    return v;
}

static int line_ends(const uint8_t *t, int i)
{
    while (i < COLS && t[i] == ' ') i++;
    return i >= COLS || !t[i];
}

/* 問いの印に当たった語から正解を引き、下に並ぶ「n. 選択肢」の n を返す。見つからなければ 0
   品名の問いの選択肢は品番、呪文の番号の問いの選択肢は呪文名 */
static int answer_key(const uint8_t *t, int kind, int a, int b)
{
    int ri = (int)((t - g_text[0]) / (COLS + 1)), item = 0, spell = 0, i, r;

    if (kind == P_NUM) {
        spell = number(t, a, b);
        if (spell < 1 || spell > g_nspell) return 0;
    } else {
        for (i = 0; i < g_nitem && !item; i++)
            if (lit(t, a, 0, g_item[i].n, g_item[i].hash) == b) item = i + 1;
        if (!item) return 0;
    }
    for (r = ri + 1; r < g_nrows; r++) {
        const uint8_t *s = g_text[r];
        int j, e;
        for (i = 0; i < COLS && s[i] == ' '; i++);
        if (i + 3 >= COLS || s[i] < '1' || s[i] > '3' || s[i + 1] != '.' || s[i + 2] != ' ') continue;
        j = i + 3;
        if (item) {
            for (e = j; e < COLS && is_digit(s[e]); e++);
            if (number(s, j, e) != item) continue;
        } else {
            e = lit(s, j, 0, g_spell[spell - 1].n, g_spell[spell - 1].hash);
            if (e < 0) continue;
        }
        if (line_ends(s, e)) return s[i] - '0';
    }
    return 0;
}

/* ---- 描き替えの決定 ---- */

static int put_bytes(uint8_t *out, int n, const uint8_t *s, int len)
{
    while (len-- > 0 && n < MAXTEXT - 1) out[n++] = *s++;
    return n;
}

static void expand(const rule_t *r, const cand_t *c, const uint8_t *t, const uint8_t *ja, uint8_t *out)
{
    uint8_t kind[MAXCAP];
    const uint8_t *p;
    const tok_t *o;
    int nk = 0, n = 0;

    for (o = r->key; o->kind; o++)
        if (o->kind <= P_OPT && nk < MAXCAP) kind[nk++] = o->kind;
    for (p = ja; *p; p++) {
        int k = *p - 0x10;
        if (*p == M_ANSWER) {
            /* EUC-JP の「（答え：n番）」。選択肢がまだ出ていなければ何も足さない */
            uint8_t s[] = "\xa1\xca\xc5\xfa\xa4\xa8\xa1\xa7" "0" "\xc8\xd6\xa1\xcb";
            int key = nk ? answer_key(t, kind[0], c->cap[0].a, c->cap[0].b) : 0;
            if (key) {
                s[8] = (uint8_t)('0' + key);
                n = put_bytes(out, n, s, sizeof s - 1);
            }
            continue;
        }
        if (*p < 0x10 || *p >= 0x10 + MAXCAP) {
            n = put_bytes(out, n, p, 1);
        } else if (k < nk) {
            int a = c->cap[k].a, b = c->cap[k].b, tail;
            const uint8_t *w = kind[k] == P_TERM ? find_term(t, a, b, &tail) : NULL;
            if (w) {
                n = put_bytes(out, n, w, (int)strlen((const char *)w));
                while (tail < b && t[tail] == ' ') tail++;
                n = put_bytes(out, n, t + tail, b - tail);
            } else {
                n = put_bytes(out, n, t + a, b - a);
            }
        }
    }
    if ((r->flags & F_TERM) && c->b > c->tail) {
        int a = c->tail;
        while (t[a] == ' ') a++;
        n = put_bytes(out, n, t + a, c->b - a);
    }
    out[n] = 0;
}

static int units(const uint8_t *s)
{
    int n = 0;
    while (*s) {
        if (*s >= 0x80 && s[1]) { n += 2; s += 2; }
        else { n++; s++; }
    }
    return n;
}

#ifdef PHJP_TEST
static void log_rest(const row_t *row, const uint8_t *done)
{
    static uint32_t seen[2048];
    static int nseen;
    char s[COLS + 1];
    uint32_t h = 5381;
    int i, letters = 0, best = 0;

    for (i = 0; i < COLS; i++) {
        int c = row->c[i].ch;
        s[i] = done[i] ? '_' : c ? (char)c : '~';
        letters = (!done[i] && c && is_word(c) && !is_digit(c)) ? letters + 1 : 0;
        if (letters > best) best = letters;
        h = h * 33 + (uint8_t)s[i];
    }
    s[COLS] = 0;
    if (best < 2) return;
    for (i = 0; i < nseen; i++) if (seen[i] == h) return;
    if (nseen < 2048) seen[nseen++] = h;
    LOG("未訳 %3d |%s|", row->y, s);
}
#endif

static int one_row(const row_t *rows, int ri, const uint8_t (*text)[COLS + 1], int nrows, op_t *ops, int nops)
{
    static cand_t cands[MAXCANDS];
    const uint8_t *t = text[ri];
    const cell_t *cell = rows[ri].c;
    const uint8_t *above = ri ? text[ri - 1] : NULL, *below = ri + 1 < nrows ? text[ri + 1] : NULL;
    uint8_t taken[COLS] = {0}, used[COLS] = {0}, done[COLS] = {0};
    int picked[COLS], npicked = 0, nc = 0, i, k, q;

    for (i = 0; i < COLS; i++) {
        if (!t[i] || t[i] == ' ') continue;
        for (q = 0; q < 2; q++)
            for (k = q ? g_any : g_first[low(t[i])]; k >= 0; k = g_rules[k].link)
                if (nc < MAXCANDS && try_rule(k, t, i, above, below, &cands[nc])) nc++;
    }
    /* 点の高いものから、重ならないように取る */
    for (;;) {
        int best = -1;
        for (i = 0; i < nc; i++) {
            const cand_t *c = &cands[i];
            if (c->score < 0 || (best >= 0 && c->score <= cands[best].score)) continue;
            best = i;
        }
        if (best < 0 || npicked >= COLS) break;
        for (k = cands[best].a; k < cands[best].b && !taken[k]; k++);
        if (k == cands[best].b) {
            for (k = cands[best].a; k < cands[best].b; k++) taken[k] = 1;
            picked[npicked++] = best;
        }
        cands[best].score = -1;
    }
    for (q = 0; q < npicked; q++) {
        const cand_t *c = &cands[picked[q]];
        const rule_t *r = &g_rules[c->rule];
        for (k = c->a; k < c->b; k++) done[k] = 1;
        for (i = 0; i < r->nseg; i++) {
            const seg_t *s = &r->seg[i];
            op_t *op = &ops[nops];
            int a = c->a, b = c->b, g, x, need;

            if (nops >= MAXOPS) return nops;
            if (r->flags & F_EXACT) {
                a = c->a + s->at;
                b = a + s->w;
                if (b > COLS) b = COLS;
            }
            for (g = a; g < b && (!cell[g].ch || cell[g].ch == ' '); g++);
            if (g >= b) continue;
            op->bg = cell[g].bg;
            op->fg = cell[g].fg;
            expand(r, c, t, s->ja, op->text);
            need = (units(op->text) + 1) / 2;

            if (s->left && a >= s->left) {
                for (x = a - s->left; x < a; x++)
                    if (cell[x].ch != ' ' || cell[x].bg != op->bg || used[x] || taken[x]) break;
                if (x == a) a -= s->left;
            }
            while (b - a < need && b < COLS && cell[b].ch == ' ' && cell[b].bg == op->bg && !used[b] && !taken[b]) b++;
            for (x = a; x < b && !used[x]; x++);
            if (b - a < need || x < b) {
                LOG("入らない %3d |%.40s| %d 升に %d 升", rows[ri].y, t, b - a, need);
                for (k = c->a; k < c->b; k++) done[k] = 0;
                continue;
            }
            for (x = a; x < b; x++) used[x] = 1;
            op->row = ri; op->a = a; op->b = b;
            nops++;
        }
    }
#ifdef PHJP_TEST
    log_rest(&rows[ri], done);
#endif
    return nops;
}

int text_translate(const row_t *rows, int nrows, op_t *ops)
{
    int i, k, nops = 0;

    if (!g_nrules) return 0;
    g_stamp++;
    g_nrows = nrows;
    for (i = 0; i < nrows; i++) {
        for (k = 0; k < COLS; k++) g_text[i][k] = rows[i].c[k].ch;
        g_text[i][COLS] = 0;
    }
    for (i = 0; i < nrows; i++)
        nops = one_row(rows, i, (const uint8_t (*)[COLS + 1])g_text, nrows, ops, nops);
    return nops;
}
