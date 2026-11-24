# ファンタジー三部作 日本語化プロジェクト

GOG.com 版・Steam 版「Phantasie Memorial Set」に収録されたファンタジー三部作を日本語で遊べるようにする、非公式のファンメイド日本語化プロジェクトです。
開発元・販売元とは関係ありません。

- サイト（作品の紹介・入れ方）: https://higesho.github.io/phantasie-ja/
- ダウンロード: [Phantasie_JP.zip](https://github.com/higesho/phantasie-ja/releases/tag/v1.4)
- 日本語化プロジェクトの一覧: https://higesho.github.io/ja/

## 作品一覧

| 作品 | 版 | 状態 |
|---|---|---|
| [ファンタジー](https://higesho.github.io/phantasie-ja/phantasie1/)（Phantasie） | DOS 版 | 公開中 |
| [ファンタジーII](https://higesho.github.io/phantasie-ja/phantasie2/)（Phantasie II） | Apple II 版 | 公開中 |
| [ファンタジーIII](https://higesho.github.io/phantasie-ja/phantasie3/)（Phantasie III: The Wrath of Nikademus） | DOS 版 | 公開中 |

## 入れ方（GOG.com 版）

1. `Phantasie_JP.zip` の「GOG版」フォルダの中身を、ゲームのフォルダ（`Phantasie 1`・`Phantasie 2`・`Phantasie 3` がある場所）に写します。
2. 次の三つのファイルが増えれば完了です。
   - `Phantasie 1\DOSBOX\winmm.dll`
   - `Phantasie 2\winmm.dll`
   - `Phantasie 3\DOSBOX\winmm.dll`
3. いつもどおり GOG Galaxy やショートカットから起動します。

## 入れ方（Steam 版）

1. Steam のライブラリで作品を右クリックし、「管理」→「ローカルファイルを閲覧」でゲームのフォルダを開きます。
2. `Phantasie_JP.zip` の「Steam版」フォルダの中身を、そのフォルダに写します。
3. 次の二つのファイルが増えれば完了です。
   - `DOSBOX\winmm.dll`（第1作と第3作）
   - `winmm.dll`（第2作。`AppleWin.exe` の隣）
4. いつもどおり Steam から起動します。

外すときは増えた `winmm.dll` を消します。セーブデータは英語版と共通です。
ゲームのファイルは書き換えません。配布物には原作のプログラムやデータは入っていません。

## ご意見・誤訳の報告

誤訳や不自然な訳に気づいたら、Issues で教えてください。

## クレジット・権利

- 東雲フォント: The Electronic Font Open Laboratory。パブリックドメインのフォントです。
- `src/` の自作プログラムは MIT License です。
- ゲームの名称・画像・文章の権利は原作の権利者にあります。
