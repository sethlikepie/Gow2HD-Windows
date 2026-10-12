path = 'ppu_recomp_001.cpp'
data = open(path, 'rb').read()
nl = b'\r\n' if b'void func_00304A48(ppu_context* ctx) {\r\n' in data else b'\n'
# remover traces FIOS2 quebrados (linha do fprintf + linha órfã seguinte)
lines = data.split(nl)
out = []
skip = False
removed = 0
for l in lines:
    if b'[FIOS2]' in l:
        removed += 1
        skip = not l.rstrip().endswith(b'} }')
        continue
    if skip and l.startswith(b'",'):
        removed += 1
        skip = False
        continue
    skip = False
    out.append(l)
data = nl.join(out)

NL = b'\\n'  # backslash-n literal em bytes
for name in (b'func_00304A48', b'func_00305638'):
    marker = b'void ' + name + b'(ppu_context* ctx) {' + nl
    assert data.count(marker) == 1, (name, nl)
    trace = (marker +
             b'        { static int n=0; if(n++<8){ fprintf(stderr,"[FIOS2] ' + name +
             b' r3=0x%X r4=0x%X' + NL + b'",(unsigned)ctx->gpr[3],(unsigned)ctx->gpr[4]); fflush(stderr);} }' + nl)
    data = data.replace(marker, trace, 1)
open(path, 'wb').write(data)
print(f'removed={removed}; traces inseridos ok')
