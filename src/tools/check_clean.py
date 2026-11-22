"""配布する DLL に原作の英文・字形・試験部が入っていないかを調べる

    check_clean.py [winmm.dll]
"""
import json
import os
import re
import subprocess
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import build

WORK = build.WORK
DLL = sys.argv[1] if len(sys.argv) > 1 else os.path.join(WORK, 'dll', 'out', 'winmm.dll')
GAME = os.path.dirname(WORK)


def printable_runs(data, n):
    return [m.group().decode('latin1') for m in re.finditer(rb'[ -~]{%d,}' % n, data)]


def main():
    data = build.rd(DLL)
    low = data.lower()
    bad = 0

    # 訳に英字のまま残すもの（合言葉など）は日本語側に入るので除く
    parts, trans = build.translations(False)
    kept = ' '.join(str(t) for t in trans.values()).lower()
    hits = set()
    for game in (1, 2, 3):
        for it in json.load(open(os.path.join(build.TR, 'src', 'ph%d.json' % game), encoding='utf-8')):
            for ln in it['lines']:
                for s in [x.strip() for x in re.split(r' {3,}', ln)]:
                    if len(s) >= 10 and re.search('[A-Za-z]{4}', s) and s.lower().encode('latin1', 'replace') in low:
                        if s.lower() not in kept:
                            hits.add(s)
    print('原作の英文: %d 件 %s' % (len(hits), sorted(hits)[:20]))
    bad += len(hits)

    exe = os.path.join(WORK, 'dll', 'out_test', 'glyphcheck.exe')
    for game in (1, 3):
        g = build.game_glyphs(game)
        got = subprocess.run([exe, str(game), os.path.join(GAME, 'Phantasie %d' % game, 'DOSBOX') + os.sep],
                             capture_output=True, timeout=20).stdout
        same = got == bytes(g)
        inside = sum(1 for c in range(33, 127) if len(set(g[c * 8:c * 8 + 8])) > 3 and bytes(g[c * 8:c * 8 + 8]) in data)
        run = sum(1 for c in range(33, 124) if bytes(g[c * 8:c * 8 + 24]) in data and any(g[c * 8:c * 8 + 24]))
        print('第%d作の字形: 実行時の読み込み %s、DLL 内の一致 1 字 %d・3 字連続 %d' % (game, '一致' if same else '不一致', inside, run))
        bad += (not same) + run

    test = [s for s in printable_runs(data, 5) if re.search(r'phjp_test|phjp_shot|PHJP-TEST', s)]
    print('試験部: %d 件' % len(test))
    bad += len(test)
    print('大きさ %d バイト' % len(data))
    return 1 if bad else 0


if __name__ == '__main__':
    sys.exit(main())
