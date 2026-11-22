"""第1作の MESSn の符号を解き、ひと続きの伝文に分ける。

伝文は 41 バイトの記録（1 バイト＋40 字）の並び。
  最初の記録のバイト = 地の文の字数
  二つ目以降の記録のバイト = 選択肢の数 + 1
選択肢は地の文の直後から 11 字ずつ並ぶ（番号を選ぶものだけ 3 字ずつ）。
"""
import re

FIELD = 11


def decode(data, n):
    k = n * 3 + 1
    out = bytearray()
    for b in data:
        out.append((b + k) & 255)
        k = (k * 33 + 31) & 255
    return bytes(out)


def messages(d):
    """(位置, 地の文の字数, 行の並び, [(選択肢の始まり, 終わり)]) を返す。位置は先頭からの字数"""
    def first(p):
        s = d[p + 1:p + 41]
        return (d[p] in (40, 80, 120, 160, 200, 240) and len(s) == 40
                and all(32 <= c < 127 for c in s) and re.search(rb'[A-Z]{3}', s))

    p = next((p for p in range(len(d) - 40) if first(p)), None)
    if p is None:
        raise ValueError('伝文の始まりが分からない')
    while p + 2 <= len(d):
        size = d[p]
        text = d[p + 1:p + 41]
        m = d[p + 41] if p + 41 < len(d) else 255
        if b'ENDING SEQU' in text:
            break
        if not 1 < size <= 240:
            p += 41
            continue
        if size > 40 and not 1 <= m <= 10:
            p += 41  # 表示されない残骸
            continue
        n, count = 0, 1
        if size > 40:
            n = m - 1
        elif 2 <= m <= 9:
            n = m - 1
        width = 3 if n == 9 else FIELD
        total = size + n * width
        if size > 40 or n:
            count = (total + 39) // 40
            if n == 9:
                count = 3
        rows = [re.sub('[^ -~]', ' ', d[q + 1:q + 41].decode('latin1')).ljust(40)
                for q in range(p, min(p + count * 41, len(d)), 41)]
        flat = ''.join(rows)
        if size < 40 and not n and flat[size:].strip():
            n = (len(flat.rstrip()) - size + FIELD - 1) // FIELD  # 一行に収まった選択肢
        picks = [(a, min(a + width, len(flat))) for a in range(size, size + n * width, width)
                 if a < len(flat) and flat[a:a + width].strip()]
        yield p, size, rows, picks
        p += count * 41
