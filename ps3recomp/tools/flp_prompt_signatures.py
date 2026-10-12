#!/usr/bin/env python3
"""flp_prompt_signatures.py -- offline identity of every GoW2 HUD button.

Reads FLP_HUDA as JSON from god_of_war_browser (github.com/mogaika/
god_of_war_browser, read-only on the local R_PermA WAD / psarc):

    gow-browser -psarc gow2.psarc -ps ps3 -gowversion 2 -i 127.0.0.1:8123
    curl -s 127.0.0.1:8123/json/pack/wad_r_perma.wad_ps3/2972 > flp.json
    python3 tools/flp_prompt_signatures.py flp.json

and prints, for each named element (XButton, CircleButton, L1Button,
DpadButton, Joystick, Stick360CW...), its mesh layers with index count, size,
texture and the order-free shape signature that
metal_prompt_shape_signature() computes in-boot (rsx_metal_backend.m). No game
data is stored: only the numbers this prints go into the renderer's table.
"""
import json,numpy as np,sys
D=json.load(open(sys.argv[1]))['Data']; F=D['FLP']; parts=D['Model']['Meshes'][0]['Parts']
f32=np.float32; M32=0xFFFFFFFF
def fnv(vals):
    h=2166136261
    for v in vals: h=((h^(v&M32))*16777619)&M32
    return h
def strips(pi):
    out=[]
    for g in parts[pi]['Groups']:
        for o in g['Objects']:
            for pk in o['Packets']:
                for p in pk:
                    T=p['Trias']; out.append(list(zip(T['X'],T['Y'],T['Skip'])))
    return out
def tris(pi):
    s=[]
    for st in strips(pi):
        for i in range(2,len(st)):
            if not st[i][2]: s.append([st[j][:2] for j in (i-2,i-1,i)])
    return s
def osig(T):
    """Order-free signature: every triangle's sorted quantized corners hashed,
    summed mod 2^32 (commutative), mixed with the triangle count."""
    xs=[p[0] for t in T for p in t]; ys=[p[1] for t in T for p in t]
    xmin,ymin,xmax,ymax=f32(min(xs)),f32(min(ys)),f32(max(xs)),f32(max(ys))
    if not (xmax>xmin and ymax>ymin): return None
    sx=f32(1024)/f32(xmax-xmin); sy=f32(1024)/f32(ymax-ymin)
    acc=0
    for t in T:
        q=sorted((int(np.rint(f32(f32(x)-xmin)*sx)),int(np.rint(f32(f32(y)-ymin)*sy))) for x,y in t)
        acc=(acc+fnv([c for p in q for c in p]))&M32
    return fnv([acc,len(T)])
def info(pi):
    T=tris(pi)
    if not T: return None
    xs=[p[0] for t in T for p in t]; ys=[p[1] for t in T for p in t]
    return dict(part=pi,idx=3*len(T),sig='%08X'%osig(T),w=max(xs)-min(xs),h=max(ys)-min(ys))
def resolve(h,seen,depth=0):
    t,i=h['TypeArrayId'],h['IdInThatTypeArray']
    out=[]
    if (t,i) in seen or depth>8: return out
    seen.add((t,i))
    if t==1:
        ref=F['MeshPartReferences'][i]; tex=[m.get('TextureName') for m in ref['Materials'] if m.get('TextureName')]
        inf=info(ref['MeshPartIndex'])
        if inf: inf['tex']=tex[0] if tex else ''; out.append(inf)
    elif t in (6,7):
        arr=F['Datas7'] if t==7 else [x['Sub1'] for x in F['Datas6']]
        o=arr[i]
        for ea in o['ElementsAnimation']:
            for kf in ea['KeyFrames']:
                out+=resolve(kf['ElementHandler'],seen,depth+1)
    return out
if __name__=='__main__':
    named={}
    def walk(o):
        if isinstance(o,dict):
            n=o.get('Name')
            if n and 'ElementHandler' in o and n not in named: named[n]=o['ElementHandler']
            for v in o.values(): walk(v)
        elif isinstance(o,list):
            for v in o: walk(v)
    walk(F)
    want=[n for n in named if any(k in n for k in ('Button','Stick','Joystick','Dpad'))]
    for n in sorted(want):
        L=resolve(named[n],set())
        print(n, ' | '.join('p%d n%d %s %.2fx%.2f%s'%(x['part'],x['idx'],x['sig'],x['w'],x['h'],(' T:'+x['tex']) if x['tex'] else '') for x in L[:8]), '(+%d)'%(len(L)-8) if len(L)>8 else '')
