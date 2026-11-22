/* 第1作・第3作の字形は、利用者のゲームのファイルから読む */
#include "phjp.h"
#include <stdlib.h>
#include <string.h>

#define PH3_GLYPHS 0x28ccc /* 展開した MAIN.EXE の実体の中の位置 */
#define MAXFILE (4 * 1024 * 1024)

static uint8_t *read_file(const char *dir, const char *rel, DWORD *size)
{
    char path[MAX_PATH * 2];
    HANDLE h;
    DWORD n, got;
    uint8_t *b = NULL;

    lstrcpyA(path, dir);
    lstrcatA(path, rel);
    h = CreateFileA(path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, NULL);
    if (h == INVALID_HANDLE_VALUE) return NULL;
    n = GetFileSize(h, NULL);
    if (n && n <= MAXFILE && (b = (uint8_t *)malloc(n)) != NULL) {
        if (!ReadFile(h, b, n, &got, NULL) || got != n) {
            free(b);
            b = NULL;
        }
    }
    CloseHandle(h);
    *size = n;
    return b;
}

static unsigned u16(const uint8_t *p) { return p[0] | p[1] << 8; }

/* 第1作: 16 バイト/字、2 ビット/点 */
static int from_font(const uint8_t *d, DWORD size, uint8_t *out)
{
    int c, y, x;

    if (size < 127 * 16) return 0;
    for (c = 33; c < 127; c++) {
        for (y = 0; y < 8; y++) {
            unsigned v = d[c * 16 + y * 2] << 8 | d[c * 16 + y * 2 + 1];
            for (x = 0; x < 8; x++)
                if (v >> (14 - 2 * x) & 3) out[c * 8 + y] |= 0x80 >> x;
        }
    }
    return 1;
}

/* EXEPACK を外した実体 */
static uint8_t *unpack(const uint8_t *d, DWORD size, DWORD *outsize)
{
    const uint8_t *h;
    uint8_t *b;
    DWORD head, eh, total, s, t, n, sig;

    if (size < 0x1c || d[0] != 'M' || d[1] != 'Z') return NULL;
    head = u16(d + 8) * 16;
    eh = u16(d + 0x16) * 16;
    if (!eh || head + eh + 18 > size) return NULL;
    h = d + head + eh;
    if (h[16] == 'R' && h[17] == 'B') sig = 16;
    else if (h[14] == 'R' && h[15] == 'B') sig = 14;
    else return NULL;
    total = u16(h + 12) * 16;
    if (total < eh || total > MAXFILE) return NULL;
    if (!(b = (uint8_t *)calloc(total + 1, 1))) return NULL;
    memcpy(b, d + head, eh);

    s = eh;
    t = total;
    if (sig == 16 && u16(h + 14) > 1) {
        n = (u16(h + 14) - 1) * 16;
        if (n >= s) goto bad;
        s -= n;
        t -= n;
    }
    while (s && b[s - 1] == 0xff) s--;
    for (;;) {
        uint8_t c;
        if (s < 3) goto bad;
        c = b[--s];
        n = b[s - 1] << 8 | b[s - 2];
        s -= 2;
        if (n > t) goto bad;
        if ((c & 0xfe) == 0xb0) {
            uint8_t v;
            if (!s) goto bad;
            v = b[--s];
            t -= n;
            memset(b + t, v, n);
        } else if ((c & 0xfe) == 0xb2) {
            if (n > s) goto bad;
            s -= n;
            t -= n;
            memmove(b + t, b + s, n);
        } else {
            goto bad;
        }
        if (c & 1) break;
    }
    *outsize = total;
    return b;
bad:
    free(b);
    return NULL;
}

int glyphs_load(int game, const char *exedir, uint8_t out[2048])
{
    DWORD size, total;
    uint8_t *d, *b;
    int ok = 0;

    memset(out, 0, 2048);
    d = read_file(exedir, game == GAME_PH1 ? "..\\GAME\\PH1\\FONT" : "..\\GAME\\PH3\\MAIN.EXE", &size);
    if (!d) return 0;
    if (game == GAME_PH1) {
        ok = from_font(d, size, out);
    } else if ((b = unpack(d, size, &total)) != NULL) {
        if (total >= PH3_GLYPHS + 127 * 8) {
            memcpy(out + 33 * 8, b + PH3_GLYPHS + 33 * 8, 94 * 8);
            ok = 1;
        }
        free(b);
    }
    free(d);
    return ok;
}
