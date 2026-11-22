"""第2作のディスクの像を、ゲームが読むセクター順に並べ直す"""
import os

ORDER = [0, 7, 14, 6, 13, 5, 12, 4, 11, 3, 10, 2, 9, 1, 8, 15]


def logical(path):
    d = open(path, 'rb').read()
    return b''.join(d[(t * 16 + s) * 256:(t * 16 + s + 1) * 256] for t in range(35) for s in ORDER)


if __name__ == '__main__':
    w = os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))), 'orig')
    for n in 'AB':
        with open(os.path.join(w, 'ph2_%s.bin' % n), 'wb') as f:
            f.write(logical(os.path.join(w, 'ph2', 'PHANT2%s.DSK' % n)))
