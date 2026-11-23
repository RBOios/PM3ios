#!/usr/bin/env python3
"""Pixel-compare pm3_dump PPM output against the Python reference PNGs.
usage: compare_with_python.py <py_png_dir> <cpp_ppm_dir>"""
import os, sys
from PIL import Image, ImageChops
py, cpp = sys.argv[1], sys.argv[2]
def walk(root):
    for d, _, fs in os.walk(root):
        for f in fs:
            yield os.path.relpath(os.path.join(d, f), root)
a = {os.path.splitext(p)[0]: p for p in walk(py) if p.endswith('.png')}
b = {os.path.splitext(p)[0]: p for p in walk(cpp) if p.endswith('.ppm')}
same = diff = 0
for k in sorted(a):
    if k not in b: print('MISSING in C++:', k); continue
    x = Image.open(os.path.join(py, a[k])).convert('RGB'); y = Image.open(os.path.join(cpp, b[k])).convert('RGB')
    if x.size == y.size and ImageChops.difference(x, y).getbbox() is None: same += 1
    else: diff += 1; print('DIFF', k, x.size, y.size)
for k in sorted(set(b) - set(a)): print('EXTRA in C++:', k)
print(f'identical {same}  different {diff}  total {len(a)}')
