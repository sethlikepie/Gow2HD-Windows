#!/usr/bin/env python3
"""Programa SPU do teste de ABI do CellSpursTaskArgument (u128).

O contrato sob teste vive em runtime/spu/spu_lifted_job.h
(`spu_run_lifted_job_ex`): o argumento de uma task SPURS e UM valor de 128 bits
carregado INTEIRO para r3 -- as 4 words big-endian de `args_ea` caem em
ctx.gpr[3]._u32[0..3]. Nao e o EA em si (ABI escalar ja refutada), nem as words
espalhadas por r3..r6.

Os testes que ja existiam (workload/adapter) so olham a word 0, portanto nao
distinguem "u128 correto" de "so a word 0 chegou". Este programa fixa as QUATRO
words: guarda o r3 inteiro (stqd guarda os 16 bytes do registo) e tambem o r4
(a segunda metade da ABI de task: { spurs EA, taskset->args }), e faz DMA PUT
dos 32 bytes para uma EA fixa que a harness le de volta.

    stqd r3, 0x100(r0)     LS[0x100..0x10F] = r3  (u128 do argumento)
    stqd r4, 0x110(r0)     LS[0x110..0x11F] = r4  (spurs EA / taskset args)
    DMA PUT LS 0x100 -> EA 0x10400, 32 bytes

A EA de resultado fica acima de 0x10000 de proposito: quando a harness liga a
guarda de range (ppu_vm_size != 0, para exercitar o fallback documentado),
spu_dma.h rejeita destinos abaixo de 0x10000.
"""
import struct, os, sys
HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, "..", "..", "..", "tools"))
from wrap_spu_elf import wrap

def w(v): return struct.pack(">I", v & 0xFFFFFFFF)
def ri16(op9, i16, rt): return w(((op9 & 0x1FF) << 23) | ((i16 & 0xFFFF) << 7) | (rt & 0x7F))
def ri18(op7, i18, rt): return w(((op7 & 0x7F) << 25) | ((i18 & 0x3FFFF) << 7) | (rt & 0x7F))
def ri10(op8, i10, ra, rt): return w(((op8 & 0xFF) << 24) | ((i10 & 0x3FF) << 14) | ((ra & 0x7F) << 7) | (rt & 0x7F))
def rr(op11, rb, ra, rt): return w(((op11 & 0x7FF) << 21) | ((rb & 0x7F) << 14) | ((ra & 0x7F) << 7) | (rt & 0x7F))
def ch(op11, channel, rt): return w(((op11 & 0x7FF) << 21) | ((channel & 0x1F) << 7) | (rt & 0x7F))

IL, ILA, LQD, STQD, WRCH, STOP = 0x81, 0x21, 0x34, 0x24, 0x10D, 0x000
MFC_LSA, MFC_EAH, MFC_EAL, MFC_Size, MFC_TagID, MFC_Cmd = 16, 17, 18, 19, 20, 21
MFC_PUT = 0x20

LS_SAVE   = 0x100     # onde r3/r4 sao guardados (imm de STQD e escalado por 16)
RESULT_EA = 0x10400   # tem de ser >= 0x10000: ver docstring
NBYTES    = 32

b  = ri16(IL, 0, 0)                      # il   r0, 0             (base = 0)
b += ri10(STQD, LS_SAVE  >> 4, 0, 3)     # stqd r3, 0x100(r0)     u128 do argumento
b += ri10(STQD, (LS_SAVE + 0x10) >> 4, 0, 4)  # stqd r4, 0x110(r0)  { spurs EA, args }
# --- DMA PUT: LS 0x100..0x11F -> EA RESULT_EA. Registos altos para nao tocar
# em r3/r4 antes de estarem guardados. ---
b += ri18(ILA, LS_SAVE,   10)            # ila  r10, 0x100        (LSA)
b += ri16(IL,  0,         11)            # il   r11, 0            (EAH)
b += ri18(ILA, RESULT_EA, 12)            # ila  r12, 0x10400      (EAL)
b += ri16(IL,  NBYTES,    13)            # il   r13, 32           (Size)
b += ri16(IL,  0,         14)            # il   r14, 0            (TagID)
b += ri16(IL,  MFC_PUT,   15)            # il   r15, 0x20         (PUT)
b += ch(WRCH, MFC_LSA,   10)
b += ch(WRCH, MFC_EAH,   11)
b += ch(WRCH, MFC_EAL,   12)
b += ch(WRCH, MFC_Size,  13)
b += ch(WRCH, MFC_TagID, 14)
b += ch(WRCH, MFC_Cmd,   15)             # dispara o PUT (LS -> memoria principal)
b += rr(STOP, 0, 0, 0)                   # stop

elf = wrap(b, base=0, entry=0, symbols=[{"name": "main", "addr": 0, "size": len(b)}])
open(os.path.join(HERE, "test_spu_task_abi.elf"), "wb").write(elf)
print(f"Wrote test_spu_task_abi.elf ({len(elf)} bytes ELF, {len(b)} bytes image)")
