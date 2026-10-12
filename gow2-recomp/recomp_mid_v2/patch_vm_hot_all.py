#!/usr/bin/env python3
"""patch_vm_hot_all.py -- o lift INTEIRO passa a usar os acessores rapidos de memoria.

Porque: medido 2026-09-18 (`ps -M` em dois instantes + `sample`, 12 s de
gameplay), o caminho critico do frame e' UMA thread PPU a 89% de um nucleo --
a main-thread, com 25 esperas em 7 878 amostras -- e as folhas do perfil dela
sao `vm_read32` e `vm_write32`. Contados no lift de producao:

    vm_read32      511 813 chamadas
    vm_write32     265 006 chamadas
    vm_read32_hot        0
    vm_write32_hot       0

Os headers rapidos (ppu_vm_hot.h, ppu_vm_write_hot.h) existem desde a leva
anterior mas so' eram ligados por patches a punhados de funcoes, e NENHUM
estava aplicado a este lift. O acessor gordo do ppu_loader e' uma funcao de
varias centenas de linhas com `__builtin_return_address(0)` (que forca frame
pointer e mata a tail-call) e dezenas de probes gated; o rapido mantem os
passos funcionais -- poll de preempcao, verificacao de faixa comprometida
(agora INLINE), load/store big-endian -- e larga o diagnostico.

MEDIDO 2026-09-18 e REFUTADO: com isto aplicado a 990 636 sitios, o jogo NAO
CHEGA AO GAMEPLAY em 2/2 corridas de 140 s -- para com draws=2, depois da FSM
do filme e com jobs SPU a correr. Confirma a nota antiga de que a leitura
rapida trava o carregamento do nivel: o acessor gordo tem, do lado da LEITURA,
ganchos de que o boot depende (hook do EOS do filme, restauro de PT, guarda de
type15/tblsize) que o rapido larga. E' por isso que existe o
ppu_vm_fast_policy, que so' liga o caminho rapido DEPOIS de o gameplay
comecar -- qualquer tentativa futura tem de usar o mesmo portao, nao aplicar
incondicionalmente. O lift foi revertido.

TENTATIVA 2 (portao de gameplay, commit c06eba19 no ps3recomp): os acessores
inline passam a cair no caminho COMPLETO enquanto ps3_vm_gameplay_ready_hot()
e' 0. Isso RESOLVE o arranque -- o jogo volta a chegar ao gameplay -- mas NAO
compra fps: 4 corridas alternadas deram referencia 17,0/22,5 e portao
19,5/18,0 (medias 20,6 vs 17,4), com o lado do portao mais instavel
(dp 4,57, p10=12).

CAUSA MEDIDA: o binario passa de 64 965 504 para 107 360 336 bytes, **+65%**.
Inlinar 990 636 acessos infla o codigo em 42 MB e o i-cache paga mais do que a
chamada custava. E' o mesmo efeito de "inlining agressivo em codigo liftado
enorme" que ja' tinha aparecido no SPU em -O2.

O que isto NAO refuta: fazer o acessor GORDO ficar barato (mover os probes
para #ifdef, tirar o __builtin_return_address) sem inlinar nada. Essa via
mantem uma chamada so', nao mexe no tamanho do codigo, e continua por testar.

GATED por construcao: isto reescreve o lift, que e' gitignored; para voltar
atras, re-liftar ou re-aplicar o catalogo sem este script. Idempotente: se o
marcador ja' esta' no header, reporta ALREADY.

Usage: patch_vm_hot_all.py <lift_dir>   rc 0 ok / 2 sem lift / 3 needle ausente
"""
from __future__ import annotations

import glob
import os
import re
import sys

MARK = "PPU_VM_HOT_ALL"
PREAMBLE = """\
/* Acessores rapidos de memoria para TODO o lift (patch_vm_hot_all.py).
 * As macros vem DEPOIS das declaracoes do ppu_recomp.h, senao renomeariam a
 * propria declaracao. PPU_VM_HOT_NO_MACRO impede o header de as definir de
 * novo. */
#define PPU_VM_HOT_ALL 1
#define PPU_VM_HOT_NO_MACRO 1
#include "ppu_vm_hot.h"
#include "ppu_vm_write_hot.h"
#define vm_read32(a)      vm_read32_hot(a)
#define vm_read64(a)      vm_read64_hot(a)
#define vm_write32(a, v)  vm_write32_hot((a), (v))
/* vm_write64 NAO entra: o vm_write64_hot inline deste header LARGA o cancelamento
 * de reserva (o comentario dele diz que so' era seguro para spills de stack de
 * uma funcao especifica). Aplicado ao lift inteiro, quebraria a semantica
 * cruzada de lwarx/stwcx que o ppu_res_stwcx depende. */
"""


def main() -> int:
    if len(sys.argv) != 2:
        print(__doc__.strip().splitlines()[-1], file=sys.stderr)
        return 2
    d = sys.argv[1]
    chunks = sorted(glob.glob(os.path.join(d, "ppu_recomp_*.cpp")))
    if not chunks:
        print(f"vmhot: sem lift em {d}", file=sys.stderr)
        return 2

    # O bloco vai em CADA chunk, nao no ppu_recomp.h partilhado: o
    # ppu_loader.cpp inclui esse header e DEFINE as proprias vm_*_hot, entao as
    # macros ali renomeavam as definicoes dele (redefinition / incomplete type).
    needle = '#include "ppu_recomp.h"\n'
    done = skipped = 0
    total = 0
    for c in chunks:
        s = open(c).read()
        if MARK in s:
            skipped += 1
        elif needle in s:
            s = s.replace(needle, needle + PREAMBLE, 1)
            open(c, "w").write(s)
            done += 1
        else:
            print(f"vmhot: needle ausente em {c}", file=sys.stderr)
            return 3
        total += len(re.findall(r"\b(vm_read32|vm_read64|vm_write32)\(", s))
    if skipped and not done:
        print(f"vmhot: ALREADY ({skipped} chunks)")
        return 0
    print(f"vmhot: APPLIED em {done} chunks ({skipped} ja' tinham), "
          f"{total} sitios cobertos por macro")
    return 0


if __name__ == "__main__":
    sys.exit(main())
