/* 検査用: 照合の本体に画面の行を流して、描き替えの結果を出す。配布物には入らない

   入力: "#id" の行、続けて画面の行（40 桁）
   出力: "#id" の行、"> 行 左 右 訳" と "= 描き替え後に残った英語の行" */
#include "phjp.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int g_game;
static uint8_t *g_dict;
static const char *g_gen;

const uint8_t *res_data(int id)
{
    char p[MAX_PATH];
    FILE *f;
    long n;
    sprintf(p, "%s\\r%d.bin", g_gen, id);
    if (!(f = fopen(p, "rb"))) return NULL;
    fseek(f, 0, SEEK_END);
    n = ftell(f);
    fseek(f, 0, SEEK_SET);
    g_dict = (uint8_t *)malloc(n + 1);
    fread(g_dict, 1, n, f);
    fclose(f);
    return g_dict;
}

static void flush(const char *id, row_t *rows, int n)
{
    static op_t ops[MAXOPS];
    int nops, i, x;
    if (!*id) return;
    nops = text_translate(rows, n, ops);
    printf("#%s\n", id);
    for (i = 0; i < nops; i++) printf("> %d %d %d %s\n", ops[i].row, ops[i].a, ops[i].b, (char *)ops[i].text);
    for (i = 0; i < n; i++) {
        char s[COLS + 1];
        for (x = 0; x < COLS; x++) s[x] = rows[i].c[x].ch ? (char)rows[i].c[x].ch : '~';
        s[COLS] = 0;
        for (x = 0; x < nops; x++)
            if (ops[x].row == i) memset(s + ops[x].a, ' ', ops[x].b - ops[x].a);
        printf("= %s\n", s);
    }
}

int main(int argc, char **argv)
{
    static row_t rows[MAXROWS];
    char buf[512], id[64] = "";
    int n = 0, x;

    if (argc < 3) return 2;
    g_game = atoi(argv[1]);
    g_gen = argv[2];
    if (!text_init()) return 1;
    while (fgets(buf, sizeof buf, stdin)) {
        buf[strcspn(buf, "\r\n")] = 0;
        if (buf[0] == '#') {
            flush(id, rows, n);
            strncpy(id, buf + 1, sizeof id - 1);
            n = 0;
        } else if (n < MAXROWS) {
            int len = (int)strlen(buf);
            memset(&rows[n], 0, sizeof rows[n]);
            for (x = 0; x < COLS; x++) {
                rows[n].c[x].ch = x < len ? (uint8_t)buf[x] : ' ';
                rows[n].c[x].fg = 1;
            }
            n++;
        }
    }
    flush(id, rows, n);
    return 0;
}
