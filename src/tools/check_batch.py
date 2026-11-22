"""訳の組を検査する: check_batch.py NN …（なしなら全部）

translation/batch/NN.tsv と translation/ja/NN.tsv を比べ、
行の抜け・幅・使えない文字・印・頭文字キー・数の違いを出す。
"""
import os
import re
import sys

WORK = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
TR = os.path.join(WORK, 'translation')


def width(s):
    return sum(1 if ord(c) < 0x80 else 2 for c in s)


def bad_chars(s):
    out = []
    for c in s:
        if ord(c) < 0x80:
            if ord(c) < 0x20:
                out.append(repr(c))
            continue
        try:
            b = c.encode('euc_jp')
        except UnicodeEncodeError:
            out.append(c)
            continue
        if len(b) != 2 or b[0] == 0x8e or 0xa9 <= b[0] <= 0xaf:
            out.append(c)  # 半角カナ・補助漢字・機種依存文字
    return out


def problems(en, limit, ja):
    if not ja.strip():
        return ['訳が空']
    errs = []
    if width(ja) > limit:
        errs.append('幅 %d > max %d' % (width(ja), limit))
    bad = bad_chars(ja)
    if bad:
        errs.append('使えない文字 ' + ' '.join(bad))
    if sorted(re.findall(r'\{\w+\}', en)) != sorted(re.findall(r'\{\w+\}', ja)):
        errs.append('波かっこの印が違う')
    for k in re.findall(r'(?:^|\s)[A-Z]{0,2}([A-Z0-9])\)(?=\s?[A-Za-z])', en):
        if k + ')' not in ja:
            errs.append('キー %s) が消えた' % k)
    for k in re.findall(r'(?:^|\s)([1-9]) ?-(?=\s?[A-Z])', en):
        if not re.search(k + ' ?-', ja):
            errs.append('番号 %s- が消えた' % k)
    for d in set(re.findall(r'\d+', en)):
        if d not in re.findall(r'\d+', ja):
            errs.append('数 %s が無い（算用数字のまま書く）' % d)
    return errs


def load(path):
    rows = [ln.split('\t') for ln in open(path, encoding='utf-8-sig').read().split('\n') if ln.strip()]
    return rows


def check(name):
    src = load(os.path.join(TR, 'batch', name + '.tsv'))[1:]
    path = os.path.join(TR, 'ja', name + '.tsv')
    if not os.path.exists(path):
        print('%s: 訳のファイルが無い' % name)
        return 1
    count = 0
    got = {}
    for n, r in enumerate(load(path), 1):
        if len(r) != 2:
            print('%s 行 %d: 「id<TAB>訳」の二欄でない' % (name, n))
            count += 1
        elif r[0] in got:
            print('%s %s: id が重複' % (name, r[0]))
            count += 1
        else:
            got[r[0]] = r[1]
    for pid, limit, _, en in src:
        if pid not in got:
            errs = ['訳の行が無い']
        else:
            errs = problems(en, int(limit), got.pop(pid))
        if errs:
            print('%s %s: %s | %s' % (name, pid, '; '.join(errs), en[:70]))
            count += 1
    for pid in got:
        print('%s %s: この組に無い id' % (name, pid))
        count += 1
    print('%s: %d 件中 誤り %d 件' % (name, len(src), count))
    return count


if __name__ == '__main__':
    names = sys.argv[1:] or sorted(f[:-4] for f in os.listdir(os.path.join(TR, 'batch')))
    sys.exit(1 if sum([check(n) for n in names]) else 0)
