#!/usr/bin/env python3
"""
Synthetic COMPLETE-MODEL .rxs generator (example / demo).

Builds a real 3D model -- a rotated, perspective-projected colored cube
(6 faces, 12 triangles) -- and emits it as an RXS1 v2 capture that the
replay harnesses (replay_mac.m / replay_main.c) render through the SAME
pipeline as a real game capture: rsx_dispatch -> vertex fetch -> raster.

No depth buffer exists in the replay's stage-2 path, so the 12 triangles are
submitted back-to-front (painter's algorithm) to look solid. Positions are
emitted in window/viewport pixel space (the replay applies the inverse
viewport transform), exactly like gen_synthetic_rxs.py's triangle.

This is a *demonstration fixture*: every byte is synthesized, no game data.
The real path to a game model is tools/rrc_export.py (RPCS3 .rrc -> .rxs).

Usage: python3 gen_model_rxs.py [out.rxs]
"""
import math
import sys
import gen_synthetic_rxs as g   # reuse constants + build() + .rxs emission

# --- cube geometry --------------------------------------------------------
CORNERS = [(x, y, z) for x in (-1, 1) for y in (-1, 1) for z in (-1, 1)]
# index = (x>0)*4 + (y>0)*2 + (z>0)
def ci(sx, sy, sz): return (sx > 0) * 4 + (sy > 0) * 2 + (sz > 0)

# faces: (4 corner indices CCW from outside), rgba color
FACES = [
    ([ci(+1,-1,-1), ci(+1,+1,-1), ci(+1,+1,+1), ci(+1,-1,+1)], (0.90, 0.20, 0.20, 1)),  # +X red
    ([ci(-1,-1,-1), ci(-1,-1,+1), ci(-1,+1,+1), ci(-1,+1,-1)], (0.20, 0.80, 0.30, 1)),  # -X green
    ([ci(-1,+1,-1), ci(-1,+1,+1), ci(+1,+1,+1), ci(+1,+1,-1)], (0.25, 0.45, 0.95, 1)),  # +Y blue
    ([ci(-1,-1,-1), ci(+1,-1,-1), ci(+1,-1,+1), ci(-1,-1,+1)], (0.95, 0.80, 0.20, 1)),  # -Y yellow
    ([ci(-1,-1,+1), ci(+1,-1,+1), ci(+1,+1,+1), ci(-1,+1,+1)], (0.20, 0.85, 0.90, 1)),  # +Z cyan
    ([ci(-1,-1,-1), ci(-1,+1,-1), ci(+1,+1,-1), ci(+1,-1,-1)], (0.90, 0.35, 0.85, 1)),  # -Z magenta
]

AX, AY = math.radians(25), math.radians(35)
CAM_DIST, FOCAL = 4.6, 210.0
CX, CY = g.WIDTH / 2.0, g.HEIGHT / 2.0

def rot(p):
    x, y, z = p
    # Ry
    x, z = x * math.cos(AY) + z * math.sin(AY), -x * math.sin(AY) + z * math.cos(AY)
    # Rx
    y, z = y * math.cos(AX) - z * math.sin(AX), y * math.sin(AX) + z * math.cos(AX)
    return (x, y, z)

def project(p):
    x, y, z = rot(p)
    zc = z + CAM_DIST
    return (CX + FOCAL * x / zc, CY - FOCAL * y / zc, zc)

# rotated depth of each corner (for painter's ordering)
RZ = [rot(c)[2] for c in CORNERS]
PROJ = [project(c) for c in CORNERS]

# order faces back-to-front by average corner depth (larger z = further)
faces_sorted = sorted(FACES, key=lambda f: -sum(RZ[i] for i in f[0]) / 4.0)

verts = []
for idxs, col in faces_sorted:
    a, b, c, d = idxs
    for tri in ((a, b, c), (a, c, d)):        # two triangles per quad face
        for vi in tri:
            sx, sy, _ = PROJ[vi]
            verts.append((sx, sy, 0.0, *col))  # x,y,z + r,g,b,a  (VERT_STRIDE=28)

g.VERTICES = verts   # 36 verts = 12 triangles

if __name__ == '__main__':
    out = sys.argv[1] if len(sys.argv) > 1 else 'model.rxs'
    g.build(out)
    print(f"  modelo: cubo 3D, {len(verts)//3} triangulos, painter-ordered")
