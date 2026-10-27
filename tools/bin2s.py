#!/usr/bin/env python3
"""Minimal bin2s: emit an ARM assembly file with <name>, <name>_end and <name>_size symbols."""
import sys, os, re
path = sys.argv[1]
data = open(path, 'rb').read()
name = re.sub(r'[^0-9A-Za-z_]', '_', os.path.basename(path))
out = ['\t.section .rodata', '\t.balign 4', f'\t.global {name}', f'\t.global {name}_end', f'\t.global {name}_size',
       f'{name}:']
for i in range(0, len(data), 16):
    out.append('\t.byte ' + ','.join(str(b) for b in data[i:i+16]))
out += [f'{name}_end:', '\t.balign 4', f'{name}_size: .int {len(data)}', '']
sys.stdout.write('\n'.join(out))
