"""訳・字形・フォントをまとめて winmm.dll を組み立てる

    build.py [test] [--partial]

訳の優先: ui.tsv > manual.tsv > fix.tsv > ja/NN.tsv > auto.json
--partial: 訳がそろっていなくても組み立てる（試験用）
"""
import collections
import glob
import json
import os
import re
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
WORK = os.path.dirname(HERE)
TR = os.path.join(WORK, 'translation')
GEN = os.path.join(WORK, 'dll', 'gen')
FONT = os.path.join(os.path.dirname(os.path.dirname(WORK)), 'Arena', 'jp_work', 'font')
sys.path.insert(0, HERE)
import answers
import check_batch
import ph2text

F_EXACT, F_ISOL, F_TERM, F_NEEDCTX = 1, 2, 4, 8
HEAD = '、。，．：；？！）］｝〉》」』】ーぁぃぅぇぉっゃゅょゎァィゥェォッャュョヮヵヶ々…・゛゜'
TAIL = '（［｛〈《「『【'
# 連結の部品。単独では訳さない（ui.tsv の文型で訳す）
# 第2作: 本文の始まりを升目から取り直す件（前の文の残骸が頭に付いていた、区画をまたいでいた）
PH2_RESTART = {816, 853, 911, 940, 952, 968, 976, 1008, 1012, 1057, 1060, 1080, 1093, 1096, 1102}
PH2_CUT = {922: 6}  # 升目の分からない区画
FRAGMENT = {'THE', 'ONE', 'AT', 'TO', 'FROM', 'HAS', 'NOT', 'IS', 'OF', 'AND', 'AN', 'FOR', 'WITH', 'IN'}
PRINTF = re.compile(r'%[-+0]?\d*(?:\.\d+)?(?:l?[dux]|s|c)')
MARK = re.compile(r'\{([nstc]|\d)\}')

errors = []


def rd(path):
    return open(path, 'rb').read()


def tsv(path, cols):
    if not os.path.exists(path):
        return
    for n, ln in enumerate(open(path, encoding='utf-8-sig').read().split('\n'), 1):
        if not ln.strip() or ln.startswith('#'):
            continue
        f = ln.split('\t')
        if len(f) != cols:
            errors.append('%s:%d: 欄が %d でない' % (os.path.basename(path), n, cols))
        else:
            yield f


# ---- フォント ----

def bdf(path):
    code, rows = None, None
    for ln in open(path, encoding='latin1'):
        f = ln.split()
        if not f:
            continue
        if f[0] == 'ENCODING':
            code = int(f[1])
        elif f[0] == 'BITMAP':
            rows = []
        elif f[0] == 'ENDCHAR':
            yield code, rows
            rows = None
        elif rows is not None:
            rows.append(int(f[0], 16) << (16 - 4 * len(f[0])))


def kanji(path, h):
    out = bytearray(94 * 94 * h * 2)
    for code, rows in bdf(path):
        ku, ten = (code >> 8) - 0x21, (code & 255) - 0x21
        if 0 <= ku < 94 and 0 <= ten < 94:
            for r, v in enumerate(rows[:h]):
                out[((ku * 94 + ten) * h + r) * 2:((ku * 94 + ten) * h + r) * 2 + 2] = v.to_bytes(2, 'big')
    return out


def ascii_font(path, h):
    out = bytearray(256 * h)
    for code, rows in bdf(path):
        if 0x20 <= code < 0x7f:
            for r, v in enumerate(rows[:h]):
                out[code * h + r] = v >> 8
    return out


def game_glyphs(game):
    out = bytearray(256 * 8)
    if game == 1:
        d = rd(os.path.join(WORK, 'orig', 'ph1', 'PH1', 'FONT'))
        for c in range(33, 127):
            for y in range(8):
                v = int.from_bytes(d[c * 16 + y * 2:c * 16 + y * 2 + 2], 'big')
                out[c * 8 + y] = sum(0x80 >> x for x in range(8) if v >> (14 - 2 * x) & 3)
    else:
        d = rd(os.path.join(WORK, 'orig', 'ph3_main.exe'))  # 展開したもの
        out[33 * 8:127 * 8] = d[0x2b40c + 33 * 8:0x2b40c + 127 * 8]
    return out


