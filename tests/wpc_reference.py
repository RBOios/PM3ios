#!/usr/bin/env python3
"""Reference dump for the C++ WPC port.
usage: wpc_reference.py <py_dir> <PM3SYS0.FPA> <out_dir> <ticks> <seed> [NAME]
Writes <NAME>.txt in the same format as `pm3_dump wpc`; with NAME also PNG frames 0/50/150."""
import sys, os
pydir, fpa_path, out, ticks, seed = sys.argv[1], sys.argv[2], sys.argv[3], int(sys.argv[4]), int(sys.argv[5])
only = sys.argv[6] if len(sys.argv) > 6 else None
sys.path.insert(0, pydir)
import wpc_play
from fpa import FPA
from wpc_meta import load
from PIL import Image
os.makedirs(out, exist_ok=True)
f = FPA(fpa_path); n = 0
for e in f.entries:
    if e[1] != 'WPC' or (only and e[0] != only): continue
    r, b1, b2 = load(f, e)
    wpc_play.FX.clear()
    try:
        m, frames = wpc_play.run(r, b1, b2, ticks, seed)
    except Exception as ex:
        print('py fail', e[0], repr(ex)[:60]); continue
    with open(f'{out}/{e[0]}.txt', 'w') as t:
        for k, fr in enumerate(frames):
            t.write(f'{k}:' + ''.join(f' {p},{i},{o},{c},{x},{y}' for p, i, o, c, x, y in fr) + '\n')
        ops = {v: k for k, v in wpc_play.FX_OPS.items()}
        for (tk, i, nm, A, B, C) in wpc_play.FX: t.write(f'fx {tk} {i} {ops[nm]} {A} {B} {C}\n')
        if only:
            res = wpc_play.make_gif(f, e[0], '/tmp/_x.gif', ticks, seed)
            if res:
                plans, bd = res
                W, H = bd[2]-bd[0], bd[3]-bd[1]
                t.write(f'bounds 1 {bd[0]} {bd[1]} {bd[2]} {bd[3]}\n')
                for idx in (0, 50, 150):
                    if idx >= len(plans): continue
                    can = Image.new('RGBA', (W, H), (70, 90, 110, 255))
                    for im, x, y in plans[idx]: can.alpha_composite(im, (x-bd[0], y-bd[1]))
                    can.convert('RGB').save(f'{out}/{e[0]}_f{idx}.png')
            else: t.write('bounds 0 0 0 0 0\n')
        else:
            # bounds without images: reuse make_gif's math via a throwaway call is slow; compute directly
            from wpc_render import SCALE
            bds = [10**9, 10**9, -10**9, -10**9]
            for fr in frames[::2]:
                for param, i, o, cur, px, py in fr:
                    a = m['anims'][cur]; g = a[0] + m['sheets'][o][4]
                    if g >= len(m['groups']): continue
                    nn, s = m['groups'][g]; rs = m['rects'][s:s+nn]
                    if not rs: continue
                    gx0 = min(q[0] for q in rs)*SCALE; gy0 = min(q[1] for q in rs)*SCALE
                    gx1 = max(q[0]+q[6] for q in rs)*SCALE; gy1 = max(q[1]+q[7] for q in rs)*SCALE
                    x = gx0 + (px+a[2])*SCALE; y = gy0 + (py+a[3])*SCALE
                    bds = [min(bds[0], x), min(bds[1], y), max(bds[2], x+gx1-gx0), max(bds[3], y+gy1-gy0)]
            t.write('bounds ' + ('0 0 0 0 0' if bds[0] > bds[2] else f'1 {bds[0]} {bds[1]} {bds[2]} {bds[3]}') + '\n')
    n += 1
print('py dumped', n)
