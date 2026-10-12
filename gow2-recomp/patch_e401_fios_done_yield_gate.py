#!/usr/bin/env python3
"""patch_e401_fios_done_yield_gate.py -- gate the FIOS-42B4-CANCEL-YIELD block (func_002B42B4) behind
PS3_FIOS_DONE_YIELD (default OFF). Idempotent; run after re-lift: python3 patch_e401_fios_done_yield_gate.py <lift_dir>

E401 (2026-09-04): the block released the giant lock, yielded and slept 50 ms right after func_0030AE58 (op delete)
in the DONE handler. That let the FIOS scheduler thread free the op and clear the user's slot (op+4) BEFORE the
sync-open tail (0x2B4434) read it -> func_002B4340 returned 0 -> the R_LglScA container was dropped -> no arena,
no scope ENTER, NULL registry, lookups at guest address 4, spin in func_002ACC30 (E387-E400). The console reads
op+4 first and frees later (oracle_optail.py). PS3_FIOS_DONE_YIELD=1 restores the old behaviour for A/B."""
import sys, glob, os
d = sys.argv[1] if len(sys.argv) > 1 else 'recomp_macos_e162'
OLD1 = '        { ppu_giant_lock_release();\n          ps3recomp_giant_lock_yield_sleep1();\n'
NEW1 = ('        { static int _yg=-1; if(_yg<0){ const char* e=getenv("PS3_FIOS_DONE_YIELD"); _yg=(e&&*e&&*e!=\'0\')?1:0; } '
        'if(_yg){ /* E401: paliativo OFF por default -- a consola nao cede aqui (oracle_optail) */\n'
        '          ppu_giant_lock_release();\n          ps3recomp_giant_lock_yield_sleep1();\n')
OLD2 = '          fprintf(stderr,"[FIOSOPEN] 42B4-CANCEL-YIELD after DONE early cancel\\n");\n          fflush(stderr); }\n'
NEW2 = '          fprintf(stderr,"[FIOSOPEN] 42B4-CANCEL-YIELD after DONE early cancel\\n");\n          fflush(stderr); } }\n'
hit = 0
for p in sorted(glob.glob(os.path.join(d, 'ppu_recomp_*.cpp'))):
    s = open(p).read()
    if '42B4-CANCEL-YIELD' not in s: continue
    if 'PS3_FIOS_DONE_YIELD"' in s: print(p, ': already gated'); hit += 1; continue
    if s.count(OLD1) != 1 or s.count(OLD2) != 1: print(p, ': pattern not found (OLD1=%d OLD2=%d)' % (s.count(OLD1), s.count(OLD2))); continue
    s = s.replace(OLD1, NEW1).replace(OLD2, NEW2); open(p, 'w').write(s); print(p, ': gated'); hit += 1
sys.exit(0 if hit else 1)
