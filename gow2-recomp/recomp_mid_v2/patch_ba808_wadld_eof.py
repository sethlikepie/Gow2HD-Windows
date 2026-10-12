#!/usr/bin/env python3
"""[BA808] -- probe do loop de espera da state machine WADLD (func_002BA808).

O QUE INSTALA
=============
Um unico bloco de diagnostico no topo de `func_002BA808`, o loop de espera do
WAD-load. Imprime, por iteracao amostrada, o estado que decide o loop:

    o        = SM object EA            (ctx->gpr[31])
    state    = o+0x1CC   1=open 2=header 3=body 0=idle
    f1B0     = o+0x1B0   operacoes em voo (enquanto !=0 o loop nem chega ao
                         teste de deadline -- spin puro)
    1C8evt   = o+0x1C8   evento de conclusao
    1C4      = o+0x1C4   timer/handle libertado na saida
    deadline26 / gpr27   os dois valores do teste de timeout

Gating: `PS3_TRACE_BA808`, OFF por default (regra 6 do CLAUDE.md). Com a env
var ausente o bloco e' um `if(_t=0)`: nao le memoria guest, nao imprime, nao
incrementa contador (o `++_s` esta' a direita do `&&`). No-op no baseline.

O que se perde sem ele: quando a espera do WAD nao completa, este e' o unico
sitio que mostra QUAL dos tres gates a segura (state, f1B0 ou deadline). Sem a
probe o sintoma e' so' "o boot fica parado depois do WAD" e volta-se a
adivinhar -- foi exactamente esse o custo em Julho.

PORQUE ESTE FICHEIRO NASCEU
===========================
A probe existia no lift de 20 jul (`recomp_macos_v2.pre_v4/ppu_recomp_005.cpp`)
mas era uma EDICAO A MAO: nenhum patch_*.py a escrevia. O re-lift apagou-a e
nada se queixou. Este script fecha esse buraco -- passa a ter escritor,
idempotente, e falha (rc!=0) se a ancora deixar de existir.

O QUE ESTE SCRIPT **NAO** REPOE, E PORQUE (decisao de 2026-07-26)
=================================================================
O mesmo bloco do lift antigo carregava, em tres funcoes (`func_002BA7F0`,
`func_002BA824`, `func_002BA808`), uma guarda FUNCIONAL: quando
`f2b_stream_eof_try_complete` publicava `g_wadld_eof_ea`, a guarda forcava
CR=lt no teste de deadline para a espera sair ja' pelo caminho de timeout
(`loc_002BA854`). Estava default-ON, com kill switch `PS3_WADLD_NO_EOF_EXIT`.
NAO e' reposta aqui: e' um paliativo cuja causa raiz foi corrigida no lifter.

Prova medida (diff das duas arvores de lift, mesma funcao `func_002BA7F0`):

    lift 20 jul (pre_v4)   ctx->gpr[3]  = (int64_t)(int32_t)(ctx->gpr[3] + ((uint32_t)0x8 << 16));
                           ctx->gpr[26] = (int64_t)(int32_t)(ctx->gpr[3] + -32276);

    lift actual            ctx->gpr[3]  = ctx->gpr[3] + (int64_t)(int32_t)((uint32_t)0x8 << 16);
                           ctx->gpr[26] = ctx->gpr[3] + (int64_t)(-32276);

O `addi`/`addis` do PPC e' uma soma de 64 bits; a forma antiga truncava o
RESULTADO a 32 bits e sign-extendia. O deadline e' `agora + 492012` ticks de
294912000 Hz (a float da TOC-0x2188 lida do EBOOT e' 294912000.0f, o clock EE
do jogo PS2 original; func_0025C0D4 devolve o timebase convertido para essa
base) -- ou seja ~1.67 ms. Com o truncamento, mal o contador EE passasse 2^31
(2^31/294912000 = 7.28 s de uptime) o deadline virava negativo, sign-extendido
para 0xFFFFFFFF........ e a comparacao do lifter e' UNSIGNED: deadline > agora
para sempre => espera eterna. E' exactamente o `deadline=0xFFFFFFFFEC9735B8`
registado em notes/2026-07-22 (0xEC9735B8 = 13.46 s de EE ticks) e explica a
flakiness "AUTO_LOAD 1/3" que a guarda mascarava: dependia de a espera cair
antes ou depois dos 7.28 s.

O lifter actual corrige-o na origem -- tools/ppu_lifter.py:877-887,
"Full 64-bit add (PowerISA): truncating to 32 bits corrupts any 64-bit
pointer/counter arithmetic built with addi". Com o deadline correcto a espera
sai sozinha em ~1.67 ms, em qualquer uptime, e pelo MESMO `loc_002BA854` que a
guarda forcava. O que a guarda ainda pouparia sao esses <=1.67 ms; o que
custaria e' divergir do console (forcar um timeout que o jogo nao pediu) e
voltar a esconder esta classe de bug. Nao vale.

Nao confundir com o timebase rebase de runtime/syscalls/sys_timer.c: esse
reduziu a exposicao (adiou o cruzamento dos 2^31), nao corrigiu a truncatura.

Ficou pendente, de proposito: `g_wadld_eof_ea` continua definido e escrito em
ppu_recomp_001.cpp (por patch_f2b_multimb_install.py) e agora nao tem leitor.
E' inerte -- o trabalho real (por state/rem/body a 0) e' feito pelo proprio
`f2b_stream_eof_try_complete`. Deixa-se ficar como marcador; limpa-lo e'
alteracao ao patch dono, nao a este.

QUANDO REPOR A GUARDA (e so' entao)
-----------------------------------
Se um run mostrar `[BA808]` com `state=0 f1B0=0` e `deadline26` PLAUSIVEL
(64 bits, ~agora+492012) a repetir-se durante muito mais do que 1.67 ms. Ai' a
hipotese "deadline corrompido" cai e ha' outra causa. O texto original da
guarda esta' recuperavel byte a byte em
`recomp_macos_v2.pre_v4/ppu_recomp_005.cpp` (func_002BA808) e
`.../ppu_recomp_002.cpp` (func_002BA7F0, func_002BA824).

USO
===
    ./patch_ba808_wadld_eof.py [DIR_DE_LIFT|CHUNK.cpp ...]

rc=0 APPLIED/ALREADY, rc=1 se a funcao ou a ancora nao existirem.
"""
from __future__ import annotations