def canon_table(glyphs):
    """同じ形の字は若い番号に読まれる。辞書の英語も同じに揃える"""
    table, seen = {}, {}
    for c in range(33, 127):
        g = bytes(glyphs[c * 8:c * 8 + 8])
        if any(g):
            seen.setdefault(g, c)
            if seen[g] != c:
                table[c] = seen[g]
    return table


# ---- 文の流し込み ----

def units(s):
    return check_batch.width(MARK.sub('00', s))


def tokens(s):
    return re.findall(r'\{\w\}|[!-z|~]+|[ -~]|[^ -~]', s)


def flow(text, widths, kinsoku=True):
    """訳を各行の幅（半角）へ流す。入らなければ None"""
    if '^' in text:  # 段落の切れ目
        paras, out, at = text.split('^'), [], 0
        for k, para in enumerate(paras):
            last = len(widths) - (len(paras) - 1 - k)
            for end in range(at + 1, last + 1):
                got = flow(para, widths[at:end], kinsoku)
                if got is not None:
                    out += got
                    at = end
                    break
            else:
                return None
        return out + [''] * (len(widths) - at)
    if '|' in text:
        out = [x.strip() for x in text.split('|')]
        if len(out) > len(widths) or any(units(x) > w for x, w in zip(out, widths)):
            return None
        return out + [''] * (len(widths) - len(out))
    toks = tokens(text)
    out, i = [], 0
    for w in widths:
        while i < len(toks) and toks[i] == ' ' and out:
            i += 1
        j, n = i, 0
        while j < len(toks) and n + units(toks[j]) <= w:
            n += units(toks[j])
            j += 1
        if j == i and i < len(toks):  # 1 語が行より長い
            t = toks[i]
            if t.startswith('{') or w < 1 or len(t) <= w:
                out.append('')
                continue
            toks[i:i + 1] = [t[:w], t[w:]]
            j = i + 1
        if kinsoku and j < len(toks):
            k = j
            while k > i + 1 and (toks[k][0] in HEAD or toks[k - 1][-1] in TAIL):
                k -= 1
            j = k if k > i + 1 else j
        out.append(''.join(toks[i:j]).rstrip())
        i = j
    return out if i >= len(toks) else None


# ---- 規則 ----

def fnv(data):
    """照合値（FNV-1a 64）。配布物には英文でなくこれを入れる"""
    h = 14695981039346656037
    for c in data:
        h = ((h ^ c) * 1099511628211) & 0xffffffffffffffff
    return h.to_bytes(8, 'little')


