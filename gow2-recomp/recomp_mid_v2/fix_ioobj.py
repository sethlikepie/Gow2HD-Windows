path = 'ppu_recomp_001.cpp'
data = open(path, 'rb').read()
old = b'fprintf(stderr,"[IOOBJ] entry r3=0x%X (\\"%s\\") r4=0x%X\\n", p, p?s:"", (unsigned)ctx->gpr[4]); fflush(stderr);} }'
new = (b'fprintf(stderr,"[IOOBJ] entry r3=0x%X magic=0x%08X vtbl=0x%08X r4=0x%X\\n", p, '
       b'p?(unsigned)vm_read32(p+4):0, p?(unsigned)vm_read32(p+0):0, (unsigned)ctx->gpr[4]); fflush(stderr);} }')
assert data.count(old) == 1, data.count(old)
data = data.replace(old, new, 1)
open(path, 'wb').write(data)
print('IOOBJ trace atualizado')
