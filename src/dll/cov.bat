@echo off
rem coverage.exe, glyphcheck.exe, dbxcheck.exe (check tools, not shipped)
call "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars32.bat" >nul
cd /d "%~dp0"
if not exist out_test\cov mkdir out_test\cov
cl /nologo /O2 /MT /W3 /utf-8 /D_CRT_SECURE_NO_WARNINGS coverage.c text.c /Fo:out_test\cov\ /Fe:out_test\coverage.exe || exit /b 1
cl /nologo /O2 /MT /W3 /utf-8 /D_CRT_SECURE_NO_WARNINGS glyphcheck.c glyphs.c /Fo:out_test\cov\ /Fe:out_test\glyphcheck.exe
cl /nologo /O2 /MT /W3 /utf-8 /D_CRT_SECURE_NO_WARNINGS dbxcheck.c dbxfind.c /Fo:out_test\cov\ /Fe:out_test\dbxcheck.exe
