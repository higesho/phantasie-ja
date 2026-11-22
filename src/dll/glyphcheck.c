/* 検査用: glyphs_load の結果をそのまま出す。配布物には入らない */
#include "phjp.h"
#include <fcntl.h>
#include <io.h>
#include <stdio.h>
#include <stdlib.h>

int main(int argc, char **argv)
{
    uint8_t g[2048];
    if (argc < 3) return 2;
    if (!glyphs_load(atoi(argv[1]), argv[2], g)) return 1;
    _setmode(_fileno(stdout), _O_BINARY);
    fwrite(g, 1, sizeof g, stdout);
    return 0;
}
