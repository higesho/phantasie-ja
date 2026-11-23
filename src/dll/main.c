/* ファンタジー三部作 日本語化 DLL の入口 */
#include "phjp.h"
#include <string.h>

int g_game;
static HMODULE g_self;
static char g_dir[MAX_PATH]; /* exe の場所。末尾は区切り */

const uint8_t *res_data(int id)
{
    HRSRC r = FindResourceA(g_self, MAKEINTRESOURCEA(id), MAKEINTRESOURCEA(10)); /* RT_RCDATA */
    HGLOBAL h = r ? LoadResource(g_self, r) : NULL;
    return h ? (const uint8_t *)LockResource(h) : NULL;
}

void *patch_iat(HMODULE mod, const char *dll, const char *name, void *fn)
{
    uint8_t *b = (uint8_t *)mod;
    IMAGE_NT_HEADERS *nt;
    IMAGE_IMPORT_DESCRIPTOR *im;
    void *old = NULL;
    DWORD rva, prot;

    if (!b) return NULL;
    nt = (IMAGE_NT_HEADERS *)(b + ((IMAGE_DOS_HEADER *)b)->e_lfanew);
    rva = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT].VirtualAddress;
    if (!rva) return NULL;
    for (im = (IMAGE_IMPORT_DESCRIPTOR *)(b + rva); im->Name; im++) {
        IMAGE_THUNK_DATA *o, *t;
        if (_stricmp((char *)(b + im->Name), dll) || !im->OriginalFirstThunk) continue;
        o = (IMAGE_THUNK_DATA *)(b + im->OriginalFirstThunk);
        t = (IMAGE_THUNK_DATA *)(b + im->FirstThunk);
        for (; o->u1.AddressOfData; o++, t++) {
            if (IMAGE_SNAP_BY_ORDINAL(o->u1.Ordinal)) continue;
            if (strcmp(((IMAGE_IMPORT_BY_NAME *)(b + o->u1.AddressOfData))->Name, name)) continue;
            VirtualProtect(&t->u1.Function, 4, PAGE_READWRITE, &prot);
            old = (void *)t->u1.Function;
            t->u1.Function = (DWORD)fn;
            VirtualProtect(&t->u1.Function, 4, prot, &prot);
        }
    }
    return old;
}

static int exists(const char *dir, const char *rel)
{
    char p[MAX_PATH * 2];
    lstrcpyA(p, dir);
    lstrcatA(p, rel);
    return GetFileAttributesA(p) != INVALID_FILE_ATTRIBUTES;
}

static int find_game(void)
{
    char *d = g_dir, *s, cmd[2048];
    int p1, p3;
    GetModuleFileNameA(NULL, d, MAX_PATH);
    s = strrchr(d, '\\');
    if (!s) return GAME_NONE;
    if (!_stricmp(s + 1, "AppleWin.exe")) {
        s[1] = 0;
        return exists(d, "GAMES\\P2\\PHANT2A.DSK") ? GAME_PH2 : GAME_NONE;
    }
    if (_stricmp(s + 1, "DOSBox.exe")) return GAME_NONE;
    s[1] = 0;
    p1 = exists(d, "..\\GAME\\PH1\\PHANTASI.EXE");
    p3 = exists(d, "..\\GAME\\PH3\\MAIN.EXE");
    if (p1 && p3) {
        /* Steam 版は一つの DOSBox を両作で使う。起動の設定（phantasieN-*.conf）で見分ける */
        lstrcpynA(cmd, GetCommandLineA(), sizeof cmd);
        for (s = cmd; *s; s++) if (*s >= 'A' && *s <= 'Z') *s += 32;
        if (strstr(cmd, "phantasie3")) return GAME_PH3;
        if (strstr(cmd, "phantasie1")) return GAME_PH1;
        return GAME_NONE;
    }
    return p1 ? GAME_PH1 : p3 ? GAME_PH3 : GAME_NONE;
}

BOOL WINAPI DllMain(HINSTANCE inst, DWORD reason, LPVOID res)
{
    if (reason != DLL_PROCESS_ATTACH) return TRUE;
    DisableThreadLibraryCalls(inst);
    g_self = inst;
    proxy_init();
    g_game = find_game();
    if (g_game == GAME_NONE) return TRUE;
#ifdef PHJP_TEST
    test_init();
#endif
    if (!overlay_init(g_dir) || !text_init()) return TRUE;
    if (g_game == GAME_PH2) applewin_install();
    else dosbox_install();
    return TRUE;
}