class Rules:
    def __init__(self, canon):
        self.canon = canon
        self.list = []
        self.seen = set()

    def en_bytes(self, en):
        out, kinds = bytearray(), []
        for part in re.split(r'(\{[nstc]\})', en):
            m = re.fullmatch(r'\{([nstc])\}', part)
            if m:
                kinds.append(m.group(1))
                out.append('nstc'.index(m.group(1)) + 1)
            else:
                for ch in part:
                    if not ' ' <= ch <= '~':
                        raise ValueError('英語に使えない字 %r' % ch)
                    out.append(self.canon.get(ord(ch), ord(ch)))
        return bytes(out), kinds

    @staticmethod
    def ja_bytes(ja, kinds):
        count = collections.Counter()
        out = bytearray()
        for part in re.split(r'(\{(?:[nstca]|\d)\})', ja):
            if part == '{a}':  # 説明書照合の答え。押す番号は DLL が画面から決める
                if not kinds:
                    raise ValueError('答えの印は印のある文型だけ')
                out.append(0x0e)
                continue
            m = re.fullmatch(r'\{([nstc]|\d)\}', part)
            if not m:
                bad = check_batch.bad_chars(part)
                if bad:
                    raise ValueError('使えない字 %s' % bad)
                out += part.encode('euc_jp')
                continue
            k = m.group(1)
            if k.isdigit():
                i = int(k) - 1
            else:
                at = [i for i, x in enumerate(kinds) if x == k]
                if count[k] >= len(at):
                    raise ValueError('印 {%s} が原文に無い' % k)
                i = at[count[k]]
                count[k] += 1
            if not 0 <= i < len(kinds):
                raise ValueError('印の番号が範囲の外')
            out.append(0x10 + i)
        return bytes(out)

    def add(self, where, en, segs, flags=0, prev='', next_='', bonus=0):
        """segs: [(位置, 幅, 左へ広げる升, 訳)]"""
        if not flags & F_EXACT:
            # 照合側は空白の連続を 1 個として読む
            en = ' '.join(en.split())
        prev, next_ = ' '.join(prev.split()), ' '.join(next_.split())
        try:
            e, kinds = self.en_bytes(en)
            ss = [(at, w, left, self.ja_bytes(ja, kinds)) for at, w, left, ja in segs]
            p, n = self.en_bytes(prev)[0], self.en_bytes(next_)[0]
            if len(e) > 200 or len(p) > 200 or len(n) > 200 or len(ss) > 12 or any(len(s[3]) > 250 for s in ss):
                raise ValueError('長すぎる')
        except (ValueError, UnicodeEncodeError) as x:
            errors.append('%s: %s | %s' % (where, x, en[:60]))
            return
        score = 4 * sum(1 for c in e if c > 4 and c != 32) + (1 if flags & F_EXACT else 0) + bonus
        key = (e.lower(), tuple(ss), flags, bonus, p if flags & F_NEEDCTX else '', n if flags & F_NEEDCTX else '')
        if key in self.seen:
            return
        self.seen.add(key)
        self.list.append((score, flags, e, p, n, ss))

    def pack(self):
        def s(b):
            return bytes([len(b)]) + b + b'\x00'

        def key(e):
            toks = [t for t in re.split(rb'([\x01-\x04])', e) if t]
            b = bytearray([len(toks)])
            for k, t in enumerate(toks):
                if t[0] <= 4:
                    b += t
                else:
                    # 頭: 最初の片は索引用の 1 字、ほかは空白で始まるかだけ
                    head = t.lower()[0] if not k else (32 if t[0] == 32 else 0)
                    b += bytes([0, len(t), head]) + fnv(t.lower())
            return bytes(b)

        def near(x):
            return bytes([len(x)]) + fnv(x.lower()) if x else b'\x00'

        out = bytearray(b'PHJ2' + len(self.list).to_bytes(2, 'little'))
        for score, flags, e, p, n, ss in self.list:
            out += score.to_bytes(2, 'little') + bytes([flags]) + key(e) + near(p) + near(n) + bytes([len(ss)])
            for at, w, left, ja in ss:
                out += bytes([at, w, left]) + s(ja)
        return bytes(out)


def marks(s):
    def kind(m):
        c = m.group()[-1]
        return {'s': '{t}', 'c': '{c}'}.get(c, '{n}')
    return PRINTF.sub(kind, s)


def short(en):
    plain = MARK.sub('', en).strip()
    return len(plain) < 12 or len(re.findall(r'[A-Za-z]+', plain)) < 3


def plain_term(en):
    return len(en) <= 24 and not MARK.search(en) and not re.search(r'[.?!:,%]$', en)


