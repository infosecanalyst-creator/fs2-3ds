#!/usr/bin/env python3
"""Measure strings in FreeSpace 2 .vf fonts exactly like gr_get_string_size (with kerning)."""
import struct, sys

class Font:
    def __init__(self, path):
        d = open(path, 'rb').read()
        (self.id, ver, self.n, self.first, self.w, self.h, self.nk, ksz, csz, psz) = struct.unpack_from('<4s9i', d, 0)
        o = 40
        self.kern = [struct.unpack_from('<bbb', d, o + i * 3) for i in range(self.nk)]
        o += ksz
        self.chars = [struct.unpack_from('<iiihh', d, o + i * 16) for i in range(self.n)]
    def char(self, c1, c2):
        L = ord(c1) - self.first
        if L < 0 or L >= self.n:
            return self.w
        spacing = self.chars[L][0]
        i = self.chars[L][3]
        if i > -1 and c2:
            L2 = ord(c2) - self.first
            if 0 <= L2 < self.n:
                while i < self.nk and self.kern[i][0] == L and self.kern[i][1] < L2:
                    i += 1
                if i < self.nk and self.kern[i][1] == L2:
                    spacing += self.kern[i][2]
        return spacing
    def width(self, s):
        w = 0
        for i, c in enumerate(s):
            w += self.char(c, s[i + 1] if i + 1 < len(s) else '')
        return w

if __name__ == '__main__':
    f = Font(sys.argv[1])
    print('height', f.h)
    for s in sys.argv[2:]:
        print(f.width(s), repr(s))
