"""第2作の迷宮・屋外の文を、ゲームと同じ始まりで取り出す。

BASIC 8650: 番地 = 3750 + 40 * 番号、CALL 800 で CR まで打ち出す。
つまり文は 40 字の升目からしか始まらない。升目より前にある字は前の文の残骸で、画面に出ない。
文の区画はトラックの頭 13 セクターを続けて使う。
"""
import collections
import os
import re

import ph2disk

WORK = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
USED = 0xd00  # トラックの中で文に使われる長さ
CHARS = set(range(32, 127)) | {8, 21}


def typed(raw):
    buf, x = [], 0
    for c in raw:
        if c == 8:
            x = max(0, x - 1)
        elif c == 21:  # 右へ一つ
            if x == len(buf):
                buf.append(' ')
            x += 1
        else:
            buf[x:x + 1] = [chr(c)]
            x += 1
    s = ''.join(buf)
    return [s[i:i + 40].rstrip() for i in range(0, len(s), 40)]


class Side:
    def __init__(self, side):
        d = ph2disk.logical(os.path.join(WORK, 'orig', 'ph2', 'PHANT2%s.DSK' % side))
        self.d = d
        # 続きの番地に並べ直したもの
        self.at = [q for q in range(len(d)) if (q & 0xfff) < USED]
        self.pos = {q: i for i, q in enumerate(self.at)}
        self.s = bytes(d[q] for q in self.at)
        votes = collections.defaultdict(collections.Counter)
        for m in re.finditer(rb'(?<=\r)[ -~]{12}', self.s):
            votes[m.start() // USED][m.start() % 40] += 1
        self.votes = votes

    def phase(self, i):
        c = collections.Counter()
        b = i // USED
        for k, w in ((b, 3), (b - 1, 1), (b + 1, 1)):
            for p, n in self.votes.get(k, {}).items():
                c[p] += n * w
        return c.most_common(1)[0][0] if c else None

    def record(self, offset):
        """抜き出した件の位置 → (本文の始まり, 打ち出される行)。本文が無ければ None"""
        i = self.pos.get(offset)
        if i is None:
            return None
        first = i
        while first > 0 and self.s[first - 1] in CHARS:
            first -= 1
        ph = self.phase(i)
        if ph is None:
            return None
        start = first + (ph - first) % 40
        end = start
        while end < len(self.s) and self.s[end] in CHARS:
            end += 1
        last = first
        while last < len(self.s) and self.s[last] in CHARS:
            last += 1
        if start >= last:
            return None
        return self.at[start], typed(self.s[start:end])
