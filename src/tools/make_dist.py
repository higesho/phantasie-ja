"""配布用の zip を作る: make_dist.py（先に build.py と check_clean.py）

zip の一番上は「GOG版」「Steam版」のフォルダ。それぞれの中身をゲームのフォルダに写す。
GOG 版は作品ごとのフォルダ、Steam 版は三作が一つのフォルダ（DOSBOX は第1・3作で共用）。
"""
import hashlib
import os
import zipfile

WORK = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
DLL = os.path.join(WORK, 'dll', 'out', 'winmm.dll')
SRC = os.path.join(WORK, 'dist_src')
FONT = os.path.join(os.path.dirname(os.path.dirname(WORK)), 'Arena', 'jp_work', 'font', 'LICENSE')
OUT = os.path.join(WORK, 'dist', 'Phantasie_JP.zip')
PLACES = ('GOG版/Phantasie 1/DOSBOX/winmm.dll', 'GOG版/Phantasie 2/winmm.dll', 'GOG版/Phantasie 3/DOSBOX/winmm.dll',
          'Steam版/DOSBOX/winmm.dll', 'Steam版/winmm.dll')


def notepad(text):
    return b'\xef\xbb\xbf' + text.replace('\r\n', '\n').replace('\n', '\r\n').encode('utf-8')


def main():
    os.makedirs(os.path.dirname(OUT), exist_ok=True)
    dll = open(DLL, 'rb').read()
    readme = open(os.path.join(SRC, '読んでください.txt'), encoding='utf-8').read()
    font = open(FONT, 'rb').read().decode('euc_jp')
    with zipfile.ZipFile(OUT, 'w', zipfile.ZIP_DEFLATED) as z:
        for p in PLACES:
            z.writestr(zipfile.ZipInfo.from_file(DLL, p), dll, zipfile.ZIP_DEFLATED)
        z.writestr('ファンタジー日本語化_読んでください.txt', notepad(readme))
        z.writestr('東雲フォント_LICENSE.txt', notepad(font))
    with zipfile.ZipFile(OUT) as z:
        for i in z.infolist():
            print('%9d  %s  %s' % (i.file_size, hashlib.md5(z.read(i)).hexdigest(), i.filename))
    print('%s %s %d バイト' % (OUT, hashlib.md5(open(OUT, 'rb').read()).hexdigest(), os.path.getsize(OUT)))


if __name__ == '__main__':
    main()
