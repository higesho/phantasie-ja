#pragma once
#include <windows.h>
#include <stdint.h>

enum { GAME_NONE, GAME_PH1, GAME_PH2, GAME_PH3 };
extern int g_game;

/* 組み込んだ資料の番号（build.py が作る） */
#define RES_DICT(game) (10 + (game))
#define RES_KANJI16 31
#define RES_ASCII16 32
#define RES_KANJI14 33
#define RES_ASCII14 34
#define RES_ANSWER 35 /* 第1作の説明書照合の答え */

#define COLS 40
#define MAXROWS 96
#define MAXOPS 160
#define MAXTEXT 256

/* 画面の文字の升。ch=0 は文字として読めない升 */
typedef struct {
    uint8_t  ch;
    uint32_t bg, fg;
} cell_t;

typedef struct {
    int    y;
    cell_t c[COLS];
} row_t;

/* 描き替え: row 行の a〜b 升を消し、a 升から text（EUC-JP）を書く */
typedef struct {
    int      row, a, b;
    uint32_t bg, fg;
    uint8_t  text[MAXTEXT];
} op_t;

void proxy_init(void);
int  dosbox_install(void);
int  applewin_install(void);

const uint8_t *res_data(int id);
int  text_init(void);
int  text_translate(const row_t *rows, int nrows, op_t *ops);
int  overlay_init(const char *exedir);
int  glyphs_load(int game, const char *exedir, uint8_t out[2048]);

/* 点は 320x200 の色番号 → 640x400 の色番号 */
void overlay_cga(const uint8_t *src, uint8_t *dst);
/* 東雲で 1 行分を描く。put は点を打つ関数 */
typedef void (*put_fn)(void *ctx, int x, int y, uint32_t colour);
void overlay_op(const op_t *op, int cw, int ch, int top, put_fn put, void *ctx);

void *patch_iat(HMODULE mod, const char *dll, const char *name, void *fn);

#ifdef PHJP_TEST
void test_init(void);
void test_frame8(const uint8_t *src, const uint8_t *dst, const uint8_t *pal);
void test_frame32(const void *bits, int w, int h, int bpp);
void test_log(const char *fmt, ...);
#define LOG(...) test_log(__VA_ARGS__)
#else
#define LOG(...) ((void)0)
#endif
