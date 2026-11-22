#pragma once
#include <stdint.h>

/* 見つけた番地（読み込み先 base での値）。n_* は書き換える頭の命令の長さ */
typedef struct {
    uint32_t setsize, start, end, drawline, flag, palette;
    int n_setsize, n_start, n_end;
    int end_arg;    /* EndUpdate が引数（中断）を読む版なら 1。0.72 は引数なし */
} dbx_t;

/* img は base に読み込んだ形の像。見つかれば 1 */
int dbx_find(const uint8_t *img, uint32_t base, uint32_t size, uint32_t text_lo, uint32_t text_hi, dbx_t *out);