import re
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from lift_paths import resolve_lift_paths  # noqa: E402

DEFAULT_LIFT = str(Path(__file__).resolve().parent.parent / "recomp_macos_v2")

FN = "func_002BA808"
MARK = "[BA808]"

FN_RE = re.compile(r"^void " + FN + r"\(ppu_context\* ctx\) \{", re.M)
NEXT_FN_RE = re.compile(r"^void func_[0-9A-Fa-f]+\(ppu_context\* ctx\) \{", re.M)

# Ancora: o rotulo do topo do loop, mas so' quando seguido da leitura do estado
# (o+0x1CC). Amarra a insercao ao codigo que a probe descreve -- se o lifter
# reordenar o corpo, isto falha em vez de plantar a probe no sitio errado.
# Nao se usa numero de linha em lado nenhum.
ANCHOR = re.compile(
    r"^loc_002BA808:\n"
    r"(?=[ \t]*ctx->gpr\[0\] = vm_read32\(ctx->gpr\[31\] \+ 0x1CC\);\n)",
    re.M,
)

# Byte a byte como estava no lift de 20 jul (pre_v4, func_002BA808).
PROBE = (
    '        { extern char* getenv(const char*); static int _t=-1;'
    ' if(_t<0)_t=getenv("PS3_TRACE_BA808")?1:0;\n'
    '          static long _s=0; if(_t && (++_s<=4 || _s%3000000==0)){'
    ' uint32_t o=(uint32_t)ctx->gpr[31];\n'
    '            fprintf(stderr,"[BA808] #%ld o=0x%08X state=0x%X f1B0=0x%X'
    ' 1C8evt=0x%X 1C4=0x%X deadline26=0x%llX gpr27=0x%llX\\n",\n'
    '              _s,o,vm_read32(o+0x1CCu),vm_read32(o+0x1B0u),'
    'vm_read32(o+0x1C8u),vm_read32(o+0x1C4u),\n'
    '              (unsigned long long)ctx->gpr[26],'
    '(unsigned long long)ctx->gpr[27]); fflush(stderr);} }\n'
)


def main() -> int:
    paths = [p for p in resolve_lift_paths(sys.argv[1:], DEFAULT_LIFT) if p.is_file()]
    if not paths:
        print("FAILED: nenhum chunk de lift encontrado")
        return 1

    target = None
    for p in paths:
        text = p.read_text(encoding="utf-8", errors="replace")
        if FN_RE.search(text):
            target = (p, text)
            break
    if target is None:
        print(f"FAILED: {FN} nao definido em nenhum dos {len(paths)} chunk(s)")
        return 1
    path, src = target

    m = FN_RE.search(src)
    nxt = NEXT_FN_RE.search(src, m.end())
    lo, hi = m.start(), (nxt.start() if nxt else len(src))
    region = src[lo:hi]

    if MARK in region:
        print(f"{path.name}: ALREADY {FN} ja' tem a probe {MARK} (nada a fazer)")
        return 0

    hits = list(ANCHOR.finditer(region))
    if len(hits) != 1:
        print(
            f"FAILED: ancora 'loc_002BA808: + read(gpr[31]+0x1CC)' aparece "
            f"{len(hits)}x em {FN} ({path.name}); esperado 1. "
            f"O shape do lift mudou -- investigar, nunca forcar."
        )
        return 1

    cut = lo + hits[0].end()
    # Path.write_text(newline=...) so' existe em 3.10+; o /usr/bin/python3 do
    # Mac de build e' 3.9.6. Abrir explicitamente mantem LF em qualquer host.
    with path.open("w", encoding="utf-8", newline="\n") as fh:
        fh.write(src[:cut] + PROBE + src[cut:])
    print(f"{path.name}: APPLIED probe {MARK} em {FN} (1 site, gated PS3_TRACE_BA808)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
