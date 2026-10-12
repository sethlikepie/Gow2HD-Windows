import glob, re
pat = re.compile(rb'= __builtin_clz\(\(uint32_t\)ctx->gpr\[(\d+)\]\);')
total = 0
files = []
for f in glob.glob('ppu_recomp_*.cpp'):
    data = open(f, 'rb').read()
    new, n = pat.subn(rb'= (uint32_t)ctx->gpr[\1] ? __builtin_clz((uint32_t)ctx->gpr[\1]) : 32;', data)
    if n:
        open(f, 'wb').write(new)
        total += n
        files.append(f)
print(f'{total} sites corrigidos em {len(files)} chunks')
print(' '.join(files))
