@echo off
rem build.bat        : release (out\winmm.dll)
rem build.bat test   : test build (out_test\winmm.dll)
call "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars32.bat" >nul
cd /d "%~dp0"
set OUT=out
set EXTRA=
set SRC=main.c proxy.c dosbox.c dbxfind.c applewin.c overlay.c text.c glyphs.c
if "%1"=="test" set OUT=out_test
if "%1"=="test" set EXTRA=/DPHJP_TEST
if "%1"=="test" set SRC=%SRC% testctl.c
if not exist %OUT% mkdir %OUT%
rc /nologo /fo %OUT%\phjp.res gen\phjp.rc || exit /b 1
cl /nologo /O2 /MT /W3 /utf-8 /D_CRT_SECURE_NO_WARNINGS %EXTRA% /LD %SRC% /Fo:%OUT%\ /Fe:%OUT%\winmm.dll /link %OUT%\phjp.res /DEF:winmm.def kernel32.lib user32.lib gdi32.lib
