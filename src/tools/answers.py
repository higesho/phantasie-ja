"""説明書照合の正解を、原作の表から作る

第1作: 三択。品名→品番、呪文の番号→呪文名を照合値で持ち、押す番号は画面の選択肢から決める
第3作: 問いごとに正解が一つに決まっているので、問いの訳に添える
"""
import os
import re
import struct

ORIG = os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))), 'orig')
PH1_DS = 0xc9f0
PH1_ITEMS, PH1_NITEM = 0x0ec0, 100   # 品番 1〜
PH1_SPELLS, PH1_NSPELL = 0x174d, 54  # 呪文 1〜
PH3_DS = 0x2740 + 0x28c00            # 展開した MAIN.EXE の中
PH3_RIGHT = 0x1c62                   # 問いごとの正解の位置（1〜3）
PH3_CHOICE = 0x1c9e                  # 問いごとの選択肢 3 つ（0〜10）
PH3_FIRST, PH3_COUNT = 1070, 10      # 問いの文の id


def rd(name):
    with open(os.path.join(ORIG, name), 'rb') as f:
        return f.read()


def ph1_tables():
    d = rd('ph1_image.bin')

    def name(table, i):
        at = PH1_DS + struct.unpack_from('<H', d, PH1_DS + table + i * 2)[0]
        return d[at:d.index(b'\0', at)].decode('ascii')

    items = [name(PH1_ITEMS, i + 1) for i in range(PH1_NITEM)]
    spells = [name(PH1_SPELLS, i) for i in range(PH1_NSPELL)]
    return items, spells


def ph3_answers():
    d = rd('ph3_main.exe')
    out = {}
    for q in range(PH3_COUNT):
        right = d[PH3_DS + PH3_RIGHT + q]
        assert 1 <= right <= 3
        out['3-%04d.0' % (PH3_FIRST + q)] = d[PH3_DS + PH3_CHOICE + q * 3 + right - 1]
    return out


def pack(canon, fnv):
    """DLL に入れる表。名前は長さと照合値だけ"""
    items, spells = ph1_tables()
    assert len(set(items)) == len(items) and len(set(spells)) == len(spells)
    out = bytearray(b'PHA1' + bytes([len(items), len(spells)]))
    for s in items + spells:
        b = bytes(canon.get(ord(c), ord(c)) for c in re.sub(' +', ' ', s)).lower()
        out += bytes([len(b)]) + fnv(b)
    return bytes(out)