def part_rules(rules, item, parts, trans):
    lines = item['lines']
    byrow = collections.defaultdict(list)
    for p in parts:
        ja = trans.get(p['id'], trans.get(p.get('same')))
        if ja is None or ja == '=' or (p['game'] == 2 and p['en'].upper() in FRAGMENT):
            continue
        ja = marks(ja)
        # 「札%4u」は札だけ訳し、右詰めの数は元の升に残す
        label = re.fullmatch(r'([A-Za-z][A-Za-z .]*?)%([3-9]|\d\d)u', p['en'])
        if label and len(p['spans']) == 1 and re.fullmatch(r'[^{]+\{n\}', ja):
            en = label.group(1)
            rules.add(p['id'], en, [(0, 0, 0, ja[:-3])], (F_ISOL | F_TERM) if short(en) else 0, bonus=1)
            continue
        junk = ''
        if p['game'] == 3 and p['from'].startswith('MAIN'):
            # 文字列の前のごみの 1 字（長さの印）は画面に出ない
            m = re.match(r"\((?![^)]*\))|'(?!s )", p['en'])
            if m:
                junk = m.group()
                ja = ja.lstrip(junk + '「').lstrip()
        spans = []
        for r, a, b in p['spans']:
            s = lines[r][a:b] if r < len(lines) else ''
            if s.strip():
                lead = len(s) - len(s.lstrip())
                en = s.strip()
                if junk and not spans and en.startswith(junk):
                    en = en[1:].strip()
                spans.append({'r': r, 'a': a + lead, 'b': max(b, a + lead + len(s.strip())), 'en': en})
        if not spans:
            continue
        own = item.get('opt') or len(spans) == 1 or '|' in ja or (
            len({s['a'] for s in spans}) > 1 and spans[-1]['a'] == 0 and spans[-1]['b'] < 8)
        left, right = min(s['a'] for s in spans), max(s['b'] for s in spans)
        for s in spans:
            s['l'], s['rt'] = s['a'], s['b']
            if own:
                continue
            others = [(a, b) for q in parts if q is not p for r, a, b in q['spans'] if r == s['r']]
            lo = max([left] + [b for a, b in others if b <= s['a']])
            hi = min([right] + [a for a, b in others if a >= s['b']])
            s['l'], s['rt'] = min(lo, s['a']), max(hi, s['b'])
        widths = [2 * (s['rt'] - s['l']) for s in spans]
        if p['deco']:
            m = re.fullmatch(r'(-{2,}) *.*? *(-{2,})', spans[0]['en'])
            if m:
                long = '%s %s %s' % (m.group(1), ja, m.group(2))
                ja = long if units(long) <= widths[0] else '--%s--' % ja
        got = flow(ja, widths) or flow(ja, widths, False)
        if got is None:
            errors.append('%s: 入らない %s に %d | %s' % (p['id'], widths, units(ja), ja[:50]))
            continue
        middle = len(spans) == 1 and spans[0]['a'] >= 3 and abs(spans[0]['a'] - (40 - spans[0]['b'])) <= 4
        for k, (s, text) in enumerate(zip(spans, got)):
            if text == '=':
                continue
            if (middle or p['deco']) and units(text) < widths[0]:
                text = ' ' * (((widths[0] - units(text)) // 2) & ~1) + text
            s['ja'] = text
            en = marks(s['en'])
            if len(re.findall('[A-Za-z]', MARK.sub('', en))) < (1 if k or p['en'] == 'N O' else 2):
                if not (k and en):
                    continue
            flags = 0
            if short(en):
                flags |= F_ISOL
            if len(spans) == 1 and plain_term(en) and short(en) and text == text.strip():
                flags |= F_TERM
            if k and len(en) < 12:
                flags |= F_NEEDCTX
            prev = marks(spans[k - 1]['en']) if k else (lines[s['r'] - 1].strip() if s['r'] else '')
            nxt = marks(spans[k + 1]['en']) if k + 1 < len(spans) else (
                lines[s['r'] + 1].strip() if s['r'] + 1 < len(lines) else '')
            prev, nxt = [x if not (MARK.search(x) or '%' in x) else '' for x in (prev, nxt)]
            rules.add(p['id'], en, [(0, 0, s['a'] - s['l'], text)], flags, prev, nxt)
            byrow[s['r']].append(s)

    # 選択肢などが詰まって並ぶ行は、行ごとの規則も作る
    for r, ss in byrow.items():
        if len(ss) < 2 or PRINTF.search(lines[r]):
            continue
        line = lines[r].rstrip()
        first = min(s['a'] for s in ss)  # 左に地の文が同居する行は、選択肢の所だけ
        segs = []
        for s in ss:
            a = s['l'] if s['l'] >= first else s['a']
            segs.append((a - first, s['b'] - a, 0, s['ja']))
        rules.add('%s 行%d' % (item['id'], r), line[first:], segs, F_EXACT)


def restart(items, parts):
    """第2作の文を、ゲームが打ち出す行に直す"""
    sides = {}
    for it in items:
        n = int(it['id'][2:])
        if n not in PH2_RESTART and n not in PH2_CUT:
            continue
        m = re.fullmatch(r'TEXT([AB])@(\w+)', it['from'])
        if n in PH2_CUT:
            lines = ph2text.typed(''.join(x.ljust(40) for x in it['lines']).rstrip().encode()[PH2_CUT[n]:])
        else:
            side = sides.setdefault(m.group(1), ph2text.Side(m.group(1)))
            lines = side.record(int(m.group(2), 16))[1]
        spans = [[r, len(x) - len(x.lstrip()), len(x)] for r, x in enumerate(lines) if x.strip()]
        mine = parts[it['id']]
        if len(mine) != 1:
            errors.append('%s: 部分が一つでない' % it['id'])
            continue
        it['lines'] = lines
        mine[0]['spans'] = spans
        mine[0]['en'] = ' '.join(' '.join(lines).split())


def ui_rules(rules, game):
    for g, en, ja in tsv(os.path.join(TR, 'ui.tsv'), 3) or ():
        if g != '*' and str(game) not in g:
            continue
        ef, jf = en.split('|'), ja.split('|')
        if len(ef) != len(jf):
            errors.append('ui: 欄の数が違う | %s' % en)
            continue
        if len(ef) == 1:
            flags = 0
            if short(en):
                flags |= F_ISOL
                if plain_term(en):
                    flags |= F_TERM
            # 手引きで引く品名は英語のまま。品名だけの用語より必ず優先する
            bonus = 200 if '{a}' in ja else 2
            rules.add('ui', en, [] if ja == '=' else [(0, 0, 0, ja)], flags, bonus=bonus)
            continue
        if MARK.search(en):
            errors.append('ui: 欄に分けた文型に印は使えない | %s' % en)
            continue
        segs, at = [], 0
        for e, j in zip(ef, jf):
            if j != '=':
                lead = len(e) - len(e.lstrip())
                if units(j) > 2 * (len(e) - lead):
                    errors.append('ui: 幅 %d > %d | %s' % (units(j), 2 * (len(e) - lead), e))
                segs.append((at + lead, len(e) - lead, 0, j))
            at += len(e)
        rules.add('ui', ''.join(ef), segs, F_EXACT, bonus=2)


def choice_rules(rules, parts, trans):
    # 呪文の三択「n. 呪文名」。番号の点の後ろは語の区切りに数えないので、番号ごと訳す
    names = {}
    for p in parts:
        ja = trans.get(p['id'], trans.get(p.get('same')))
        if ja and ja != '=':
            names.setdefault(p['en'], ja)
    for en in answers.ph1_tables()[1]:
        if en not in names:
            errors.append('三択: 呪文名の訳が無い | %s' % en)
            continue
        for k in (1, 2, 3):
            rules.add('三択', '%d. %s' % (k, en), [(0, 0, 0, '%d. %s' % (k, names[en]))], F_ISOL, bonus=2)


def load_glossary():
    g = {}
    for en, ja in tsv(os.path.join(TR, 'glossary.tsv'), 2) or ():
        g[en.lower()] = ja
    return g


def translations(partial):
    trans = json.load(open(os.path.join(TR, 'auto.json'), encoding='utf-8'))
    for path in sorted(glob.glob(os.path.join(TR, 'ja', '*.tsv'))):
        trans.update((i, t) for i, t in tsv(path, 2))
    parts = json.load(open(os.path.join(TR, 'parts.json'), encoding='utf-8'))
    hand = set()
    for name in ('fix.tsv', 'fix2.tsv', 'manual.tsv'):
        for i, t in tsv(os.path.join(TR, name), 2) or ():
            trans[i] = t
            hand.add(i)
    known = {p['id'] for p in parts}
    for i in sorted(hand - known):
        errors.append('手直しの id が無い: %s' % i)
    for p in parts:
        t = trans.get(p['id'])
        if t is None:
            continue
        t = re.sub('。([」』])', r'\1', t)
        if (p['id'] not in hand and p['en'].endswith('.') and len(p['en'].split()) >= 4
                and re.search('[ぁ-ん一-龠]$', t) and check_batch.width(t) + 2 <= p['max']):
            t += '。'
        trans[p['id']] = t
    # 第3作の説明書照合: 問いの次の行に答えを出す
    for i, n in answers.ph3_answers().items():
        if i in trans:
            trans[i] = '%s^（答え：%d）' % (trans[i], n)
    lack = [p['id'] for p in parts if p['id'] not in trans and p.get('same') not in trans]
    if lack:
        print('訳が無い %d 件: %s' % (len(lack), ' '.join(lack[:8])))
        if not partial:
            errors.append('訳がそろっていない')
    return parts, trans


def main():
    test = 'test' in sys.argv[1:]
    partial = '--partial' in sys.argv[1:]
    os.makedirs(GEN, exist_ok=True)
    parts, trans = translations(partial)
    glossary = load_glossary()
    res = {}
    for game in (1, 2, 3):
        canon = {}
        if game != 2:
            glyphs = game_glyphs(game)  # 同じ形の字を揃えるためだけに読む。配布物には入れない
            canon = canon_table(glyphs)
            if canon:
                print('第%d作 同じ形の字: %s' % (game, ' '.join('%c=%c' % kv for kv in canon.items())))
        rules = Rules(canon)
        if game == 1:
            res[35] = answers.pack(canon, fnv)
        ui_rules(rules, game)
        mine = collections.defaultdict(list)
        for p in parts:
            if p['game'] == game:
                mine[p['item']].append(p)
        items = json.load(open(os.path.join(TR, 'src', 'ph%d.json' % game), encoding='utf-8'))
        if game == 2:
            restart(items, mine)
        for it in items:
            if it['id'] in mine:
                part_rules(rules, it, mine[it['id']], trans)
        if game == 1:
            choice_rules(rules, [p for p in parts if p['game'] == 1], trans)
        for en, ja in glossary.items():
            flags = F_TERM | (F_ISOL if short(en) else 0)
            rules.add('用語', en, [(0, 0, 0, ja)], flags)
        for s in rules.list:
            if s[2][:1] and s[2][0] <= 4 and len(re.findall(rb'[A-Za-z]', s[2])) < 2:
                errors.append('第%d作: 文字の少ない文型 %r' % (game, s[2]))
        res[10 + game] = rules.pack()
        print('第%d作 規則 %d 件 %d バイト' % (game, len(rules.list), len(res[10 + game])))
    res[31] = kanji(os.path.join(FONT, 'shnmk16.bdf'), 16)
    res[32] = ascii_font(os.path.join(FONT, 'shnm8x16a.bdf'), 16)
    res[33] = kanji(os.path.join(FONT, 'shnmk14.bdf'), 14)
    res[34] = ascii_font(os.path.join(FONT, 'shnm7x14a.bdf'), 14)

    for e in errors[:120]:
        print('誤り', e)
    if errors:
        print('誤り %d 件' % len(errors))
        if not partial:
            sys.exit(1)
    with open(os.path.join(GEN, 'phjp.rc'), 'w', newline='\r\n') as rc:
        for i in sorted(res):
            name = 'r%d.bin' % i
            with open(os.path.join(GEN, name), 'wb') as f:
                f.write(res[i])
            rc.write('%d RCDATA "gen/%s"\n' % (i, name))
    args = ['cmd', '/c', '.\\build.bat'] + (['test'] if test else [])
    r = subprocess.run(args, cwd=os.path.join(WORK, 'dll'), capture_output=True, timeout=110)
    print((r.stdout + r.stderr).decode('cp932', 'replace'))
    sys.exit(r.returncode)


if __name__ == '__main__':
    main()
