/* 検査用: DOSBox の exe を読み込み先の形に並べて dbx_find に掛ける。配布物には入らない
   dbxcheck.exe DOSBox.exe … → 1 行ずつ「見つけた番地」か「見つからない」 */
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "dbxfind.h"

static int check(const char *path)
{
    FILE *fp = fopen(path, "rb");
    long n;
    uint8_t *d, *img;
    IMAGE_NT_HEADERS *nt;
    IMAGE_SECTION_HEADER *s;
    uint32_t size, lo = 0, hi = 0;
    dbx_t f;
    int i, ok;

    if (!fp) { printf("読めない\t%s\n", path); return 1; }
    fseek(fp, 0, SEEK_END); n = ftell(fp); fseek(fp, 0, SEEK_SET);
    d = (uint8_t *)malloc(n);
    fread(d, 1, n, fp); fclose(fp);
    nt = (IMAGE_NT_HEADERS *)(d + ((IMAGE_DOS_HEADER *)d)->e_lfanew);
    if (nt->FileHeader.Machine != IMAGE_FILE_MACHINE_I386) { printf("32 ビットでない\t%s\n", path); free(d); return 1; }
    size = nt->OptionalHeader.SizeOfImage;
    img = (uint8_t *)calloc(size, 1);
    memcpy(img, d, nt->OptionalHeader.SizeOfHeaders);
    s = IMAGE_FIRST_SECTION(nt);
    for (i = 0; i < nt->FileHeader.NumberOfSections; i++, s++) {
        uint32_t m = s->SizeOfRawData < s->Misc.VirtualSize ? s->SizeOfRawData : s->Misc.VirtualSize;
        if (s->VirtualAddress + m <= size && s->PointerToRawData + m <= (uint32_t)n)
            memcpy(img + s->VirtualAddress, d + s->PointerToRawData, m);
        if (!lo && (s->Characteristics & IMAGE_SCN_MEM_EXECUTE)) {
            lo = nt->OptionalHeader.ImageBase + s->VirtualAddress;
            hi = lo + s->Misc.VirtualSize;
        }
    }
    ok = dbx_find(img, nt->OptionalHeader.ImageBase, size, lo, hi, &f);
    if (ok)
        printf("setsize=%x start=%x end=%x drawline=%x flag=%x pal=%x 頭=%d,%d,%d 中断=%d\t%s\n",
               f.setsize, f.start, f.end, f.drawline, f.flag, f.palette, f.n_setsize, f.n_start, f.n_end, f.end_arg, path);
    else
        printf("見つからない\t%s\n", path);
    free(img); free(d);
    return !ok;
}

int main(int argc, char **argv)
{
    int i, bad = 0;
    for (i = 1; i < argc; i++) bad += check(argv[i]);
    return bad;
}
