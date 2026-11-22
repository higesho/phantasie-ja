"""第2作の像から Applesoft BASIC の行を読める形に戻す"""
import struct

TOKENS = ('END FOR NEXT DATA INPUT DEL DIM READ GR TEXT PR# IN# CALL PLOT HLIN VLIN HGR2 HGR HCOLOR= HPLOT '
          'DRAW XDRAW HTAB HOME ROT= SCALE= SHLOAD TRACE NOTRACE NORMAL INVERSE FLASH COLOR= POP VTAB HIMEM: '
          'LOMEM: ONERR RESUME RECALL STORE SPEED= LET GOTO RUN IF RESTORE & GOSUB RETURN REM STOP ON WAIT '
          'LOAD SAVE DEF POKE PRINT CONT LIST CLEAR GET NEW TAB( TO FN SPC( THEN AT NOT STEP + - * / ^ AND OR '
          '> = < SGN INT ABS USR FRE SCRN( PDL POS SQR RND LOG EXP COS SIN TAN ATN PEEK LEN STR$ VAL ASC CHR$ '
          'LEFT$ RIGHT$ MID$').split()


def programs(d):
    """(開始位置, [(行番号, 中身)]) を返す"""
    out = []
    i = 0
    while i < len(d) - 8:
        lines = []
        p = i
        prev = None
        while p < len(d) - 5:
            link, num = struct.unpack_from('<HH', d, p)
            if not 0x0800 < link < 0xc000 or num > 63999:
                break
            z = d.find(b'\x00', p + 4, p + 4 + 300)
            if z <= p + 4:
                break
            n = z + 1 - p
            if prev is not None and (link - prev[0] != n or num <= prev[1]):
                break
            lines.append((num, d[p + 4:z]))
            prev = (link, num)
            p = z + 1
        if len(lines) >= 4:
            out.append((i, lines))
            i = p
        else:
            i += 1
    return out


def text(body):
    s = []
    quote = False
    for b in body:
        if b == 34:
            quote = not quote
        if b >= 0x80 and not quote:
            k = b - 0x80
            s.append(' %s ' % TOKENS[k] if k < len(TOKENS) else '<%02x>' % b)
        elif 32 <= b < 127:
            s.append(chr(b))
        else:
            s.append('<%02x>' % b)
    return ''.join(s)


if __name__ == '__main__':
    import sys
    d = open(sys.argv[1], 'rb').read()
    for at, lines in programs(d):
        print('######## %05x %d 行' % (at, len(lines)))
        for num, body in lines:
            print(num, text(body))
