# ファンタジー三部作 日本語化 ソース

GOG 版 Phantasie Memorial Set 用の `winmm.dll` と、それを組み立てる道具です。

- `dll/` … winmm.dll の本体。`build.bat`（Visual Studio の x86 の cl と rc）で `dll/out/winmm.dll` ができます
- `dll/cov.bat` … 検査用の coverage.exe・glyphcheck.exe を作る
- `tools/build.py` … 訳とフォントから DLL に入れるデータを作り、`build.bat` を呼ぶ
- `tools/answers.py` … 説明書照合の答えの表をゲームのファイルから作る
- `tools/check_batch.py` … 訳を検査する
- `tools/check_clean.py` … DLL に原作の文や字形が入っていないか検査する
- `tools/ph1mess.py`・`tools/ph2basic.py`・`tools/ph2disk.py`・`tools/ph2text.py`・`tools/unexepack.py` … ゲームのファイルを読む
- `tools/make_dist.py` … 配布用の zip を作る

必要なもの: Python 3、Visual Studio。
ゲーム本体（GOG 版）、訳のファイル（`translation/`）、東雲フォント（BDF）は各自で用意してください。
