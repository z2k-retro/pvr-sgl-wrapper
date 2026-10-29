#!/usr/bin/env python3
"""Resolve sgl.dll+0xOFFSET entries (as in pvrsgl.log) with build/sgl.map.
Usage: tools/addr2sym.py [build/sgl.map] < pvrsgl.log"""
import re, sys
mapfile = sys.argv[1] if len(sys.argv) > 1 else 'build/sgl.map'
syms, base = [], 0x10000000
for l in open(mapfile):
    m = re.match(r'\s+0x([0-9a-f]+)\s+__image_base__ = ', l)
    if m:
        base = int(m.group(1), 16)
    m = re.match(r'\s+0x([0-9a-f]{8})\s+(_[A-Za-z_]\S*)\s*$', l)
    if m:
        syms.append((int(m.group(1), 16), m.group(2)))
syms.sort()
def look(off):
    va, best = base + off, None
    for a, n in syms:
        if a > va:
            break
        best = (a, n)
    return '%s+0x%x' % (best[1][1:], va - best[0]) if best else '?'
for line in sys.stdin:
    sys.stdout.write(re.sub(r'sgl\.dll\+0x([0-9a-f]+)',
                            lambda m: 'sgl.dll:' + look(int(m.group(1), 16)), line))
